#include "../storage.cpp"
#include "../platform/sql_key_registry.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../mailbox_scoped_enrollment.hpp"
#include "../delivery_manager.hpp"
#include "../reliable_peer_protocol.hpp"
#include <chrono>
#include <iostream>
using namespace peerchat;
static void check(bool value){if(!value)throw std::runtime_error("Scoped enrollment regression failed");}
template<class F>static void rejects(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed);}
struct Signing final : platform::IdentityProvider {Profile profile;explicit Signing(Profile p):profile(std::move(p)){}Blob publicKey()override{return profile.publicKey;}Blob sign(const Blob& b)override{return signDevicePayload(b,profile);}};
struct Recipient final : platform::RecipientKeyProvider {
    platform::WindowsRecipientKeyProvider actual;unsigned creates=0;
    bool supported()const override{return actual.supported();}bool exists(const std::string& a)override{return actual.exists(a);}
    std::shared_ptr<platform::RecipientKeyHandle> create(const std::string& a)override{++creates;return actual.create(a);}
    std::shared_ptr<platform::RecipientKeyHandle> open(const std::string& a)override{return actual.open(a);}
};
struct Alias final {std::string name="PeerChat.Recipient.test.scoped."+Storage::newMessageId();~Alias(){platform::recipient_cng::Object p,k;if(NCryptOpenStorageProvider(&p.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;auto n=platform::recipient_cng::alias(name);if(NCryptOpenKey(p.value,&k.value,n.c_str(),0,0)==0&&NCryptDeleteKey(k.value,0)==0)k.value=0;}};
struct RejectOpen final : platform::HpkeProvider {
    platform::HpkeProvider& actual;explicit RejectOpen(platform::HpkeProvider& p):actual(p){}
    platform::HpkeEnvelope seal(const Bytes& p,const Bytes& i,const Bytes& a,const Bytes& b,const std::atomic<bool>* c=nullptr)override{return actual.seal(p,i,a,b,c);}
    Bytes open(const std::shared_ptr<platform::RecipientKeyHandle>&,const Bytes&,const Bytes&,const Bytes&,const platform::HpkeEnvelope&,const std::atomic<bool>* =nullptr)override{throw std::runtime_error("Injected possession failure");}
    void shutdown()noexcept override{}
};
int main(int argc,char** argv){try{
    check(argc==2);std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    Storage recipientStore((root/L"recipient").wstring()),senderStore((root/L"sender").wstring());Signing recipient(recipientStore.profile()),sender(senderStore.profile());Alias alias;Recipient provider;
    auto hpke=platform::createShippingHpke();platform::WindowsRecordProtector protection;
    auto database=std::make_shared<storage_detail::WindowsSqlDatabase>((root/L"registry").wstring());
    platform::SqlKeyRegistryStore sql(database);platform::ProtectedKeyRegistry registry(sql,protection,recipient.profile.peerId);DeviceKeyManager keys(recipient,provider,registry);
    auto oldId=Storage::newMessageId();auto metadata=keys.provisionRecipient(oldId,alias.name,2,now);
    mailbox::RecipientBinding old;old.accountId=19;old.device=recipient.profile.peerId;old.keyId=oldId;old.generation=2;old.createdUtc=now;old.publicKey=metadata.publicKey;old.storageAllowed=true;
    old.signature=recipient.sign(mailbox::bindingPayload(old));auto oldWire=mailbox::serializeBinding(old);keys.recordRegistration(oldId,oldWire,false);keys.recordRegistration(oldId,oldWire,true);
    // An unrelated retired alias is deliberately absent from CNG. Neither an
    // empty scoped table nor the valid gen2 enrollment may try to open it.
    registry.locked([&]{std::vector<DeviceKeyMetadata> entries;check(registry.read(entries));auto missing=entries[0];
        missing.id=Storage::newMessageId();missing.providerAlias=alias.name+".missing";missing.generation=1;missing.state=DeviceKeyState::Retired;
        missing.registration=DeviceKeyRegistration::NotRegistered;missing.signedBinding.clear();entries.insert(entries.begin(),missing);registry.replace(entries);});
    Contact origin{sender.profile.peerId,L"Preserved sender contact",sender.publicKey(),{},true};recipientStore.saveContact(origin);
    StoredMessage history;history.id=Storage::newMessageId();history.sender=origin.peerId;history.recipient=old.device;history.text=L"Original account history";history.utcMilliseconds=now;history.logicalOrder=1;history.delivery=Delivery::Delivered;check(recipientStore.append(origin.peerId,history));
    reliable::AuthenticatedFriendSnapshot account;account.replace(19,1,{{5,origin.peerId,{origin.peerId}}});
    auto queued=reliable::queueText(recipientStore,recipient.profile,&origin,origin.peerId,Storage::newMessageId(),L"Original queued message",now,&account);
    Blob retained;sql.locked([&]{check(sql.read(retained));});
    auto unchanged=[&]{Blob after;sql.locked([&]{check(sql.read(after));});check(after==retained&&provider.creates==1);
        auto contact=recipientStore.contacts();check(contact.size()==1&&contact[0].publicKey==origin.publicKey&&contact[0].verified);
        check(recipientStore.message(history.id)->text==history.text);auto pending=recipientStore.pending(origin.peerId);check(pending.size()==1&&pending[0].id==queued.id&&pending[0].recipient==queued.recipient);};
    const std::string scope="server.example:443/pin/"+std::string(64,'a'),otherScope="server.example:443/pin/"+std::string(64,'b');
    mailbox::ScopedEnrollment enrollment(keys,recipient,*database,protection,*hpke,verifyDevicePayload,Storage::publicKeyId);
    auto rowCount=[&]{std::int64_t n=0;registry.locked([&]{platform::CheckedSqlStatement s(*database,"SELECT COUNT(*) FROM mailbox_scoped_enrollment");check(s.row());n=s.number(0);});return n;};
    check(rowCount()==0&&enrollment.offer(scope,4)&&!enrollment.bindingFor(scope,4));check(rowCount()==0);unchanged(); // Denied/dismissed offer writes nothing.
    auto altered=old;altered.accountId=18;altered.signature=recipient.sign(mailbox::bindingPayload(altered));
    rejects([&]{enrollment.approve(scope,4,mailbox::serializeBinding(altered),now);});check(rowCount()==0);unchanged();
    RejectOpen failHpke(*hpke);mailbox::ScopedEnrollment failed(keys,recipient,*database,protection,failHpke,verifyDevicePayload,Storage::publicKeyId);
    rejects([&]{failed.approve(scope,4,oldWire,now);});check(rowCount()==0);unchanged();
    auto target=enrollment.approve(scope,4,oldWire,now);auto targetWire=mailbox::serializeBinding(target);
    check(target.accountId==4&&target.generation==1&&target.predecessorId.empty()&&target.keyId!=oldId&&target.publicKey==old.publicKey&&target.storageAllowed);
    check(rowCount()==1&&!enrollment.offer(scope,4));check(mailbox::serializeBinding(enrollment.approve(scope,4,oldWire,now+1))==targetWire);unchanged();
    rejects([&]{enrollment.withAccepted(scope,4,target,[](const auto&){});});
    auto wrong=target;wrong.storageAllowed=false;wrong.signature=recipient.sign(mailbox::bindingPayload(wrong));rejects([&]{enrollment.accepted(scope,4,wrong);});
    // Reopen an entirely new SQL connection/store/manager before retry/accept.
    {
        auto reopened=std::make_shared<storage_detail::WindowsSqlDatabase>((root/L"registry").wstring());platform::SqlKeyRegistryStore newSql(reopened);
        platform::ProtectedKeyRegistry newRegistry(newSql,protection,old.device);DeviceKeyManager newKeys(recipient,provider,newRegistry);
        mailbox::ScopedEnrollment restart(newKeys,recipient,*reopened,protection,*hpke,verifyDevicePayload,Storage::publicKeyId);
        check(mailbox::serializeBinding(*restart.bindingFor(scope,4))==targetWire);restart.accepted(scope,4,target);restart.accepted(scope,4,target);
        delivery::DeliveryManager deliveries(recipientStore,old.device);
        auto seal=[&](const mailbox::RecipientBinding& binding){mailbox::Header h;h.senderAccount=5;h.recipientAccount=binding.accountId;h.senderDevice=origin.peerId;h.recipientDevice=old.device;h.keyId=binding.keyId;h.keyGeneration=binding.generation;
            h.messageId=Storage::newMessageId();h.createdUtc=now;h.expiresUtc=now+600000;h.logicalOrder=10;h.storageAllowed=true;return mailbox::serialize(mailbox::seal(h,mailbox::textBody(Blob{'n','e','w'},now),binding,recipient.publicKey(),verifyDevicePayload,Storage::publicKeyId,sender,*hpke));};
        auto wire=seal(target),oldEnvelope=seal(old);
        rejects([&]{deliveries.acceptMailboxScoped(wire,5,origin,target,restart,scope,19,recipient,*hpke,now+1);});
        rejects([&]{deliveries.acceptMailboxScoped(wire,5,origin,target,restart,otherScope,4,recipient,*hpke,now+1);});
        rejects([&]{deliveries.acceptMailboxScoped(oldEnvelope,5,origin,target,restart,scope,4,recipient,*hpke,now+1);});
        auto received=deliveries.acceptMailboxScoped(wire,5,origin,target,restart,scope,4,recipient,*hpke,now+1);check(received.inserted);
        auto duplicate=deliveries.acceptMailboxScoped(wire,5,origin,target,restart,scope,4,recipient,*hpke,now+1);check(!duplicate.inserted&&duplicate.receipt==received.receipt);
        check(recipientStore.message(mailbox::parse(wire).header.messageId)->text==L"new");
        auto receipt=mailbox::parseReceipt(received.receipt);check(mailbox::verifyReceipt(receipt,4,old.device,origin.peerId,receipt.messageId,delivery::DeliveryManager::mailboxDigest(wire),recipient.publicKey(),verifyDevicePayload,Storage::publicKeyId));unchanged();
        // Corrupt only the supplemental protected row. Original source survives.
        Blob protectedRow;newRegistry.locked([&]{platform::CheckedSqlStatement q(*reopened,"SELECT b FROM mailbox_scoped_enrollment");check(q.row());protectedRow=q.bytes(0);
            auto corrupt=protectedRow;corrupt.back()^=1;platform::CheckedSqlStatement u(*reopened,"UPDATE mailbox_scoped_enrollment SET b=?");u.blob(1,corrupt);u.row();});
        rejects([&]{restart.bindingFor(scope,4);});unchanged();
        newRegistry.locked([&]{platform::CheckedSqlStatement u(*reopened,"UPDATE mailbox_scoped_enrollment SET b=?");u.blob(1,protectedRow);u.row();});
        auto secondary=restart.approve(otherScope,4,oldWire,now);restart.accepted(otherScope,4,secondary);
        check(secondary.keyId!=target.keyId&&secondary.publicKey==target.publicKey);
        restart.revoke(scope,4,target);restart.revoke(scope,4,target);
        rejects([&]{restart.bindingFor(scope,4);});rejects([&]{restart.offer(scope,4);});
        rejects([&]{restart.approve(scope,4,oldWire,now);});rejects([&]{restart.accepted(scope,4,target);});
        rejects([&]{restart.withAccepted(scope,4,target,[](const auto&){});});
        rejects([&]{deliveries.acceptMailboxScoped(wire,5,origin,target,restart,scope,4,recipient,*hpke,now+1);});
        auto secondaryWire=seal(secondary);
        auto secondaryReceive=deliveries.acceptMailboxScoped(secondaryWire,5,origin,secondary,restart,otherScope,4,recipient,*hpke,now+1);
        check(secondaryReceive.inserted&&recipientStore.message(mailbox::parse(wire).header.messageId)->text==L"new");
        auto secondaryDuplicate=deliveries.acceptMailboxScoped(secondaryWire,5,origin,secondary,restart,otherScope,4,recipient,*hpke,now+1);
        check(!secondaryDuplicate.inserted&&secondaryDuplicate.receipt==secondaryReceive.receipt);unchanged();
        newRegistry.locked([&]{std::vector<DeviceKeyMetadata> entries;check(newRegistry.read(entries));for(auto& entry:entries)if(entry.id==oldId)entry.state=DeviceKeyState::Retired;newRegistry.replace(entries);});
        unsigned admitted=0;restart.withAccepted(otherScope,4,secondary,[&](const auto&){++admitted;});check(admitted==1);
        newRegistry.locked([&]{std::vector<DeviceKeyMetadata> entries;check(newRegistry.read(entries));auto unrelated=entries.back();unrelated.id=Storage::newMessageId();unrelated.providerAlias=alias.name+".othermissing";
            unrelated.state=DeviceKeyState::Active;unrelated.generation=3;unrelated.predecessorId=oldId;
            auto proof=old;proof.keyId=unrelated.id;proof.generation=3;proof.predecessorId=oldId;proof.signature=recipient.sign(mailbox::bindingPayload(proof));unrelated.signedBinding=mailbox::serializeBinding(proof);entries.push_back(unrelated);newRegistry.replace(entries);});
        check(!restart.offer(otherScope,4)); // Existing accepted retired source wins; unrelated inaccessible Active is never opened.
        rejects([&]{restart.approve(otherScope,4,oldWire,now);}); // Retired material cannot approve an enrollment.
        newKeys.revokeRecipient(oldId);rejects([&]{restart.bindingFor(otherScope,4);});rejects([&]{restart.withAccepted(otherScope,4,secondary,[](const auto&){});});
    }
    hpke->shutdown();std::cout<<"PASS: explicit scoped enrollment reuses original opaque material; offer/denial no rows; failed possession no mutation; exact pending binding survives new SQL owner; accepted scope/account/source lease gates durable import and signed duplicate ACK; wrong scope/account/old wire/corruption fail closed; scoped revoke cannot resurrect and leaves another accepted scope operational; physical revoke blocks both; PKR1/contact/history/outbox preserved until explicit physical test revocation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
