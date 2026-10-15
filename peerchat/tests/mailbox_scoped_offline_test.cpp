#include "../storage.cpp"
#include "../platform/sql_key_registry.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../mailbox_scoped_enrollment.hpp"
#include "../delivery_manager.hpp"
#include "../reliable_peer_protocol.hpp"
#include <fstream>
#include <iostream>
using namespace peerchat;
static void check(bool value){if(!value)throw std::runtime_error("Scoped offline restart regression failed");}
struct Signing final : platform::IdentityProvider {
    Profile profile;explicit Signing(Profile value):profile(std::move(value)){}
    Blob publicKey()override{return profile.publicKey;}
    Blob sign(const Blob& value)override{return signDevicePayload(value,profile);}
};
struct TestAlias final {
    std::string value="PeerChat.Recipient.test.scoped-offline."+Storage::newMessageId();
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
    const auto senderPath=root/L"sender",recipientPath=root/L"recipient",registryPath=root/L"recipient-registry",spool=root/L"server-ciphertext.bin";
    const std::string scope="scoped-offline.example:443/pin/"+std::string(64,'a');
    TestAlias alias;auto hpke=platform::createShippingHpke();
    Blob senderPublic,recipientPublic,sourceWire,targetWire,receipt,sourceProtected,materialPublic,storedWire;
    std::string senderId,recipientId,messageId,materialId;
    mailbox::RecipientBinding target;
    constexpr std::uint64_t now=1700000000000ULL;
    const std::wstring text=L"Scoped offline delivery after complete restart \u2603";
    {
        Storage store(recipientPath.wstring());Signing identity(store.profile());recipientPublic=identity.publicKey();recipientId=identity.profile.peerId;
        auto database=std::make_shared<storage_detail::WindowsSqlDatabase>(registryPath.wstring());
        platform::SqlKeyRegistryStore sql(database);platform::WindowsRecordProtector protection;
        platform::ProtectedKeyRegistry registry(sql,protection,recipientId);platform::WindowsRecipientKeyProvider provider;DeviceKeyManager keys(identity,provider,registry);
        mailbox::KeyCoordinator coordinator(keys,identity,verifyDevicePayload,Storage::publicKeyId);
        auto source=coordinator.prepare(19,true,[&](std::uint32_t generation){check(generation==1);return alias.value;},Storage::newMessageId,now);
        coordinator.accepted(source);sourceWire=mailbox::serializeBinding(source);materialId=source.keyId;materialPublic=source.publicKey;
        sql.locked([&]{check(sql.read(sourceProtected));});
        mailbox::ScopedEnrollment enrollment(keys,identity,*database,protection,*hpke,verifyDevicePayload,Storage::publicKeyId);
        check(enrollment.offer(scope,4).has_value());target=enrollment.approve(scope,4,sourceWire,now);targetWire=mailbox::serializeBinding(target);
        check(target.accountId==4&&target.keyId!=materialId&&target.publicKey==materialPublic);
        enrollment.accepted(scope,4,target); // Simulated exact authenticated server acceptance.
        Blob unchanged;sql.locked([&]{check(sql.read(unchanged));});check(unchanged==sourceProtected);
    } // Entire recipient Storage, registry, identity wrapper and key managers close.
    {
        Storage store(senderPath.wstring());Signing identity(store.profile());senderPublic=identity.publicKey();senderId=identity.profile.peerId;
        Contact destination{recipientId,L"Scoped offline recipient",recipientPublic,{},true};store.saveContact(destination);
        reliable::AuthenticatedFriendSnapshot account;account.replace(5,1,{{4,recipientId,{recipientId}}});
        auto message=reliable::queueText(store,identity.profile,&destination,recipientId,Storage::newMessageId(),text,now,&account);messageId=message.id;
        delivery::DeliveryManager deliveries(store,senderId);
        storedWire=deliveries.prepareMailbox(messageId,5,4,target,destination,identity,*hpke,now,true,false);check(!storedWire.empty());
        write(spool,storedWire);deliveries.mailboxStored(messageId,recipientId,storedWire);
        check(store.message(messageId)->delivery==Delivery::Stored&&store.mailboxEnvelope(messageId,recipientId)==storedWire);
    } // Sender is fully disconnected before recipient fetch/reconnect attempts.
    // Network/server execution and account metadata are explicitly simulated.
    // The immutable disk spool substitutes for a server retaining ciphertext.
    // Every profile, protected registry, enrollment, HPKE and delivery operation
    // below uses the actual native implementation with newly opened owners.
    for(unsigned restart=0;restart<3;++restart){
        check(read(spool)==storedWire);
        Storage store(recipientPath.wstring());Signing identity(store.profile());check(identity.publicKey()==recipientPublic&&identity.profile.peerId==recipientId);
        Contact origin{senderId,L"Scoped offline sender",senderPublic,{},true};store.saveContact(origin);
        auto database=std::make_shared<storage_detail::WindowsSqlDatabase>(registryPath.wstring());
        platform::SqlKeyRegistryStore sql(database);platform::WindowsRecordProtector protection;
        platform::ProtectedKeyRegistry registry(sql,protection,recipientId);platform::WindowsRecipientKeyProvider provider;DeviceKeyManager keys(identity,provider,registry);
        Blob retained;sql.locked([&]{check(sql.read(retained));});check(retained==sourceProtected);
        auto entries=keys.snapshot();check(entries.size()==1&&entries[0].id==materialId&&entries[0].providerAlias==alias.value&&entries[0].publicKey==materialPublic&&entries[0].signedBinding==sourceWire&&entries[0].registration==DeviceKeyRegistration::Accepted);
        mailbox::ScopedEnrollment enrollment(keys,identity,*database,protection,*hpke,verifyDevicePayload,Storage::publicKeyId);
        auto reopened=enrollment.bindingFor(scope,4);check(reopened&&mailbox::serializeBinding(*reopened)==targetWire&&!enrollment.offer(scope,4));
        delivery::DeliveryManager deliveries(store,recipientId);
        if(restart==0){check(!store.message(messageId));continue;} // Fetch connection fails; ciphertext remains on spool.
        auto received=deliveries.acceptMailboxScoped(read(spool),5,origin,*reopened,enrollment,scope,4,identity,*hpke,now+100);
        check(received.inserted==(restart==1));check(store.message(messageId)&&store.message(messageId)->text==text);
        if(restart==1)receipt=received.receipt; // ACK connection fails after durable history commit; retain ciphertext.
        else check(received.receipt==receipt); // Full restart replay returns exact durable signed receipt.
        Blob unchanged;sql.locked([&]{check(sql.read(unchanged));});check(unchanged==sourceProtected&&read(spool)==storedWire);
    } // Recipient owners close again before sender can reconcile the signed ACK.
    {
        Storage store(senderPath.wstring());Signing identity(store.profile());
        check(identity.publicKey()==senderPublic&&store.message(messageId)->delivery==Delivery::Stored&&store.mailboxEnvelope(messageId,recipientId)==storedWire);
        delivery::DeliveryManager deliveries(store,senderId);Contact destination{recipientId,L"Scoped offline recipient",recipientPublic,{},true};
        check(deliveries.acknowledgeMailbox(receipt,4,destination));check(!deliveries.acknowledgeMailbox(receipt,4,destination));
        check(store.message(messageId)->delivery==Delivery::Delivered);
    }
    hpke->shutdown();
    std::cout<<"PASS: old account19 PKR1 preserved; explicit account4 enrollment reuses exact opaque material; sender stores ciphertext and closes; full recipient profile/registry/enrollment restarts survive fetch failure and lost ACK; scoped decrypt commits once, duplicate returns exact signed receipt; full sender reopen reconciles once. Server transport/auth simulated by immutable ciphertext spool.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
