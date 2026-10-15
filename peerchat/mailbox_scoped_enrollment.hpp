#pragma once
#include "mailbox_key_coordinator.hpp"
#include "platform/storage_backend.hpp"
#include "platform/record_protection.hpp"
#include <optional>

namespace peerchat::mailbox {
// Supplemental registration authority only. Original PKR1 material, aliases,
// bindings and revocation remain owned by DeviceKeyManager. The database MUST
// be the exact connection backing that manager's SqlKeyRegistryStore: its
// existing operation lease supplies the transaction for every record access.
// Callers retain the existing profile gate around this adapter and other uses
// of that connection, including schema setup and the short locator transaction.
class ScopedEnrollment {
    DeviceKeyManager& keys_;platform::IdentityProvider& identity_;
    platform::SqlDatabase& database_;platform::RecordProtector& protection_;
    platform::HpkeProvider& hpke_;Verify verify_;Fingerprint fingerprint_;
    enum class State : unsigned char {Pending=0,Accepted=1,Revoked=2};
    struct Record {std::string scope,device,material,sourceDigest;std::uint64_t account=0;Bytes binding;State state=State::Pending;};
    struct WipeBytes {Blob& bytes;~WipeBytes(){volatile unsigned char* p=bytes.data();for(std::size_t i=0;i<bytes.size();++i)p[i]=0;}};
    std::string device(){return fingerprint_(identity_.publicKey());}
    std::string key(const std::string& scope,std::uint64_t current,const std::string& local) {
        require(!scope.empty()&&scope.size()<=2048&&account(current)&&id(local,64));
        Writer writer("PeerChat/scoped-mailbox-enrollment-key/v1");writer.text(scope);writer.number(current,8);writer.text(local);
        auto value=fingerprint_(writer.bytes);require(id(value,64));return value;
    }
    std::optional<Record> load(const std::string& scope,std::uint64_t current,const std::string& local) {
        auto rowKey=key(scope,current,local);
        platform::CheckedSqlStatement statement(database_,"SELECT b FROM mailbox_scoped_enrollment WHERE k=?");statement.text(1,rowKey);
        if(!statement.row())return std::nullopt;
        auto protectedBytes=statement.bytes(0);require(protectedBytes.size()<=16384);
        auto bytes=protection_.unprotect(protectedBytes,"PeerChat/scoped-mailbox-enrollment/v1/"+rowKey);
        require(bytes.size()<=12288);Reader reader(bytes,"PeerChat/scoped-mailbox-enrollment-record/v1");Record record;
        record.scope=reader.text(2048);record.account=reader.number(8);record.device=reader.text(64);record.material=reader.text(32);
        record.sourceDigest=reader.text(64);record.binding=reader.field(4096);auto state=reader.number(1);reader.end();
        require(state<=2&&record.scope==scope&&record.account==current&&record.device==local&&id(record.material,32)&&id(record.sourceDigest,64));
        record.state=static_cast<State>(state);return record;
    }
    void save(const Record& record) {
        Writer writer("PeerChat/scoped-mailbox-enrollment-record/v1");writer.text(record.scope);writer.number(record.account,8);writer.text(record.device);
        writer.text(record.material);writer.text(record.sourceDigest);writer.field(record.binding);writer.number(static_cast<unsigned>(record.state),1);
        require(writer.bytes.size()<=12288);auto rowKey=key(record.scope,record.account,record.device);
        auto bytes=protection_.protect(writer.bytes,"PeerChat/scoped-mailbox-enrollment/v1/"+rowKey);require(!bytes.empty()&&bytes.size()<=16384);
        platform::CheckedSqlStatement statement(database_,"INSERT INTO mailbox_scoped_enrollment(k,b) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET b=excluded.b");
        statement.text(1,rowKey);statement.blob(2,bytes);statement.row();
    }
    RecipientBinding source(const DeviceKeyMetadata& metadata,std::uint64_t current,const std::string& local) {
        require(metadata.registration==DeviceKeyRegistration::Accepted&&!metadata.signedBinding.empty());
        auto binding=parseBinding(metadata.signedBinding);auto identity=identity_.publicKey();
        require(binding.accountId!=current&&binding.device==local&&binding.keyId==metadata.id&&binding.publicKey==metadata.publicKey&&
            binding.generation==metadata.generation&&binding.createdUtc==metadata.createdUtcMilliseconds&&binding.predecessorId==metadata.predecessorId&&
            verifyBinding(binding,binding.accountId,local,identity,verify_,fingerprint_));return binding;
    }
    RecipientBinding validate(const Record& record,const DeviceKeyMetadata& metadata,bool allowRevoked=false) {
        auto original=source(metadata,record.account,record.device);
        require((allowRevoked||record.state!=State::Revoked)&&(record.state!=State::Pending||metadata.state==DeviceKeyState::Active));
        require(record.material==metadata.id&&record.sourceDigest==fingerprint_(metadata.signedBinding));
        auto binding=parseBinding(record.binding);
        require(binding.keyId!=original.keyId&&binding.generation==1&&binding.predecessorId.empty()&&binding.publicKey==original.publicKey&&binding.storageAllowed&&
            verifyBinding(binding,record.account,record.device,identity_.publicKey(),verify_,fingerprint_));return binding;
    }
    template<class F> bool stored(const std::string& scope,std::uint64_t current,bool accepted,F operation,bool allowRevoked=false) {
        const auto local=device();key(scope,current,local);std::optional<Record> locator;
        // This short read only locates the source ID. It grants no authority
        // and closes before acquiring the original manager's transaction.
        // Unrelated inaccessible retired keys must not block normal profiles.
        {platform::SqlTransaction transaction(database_);locator=load(scope,current,local);transaction.commit();}
        if(!locator)return false;
        keys_.withRecipient(locator->material,true,[&](const DeviceKeyMetadata& metadata,const auto& handle){
            auto record=load(scope,current,local);
            require(record&&record->material==locator->material&&record->sourceDigest==locator->sourceDigest&&
                record->binding==locator->binding&&record->state==locator->state);
            auto binding=validate(*record,metadata,allowRevoked);require(!accepted||record->state==State::Accepted);
            operation(*record,binding,handle);
        });return true;
    }
public:
    ScopedEnrollment(DeviceKeyManager& keys,platform::IdentityProvider& identity,platform::SqlDatabase& database,
                     platform::RecordProtector& protection,platform::HpkeProvider& hpke,Verify verify,Fingerprint fingerprint)
        :keys_(keys),identity_(identity),database_(database),protection_(protection),hpke_(hpke),verify_(std::move(verify)),fingerprint_(std::move(fingerprint)) {
        database_.execute("CREATE TABLE IF NOT EXISTS mailbox_scoped_enrollment(k TEXT PRIMARY KEY,b BLOB NOT NULL);");
    }
    std::optional<RecipientBinding> offer(const std::string& scope,std::uint64_t current) {
        const auto local=device();key(scope,current,local);std::optional<RecipientBinding> result;
        if(bindingFor(scope,current))return std::nullopt;
        for(const auto& entry:keys_.snapshot()) {
            if(entry.state!=DeviceKeyState::Active||entry.signedBinding.empty())continue;
            // A normal same-account binding needs no cross-account enrollment.
            if(parseBinding(entry.signedBinding).accountId==current)continue;
            keys_.withRecipient(entry.id,false,[&](const DeviceKeyMetadata& metadata,const auto&){
                require(metadata.state==DeviceKeyState::Active);auto original=source(metadata,current,local);
                auto record=load(scope,current,local);if(record){validate(*record,metadata);return;}result=original;
            });break;
        }
        return result;
    }
    RecipientBinding approve(const std::string& scope,std::uint64_t current,const Blob& expectedSourceWire,std::uint64_t now) {
        const auto local=device();key(scope,current,local);require(account(now)&&expectedSourceWire.size()<=4096);
        auto expected=parseBinding(expectedSourceWire);RecipientBinding result;
        keys_.withRecipient(expected.keyId,false,[&](const DeviceKeyMetadata& metadata,const auto& handle){
            require(metadata.state==DeviceKeyState::Active&&metadata.signedBinding==expectedSourceWire);source(metadata,current,local);
            auto existing=load(scope,current,local);if(existing){result=validate(*existing,metadata);return;}
            platform::CheckedSqlStatement count(database_,"SELECT COUNT(*) FROM mailbox_scoped_enrollment");require(count.row()&&count.number(0)<32);
            RecipientBinding proposed;proposed.accountId=current;proposed.device=local;proposed.keyId=Storage::newMessageId();proposed.generation=1;
            proposed.createdUtc=now;proposed.publicKey=metadata.publicKey;proposed.storageAllowed=true;
            require(proposed.keyId!=metadata.id);proposed.signature=identity_.sign(bindingPayload(proposed));auto wire=serializeBinding(proposed);
            require(verifyBinding(proposed,current,local,identity_.publicKey(),verify_,fingerprint_));
            Writer challenge("PeerChat/scoped-mailbox-possession/v1");challenge.text(scope);challenge.number(current,8);challenge.text(local);
            challenge.text(fingerprint_(metadata.signedBinding));challenge.text(fingerprint_(wire));challenge.text(Storage::newMessageId());
            const auto random=Storage::newMessageId();Blob secret(random.begin(),random.end());
            WipeBytes erase{secret};
            auto encrypted=hpke_.seal(metadata.publicKey,challenge.bytes,challenge.bytes,secret);
            auto opened=hpke_.open(handle,metadata.publicKey,challenge.bytes,challenge.bytes,encrypted);WipeBytes eraseOpened{opened};
            require(opened.size()==secret.size());unsigned difference=0;for(std::size_t i=0;i<secret.size();++i)difference|=opened[i]^secret[i];require(difference==0);
            Record record{scope,local,metadata.id,fingerprint_(metadata.signedBinding),current,wire,State::Pending};save(record);result=proposed;
        });return result;
    }
    std::optional<RecipientBinding> bindingFor(const std::string& scope,std::uint64_t current) {
        std::optional<RecipientBinding> result;stored(scope,current,false,[&](const auto&,const auto& binding,const auto&){result=binding;});return result;
    }
    void accepted(const std::string& scope,std::uint64_t current,const RecipientBinding& exact) {
        bool found=stored(scope,current,false,[&](auto record,const auto&,const auto&){require(record.binding==serializeBinding(exact));if(record.state!=State::Accepted){record.state=State::Accepted;save(record);}});
        require(found);
    }
    // Local authorization revocation only. Preserve ciphertext, original key,
    // old binding and this immutable scoped proof; never revive it implicitly.
    void revoke(const std::string& scope,std::uint64_t current,const RecipientBinding& exact) {
        bool found=stored(scope,current,false,[&](auto record,const auto&,const auto&){require(record.binding==serializeBinding(exact));if(record.state!=State::Revoked){record.state=State::Revoked;save(record);}},true);
        require(found);
    }
    void withAccepted(const std::string& scope,std::uint64_t current,const RecipientBinding& exact,
                      const std::function<void(const std::shared_ptr<platform::RecipientKeyHandle>&)>& operation) {
        require(exact.accountId==current);bool found=stored(scope,current,true,[&](const auto& record,const auto&,const auto& handle){require(record.binding==serializeBinding(exact));operation(handle);});require(found);
    }
};
}
