#include "../mailbox_protocol.hpp"
#include "../group.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../platform/protected_key_registry.hpp"
#include "../delivery_manager.hpp"
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace peerchat;
static void check(bool ok){if(!ok)throw std::runtime_error("Mailbox protocol regression failed");}
template<class F>static void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected);}
struct Signing : platform::IdentityProvider {
    Profile profile;explicit Signing(Profile p):profile(std::move(p)){}
    Blob publicKey()override{return profile.publicKey;}
    Blob sign(const Blob& data)override{return signDevicePayload(data,profile);}
};
struct RecipientFixture {
    std::string alias="PeerChat.Recipient.test.protocol."+std::to_string(GetCurrentProcessId())+"."+std::to_string(GetTickCount64());
    std::shared_ptr<platform::RecipientKeyHandle> key;
    RecipientFixture(){platform::WindowsRecipientKeyProvider provider;key=provider.create(alias);}
    ~RecipientFixture(){key.reset();platform::recipient_cng::Object provider,opened;
        if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;
        auto name=platform::recipient_cng::alias(alias);
        if(NCryptOpenKey(provider.value,&opened.value,name.c_str(),0,0)==0&&NCryptDeleteKey(opened.value,0)==0)opened.value=0;
    }
};
struct RegistryFixture : DeviceKeyRegistry {
    std::vector<DeviceKeyMetadata> entries;std::mutex gate;
    void locked(const std::function<void()>& f)override{std::lock_guard<std::mutex> lock(gate);f();}
    bool read(std::vector<DeviceKeyMetadata>& out)override{out=entries;return !out.empty();}
    void replace(const std::vector<DeviceKeyMetadata>& in)override{entries=in;}
};
int main(int argc,char** argv){try{
    check(argc==2);std::filesystem::path root(argv[1]);std::filesystem::create_directories(root);
    Storage senderStore((root/L"sender").wstring()),recipientStore((root/L"recipient").wstring()),outsiderStore((root/L"outsider").wstring());
    Signing sender(senderStore.profile()),recipient(recipientStore.profile()),outsider(outsiderStore.profile());
    auto senderId=sender.profile.peerId,recipientId=recipient.profile.peerId;
    RecipientFixture fixture;auto hpke=platform::createShippingHpke();
    mailbox::Verify verify=verifyDevicePayload;mailbox::Fingerprint fingerprint=Storage::publicKeyId;
    mailbox::RecipientBinding binding;binding.accountId=8;binding.device=recipientId;binding.keyId=std::string(32,'a');binding.generation=1;
    binding.createdUtc=100;binding.publicKey=fixture.key->publicKey();binding.storageAllowed=true;binding.signature=recipient.sign(mailbox::bindingPayload(binding));
    check(mailbox::verifyBinding(binding,8,recipientId,recipient.publicKey(),verify,fingerprint));
    check(!mailbox::verifyBinding(binding,9,recipientId,recipient.publicKey(),verify,fingerprint));
    auto substituted=binding;substituted.publicKey[1]^=1;check(!mailbox::verifyBinding(substituted,8,recipientId,recipient.publicKey(),verify,fingerprint));
    substituted.signature=outsider.sign(mailbox::bindingPayload(substituted));check(!mailbox::verifyBinding(substituted,8,recipientId,recipient.publicKey(),verify,fingerprint));
    mailbox::Header header;header.senderAccount=7;header.recipientAccount=8;header.senderDevice=senderId;header.recipientDevice=recipientId;
    auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    header.keyId=binding.keyId;header.keyGeneration=1;header.messageId=Storage::newMessageId();header.createdUtc=now;header.expiresUtc=now+600000;header.logicalOrder=11;header.storageAllowed=true;
    Blob plain={'s','y','n','t','h','e','t','i','c',0,'t','e','x','t'};
    auto value=mailbox::seal(header,plain,binding,recipient.publicKey(),verify,fingerprint,sender,*hpke);
    auto wire=mailbox::serialize(value);auto parsed=mailbox::parse(wire);
    check(mailbox::open(parsed,binding,7,sender.publicKey(),verify,fingerprint,fixture.key,*hpke,now+100)==plain);
    check(std::search(wire.begin(),wire.end(),plain.begin(),plain.end())==wire.end());
    rejects([&]{mailbox::open(parsed,binding,9,sender.publicKey(),verify,fingerprint,fixture.key,*hpke,now+100);});
    rejects([&]{mailbox::open(parsed,binding,7,outsider.publicKey(),verify,fingerprint,fixture.key,*hpke,now+100);});
    rejects([&]{mailbox::open(parsed,binding,7,sender.publicKey(),verify,fingerprint,fixture.key,*hpke,header.expiresUtc);});
    auto bad=parsed;bad.encrypted.ciphertext[0]^=1;rejects([&]{mailbox::open(bad,binding,7,sender.publicKey(),verify,fingerprint,fixture.key,*hpke,now+100);});
    bad=parsed;bad.header.logicalOrder++;rejects([&]{mailbox::open(bad,binding,7,sender.publicKey(),verify,fingerprint,fixture.key,*hpke,now+100);});
    // Even an authentic signature cannot transplant ciphertext between message IDs.
    bad.signature=sender.sign(mailbox::signingPayload(bad));rejects([&]{mailbox::open(bad,binding,7,sender.publicKey(),verify,fingerprint,fixture.key,*hpke,now+100);});
    for(std::size_t i=0;i<wire.size();++i)rejects([&]{mailbox::parse(Blob(wire.begin(),wire.begin()+i));});
    auto suffix=wire;suffix.push_back(0);rejects([&]{mailbox::parse(suffix);});
    auto forbidden=header;forbidden.storageAllowed=false;rejects([&]{mailbox::seal(forbidden,plain,binding,recipient.publicKey(),verify,fingerprint,sender,*hpke);});
    auto noConsent=binding;noConsent.storageAllowed=false;noConsent.signature=recipient.sign(mailbox::bindingPayload(noConsent));
    rejects([&]{mailbox::seal(header,plain,noConsent,recipient.publicKey(),verify,fingerprint,sender,*hpke);});
    mailbox::Receipt receipt;receipt.recipientAccount=8;receipt.recipientDevice=recipientId;receipt.senderDevice=senderId;receipt.messageId=header.messageId;
    receipt.envelopeDigest=Blob(32,1);receipt.signature=recipient.sign(mailbox::receiptPayload(receipt));
    check(mailbox::verifyReceipt(receipt,8,recipientId,senderId,header.messageId,Blob(32,1),recipient.publicKey(),verify,fingerprint));
    check(!mailbox::verifyReceipt(receipt,8,recipientId,senderId,header.messageId,Blob(32,2),recipient.publicKey(),verify,fingerprint));
    receipt.signature=outsider.sign(mailbox::receiptPayload(receipt));check(!mailbox::verifyReceipt(receipt,8,recipientId,senderId,header.messageId,Blob(32,1),recipient.publicKey(),verify,fingerprint));
    auto bindingWire=mailbox::serializeBinding(binding);check(mailbox::parseBinding(bindingWire).device==recipientId);
    auto overflow=binding;overflow.generation=~std::uint64_t{0};rejects([&]{mailbox::bindingPayload(overflow);});
    overflow=binding;overflow.createdUtc=~std::uint64_t{0};rejects([&]{mailbox::bindingPayload(overflow);});
    rejects([&]{mailbox::textBody(plain,~std::uint64_t{0});});
    check(mailbox::parseBindingBatch(mailbox::bindingBatch({bindingWire})).size()==1);
    rejects([&]{mailbox::bindingBatch(std::vector<Blob>(33,bindingWire));});
    auto digestHex=Storage::publicKeyId(wire);receipt.envelopeDigest.clear();
    for(std::size_t i=0;i<digestHex.size();i+=2)receipt.envelopeDigest.push_back(static_cast<unsigned char>(std::stoul(digestHex.substr(i,2),nullptr,16)));
    receipt.signature=recipient.sign(mailbox::receiptPayload(receipt));
    auto receiptWire=mailbox::serializeReceipt(receipt);check(mailbox::parseReceipt(receiptWire).messageId==header.messageId);
    for(std::size_t i=0;i<bindingWire.size();++i)rejects([&]{mailbox::parseBinding(Blob(bindingWire.begin(),bindingWire.begin()+i));});
    for(std::size_t i=0;i<receiptWire.size();++i)rejects([&]{mailbox::parseReceipt(Blob(receiptWire.begin(),receiptWire.begin()+i));});
    // Content-free signed fixtures for the independent managed server parser.
    auto write=[&](const char* name,const Blob& bytes){std::ofstream file(root/name,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(static_cast<bool>(file));};
    write("sender-public.bin",sender.publicKey());write("recipient-public.bin",recipient.publicKey());write("binding.bin",bindingWire);write("envelope.bin",wire);write("receipt.bin",receiptWire);
    mailbox::Revocation revoke;revoke.accountId=8;revoke.device=recipientId;revoke.keyId=binding.keyId;revoke.generation=1;
    revoke.signature=recipient.sign(mailbox::revocationPayload(revoke));auto revocationWire=mailbox::serializeRevocation(revoke);
    check(mailbox::parseRevocation(revocationWire).device==recipientId);write("revocation.bin",revocationWire);
    std::filesystem::create_directories(root/"quota");
    for(unsigned i=0;i<257;++i){auto h=header;h.messageId=Storage::newMessageId();h.logicalOrder=1000+i;
        auto name="quota/small-"+std::to_string(i)+".bin";write(name.c_str(),mailbox::serialize(mailbox::seal(h,plain,binding,recipient.publicKey(),verify,fingerprint,sender,*hpke)));}
    for(unsigned i=0;i<129;++i){auto h=header;h.messageId=Storage::newMessageId();h.logicalOrder=2000+i;
        auto name="quota/large-"+std::to_string(i)+".bin";write(name.c_str(),mailbox::serialize(mailbox::seal(h,Blob(mailbox::MaxBody,0x51),binding,recipient.publicKey(),verify,fingerprint,sender,*hpke)));}
    RecipientFixture rotationKey;auto rotated=binding;rotated.keyId=std::string(32,'c');rotated.predecessorId=binding.keyId;rotated.generation=2;rotated.publicKey=rotationKey.key->publicKey();rotated.signature=recipient.sign(mailbox::bindingPayload(rotated));
    write("rotated-binding.bin",mailbox::serializeBinding(rotated));auto nextHeader=header;nextHeader.keyId=rotated.keyId;nextHeader.keyGeneration=2;nextHeader.messageId=Storage::newMessageId();
    write("rotated-envelope.bin",mailbox::serialize(mailbox::seal(nextHeader,plain,rotated,recipient.publicKey(),verify,fingerprint,sender,*hpke)));

    // DeliveryManager remains the durable owner, not a mailbox transport shim.
    RegistryFixture registry;DeviceKeyMetadata metadata;metadata.id=binding.keyId;metadata.providerAlias=fixture.alias;
    metadata.algorithm=DeviceKeyManager::recipientSuite;metadata.generation=1;metadata.createdUtcMilliseconds=100;
    metadata.state=DeviceKeyState::Active;metadata.registration=DeviceKeyRegistration::Accepted;
    metadata.publicKey=binding.publicKey;metadata.signedBinding=bindingWire;registry.entries.push_back(metadata);
    platform::WindowsRecipientKeyProvider cng;DeviceKeyManager keys(recipient,cng,registry);
    Contact destination{recipientId,L"Synthetic recipient",recipient.publicKey(),{},true};
    Contact origin{senderId,L"Synthetic sender",sender.publicKey(),{},true};
    StoredMessage message{Storage::newMessageId(),senderId,recipientId,L"Durable mailbox text \u2603",now,Delivery::Queued,0,17};
    check(senderStore.append(recipientId,message));delivery::DeliveryManager outgoing(senderStore,senderId),incoming(recipientStore,recipientId);
    check(outgoing.prepareMailbox(message.id,7,8,binding,destination,sender,*hpke,now,false,false).empty());
    check(outgoing.prepareMailbox(message.id,7,8,binding,destination,sender,*hpke,now,true,true).empty());
    auto submitted=outgoing.prepareMailbox(message.id,7,8,binding,destination,sender,*hpke,now,true,false);
    RecipientFixture secondDeviceKey;auto secondBinding=binding;secondBinding.device=outsider.profile.peerId;secondBinding.keyId=std::string(32,'b');secondBinding.publicKey=secondDeviceKey.key->publicKey();secondBinding.signature=outsider.sign(mailbox::bindingPayload(secondBinding));
    Contact secondDevice{outsider.profile.peerId,L"Same-account authorized second device",outsider.publicKey(),{},true};
    rejects([&]{outgoing.prepareMailbox(message.id,7,8,secondBinding,secondDevice,sender,*hpke,now,true,false);});
    auto secondWire=outgoing.prepareMailbox(message.id,7,8,secondBinding,secondDevice,sender,*hpke,now,true,false,&destination);
    check(secondWire!=submitted&&mailbox::parse(secondWire).header.messageId==message.id);
    check(senderStore.pendingMailboxUploads().size()==1);
    check(outgoing.prepareMailbox(message.id,7,8,binding,destination,sender,*hpke,now+1,true,false)==submitted);
    check(senderStore.message(message.id)->delivery==Delivery::Queued);
    rejects([&]{outgoing.mailboxStored(message.id,recipientId,wire);});
    outgoing.mailboxStored(message.id,recipientId,submitted);check(senderStore.message(message.id)->delivery==Delivery::Stored&&outgoing.pendingMessages(recipientId).size()==1);
    auto received=incoming.acceptMailbox(submitted,7,origin,binding,keys,recipient,*hpke,now+100);
    check(received.inserted&&recipientStore.message(message.id)->text==message.text);
    auto repeated=incoming.acceptMailbox(submitted,7,origin,binding,keys,recipient,*hpke,now+100);
    check(!repeated.inserted&&repeated.receipt==received.receipt);
    check(!outgoing.acknowledgeMailbox(received.receipt,9,destination));
    check(outgoing.acknowledgeMailbox(received.receipt,8,destination));check(!outgoing.acknowledgeMailbox(received.receipt,8,destination));
    check(senderStore.message(message.id)->delivery==Delivery::Delivered&&outgoing.pendingMessages(recipientId).empty());
    // A first device ACK drains the logical outbox but cannot discard another
    // authorized device's durable unsent envelope. Same ID survives a reopen.
    check(senderStore.pendingMailboxUploads().size()==1);
    {
        Storage reopened((root/L"sender").wstring());delivery::DeliveryManager retry(reopened,senderId);
        check(reopened.pendingMailboxUploads().size()==1);
        check(retry.prepareMailbox(message.id,7,8,secondBinding,secondDevice,sender,*hpke,now+100,true,false,&destination)==secondWire);
        retry.mailboxStored(message.id,secondDevice.peerId,secondWire);
        check(reopened.pendingMailboxUploads().empty()&&reopened.message(message.id)->delivery==Delivery::Delivered);
    }
    senderStore.acknowledge(message.id,Delivery::Stored);check(senderStore.message(message.id)->delivery==Delivery::Delivered);
    // Live ACK wins the same durable ID; mailbox replay remains exactly once.
    message.id=Storage::newMessageId();message.logicalOrder=18;check(senderStore.append(recipientId,message));
    submitted=outgoing.prepareMailbox(message.id,7,8,binding,destination,sender,*hpke,now,true,false);
    outgoing.mailboxStored(message.id,recipientId,submitted);
    check(outgoing.authorizeTransport({900},recipientId)&&outgoing.submittedMessage(recipientId,message.id,{900}));
    check(outgoing.acknowledgeMessage(recipientId,{900},message.id));
    received=incoming.acceptMailbox(submitted,7,origin,binding,keys,recipient,*hpke,now+100);
    check(!outgoing.acknowledgeMailbox(received.receipt,8,destination));
    keys.revokeRecipient(binding.keyId);rejects([&]{incoming.acceptMailbox(submitted,7,origin,binding,keys,recipient,*hpke,now+100);});
    senderStore.eraseHistory(recipientId);check(senderStore.mailboxEnvelope(message.id,recipientId).empty());
    hpke->shutdown();
    std::cout<<"PASS: canonical binding/device signatures, opaque HPKE envelopes, identity/AAD/tamper/expiry/permission rejection, strict parse, recipient receipts and independent multi-device uploads surviving first ACK/restart\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
