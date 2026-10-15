#include "../group_attachment_key_envelope.hpp"
#include "../windows_transfer_io.hpp"
#include "../platform/windows_recipient_key.hpp"
#include "../platform/windows_sql_database.hpp"
#include "../platform/sql_key_registry.hpp"
#include "../platform/windows_record_protection.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace peerchat;
namespace keys=group_attachments::keys;
static void check(bool ok){if(!ok)throw std::runtime_error("Attachment key regression failed");}
template<class F>static void rejects(F operation){bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}check(rejected);}
struct Signing:platform::IdentityProvider {Profile profile;explicit Signing(Profile p):profile(std::move(p)){}Blob publicKey()override{return profile.publicKey;}Blob sign(const Blob& bytes)override{return signDevicePayload(bytes,profile);}};
struct Source:transfer::Source {Blob bytes;explicit Source(size_t size):bytes(size,123){}std::uint64_t size()const override{return bytes.size();}std::string name()const override{return u8"private-📷-日本.png";}Blob read(std::uint64_t offset,size_t count)override{return Blob(bytes.begin()+offset,bytes.begin()+offset+count);}};
struct Sink:transfer::Sink {Blob bytes;bool committed=false;void append(const Blob& value)override{bytes.insert(bytes.end(),value.begin(),value.end());}void commit()override{committed=true;}void discard()override{bytes.clear();committed=false;}};
struct Key {
 std::string alias="PeerChat.Recipient.test.attachment."+Storage::newMessageId();std::shared_ptr<platform::RecipientKeyHandle> handle;
 Key(){platform::WindowsRecipientKeyProvider provider;handle=provider.create(alias);}
 ~Key(){handle.reset();platform::recipient_cng::Object provider,opened;if(NCryptOpenStorageProvider(&provider.value,MS_KEY_STORAGE_PROVIDER,0)!=0)return;auto name=platform::recipient_cng::alias(alias);if(NCryptOpenKey(provider.value,&opened.value,name.c_str(),0,0)==0&&NCryptDeleteKey(opened.value,0)==0)opened.value=0;}
};
static void write(const std::filesystem::path& path,const Blob& value){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(value.data()),value.size());check(bool(file));}
static void managedRoundTrip(const std::filesystem::path& root,const std::string& assembly){
 auto command=L"dotnet \""+unicode::decode(assembly)+L"\" --native-keys \""+root.wstring()+L"\" \""+(root/L"managed-cache").wstring()+L"\"";
 STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION process{};check(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&start,&process));auto waited=WaitForSingleObject(process.hProcess,60000);if(waited!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,2);WaitForSingleObject(process.hProcess,5000);}DWORD exit=1;bool success=waited==WAIT_OBJECT_0&&GetExitCodeProcess(process.hProcess,&exit)&&exit==0;CloseHandle(process.hThread);CloseHandle(process.hProcess);check(success);
}
static Blob read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);check(bool(file));return Blob(std::istreambuf_iterator<char>(file),{});}
static void run(const std::filesystem::path& root,bool sameAccount,const std::string& managed){
 Storage sourceStore((root/L"source").wstring()),recipientStore((root/L"recipient").wstring());Signing source(sourceStore.profile()),recipient(recipientStore.profile());auto recipientAccount=sameAccount?1u:2u;
 auto local=newGroupId(),server=newGroupId();GroupLedger ledger(true);GroupEvent e;e.groupId=local;e.logicalOrder=1;e.kind=GroupEventKind::Create;e.actorAccountId=e.subjectAccountId=1;e.actorPeerId=e.subjectPeerId=source.profile.peerId;e.subjectPublicKey=source.publicKey();e.subjectName=L"Source";e.value=L"Attachment fixture";signGroupEvent(e,source.profile);check(ledger.append(e));
 e={};e.groupId=local;e.logicalOrder=2;e.actorAccountId=1;e.actorPeerId=source.profile.peerId;e.subjectAccountId=recipientAccount;e.subjectPeerId=recipient.profile.peerId;e.subjectPublicKey=recipient.publicKey();e.subjectName=L"Recipient";
 if(sameAccount){e.kind=GroupEventKind::AuthorizeDevice;e.value=L"PeerChat/group-device-authorization/v1";signGroupEvent(e,source.profile);check(ledger.append(e));}
 else{e.kind=GroupEventKind::Invite;signGroupEvent(e,source.profile);check(ledger.append(e));e={};e.groupId=local;e.logicalOrder=3;e.kind=GroupEventKind::Accept;e.actorAccountId=e.subjectAccountId=2;e.actorPeerId=e.subjectPeerId=recipient.profile.peerId;signGroupEvent(e,recipient.profile);check(ledger.append(e));}
 auto state=ledger.state();group_keys::RosterSnapshot original{local,server,source.profile.peerId,2,{{1,source.profile.peerId,GroupRole::Owner,false},{recipientAccount,recipient.profile.peerId,sameAccount?GroupRole::Owner:GroupRole::Member,false}}};auto current=original;current.generation=3;
 keys::Authority authority{state,original,state,current,std::string(32,'a'),std::string(32,'b'),std::string(32,'b'),std::string(32,'a'),601000};
 Key key;platform::WindowsRecipientKeyProvider provider;mailbox::RecipientBinding binding;binding.accountId=recipientAccount;binding.device=recipient.profile.peerId;binding.keyId=Storage::newMessageId();binding.generation=1;binding.createdUtc=100;binding.publicKey=key.handle->publicKey();binding.storageAllowed=true;binding.signature=recipient.sign(mailbox::bindingPayload(binding));
 platform::WindowsRecordProtector protection;auto database=platform::openWindowsSqlDatabase((root/L"recipient"/L"key-registry").wstring());platform::SqlKeyRegistryStore store(database);platform::ProtectedKeyRegistry registry(store,protection,recipient.profile.peerId);
 DeviceKeyMetadata metadata;metadata.id=binding.keyId;metadata.providerAlias=key.alias;metadata.algorithm=DeviceKeyManager::recipientSuite;metadata.generation=1;metadata.createdUtcMilliseconds=100;metadata.state=DeviceKeyState::Active;metadata.registration=DeviceKeyRegistration::Accepted;metadata.publicKey=binding.publicKey;metadata.signedBinding=mailbox::serializeBinding(binding);registry.locked([&]{registry.replace({metadata});});
 DeviceKeyManager manager(recipient,provider,registry);auto hpke=platform::createShippingHpke();Source file(1048577);Sink encrypted;transfer::windows::Sha256 ph,ch;auto manifest=group_attachments::seal(file,encrypted,{Storage::newMessageId(),server,source.profile.peerId,1,2},ph,ch);
 group_attachments::Body body{1000,1,group_attachments::Presentation::Picture,"image/png",std::move(manifest)};auto descriptor=group_attachments::uploadDescriptor(body.manifest,Storage::newMessageId(),source.profile);
 keys::Header header{descriptor,authority.sourceMembership,authority.recipientMembership,binding,1000,601000};auto envelope=keys::seal(header,body,authority,source,*hpke);auto wire=keys::serialize(envelope);auto parsed=keys::parse(wire);
 check(recipientStore.groupKey(local,2).empty()&&recipientStore.groupKey(local,3).empty());auto received=keys::open(parsed,authority,binding.device,recipientAccount,manager,*hpke,1001);check(received.manifest.key==body.manifest.key&&received.manifest.name==file.name());
 struct Encrypted:Source {explicit Encrypted(Blob value):Source(0){bytes=std::move(value);}} ciphertext(encrypted.bytes);Sink output;transfer::windows::Sha256 p2,c2;group_attachments::open(ciphertext,output,received.manifest,p2,c2);check(output.committed&&output.bytes==file.bytes);
 auto secret=group_attachments::encodeBody(body);check(std::search(wire.begin(),wire.end(),secret.bytes.begin(),secret.bytes.end())==wire.end());check(std::search(wire.begin(),wire.end(),body.manifest.key.begin(),body.manifest.key.end())==wire.end());
 for(size_t n=0;n<wire.size();++n)rejects([&]{keys::parse(Blob(wire.begin(),wire.begin()+n));});
 auto suffix=wire;suffix.push_back(0);rejects([&]{keys::parse(suffix);});
 auto fail=[&](keys::Envelope bad){rejects([&]{keys::open(bad,authority,binding.device,recipientAccount,manager,*hpke,1001);});};
 auto bad=parsed;bad.encrypted.ciphertext[0]^=1;fail(bad);bad=parsed;bad.encrypted.enc[1]^=1;fail(bad);bad=parsed;bad.header.recipient.generation++;fail(bad);bad=parsed;bad.header.recipientMembership[0]='c';bad.signature=source.sign(keys::signingPayload(bad));fail(bad);
 bad=parsed;bad.header.created++;bad.signature=source.sign(keys::signingPayload(bad));fail(bad); // Authentic re-sign cannot transplant HPKE AAD.
 auto rejoined=authority;rejoined.currentRecipientMembership=std::string(32,'c');rejects([&]{keys::open(parsed,rejoined,binding.device,recipientAccount,manager,*hpke,1001);});
 auto earlierExpiry=authority;earlierExpiry.cacheExpires=600999;rejects([&]{keys::open(parsed,earlierExpiry,binding.device,recipientAccount,manager,*hpke,1001);});
 auto extended=authority;extended.cacheExpires=1000+30ull*24*60*60*1000;auto longHeader=header;longHeader.expires=extended.cacheExpires;auto longEnvelope=keys::seal(longHeader,body,extended,source,*hpke);check(keys::open(longEnvelope,extended,binding.device,recipientAccount,manager,*hpke,1001).manifest.key==body.manifest.key);
 auto removed=current;removed.devices.pop_back();keys::Authority revoked{state,original,state,removed,authority.sourceMembership,authority.recipientMembership,authority.currentRecipientMembership,authority.currentSourceMembership,authority.cacheExpires};rejects([&]{keys::open(parsed,revoked,binding.device,recipientAccount,manager,*hpke,1001);});
 rejects([&]{keys::open(parsed,authority,binding.device,recipientAccount,manager,*hpke,601000);});rejects([&]{keys::open(parsed,authority,source.profile.peerId,1,manager,*hpke,1001);});
 write(root/L"descriptor.bin",descriptor);write(root/L"key-envelope.bin",wire);write(root/L"binding.bin",mailbox::serializeBinding(binding));write(root/L"source-public.bin",source.publicKey());write(root/L"recipient-public.bin",recipient.publicKey());write(root/L"ciphertext.bin",encrypted.bytes);
 if(!managed.empty()){
  managedRoundTrip(root,managed);auto retainedWire=read(root/L"cache-return-key.bin");check(retainedWire==wire);auto retained=keys::open(keys::parse(retainedWire),authority,binding.device,recipientAccount,manager,*hpke,1001);Encrypted fromCache(read(root/L"cache-return-ciphertext.bin"));Sink verified;transfer::windows::Sha256 p3,c3;group_attachments::open(fromCache,verified,retained.manifest,p3,c3);check(verified.committed&&verified.bytes==file.bytes);
 }
 metadata.state=DeviceKeyState::Retired;registry.locked([&]{registry.replace({metadata});});
 {auto reopenedDb=platform::openWindowsSqlDatabase((root/L"recipient"/L"key-registry").wstring());platform::SqlKeyRegistryStore reopenedStore(reopenedDb);platform::ProtectedKeyRegistry reopenedRegistry(reopenedStore,protection,recipient.profile.peerId);DeviceKeyManager reopened(recipient,provider,reopenedRegistry);auto retained=keys::open(keys::parse(wire),authority,binding.device,recipientAccount,reopened,*hpke,1001);check(retained.manifest.key==body.manifest.key);}
 manager.revokeRecipient(binding.keyId);rejects([&]{keys::open(parsed,authority,binding.device,recipientAccount,manager,*hpke,1001);});hpke->shutdown();
}
int main(int argc,char** argv){try{check(argc==2||argc==3);auto root=std::filesystem::absolute(argv[1]);auto managed=argc==3?std::string(argv[2]):std::string{};run(root/L"cross-account",false,managed);run(root/L"same-account",true,managed);std::cout<<"PASS attachment-only production HPKE, distinct/same-account signed devices, no group key, newer epoch, exact decrypt, protected registry reopen/retirement, revocation/rejoin, tamper/AAD/expiry rejection and bounded canonical parsing; no production cache enablement\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
