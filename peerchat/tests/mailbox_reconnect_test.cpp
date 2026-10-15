#include "../storage.cpp"
#include "../platform/sql_key_registry.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../mailbox_key_coordinator.hpp"
#include "../delivery_manager.hpp"
#include "../reliable_peer_protocol.hpp"
#include <fstream>
#include <iostream>
using namespace peerchat;
static void check(bool value){if(!value)throw std::runtime_error("Offline mailbox reconnect regression failed");}
struct Signing final : platform::IdentityProvider {
    Profile profile;explicit Signing(Profile value):profile(std::move(value)){}
    Blob publicKey()override{return profile.publicKey;}
    Blob sign(const Blob& value)override{return signDevicePayload(value,profile);}
};
struct TestAlias final {
    std::string value="PeerChat.Recipient.test.reconnect."+Storage::newMessageId();
    ~TestAlias(){platform::recipient_cng::Object provider,key;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;
        auto name=platform::recipient_cng::alias(value);
        if(NCryptOpenKey(provider.value,&key.value,name.c_str(),0,0)==0&&NCryptDeleteKey(key.value,0)==0)key.value=0;
    }
};
static Blob read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);check(bool(file));return Blob(std::istreambuf_iterator<char>(file),{});}
static void write(const std::filesystem::path& path,const Blob& bytes){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());check(bool(file));}
int main(int argc,char** argv){try{
    check(argc==2);std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
    const auto senderPath=root/L"sender",recipientPath=root/L"recipient",registryPath=root/L"recipient-registry";
    const auto spool=root/L"server-ciphertext.bin";
    TestAlias alias;auto hpke=platform::createShippingHpke();
    Blob senderPublic,recipientPublic,bindingWire,receipt;
    std::string senderId,recipientId,messageId;
    mailbox::RecipientBinding binding;
    constexpr std::uint64_t now=1700000000000ULL;
    const std::wstring text=L"Offline reconnect mailbox \u2603";
    {
        Storage store(recipientPath.wstring());Signing identity(store.profile());
        recipientPublic=identity.publicKey();recipientId=identity.profile.peerId;
        auto database=std::make_shared<storage_detail::WindowsSqlDatabase>(registryPath.wstring());
        platform::SqlKeyRegistryStore sql(database);platform::WindowsRecordProtector protection;
        platform::ProtectedKeyRegistry registry(sql,protection,recipientId);
        platform::WindowsRecipientKeyProvider provider;DeviceKeyManager keys(identity,provider,registry);
        mailbox::KeyCoordinator coordinator(keys,identity,verifyDevicePayload,Storage::publicKeyId);
        binding=coordinator.prepare(4,true,[&](std::uint32_t generation){check(generation==1);return alias.value;},Storage::newMessageId,now);
        coordinator.accepted(binding);bindingWire=mailbox::serializeBinding(binding);
    } // Recipient profile, registry, coordinator and opaque key handles close.
    {
        Storage store(senderPath.wstring());Signing identity(store.profile());
        senderPublic=identity.publicKey();senderId=identity.profile.peerId;
        Contact destination{recipientId,L"Isolated recipient",recipientPublic,{},true};store.saveContact(destination);
        reliable::AuthenticatedFriendSnapshot account;account.replace(7,1,{{4,recipientId,{recipientId}}});
        auto message=reliable::queueText(store,identity.profile,&destination,recipientId,Storage::newMessageId(),text,now,&account);
        messageId=message.id;delivery::DeliveryManager deliveries(store,senderId);
        auto encrypted=deliveries.prepareMailbox(messageId,7,4,binding,destination,identity,*hpke,now,true,false);
        check(!encrypted.empty());write(spool,encrypted);
        deliveries.mailboxStored(messageId,recipientId,encrypted);
        check(store.message(messageId)->delivery==Delivery::Stored);
        check(store.mailboxEnvelope(messageId,recipientId)==encrypted);
    } // Sender disconnect: no sender Storage, identity or delivery owner remains.

    // Server transport/auth are simulated by an immutable ciphertext disk spool
    // and fixed authenticated account/contact metadata. Native crypto, protected
    // registry, message transaction, dedupe and signed receipt are production code.
    for(unsigned restart=0;restart<3;++restart){
        check(std::filesystem::exists(spool));
        Storage store(recipientPath.wstring());Signing identity(store.profile());
        check(identity.publicKey()==recipientPublic&&identity.profile.peerId==recipientId);
        Contact origin{senderId,L"Isolated sender",senderPublic,{},true};store.saveContact(origin);
        auto database=std::make_shared<storage_detail::WindowsSqlDatabase>(registryPath.wstring());
        platform::SqlKeyRegistryStore sql(database);platform::WindowsRecordProtector protection;
        platform::ProtectedKeyRegistry registry(sql,protection,recipientId);
        platform::WindowsRecipientKeyProvider provider;DeviceKeyManager keys(identity,provider,registry);
        mailbox::KeyCoordinator coordinator(keys,identity,verifyDevicePayload,Storage::publicKeyId);
        auto reopened=coordinator.prepare(4,true,[&](std::uint32_t){throw std::runtime_error("Unexpected recipient reprovision");return alias.value;},Storage::newMessageId,now+100);
        check(mailbox::serializeBinding(reopened)==bindingWire);coordinator.accepted(reopened);
        check(keys.snapshot().size()==1&&keys.snapshot()[0].providerAlias==alias.value);
        delivery::DeliveryManager deliveries(store,recipientId);
        if(restart==0){
            // Transient fetch disconnect before receipt: spool remains untouched.
            check(!store.message(messageId));continue;
        }
        auto received=deliveries.acceptMailbox(read(spool),7,origin,reopened,keys,identity,*hpke,now+100);
        check(received.inserted==(restart==1));
        check(store.message(messageId)&&store.message(messageId)->text==text);
        if(restart==1)receipt=received.receipt; // ACK lost: do not remove spool.
        else check(received.receipt==receipt); // Restart/replay yields exact ACK.
        check(std::filesystem::exists(spool));
    }
    {
        Storage store(senderPath.wstring());Signing identity(store.profile());
        check(identity.publicKey()==senderPublic&&store.message(messageId)->delivery==Delivery::Stored);
        check(store.mailboxEnvelope(messageId,recipientId)==read(spool));
        delivery::DeliveryManager deliveries(store,senderId);
        Contact destination{recipientId,L"Isolated recipient",recipientPublic,{},true};
        check(deliveries.acknowledgeMailbox(receipt,4,destination));
        check(!deliveries.acknowledgeMailbox(receipt,4,destination));
        check(store.message(messageId)->delivery==Delivery::Delivered);
    }
    hpke->shutdown();
    std::cout<<"PASS: offline ciphertext stored; sender closed; recipient profile/protected SQL registry reopened across fetch disconnect and lost ACK; same binding/key retained; real HPKE decrypt and durable history; replay returns identical signed receipt; reopened sender reconciles exactly once. Transport/server auth simulated by ciphertext spool.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
