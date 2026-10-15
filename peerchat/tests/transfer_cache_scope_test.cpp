#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include "group_attachment_blob.hpp"
#include "archive_attachment_sync.hpp"
#include <winioctl.h>
#define check(value) do{if(!(value))throw std::runtime_error("Cache scope assertion at line "+std::to_string(__LINE__));}while(false)
struct SizedSource:Source {
 std::uint64_t length;explicit SizedSource(std::uint64_t size):length(size){}
 std::uint64_t size()const override{return length;}std::string name()const override{return "ciphertext.peerchat";}
 Blob read(std::uint64_t offset,size_t count)override{return Blob(std::min<std::uint64_t>(count,length-offset));}
};
Blob control(Kind kind,const Metadata& metadata){protocol::Writer w(protocol::FileTransfer);w.number(1);w.number(static_cast<unsigned>(kind));w.string(metadata.id);if(kind==Kind::Offer){w.string(metadata.name);w.number(metadata.size);w.bytes(Blob(metadata.digest.begin(),metadata.digest.end()));}return w.data;}
int main(){try{
 static_assert(MaxFileBytes==256ull*1024*1024);static_assert(MaxCacheCipherBytes==group_attachments::MaxCipherBytes);
 std::string peer(64,'b');Metadata maximum{std::string(32,'1'),"ciphertext.peerchat",MaxCacheCipherBytes,{}};
 TransferManager manager([]{return std::make_unique<windows::Sha256>();});
 rejected([&]{manager.offer(peer,maximum.id,std::make_unique<SizedSource>(maximum.size),1);});
 rejected([&]{manager.receive(peer,control(Kind::Offer,maximum),1);});check(manager.snapshots().empty());
 rejected([&]{archive::encodeAttachmentFetch({maximum.id,std::string(32,'9'),maximum});});
 manager.offerCacheCiphertext(peer,maximum,MaxFileBytes,std::make_unique<SizedSource>(maximum.size),1);check(manager.snapshots()[0].purpose==Purpose::GroupCacheCiphertext);manager.cancel(maximum.id);
 auto over=maximum;over.size++;over.id=std::string(32,'2');rejected([&]{manager.offerCacheCiphertext(peer,over,MaxFileBytes,std::make_unique<SizedSource>(over.size),2);});
 TransferManager receiver([]{return std::make_unique<windows::Sha256>();});receiver.receiveCacheCiphertext(peer,maximum,MaxFileBytes,control(Kind::Offer,maximum),1);
 rejected([&]{receiver.receive(peer,control(Kind::Cancel,maximum),2);});check(receiver.snapshots()[0].state==State::AwaitingAcceptance);
 auto wrong=maximum;wrong.digest[0]^=1;rejected([&]{receiver.receiveCacheCiphertext(peer,wrong,MaxFileBytes,control(Kind::Cancel,maximum),2);});
 receiver.receiveCacheCiphertext(peer,maximum,MaxFileBytes,control(Kind::Cancel,maximum),3);check(receiver.snapshots()[0].state==State::Cancelled);
 TransferManager legacy([]{return std::make_unique<windows::Sha256>();});auto small=maximum;small.size=37;legacy.receive(peer,control(Kind::Offer,small),1);
 rejected([&]{legacy.receiveCacheCiphertext(peer,small,1,control(Kind::Cancel,small),2);});check(legacy.snapshots()[0].purpose==Purpose::PeerFile);
 // Existing real protected journal must refuse cache entries, not erase purpose
 // or make older versions unable to read their legacy transfer history.
 auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/unicode::decode(std::string(32,'c'));
 root+=std::to_wstring(GetTickCount64());std::filesystem::create_directories(root);windows::DurableJournal journal(root.wstring(),peer);
 TransferManager protectedManager([]{return std::make_unique<windows::Sha256>();},&journal);protectedManager.restore(1);
 rejected([&]{protectedManager.offerCacheCiphertext(peer,small,1,std::make_unique<SizedSource>(37),2);});check(protectedManager.snapshots().empty()&&journal.load().empty());
 // The upper-bound source is sparse: verify 64-bit file accounting without
 // allocating or transmitting a 256-MiB fixture.
 auto sparse=root/L"maximum-ciphertext.bin";HANDLE file=CreateFileW(sparse.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(file!=INVALID_HANDLE_VALUE);DWORD returned=0;
 check(DeviceIoControl(file,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&returned,nullptr));LARGE_INTEGER end{};end.QuadPart=MaxCacheCipherBytes;check(SetFilePointerEx(file,end,nullptr,FILE_BEGIN)&&SetEndOfFile(file)&&FlushFileBuffers(file));CloseHandle(file);
 rejected([&]{windows::FileSource ordinary(sparse.wstring());});
 {windows::FileSource encryptedSource(sparse.wstring(),"ciphertext.peerchat",Purpose::GroupCacheCiphertext);check(encryptedSource.size()==MaxCacheCipherBytes);DurableState saved;saved.purpose=Purpose::GroupCacheCiphertext;saved.location=encryptedSource.resumeToken();auto reopened=windows::FileSource::reopen(saved);check(reopened->size()==MaxCacheCipherBytes&&reopened->name()=="ciphertext.peerchat");saved.purpose=Purpose::PeerFile;rejected([&]{windows::FileSource::reopen(saved);});}
 // The same production engine handles nonempty ciphertext framing, hashes,
 // bounded credits, ACKs and completion. No replacement cache transfer loop.
 auto original=data(1048577);MemorySource source(original,"original.png");Result sealed;MemorySink encrypted(sealed);windows::Sha256 ph,ch;
 auto manifest=group_attachments::seal(source,encrypted,{std::string(32,'3'),std::string(32,'4'),std::string(64,'a'),4,7},ph,ch);
 Metadata context{manifest.binding.attachment,"ciphertext.peerchat",manifest.cipherSize(),manifest.cipherDigest};
 TransferManager sender([]{return std::make_unique<windows::Sha256>();}),reader([]{return std::make_unique<windows::Sha256>();});
 sender.offerCacheCiphertext(peer,context,manifest.size,std::make_unique<MemorySource>(sealed.bytes,"ciphertext.peerchat"),1);
 Result downloaded;bool accepted=false;unsigned ordinarySends=0;std::deque<std::pair<bool,Blob>> frames;
 for(std::uint64_t now=2;now<10000&&sender.snapshots()[0].state!=State::Complete;++now){
  if(now==2){sender.tick(now,[](const std::string&){return 1;},[&](const std::string&,const Blob&){++ordinarySends;return true;});}
  check(ordinarySends==0);
  sender.tickScoped(now,[](const Snapshot& row){check(row.purpose==Purpose::GroupCacheCiphertext);return 2;},[&](const Snapshot&,const Blob& frame){frames.emplace_back(true,frame);return true;});
  reader.tickScoped(now,[](const Snapshot& row){check(row.purpose==Purpose::GroupCacheCiphertext);return 2;},[&](const Snapshot&,const Blob& frame){frames.emplace_back(false,frame);return true;});
  while(!frames.empty()){auto frame=std::move(frames.front());frames.pop_front();if(frame.first)reader.receiveCacheCiphertext(peer,context,manifest.size,frame.second,now);else sender.receiveCacheCiphertext(peer,context,manifest.size,frame.second,now);}
  if(!accepted&&!reader.snapshots().empty()){reader.accept(context.id,std::make_unique<MemorySink>(downloaded),now);accepted=true;}
 }
 check(sender.snapshots()[0].state==State::Complete&&downloaded.committed&&downloaded.bytes==sealed.bytes);
 MemorySource received(downloaded.bytes);Result plain;MemorySink output(plain);windows::Sha256 p2,c2;group_attachments::open(received,output,manifest,p2,c2);check(plain.committed&&plain.bytes==original);
 std::cout<<"PASS explicit cache purpose, exact 256-MiB plaintext/AEAD bounds, DM/archive isolation, legacy-journal rejection, production TransferManager encrypted-object round trip and separate plaintext verification; cache transport remains gated\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
