#include "../storage.cpp"
#include "../platform/sql_key_registry.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../mailbox_key_coordinator.hpp"
#include "../delivery_manager.hpp"
#include "../reliable_peer_protocol.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace peerchat;
static void check(bool value){if(!value)throw std::runtime_error("Isolated reenrollment regression failed");}
template<class F>static void rejects(F operation){bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed);}
struct Signing final : platform::IdentityProvider {
    Profile profile;explicit Signing(Profile value):profile(std::move(value)){}
    Blob publicKey()override{return profile.publicKey;}
    Blob sign(const Blob& value)override{return signDevicePayload(value,profile);}
};
struct Alias final {
    std::string name="PeerChat.Recipient.test.reenrollment."+Storage::newMessageId();
    ~Alias(){platform::recipient_cng::Object provider,key;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;
        auto wide=platform::recipient_cng::alias(name);
        if(NCryptOpenKey(provider.value,&key.value,wide.c_str(),0,0)==0&&NCryptDeleteKey(key.value,0)==0)key.value=0;
    }
};
static void write(const std::filesystem::path& root,const char* name,const Blob& bytes){std::ofstream file(root/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());check(bool(file));}
int main(int argc,char** argv){try{
    check(argc==2);std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
    auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    Storage recipientStore((root/L"synthetic-recipient").wstring()),senderStore((root/L"synthetic-sender").wstring());
    Signing recipient(recipientStore.profile()),sender(senderStore.profile());Alias alias;
    auto database=std::make_shared<storage_detail::WindowsSqlDatabase>((root/L"synthetic-retained-registry").wstring());
    platform::SqlKeyRegistryStore sql(database);platform::WindowsRecordProtector protection;
    platform::ProtectedKeyRegistry registry(sql,protection,recipient.profile.peerId);
    platform::WindowsRecipientKeyProvider provider;DeviceKeyManager keys(recipient,provider,registry);
    const auto oldId=Storage::newMessageId(),genesisId=Storage::newMessageId();auto metadata=keys.provisionRecipient(oldId,alias.name,2,now);
    registry.locked([&]{std::vector<DeviceKeyMetadata> entries;check(registry.read(entries));entries[0].predecessorId=genesisId;registry.replace(entries);});
    mailbox::RecipientBinding old;old.accountId=19;old.device=recipient.profile.peerId;old.keyId=oldId;old.generation=2;
    old.createdUtc=now;old.publicKey=metadata.publicKey;old.storageAllowed=true;old.predecessorId=genesisId;
    old.signature=recipient.sign(mailbox::bindingPayload(old));auto oldWire=mailbox::serializeBinding(old);
    keys.recordRegistration(oldId,oldWire,false);keys.recordRegistration(oldId,oldWire,true);
    Contact origin{sender.profile.peerId,L"Retained verified contact",sender.publicKey(),{},true};recipientStore.saveContact(origin);
    StoredMessage history;history.id=Storage::newMessageId();history.sender=sender.profile.peerId;history.recipient=recipient.profile.peerId;
    history.text=L"Retained account19 history";history.utcMilliseconds=now;history.logicalOrder=1;history.delivery=Delivery::Delivered;
    check(recipientStore.append(origin.peerId,history));
    reliable::AuthenticatedFriendSnapshot account;account.replace(19,1,{{5,origin.peerId,{origin.peerId}}});
    auto queued=reliable::queueText(recipientStore,recipient.profile,&origin,origin.peerId,Storage::newMessageId(),L"Retained account19 queued text",now,&account);
    const auto pending=recipientStore.pending(origin.peerId);check(pending.size()==1&&pending[0].id==queued.id);
    Blob retained;sql.locked([&]{check(sql.read(retained));});
    auto unchanged=[&]{
        Blob after;sql.locked([&]{check(sql.read(after));});check(after==retained);
        auto entries=keys.snapshot();check(entries.size()==1&&entries[0].id==oldId&&entries[0].providerAlias==alias.name&&entries[0].generation==2&&entries[0].signedBinding==oldWire&&entries[0].registration==DeviceKeyRegistration::Accepted);
        check(recipientStore.profile().publicKey==recipient.publicKey());
        auto contacts=recipientStore.contacts();check(contacts.size()==1&&contacts[0].peerId==origin.peerId&&contacts[0].publicKey==origin.publicKey&&contacts[0].verified);
        check(recipientStore.message(history.id)->text==history.text&&recipientStore.message(history.id)->logicalOrder==history.logicalOrder);
        auto queue=recipientStore.pending(origin.peerId);check(queue.size()==1&&queue[0].id==queued.id&&queue[0].recipient==queued.recipient&&queue[0].text==queued.text&&queue[0].delivery==Delivery::Queued);
    };
    mailbox::KeyCoordinator coordinator(keys,recipient,verifyDevicePayload,Storage::publicKeyId);
    mailbox::KeyPreparationStage stage=mailbox::KeyPreparationStage::RegistrySnapshot;
    rejects([&]{coordinator.prepare(4,true,[&](std::uint32_t){throw std::runtime_error("Reprovision forbidden");return alias.name;},Storage::newMessageId,now,[&](auto value){stage=value;});});
    check(stage==mailbox::KeyPreparationStage::BindingAccount);unchanged();

    // TEST-ONLY proposed binding. No enrollment adapter, registry write, server
    // migration, consent decision, or production authorization is implemented.
    auto proposed=old;proposed.accountId=4;proposed.keyId=Storage::newMessageId();proposed.generation=1;proposed.predecessorId.clear();
    proposed.signature=recipient.sign(mailbox::bindingPayload(proposed));auto proposedWire=mailbox::serializeBinding(proposed);
    check(proposed.publicKey==old.publicKey&&proposed.keyId!=old.keyId);
    check(mailbox::verifyBinding(proposed,4,old.device,recipient.publicKey(),verifyDevicePayload,Storage::publicKeyId));
    auto hpke=platform::createShippingHpke();auto opaque=keys.openRecipient(oldId);
    check(opaque->publicKey()==old.publicKey);
    Blob nonce(32);check(BCryptGenRandom(nullptr,nonce.data(),nonce.size(),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0);
    mailbox::Writer context("PeerChat/test-reenrollment-possession/v1");context.text(std::string(64,'a'));context.number(4,8);context.text(old.device);
    context.field(digestDevicePayload(oldWire));context.field(digestDevicePayload(proposedWire));context.field(nonce);
    auto challenge=hpke->seal(old.publicKey,context.bytes,context.bytes,nonce);
    auto answer=hpke->open(opaque,old.publicKey,context.bytes,context.bytes,challenge);check(answer==nonce);
    auto changedContext=context.bytes;changedContext.back()^=1;
    rejects([&]{hpke->open(opaque,old.publicKey,changedContext,context.bytes,challenge);});
    SecureZeroMemory(answer.data(),answer.size());SecureZeroMemory(nonce.data(),nonce.size());
    rejects([&]{keys.recordRegistration(oldId,proposedWire,false);});unchanged();

    Blob plain=mailbox::textBody(Blob{'i','s','o','l','a','t','e','d'},now);
    auto envelope=[&](const mailbox::RecipientBinding& binding){mailbox::Header h;h.senderAccount=5;h.recipientAccount=binding.accountId;
        h.senderDevice=sender.profile.peerId;h.recipientDevice=old.device;h.keyId=binding.keyId;h.keyGeneration=binding.generation;
        h.messageId=Storage::newMessageId();h.createdUtc=now;h.expiresUtc=now+3600000;h.logicalOrder=99;h.storageAllowed=true;
        return mailbox::seal(h,plain,binding,recipient.publicKey(),verifyDevicePayload,Storage::publicKeyId,sender,*hpke);};
    auto oldEnvelope=envelope(old),newEnvelope=envelope(proposed);
    auto newEnvelopeWire=mailbox::serialize(newEnvelope);
    check(mailbox::open(newEnvelope,proposed,5,sender.publicKey(),verifyDevicePayload,Storage::publicKeyId,opaque,*hpke,now+1)==plain);
    rejects([&]{mailbox::open(oldEnvelope,proposed,5,sender.publicKey(),verifyDevicePayload,Storage::publicKeyId,opaque,*hpke,now+1);});
    delivery::DeliveryManager deliveries(recipientStore,recipient.profile.peerId);
    rejects([&]{deliveries.acceptMailbox(newEnvelopeWire,5,origin,proposed,keys,recipient,*hpke,now+1);});
    // Demonstrate the existing immutable-binding guard even when the proposed
    // key ID is forced to the old ID; no missing-ID shortcut explains rejection.
    auto forced=proposed;forced.keyId=oldId;forced.signature=recipient.sign(mailbox::bindingPayload(forced));
    auto forcedEnvelope=envelope(forced);
    rejects([&]{deliveries.acceptMailbox(mailbox::serialize(forcedEnvelope),5,origin,forced,keys,recipient,*hpke,now+1);});
    check(!recipientStore.message(newEnvelope.header.messageId)&&!recipientStore.message(forcedEnvelope.header.messageId));unchanged();
    mailbox::Receipt receipt;receipt.recipientAccount=4;receipt.recipientDevice=old.device;receipt.senderDevice=sender.profile.peerId;
    receipt.messageId=newEnvelope.header.messageId;receipt.envelopeDigest=digestDevicePayload(newEnvelopeWire);
    receipt.signature=recipient.sign(mailbox::receiptPayload(receipt));auto receiptWire=mailbox::serializeReceipt(receipt);
    check(mailbox::verifyReceipt(receipt,4,old.device,sender.profile.peerId,receipt.messageId,receipt.envelopeDigest,recipient.publicKey(),verifyDevicePayload,Storage::publicKeyId));
    auto badGeneration=proposed;badGeneration.generation=2;badGeneration.signature=recipient.sign(mailbox::bindingPayload(badGeneration));
    auto oldGenesis=old;oldGenesis.generation=1;oldGenesis.keyId=genesisId;oldGenesis.predecessorId.clear();oldGenesis.signature=recipient.sign(mailbox::bindingPayload(oldGenesis));
    write(root,"recipient-public.bin",recipient.publicKey());write(root,"sender-public.bin",sender.publicKey());
    write(root,"old-binding19.bin",oldWire);write(root,"new-binding4.bin",proposedWire);
    write(root,"old-envelope19.bin",mailbox::serialize(oldEnvelope));write(root,"new-envelope4.bin",newEnvelopeWire);
    write(root,"new-receipt4.bin",receiptWire);write(root,"new-binding4-invalid-generation.bin",mailbox::serializeBinding(badGeneration));
    write(root,"old-genesis-binding19.bin",mailbox::serializeBinding(oldGenesis));
    opaque.reset();hpke->shutdown();
    std::cout<<"PASS: retained account19/gen2 binding fails current account4 at binding_account without protected-byte/history/contact/queue change; same opaque key passes scoped HPKE self-challenge and low-level account4 decrypt; old-account replay rejected; immutable registration/durable receive guards still reject proposed enrollment. TEST-ONLY crypto fixtures, no production reenrollment adapter or migration.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
