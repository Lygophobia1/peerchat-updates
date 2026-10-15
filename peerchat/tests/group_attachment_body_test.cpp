#define main legacy_group_envelope_main
#include "group_message_envelope_test.cpp"
#undef main
#include "group_attachment_body.hpp"
#include "group_attachment_descriptor.hpp"
#include "group_text.hpp"
#include "windows_transfer_io.hpp"
struct ObjectSource:transfer::Source {
 Blob bytes;explicit ObjectSource(Blob value):bytes(std::move(value)){}
 std::uint64_t size()const override{return bytes.size();}std::string name()const override{return u8"private-📷-日本.png";}
 Blob read(std::uint64_t offset,size_t count)override{require(offset<=bytes.size(),"Source offset");return Blob(bytes.begin()+offset,bytes.begin()+std::min<std::uint64_t>(bytes.size(),offset+count));}
};
struct ObjectSink:transfer::Sink {
 Blob bytes;bool committed=false;
 void append(const Blob& value)override{bytes.insert(bytes.end(),value.begin(),value.end());}void commit()override{committed=true;}void discard()override{bytes.clear();committed=false;}
};
static Blob fixtureBytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);require(bool(file),"Fixture file missing");return Blob(std::istreambuf_iterator<char>(file),{});}
static void fixtureWrite(const std::filesystem::path& path,const Blob& bytes){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());require(bool(file),"Fixture write failed");}
int main(int argc,char** argv){try{
 if(argc==3&&std::string(argv[2])=="--verify-cached"){
  auto root=std::filesystem::path(argv[1]);auto descriptor=fixtureBytes(root/L"cache-fixture"/L"descriptor.bin");protocol::Reader parsed(descriptor);require(parsed.type()==protocol::GroupEvent&&parsed.string()=="PeerChat/group-cache-upload/v1","Public descriptor context");auto attachment=parsed.string(),server=parsed.string(),message=parsed.string();
  Storage member((root/L"member").wstring());auto record=member.authenticatedGroupMessage(server,message);auto body=group_attachments::decodeBody(record.plaintext);require(body.manifest.binding.attachment==attachment,"Retained attachment identity mismatch");
  transfer::windows::FileSource encrypted((root/L"cache-fixture"/L"cache-return.bin").wstring(),"ciphertext.peerchat",transfer::Purpose::GroupCacheCiphertext);ObjectSink output;transfer::windows::Sha256 ph,ch;group_attachments::open(encrypted,output,body.manifest,ph,ch);require(output.committed&&output.bytes==Blob(1048577,123),"Managed cache return lost native authenticated content");std::cout<<"PASS actual native group envelope/signature and cached ciphertext interop with protected recipient history\n";return 0;
 }
 require(argc==2,"Isolated profile path required");auto root=std::filesystem::path(argv[1]);Storage ownerStore((root/L"owner").wstring()),memberStore((root/L"member").wstring());auto owner=ownerStore.profile(),member=memberStore.profile();
 auto local=newGroupId(),server=newGroupId();auto state=ledger(local,owner,member);group_keys::RosterSnapshot roster{local,server,owner.peerId,2,{{1,owner.peerId,GroupRole::Owner,false},{2,member.peerId,GroupRole::Member,false}}};
 group_keys::GroupKeyManager ownerKeys(ownerStore,1,owner.peerId),memberKeys(memberStore,2,member.peerId);ownerKeys.apply(roster);memberKeys.apply(roster);require(ownerKeys.ensureCurrentKey(server,state),"Current key creation");auto offers=ownerStore.pendingGroupKeyHandoffs(member.peerId);require(offers.size()==1&&memberKeys.accept(offers[0],owner.peerId,state),"Authenticated group key handoff");
 MessageLayer sender(ownerStore,ownerKeys,owner,1),receiver(memberStore,memberKeys,member,2);
 Blob original(1048577,123);ObjectSource source(original);ObjectSink ciphertext;transfer::windows::Sha256 ph,ch;
 auto manifest=group_attachments::seal(source,ciphertext,{Storage::newMessageId(),server,owner.peerId,1,2},ph,ch);
 group_attachments::Body attachment{1700000000000ull,1,group_attachments::Presentation::Picture,"image/png",std::move(manifest)};
 auto plain=group_attachments::encodeBody(attachment);auto decoded=group_attachments::decodeBody(plain.bytes);require(decoded.manifest.name==source.name()&&decoded.manifest.key==attachment.manifest.key,"Private manifest codec");
 require(!group_text::valid(plain.bytes),"Legacy text parser accepted typed attachment");auto created=sender.create(local,state,plain.bytes);require(created.result==Result::Accepted,"Existing E2EE layer rejected attachment body");
 std::filesystem::create_directories(root/L"cache-fixture"/L"ciphertext");fixtureWrite(root/L"cache-fixture"/L"descriptor.bin",group_attachments::uploadDescriptor(attachment.manifest,created.envelope.messageId,owner));fixtureWrite(root/L"cache-fixture"/L"announcement.bin",created.wire);fixtureWrite(root/L"cache-fixture"/L"ciphertext"/unicode::decode(attachment.manifest.binding.attachment+"-encrypted.bin"),ciphertext.bytes);
 require(group_attachments::validBody(plain.bytes,created.envelope),"Envelope binding rejected");
 auto wrong=created.envelope;wrong.senderAccount=2;require(!group_attachments::validBody(plain.bytes,wrong),"Wrong account body accepted");wrong=created.envelope;wrong.senderDevice=member.peerId;require(!group_attachments::validBody(plain.bytes,wrong),"Wrong device body accepted");wrong=created.envelope;wrong.groupId=newGroupId();require(!group_attachments::validBody(plain.bytes,wrong),"Wrong group body accepted");wrong=created.envelope;wrong.keyGeneration++;require(!group_attachments::validBody(plain.bytes,wrong),"Wrong key epoch body accepted");
 // Unsupported legacy UI must reject before history/dedupe/ACK authority. A
 // later capable receive of the same ID must still work normally.
 expect(receiver.receive(local,state,created.wire,group_text::valid),Result::InvalidMessageBody);require(memberStore.authenticatedGroupMessage(server,created.envelope.messageId).envelope.empty(),"Unsupported body reserved durable message ID");
 auto validator=[&](const Blob& value){return group_attachments::validBody(value,created.envelope);};auto received=receiver.receive(local,state,created.wire,validator);expect(received,Result::Accepted);expect(receiver.receive(local,state,created.wire,validator),Result::DuplicateMessage);
 auto privateKey=attachment.manifest.key;auto filename=source.name();auto name=Blob(filename.begin(),filename.end());
 require(std::search(created.wire.begin(),created.wire.end(),privateKey.begin(),privateKey.end())==created.wire.end(),"Server envelope exposed attachment key");
 require(std::search(created.wire.begin(),created.wire.end(),name.begin(),name.end())==created.wire.end(),"Server envelope exposed filename");
 {Storage reopened((root/L"member").wstring());auto retained=reopened.authenticatedGroupMessage(server,created.envelope.messageId);auto saved=group_attachments::decodeBody(retained.plaintext);ObjectSource encrypted(ciphertext.bytes);ObjectSink recovered;transfer::windows::Sha256 p2,c2;group_attachments::open(encrypted,recovered,saved.manifest,p2,c2);require(recovered.committed&&recovered.bytes==original,"Restarted protected history lost exact attachment key/content");}
 for(size_t length=0;length<plain.bytes.size();++length){Blob shortBody(plain.bytes.begin(),plain.bytes.begin()+length);require(!group_attachments::validBody(shortBody,created.envelope),"Truncated body accepted");}
 Blob oversized(4097);require(!group_attachments::validBody(oversized,created.envelope),"Oversized body accepted");decoded.contentType="text/html";bool rejected=false;try{group_attachments::encodeBody(decoded);}catch(...){rejected=true;}require(rejected,"Unsupported presentation accepted");
 auto text=group_text::encode({1700000000001ull,2,L"ordinary text after unsupported attachment"});auto next=sender.create(local,state,text);expect(receiver.receive(local,state,next.wire,group_text::valid),Result::Accepted);
 std::cout<<"PASS bounded private attachment body through actual signed group E2EE/history/reopen/dedupe, exact key/content, envelope binding and legacy text isolation; production routing remains disabled\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
