#pragma once
#include "device_identity_directory.hpp"
#include "platform/storage_backend.hpp"
#include "group.hpp"
#include "protocol.hpp"
#include <map>

namespace peerchat::device_identity {
// Separate database from message and recipient-key journals. Access occurs only
// on the existing native application worker/gate; no network work is performed.
class SqlDirectoryStore final:public platform::KeyRegistryStore {
    platform::SqlDatabase& db_;std::string key_;bool inside_=false;
public:
    SqlDirectoryStore(platform::SqlDatabase& db,std::string key):db_(db),key_(std::move(key)){}
    void locked(const std::function<void()>& operation)override {
        require(!inside_);platform::SqlTransaction tx(db_);inside_=true;
        try{operation();inside_=false;tx.commit();}catch(...){inside_=false;throw;}
    }
    bool read(Bytes& result)override {
        require(inside_);platform::CheckedSqlStatement q(db_,"SELECT b FROM identity_directories WHERE scope=?");q.text(1,key_);
        if(!q.row()){result.clear();return false;}result=q.bytes(0);return true;
    }
    void replace(const Bytes& value)override {
        require(inside_&&!value.empty()&&value.size()<=1048576);
        platform::CheckedSqlStatement q(db_,"INSERT INTO identity_directories(scope,b) VALUES(?,?) ON CONFLICT(scope) DO UPDATE SET b=excluded.b");
        q.text(1,key_);q.blob(2,value);q.row();
    }
};
class Application {
    platform::IdentityProvider& identity_;platform::SqlDatabase& db_;platform::RecordProtector& protection_;
    std::string profile_;ScopedSigner signer_;
    struct Current {Scope scope;std::uint64_t monotonicDeadline=0;};
    std::map<std::string,Current> current_;
    std::map<std::string,std::uint64_t> claims_;
    std::uint64_t generation_=0,account_=0;
    std::string pin_;
    static std::string key(const std::string& pin,std::uint64_t account){return pin+":"+std::to_string(account);}
    void bind(std::uint64_t account,std::uint64_t generation,const std::string& pin) {
        if(account_!=account||generation_!=generation||pin_!=pin){current_.clear();claims_.clear();account_=account;generation_=generation;pin_=pin;}
    }
    Directory directory(SqlDirectoryStore& store,const std::string& pin,std::uint64_t account) {
        return Directory(store,protection_,profile_,pin,account,verifyDevicePayload,Storage::publicKeyId);
    }
public:
    Application(platform::IdentityProvider& identity,platform::SqlDatabase& database,platform::RecordProtector& protection,const std::string& profile)
        :identity_(identity),db_(database),protection_(protection),profile_(profile),signer_(identity,verifyDevicePayload,Storage::publicKeyId) {
        require(mailbox::id(profile,64));db_.execute("PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS identity_directories(scope TEXT PRIMARY KEY,b BLOB NOT NULL);");
    }
    Bytes resolve(std::uint64_t localAccount,std::uint64_t generation,std::uint64_t account,const std::string& device,
                  std::uint64_t wall,std::uint64_t steady) {
        require(localAccount==account_&&generation==generation_);
        auto found=current_.find(key(pin_,account));require(found!=current_.end()&&steady<found->second.monotonicDeadline);
        SqlDirectoryStore store(db_,key(pin_,account));auto d=directory(store,pin_,account);
        return d.resolve(found->second.scope,device,generation,wall);
    }
    Bytes execute(const Bytes& input,std::uint64_t localAccount,std::uint64_t generation,
                  std::uint64_t wall,std::uint64_t steady,const std::vector<Contact>& contacts,
                  const std::function<std::uint64_t(const std::string&)>& mappedAccount) {
        require(input.size()<=70000&&localAccount&&generation);
        protocol::Reader r(input);require(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/IdentityDirectoryCommand/v1");
        auto op=r.number(),account=r.number(),expected=r.number();auto pin=r.string();auto started=r.number();
        require(account==localAccount&&expected==generation&&mailbox::id(pin,64)&&started<=wall&&wall-started<=30000);
        bind(account,generation,pin);
        if(op==0){r.end();Possession p{pin,profile_,account,identity_.publicKey(),{}};
            require(Storage::publicKeyId(p.key)==profile_);p.signature=identity_.sign(possessionPayload(p));return encode(p);}
        if(op==1){auto payload=r.bytes();auto confirmed=r.string();r.end();auto claim=unsignedAuthority(payload,verifyDevicePayload,Storage::publicKeyId);
            require(claim.identity.account==account&&claim.identity.server==pin&&claim.identity.device==profile_&&
                claim.identity.key==identity_.publicKey()&&confirmed==profile_&&claim.issued<=wall&&wall<claim.expires);
            for(auto it=claims_.begin();it!=claims_.end();){if(it->second<=wall)it=claims_.erase(it);else ++it;}
            require(claims_.size()<128&&claims_.emplace(claim.issuance,claim.expires).second);
            claim.signature=identity_.sign(payload);require(verifyDevicePayload(payload,claim.signature,claim.identity.key));return encode(claim);}
        if(op==2){auto payload=r.bytes();auto confirmed=r.string();r.end();auto e=unsignedEndorsement(payload,verifyDevicePayload,Storage::publicKeyId);
            require(e.target.account==account&&e.target.server==pin&&e.authority==profile_&&confirmed==e.target.device);
            auto found=current_.find(key(pin,account));require(found!=current_.end()&&steady<found->second.monotonicDeadline);
            // Existing explicit own-authority consent must still resolve under
            // fresh directory state; a challenge cannot appoint its own signer.
            require(resolve(account,generation,account,profile_,wall,steady)==identity_.publicKey());
            auto s=found->second.scope;require(s.revision==e.revision&&s.authorityGeneration==e.authorityGeneration);
            s.session=e.session;s.targetCurrent=[&](const std::string& device,std::uint64_t gen,const Bytes& wire){return device==e.target.device&&gen==e.targetGeneration&&wire.empty();};
            Approval approved{pin,e.authority,confirmed,e.issuance,e.target.key,account,e.authorityGeneration,e.targetGeneration,e.revision,generation,e.session,e.issued,e.expires};
            return signer_.sign(e,approved,s,generation,wall);}
        require(op==3);auto remote=r.number(),revision=r.number();auto rootWire=r.bytes();auto rootActive=r.number();auto count=r.number();
        require(mailbox::account(remote)&&mailbox::account(revision)&&rootActive<=1&&count<=32);
        current_.erase(key(pin,remote)); // Rejected refresh cannot retain an earlier lease.
        auto root=authority(rootWire,verifyDevicePayload,Storage::publicKeyId);
        require(root.identity.server==pin&&root.identity.account==remote&&root.revision<=revision);
        std::map<std::string,std::pair<Endorsement,Bytes>> all;std::vector<Bytes> active;
        for(std::uint64_t n=0;n<count;++n){auto wire=r.bytes();auto enabled=r.number();require(enabled<=1);
            auto e=endorsement(wire,verifyDevicePayload,Storage::publicKeyId);
            require(e.target.account==remote&&e.target.server==pin&&e.authorityKey==root.identity.key&&
                e.authority==root.identity.device&&e.authorityGeneration==root.generation&&e.revision<=revision&&
                all.emplace(e.target.device,std::make_pair(e,wire)).second);
            if(enabled){require(remote==account||mappedAccount(e.target.device)==remote);active.push_back(wire);}
        }
        auto confirmed=r.string();r.end();
        if(rootActive)require(remote==account||mappedAccount(root.identity.device)==remote);
        // Conflicting existing public keys are never overwritten or ignored.
        for(const auto& contact:contacts){
            if(contact.peerId==root.identity.device)require(contact.publicKey==root.identity.key);
            auto found=all.find(contact.peerId);if(found!=all.end())require(contact.publicKey==found->second.first.target.key);
        }
        Scope s;s.server=pin;s.account=remote;s.authority=root.identity.device;s.authorityKey=root.identity.key;
        s.authorityGeneration=root.generation;s.revision=revision;s.localGeneration=generation;s.session=root.session;
        s.validUntil=started+60000;s.authorityActive=rootActive==1;
        s.targetCurrent=[active](const std::string& device,std::uint64_t gen,const Bytes& wire){
            if(std::find(active.begin(),active.end(),wire)==active.end())return false;
            auto e=endorsement(wire,verifyDevicePayload,Storage::publicKeyId);return e.target.device==device&&e.targetGeneration==gen;};
        SqlDirectoryStore store(db_,key(pin,remote));auto d=directory(store,pin,remote);
        if(!s.authorityActive){require(confirmed.empty());d.revokeAuthority(s,generation,wall);return {1};}
        if(!confirmed.empty())d.bootstrap(s,confirmed,generation,wall);
        d.reconcile(s,active,generation,wall);
        require(current_.size()<256||current_.count(key(pin,remote)));current_[key(pin,remote)]={std::move(s),steady+60000-(wall-started)};return {1};
    }
};
}
