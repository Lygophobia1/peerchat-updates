#include "../archive_history.hpp"
#include "../archive_session.hpp"
#include "../delivery_manager.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../platform/protected_key_registry.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>
#include <set>

using namespace peerchat;
namespace ar=peerchat::archive;

static void check(bool value){if(!value)throw std::runtime_error("Archive/mailbox receipt integration regression");}
template<class F>static void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected);}

struct Signing final:platform::IdentityProvider {
    Profile profile;explicit Signing(Profile value):profile(std::move(value)){}
    Blob publicKey()override{return profile.publicKey;}
    Blob sign(const Blob& value)override{return signDevicePayload(value,profile);}
};
struct RecipientKey final {
    std::string alias="PeerChat.Recipient.archive.mailbox."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetTickCount64());
    std::shared_ptr<platform::RecipientKeyHandle> handle;
    RecipientKey(){platform::WindowsRecipientKeyProvider provider;handle=provider.create(alias);}
    ~RecipientKey(){handle.reset();platform::recipient_cng::Object provider,key;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;
        auto name=platform::recipient_cng::alias(alias);
        if(NCryptOpenKey(provider.value,&key.value,name.c_str(),0,0)==0&&NCryptDeleteKey(key.value,0)==0)key.value=0;
    }
};
struct Registry final:DeviceKeyRegistry {
    std::vector<DeviceKeyMetadata> entries;std::mutex mutex;
    void locked(const std::function<void()>& operation)override{std::lock_guard<std::mutex> guard(mutex);operation();}
    bool read(std::vector<DeviceKeyMetadata>& output)override{output=entries;return !output.empty();}
    void replace(const std::vector<DeviceKeyMetadata>& value)override{entries=value;}
};

