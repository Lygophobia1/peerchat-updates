#define main legacy_mailbox_test_main
#include "mailbox_protocol_test.cpp"
#undef main
#include "../dm_attachment_receipt.hpp"
#include "attachment_thumbnail_fixture.hpp"
#include "../dm_attachment_finalizer.hpp"
#include "../dm_attachment_application.hpp"
#include "../group_attachment_body.hpp"
#include "../windows_transfer_io.hpp"
struct MemorySource:transfer::Source {
 Blob bytes;explicit MemorySource(Blob b):bytes(std::move(b)){}
 std::uint64_t size()const override{return bytes.size();}std::string name()const override{return u8"private-📷-日本.png";}
 Blob read(std::uint64_t offset,size_t count)override{check(offset<=bytes.size());return Blob(bytes.begin()+offset,bytes.begin()+std::min<std::uint64_t>(bytes.size(),offset+count));}
};
struct MemorySink:transfer::Sink {Blob bytes;bool committed=false;void append(const Blob& b)override{bytes.insert(bytes.end(),b.begin(),b.end());}void commit()override{committed=true;}void discard()override{bytes.clear();committed=false;}};
struct UniqueRecipientFixture {
 std::string alias="PeerChat.Recipient.test.dm."+Storage::newMessageId();std::shared_ptr<platform::RecipientKeyHandle> key;
 UniqueRecipientFixture(){platform::WindowsRecipientKeyProvider provider;key=provider.create(alias);}
 ~UniqueRecipientFixture(){key.reset();platform::recipient_cng::Object provider,opened;if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;auto name=platform::recipient_cng::alias(alias);if(NCryptOpenKey(provider.value,&opened.value,name.c_str(),0,0)==0&&NCryptDeleteKey(opened.value,0)==0)opened.value=0;}
};
#include "dm_attachment_application_fixture.hpp"
#include "dm_attachment_storage_fixture.hpp"
#include "dm_attachment_preflight_fixture.hpp"
int main(int argc,char** argv){try{
 check(argc==2);auto root=std::filesystem::path(argv[1]);Storage senderStore((root/L"sender").wstring()),receiverStore((root/L"receiver").wstring()),siblingStore((root/L"sibling").wstring());Signing sender(senderStore.profile()),receiver(receiverStore.profile()),sibling(siblingStore.profile());UniqueRecipientFixture sourceKey,targetKey,siblingKey;auto hpke=platform::createShippingHpke();
 auto binding=[](Signing& s,UniqueRecipientFixture& k,std::uint64_t account,char id){mailbox::RecipientBinding b;b.accountId=account;b.device=s.profile.peerId;b.keyId=std::string(32,id);b.generation=1;b.createdUtc=100;b.publicKey=k.key->publicKey();b.storageAllowed=true;b.signature=s.sign(mailbox::bindingPayload(b));return b;};
 auto source=binding(sender,sourceKey,7,'a'),target=binding(receiver,targetKey,8,'b'),other=binding(sibling,siblingKey,8,'d');std::string relationship(32,'c');protocol::Writer audience(protocol::FileTransfer);audience.string("PeerChat/dm-cache-audience/v1");audience.string(relationship);audience.bytes(mailbox::serializeBinding(source));std::vector<mailbox::RecipientBinding> frozen{target,other};std::sort(frozen.begin(),frozen.end(),[](const auto& a,const auto& b){return a.device<b.device;});audience.number(frozen.size());for(const auto& b:frozen)audience.bytes(mailbox::serializeBinding(b));
 mailbox::Header header;header.senderAccount=7;header.senderDevice=sender.profile.peerId;header.recipientAccount=8;header.recipientDevice=receiver.profile.peerId;header.keyId=target.keyId;header.keyGeneration=1;header.messageId=Storage::newMessageId();header.createdUtc=1000;header.expiresUtc=2000;header.logicalOrder=9;header.storageAllowed=true;
 // Sibling was frozen at send time, then revoked: sender validation retains
 // its signature check; receiver authorization concerns source + local target.
 dm_attachments::Evidence evidence;evidence.identity=[&](std::uint64_t a,const std::string& d){if(a==7&&d==sender.profile.peerId)return sender.publicKey();if(a==8&&d==receiver.profile.peerId)return receiver.publicKey();if(a==8&&d==sibling.profile.peerId)return sibling.publicKey();return Blob{};};evidence.current=[&](const auto& b,const std::string& rel,std::uint64_t otherAccount){return rel==relationship&&((b.accountId==7&&otherAccount==8&&mailbox::serializeBinding(b)==mailbox::serializeBinding(source))||(b.accountId==8&&otherAccount==7&&mailbox::serializeBinding(b)==mailbox::serializeBinding(target)));};
 group_attachments::Binding context;context.attachment=Storage::newMessageId();context.sender=sender.profile.peerId;context.account=7;context.direct=true;context.recipientAccount=8;context.relationship=relationship;context.audienceDigest=Storage::publicKeyId(audience.data);
 MemorySource plain(Blob(70001,73));MemorySink cipher;transfer::windows::Sha256 ph,ch;auto manifest=group_attachments::seal(plain,cipher,context,ph,ch);
 auto signedDescriptor=dm_attachments::descriptor(manifest,header.messageId,sender.profile);dm_attachments::Body body{990,9,"image/png",std::move(manifest),signedDescriptor,audience.data,mailbox::headerPayload(header)};
 auto privateBody=dm_attachments::encodeBody(body,header);auto envelope=mailbox::seal(header,privateBody.bytes,target,receiver.publicKey(),verifyDevicePayload,Storage::publicKeyId,sender,*hpke);auto wire=mailbox::serialize(envelope);auto recovered=dm_attachments::openBody(mailbox::parse(wire),target,evidence,targetKey.key,*hpke,1100);check(recovered.manifest.name==plain.name());
 // A fresh recipient need not trust another device in the sender's frozen
 // audience. No sibling identity is imported, looked up or authorized here.
 {
  auto fresh=evidence;unsigned siblingLookups=0;
  fresh.identity=[&](std::uint64_t account,const std::string& device){
   if(account==8&&device==sibling.profile.peerId){++siblingLookups;return Blob{};}
   return evidence.identity(account,device);
  };
  auto opened=dm_attachments::openBody(envelope,target,fresh,targetKey.key,*hpke,1100);
  check(opened.manifest.key==body.manifest.key&&siblingLookups==0);
  dm_attachments::validateReceivedBody(opened,header,fresh);check(siblingLookups==0);
  // Sender validation remains strict, including every sibling identity.
  rejects([&]{dm_attachments::validateBody(opened,header,&fresh);});check(siblingLookups>0);
  for(const auto& missing:{source.device,target.device}){
   auto denied=fresh;denied.identity=[&,missing](auto account,const auto& device){return device==missing?Blob{}:fresh.identity(account,device);};
   rejects([&]{dm_attachments::openBody(envelope,target,denied,targetKey.key,*hpke,1100);});
   denied.identity=[&,missing](auto account,const auto& device){return device==missing?sibling.publicKey():fresh.identity(account,device);};
   rejects([&]{dm_attachments::openBody(envelope,target,denied,targetKey.key,*hpke,1100);});
   denied=fresh;denied.current=[&,missing](const auto& binding,const auto& rel,auto counterpart){return binding.device!=missing&&fresh.current(binding,rel,counterpart);};
   rejects([&]{dm_attachments::openBody(envelope,target,denied,targetKey.key,*hpke,1100);});
  }
  rejects([&]{dm_attachments::openBody(envelope,target,fresh,targetKey.key,*hpke,2001);});
  rejects([&]{dm_attachments::openBody(envelope,other,fresh,siblingKey.key,*hpke,1100);});
  auto altered=dm_attachments::decodeStoredBody(privateBody.bytes,header);altered.audience.back()^=1;rejects([&]{dm_attachments::validateReceivedBody(altered,header,fresh);});
  auto alteredDescriptor=dm_attachments::decodeStoredBody(privateBody.bytes,header);alteredDescriptor.descriptor.back()^=1;rejects([&]{dm_attachments::validateReceivedBody(alteredDescriptor,header,fresh);});
  auto wrong=header;wrong.recipientAccount=9;rejects([&]{dm_attachments::validateReceivedBody(opened,wrong,fresh);});
  wrong=header;wrong.keyGeneration++;rejects([&]{dm_attachments::validateReceivedBody(opened,wrong,fresh);});
  std::cout<<"PASS exact cached recipient accepts unknown sibling without weakening sender, local key, audience or expiry checks\n";
 }
 {
  auto photo=dm_attachments::decodeStoredBody(privateBody.bytes,header);photo.thumbnail=peerchat::tests::thumbnailFixture();check(attachment_thumbnails::valid(photo.thumbnail));
  auto encoded=dm_attachments::encodeBody(photo,header);protocol::Reader version(encoded.bytes);version.type();check(version.string()=="PeerChat/dm-attachment-private/v2");
  auto sealed=mailbox::seal(header,encoded.bytes,target,receiver.publicKey(),verifyDevicePayload,Storage::publicKeyId,sender,*hpke);auto ciphertext=mailbox::serialize(sealed);
  check(std::search(ciphertext.begin(),ciphertext.end(),photo.thumbnail.begin(),photo.thumbnail.end())==ciphertext.end());
  auto opened=dm_attachments::openBody(sealed,target,evidence,targetKey.key,*hpke,1100);check(opened.thumbnail==photo.thumbnail&&opened.manifest.key==body.manifest.key);
  auto restart=dm_attachments::decodeStoredBody(encoded.bytes,header);check(restart.thumbnail==photo.thumbnail);dm_attachments::validateBody(restart,header,&evidence);
  photo.thumbnail.resize(2049);rejects([&]{dm_attachments::encodeBody(photo,header);});photo.thumbnail=Blob{1,2,3};rejects([&]{dm_attachments::encodeBody(photo,header);});
  photo.thumbnail.clear();check(dm_attachments::encodeBody(photo,header).bytes==privateBody.bytes);
  sealed.encrypted.ciphertext.back()^=1;rejects([&]{dm_attachments::openBody(sealed,target,evidence,targetKey.key,*hpke,1100);});
 }
 MemorySource encrypted(cipher.bytes);MemorySink output;transfer::windows::Sha256 p2,c2;group_attachments::open(encrypted,output,recovered.manifest,p2,c2);check(output.committed&&output.bytes==plain.bytes);
 auto stored=dm_attachments::decodeStoredBody(privateBody.bytes,header);check(stored.manifest.key==body.manifest.key);dm_attachments::validateBody(stored,header,&evidence);
 {MemorySource input(cipher.bytes);MemorySink sink;transfer::windows::Sha256 p,c;dm_attachments::Finalizer finalizer(input,sink,stored.manifest,p,c);check(!finalizer.completion()&&finalizer.processedBytes()==0);std::size_t steps=0;while(!finalizer.completion()){auto before=sink.bytes.size();bool done=finalizer.step();check(sink.bytes.size()-before<=group_attachments::BlockBytes);check(done==finalizer.completion());if(!done)check(!sink.committed);++steps;}check(steps==stored.manifest.blocks()+2&&sink.committed&&sink.bytes==plain.bytes&&finalizer.processedBytes()==plain.size());check(finalizer.step());rejects([&]{finalizer.cancel();});check(sink.committed&&sink.bytes==plain.bytes);}
 {auto bytes=cipher.bytes;bytes.back()^=1;MemorySource input(bytes);MemorySink sink;transfer::windows::Sha256 p,c;dm_attachments::Finalizer finalizer(input,sink,stored.manifest,p,c);rejects([&]{while(!finalizer.step()){};});check(!sink.committed&&sink.bytes.empty()&&!finalizer.completion());rejects([&]{finalizer.step();});}
 {auto bytes=cipher.bytes;bytes.pop_back();MemorySource input(bytes);MemorySink sink;transfer::windows::Sha256 p,c;rejects([&]{dm_attachments::Finalizer finalizer(input,sink,stored.manifest,p,c);});check(!sink.committed&&sink.bytes.empty());}
 {MemorySource input(cipher.bytes);MemorySink sink;transfer::windows::Sha256 p,c;dm_attachments::Finalizer finalizer(input,sink,stored.manifest,p,c);check(!finalizer.step());check(!finalizer.step()&&!sink.bytes.empty());finalizer.cancel();check(sink.bytes.empty()&&!sink.committed);rejects([&]{finalizer.step();});}
 {MemorySource input(cipher.bytes);MemorySink sink;transfer::windows::Sha256 p,c;{dm_attachments::Finalizer finalizer(input,sink,stored.manifest,p,c);finalizer.step();finalizer.step();check(!sink.bytes.empty());}check(sink.bytes.empty()&&!sink.committed);}
 {auto wrongHash=dm_attachments::decodeStoredBody(privateBody.bytes,header);wrongHash.manifest.plainDigest[0]^=1;MemorySource input(cipher.bytes);MemorySink sink;transfer::windows::Sha256 p,c;dm_attachments::Finalizer finalizer(input,sink,wrongHash.manifest,p,c);rejects([&]{while(!finalizer.step()){};});check(sink.bytes.empty()&&!sink.committed);}
 auto name=plain.name();check(std::search(wire.begin(),wire.end(),name.begin(),name.end())==wire.end());check(std::search(wire.begin(),wire.end(),body.manifest.key.begin(),body.manifest.key.end())==wire.end());rejects([&]{mailbox::parseTextBody(privateBody.bytes);});rejects([&]{group_attachments::decodeBody(privateBody.bytes);});
 for(std::size_t i=0;i<privateBody.bytes.size();++i)rejects([&]{dm_attachments::decodeBody(Blob(privateBody.bytes.begin(),privateBody.bytes.begin()+i),header,evidence);});
 auto changed=privateBody.bytes;changed.push_back(0);rejects([&]{dm_attachments::decodeBody(changed,header,evidence);});auto badHeader=header;badHeader.messageId=Storage::newMessageId();rejects([&]{dm_attachments::decodeBody(privateBody.bytes,badHeader,evidence);});badHeader=header;badHeader.recipientAccount=9;rejects([&]{dm_attachments::decodeBody(privateBody.bytes,badHeader,evidence);});badHeader=header;badHeader.keyGeneration++;rejects([&]{dm_attachments::decodeBody(privateBody.bytes,badHeader,evidence);});badHeader=header;badHeader.recipientDevice=std::string(64,'e');rejects([&]{dm_attachments::decodeBody(privateBody.bytes,badHeader,evidence);});
 auto denied=evidence;denied.current=[](const auto&,const auto&,auto){return false;};rejects([&]{dm_attachments::openBody(envelope,target,denied,targetKey.key,*hpke,1100);});denied=evidence;denied.identity=[](auto,const auto&){return Blob{};};rejects([&]{dm_attachments::decodeBody(privateBody.bytes,header,denied);});
 auto descriptor=body.descriptor;body.descriptor.back()^=1;rejects([&]{dm_attachments::encodeBody(body,header);});body.descriptor=descriptor;auto fullAudience=body.audience;body.audience.back()^=1;rejects([&]{dm_attachments::encodeBody(body,header);});body.audience=fullAudience;
 auto receipt=dm_attachments::verifiedReceipt(recovered,header,1200,receiver.profile);auto receiptWire=dm_attachments::serializeReceipt(receipt);check(dm_attachments::verifyReceipt(dm_attachments::parseReceipt(receiptWire),body,header,evidence));check(std::search(receiptWire.begin(),receiptWire.end(),body.manifest.plainDigest.begin(),body.manifest.plainDigest.end())==receiptWire.end());
 auto badReceipt=receipt;badReceipt.signature.back()^=1;check(!dm_attachments::verifyReceipt(badReceipt,body,header,evidence));badReceipt=receipt;badReceipt.recipientAccount=9;badReceipt.signature=receiver.sign(dm_attachments::receiptPayload(badReceipt));check(!dm_attachments::verifyReceipt(badReceipt,body,header,evidence));badReceipt=receipt;badReceipt.audienceDigest=std::string(64,'e');badReceipt.signature=receiver.sign(dm_attachments::receiptPayload(badReceipt));check(!dm_attachments::verifyReceipt(badReceipt,body,header,evidence));check(!dm_attachments::verifyReceipt(receipt,body,header,denied));rejects([&]{mailbox::parseReceipt(receiptWire);});for(std::size_t i=0;i<receiptWire.size();++i)rejects([&]{dm_attachments::parseReceipt(Blob(receiptWire.begin(),receiptWire.begin()+i));});
 mailbox::Receipt notice;notice.recipientAccount=8;notice.recipientDevice=header.recipientDevice;notice.senderDevice=header.senderDevice;notice.messageId=header.messageId;notice.envelopeDigest=Blob(32,1);notice.signature=receiver.sign(mailbox::receiptPayload(notice));rejects([&]{dm_attachments::parseReceipt(mailbox::serializeReceipt(notice));});
 auto fixture=[&](const char* name,const Blob& value){std::ofstream file(root/name,std::ios::binary);file.write(reinterpret_cast<const char*>(value.data()),value.size());check(bool(file));};
 fixture("descriptor.bin",body.descriptor);fixture("audience.bin",body.audience);fixture("ciphertext.bin",cipher.bytes);fixture("notice.bin",wire);fixture("verifiedreceipt.bin",receiptWire);fixture("sender-public.bin",sender.publicKey());fixture("recipient-public.bin",receiver.publicKey());fixture("source-binding.bin",mailbox::serializeBinding(source));fixture("recipient-binding.bin",mailbox::serializeBinding(target));
 for(const auto& recipient:frozen){auto h=header;h.recipientDevice=recipient.device;h.keyId=recipient.keyId;h.keyGeneration=recipient.generation;auto copy=dm_attachments::decodeStoredBody(privateBody.bytes,header);copy.mailboxBinding=mailbox::headerPayload(h);auto p=dm_attachments::encodeBody(copy,h);auto identity=recipient.device==receiver.profile.peerId?receiver.publicKey():sibling.publicKey();auto e=mailbox::seal(h,p.bytes,recipient,identity,verifyDevicePayload,Storage::publicKeyId,sender,*hpke);auto filename="notice-"+recipient.device+".bin";fixture(filename.c_str(),mailbox::serialize(e));}
 // Legacy group codec remains byte-compatible and rejects DM substitution.
 group_attachments::Binding group{Storage::newMessageId(),Storage::newMessageId(),sender.profile.peerId,7,1};MemorySink gc;transfer::windows::Sha256 gp,gh;auto gm=group_attachments::seal(plain,gc,group,gp,gh);group_attachments::Body gb{990,9,group_attachments::Presentation::Picture,"image/png",std::move(gm)};auto encodedGroup=group_attachments::encodeBody(gb);auto decodedGroup=group_attachments::decodeBody(encodedGroup.bytes);check(!decodedGroup.manifest.binding.direct&&decodedGroup.manifest.name==plain.name());rejects([&]{dm_attachments::decodeBody(encodedGroup.bytes,header,evidence);});
 applicationFixture(senderStore,receiverStore,sender,receiver,source,target,targetKey.key,*hpke,evidence);
 storageFixture(root);
 preflightAudienceFixture(root,sender,receiver,sibling,source,target,other,*hpke,evidence);
 hpke->shutdown();std::cout<<"PASS DM private HPKE body, real blob roundtrip, Unicode, exact audience/header, current evidence, signature/scope tamper, distinct verified receipt, legacy group codec\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