int main(int argc,char** argv){try{
    check(argc==2);const std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
    Storage source((root/L"archive-source").wstring()),recipient((root/L"recipient").wstring()),senderStore((root/L"sender").wstring());
    Signing sourceIdentity(source.profile()),recipientIdentity(recipient.profile()),senderIdentity(senderStore.profile());
    const auto sourceId=sourceIdentity.profile.peerId,recipientId=recipientIdentity.profile.peerId,senderId=senderIdentity.profile.peerId;
    // In production the authenticated friend/enrollment directory publishes
    // these ownership bindings before archive proofs or mailbox work run.
    recipient.rememberAuthenticatedDeviceAccount(sourceId,8);
    recipient.rememberAuthenticatedDeviceAccount(recipientId,8);
    recipient.rememberAuthenticatedDeviceAccount(senderId,9);
    std::set<std::string> ownDevices{sourceId,recipientId};std::map<std::string,ar::CurrentGrant> published;
    auto member=[&](std::uint64_t account,const std::string& device){return account==8&&ownDevices.count(device)>0;};
    auto grants=[&](std::uint64_t account,const std::string& issuer,const std::string& device)->std::optional<ar::CurrentGrant>{
        if(account!=8||issuer!=sourceId||published.count(device)==0)return {};
        return published.at(device);
    };
    auto directory=[&](const std::string& device)->std::optional<std::uint64_t>{
        if(device==senderId)return 9;
        if(ownDevices.count(device))return 8;
        return {};
    };
    ar::Authorization sourcePolicy(source,sourceIdentity.profile,8,member,grants),recipientPolicy(recipient,recipientIdentity.profile,8,member,grants);
    sourcePolicy.approveAuthority(sourceIdentity.profile.publicKey,true);recipientPolicy.approveAuthority(sourceIdentity.profile.publicKey,true);
    auto serve=sourcePolicy.issue(sourceIdentity.profile.publicKey,ar::AllCapabilities,false,100,true);
    auto read=sourcePolicy.issue(recipientIdentity.profile.publicKey,ar::AllCapabilities,false,100,true);
    for(const auto& grant:{serve,read}){published[grant.device]={grant.generation,grant.capabilities,grant.issuanceId,grant.revoked};recipientPolicy.accept(grant);}
    auto scope=ar::sessionAuthorization(recipientPolicy,sourceId,sourceId,recipientId);
    ar::ProvenanceVerifier prover(sourcePolicy,source,sourceIdentity.profile,8,directory);
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    StoredMessage original{Storage::newMessageId(),senderId,sourceId,L"Archive-first sibling \u2603",now,Delivery::Delivered,0,23};
    check(source.append(senderId,original));
    auto assertion=prover.attest({original,9,8},sourceId,now);
    delivery::DeliveryManager receiver(recipient,recipientId);
    check(receiver.acceptArchiveHistory(recipientIdentity.profile,recipientPolicy,scope,assertion,directory));
    check(!receiver.acceptArchiveHistory(recipientIdentity.profile,recipientPolicy,scope,assertion,directory));
    auto archived=recipient.message(original.id);
    check(archived&&archived->recipient==sourceId&&archived->delivery==Delivery::Sent&&recipient.pending(senderId).empty());
    check(!recipient.archiveHistoryProof(original.id).empty());

    RecipientKey privateKey;Registry registry;platform::WindowsRecipientKeyProvider provider;
    mailbox::RecipientBinding binding;binding.accountId=8;binding.device=recipientId;binding.keyId=std::string(32,'a');
    binding.generation=1;binding.createdUtc=now;binding.publicKey=privateKey.handle->publicKey();binding.storageAllowed=true;
    binding.signature=recipientIdentity.sign(mailbox::bindingPayload(binding));
    DeviceKeyMetadata metadata;metadata.id=binding.keyId;metadata.providerAlias=privateKey.alias;
    metadata.algorithm=DeviceKeyManager::recipientSuite;metadata.generation=1;metadata.createdUtcMilliseconds=now;
    metadata.state=DeviceKeyState::Active;metadata.registration=DeviceKeyRegistration::Accepted;
    metadata.publicKey=binding.publicKey;metadata.signedBinding=mailbox::serializeBinding(binding);registry.entries.push_back(metadata);
    DeviceKeyManager keys(recipientIdentity,provider,registry);
    auto hpke=platform::createShippingHpke();
    mailbox::Header header;header.senderAccount=9;header.recipientAccount=8;header.senderDevice=senderId;
    header.recipientDevice=recipientId;header.keyId=binding.keyId;header.keyGeneration=1;header.messageId=original.id;
    header.createdUtc=now;header.expiresUtc=now+600000;header.logicalOrder=original.logicalOrder;header.storageAllowed=true;
    auto encoded=unicode::encode(original.text);
    Blob text(encoded.begin(),encoded.end());
    auto body=mailbox::textBody(text,original.utcMilliseconds);
    auto wire=mailbox::serialize(mailbox::seal(header,body,binding,recipientIdentity.publicKey(),verifyDevicePayload,
        Storage::publicKeyId,senderIdentity,*hpke));
    auto digest=delivery::DeliveryManager::mailboxDigest(wire);
    check(recipient.mailboxReceipt(original.id,senderId,digest).empty()); // History alone is never a device ACK.
    Contact verifiedSender{senderId,L"Verified sender",senderIdentity.publicKey(),{},true};
    auto changed=body;changed=mailbox::textBody(Blob{'w','r','o','n','g'},original.utcMilliseconds);
    auto changedWire=mailbox::serialize(mailbox::seal(header,changed,binding,recipientIdentity.publicKey(),verifyDevicePayload,
        Storage::publicKeyId,senderIdentity,*hpke));
    rejects([&]{receiver.acceptMailbox(changedWire,9,verifiedSender,binding,keys,recipientIdentity,*hpke,now+100);});
    check(recipient.mailboxReceipt(original.id,senderId,delivery::DeliveryManager::mailboxDigest(changedWire)).empty());
    rejects([&]{receiver.acceptMailbox(wire,10,verifiedSender,binding,keys,recipientIdentity,*hpke,now+100);});
    check(recipient.mailboxReceipt(original.id,senderId,digest).empty());
    auto tampered=wire;tampered.back()^=1;
    rejects([&]{receiver.acceptMailbox(tampered,9,verifiedSender,binding,keys,recipientIdentity,*hpke,now+100);});
    auto result=receiver.acceptMailbox(wire,9,verifiedSender,binding,keys,recipientIdentity,*hpke,now+100);
    check(!result.inserted&&!result.receipt.empty()&&recipient.mailboxReceipt(original.id,senderId,digest)==result.receipt);
    check(mailbox::verifyReceipt(mailbox::parseReceipt(result.receipt),8,recipientId,senderId,original.id,digest,
        recipientIdentity.publicKey(),verifyDevicePayload,Storage::publicKeyId));
    auto repeated=receiver.acceptMailbox(wire,9,verifiedSender,binding,keys,recipientIdentity,*hpke,now+100);
    check(!repeated.inserted&&repeated.receipt==result.receipt);
    {
        Storage reopened((root/L"recipient").wstring());delivery::DeliveryManager resumed(reopened,recipientId);
        check(reopened.archiveHistoryProof(original.id)==recipient.archiveHistoryProof(original.id));
        auto replay=resumed.acceptMailbox(wire,9,verifiedSender,binding,keys,recipientIdentity,*hpke,now+100);
        check(!replay.inserted&&replay.receipt==result.receipt&&reopened.history(senderId).size()==1);
    }
    hpke->shutdown();
    std::cout<<"PASS archive-first sibling proof plus actual authenticated mailbox receive creates one durable signed receipt; replay/restart dedupe; wrong content/account/tamper rejected; history alone grants no ACK\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
