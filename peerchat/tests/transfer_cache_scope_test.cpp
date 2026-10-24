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
static void cacheCapabilityInterference(){
 auto hash=[](){return std::make_unique<windows::Sha256>();};TransferManager sender(hash);
 std::string peer(64,'b');Metadata ordinary{std::string(32,'5'),"ordinary.bin",1,{}};
 sender.offer(peer,ordinary.id,std::make_unique<MemorySource>(Blob{7},ordinary.name),1);
 auto peerRoute=[](const auto&){return 99;};auto sink=[](const auto&,const Blob&){return true;};
 sender.tick(2,peerRoute,sink);protocol::Writer resume(protocol::FileTransfer);resume.data=control(Kind::Resume,ordinary);resume.number(0);sender.receive(peer,resume.data,3);
 sender.tick(4,peerRoute,sink);resume.data=control(Kind::Resume,ordinary);resume.number(1);sender.receive(peer,resume.data,5);sender.tick(6,peerRoute,sink);sender.receive(peer,control(Kind::Complete,ordinary),7);
 check(sender.snapshots().front().state==State::Complete);
 auto ciphertext=data(70068);windows::Sha256 digest;for(size_t at=0;at<ciphertext.size();at+=ChunkBytes)digest.update(ciphertext.data()+at,std::min(ChunkBytes,ciphertext.size()-at));Metadata cached{std::string(32,'6'),"ciphertext.peerchat",ciphertext.size(),digest.finish()};
 sender.configure("",{ExtendedChunkBytes,4,0});sender.cacheCapability(Purpose::DirectCacheCiphertext,peer,cached.id,2,true);
 sender.offerDirectCacheCiphertext(peer,cached,70000,std::make_unique<MemorySource>(ciphertext,cached.name),8);
 sender.cacheCapability(Purpose::GroupCacheCiphertext,peer,std::string(32,'7'),3,true);
 rejected([&]{sender.cacheCapability(Purpose::GroupCacheCiphertext,peer,cached.id,2,true);});
 rejected([&]{sender.cacheCapability(Purpose::DirectCacheCiphertext,std::string(64,'c'),cached.id,2,true);});
 // A retained ordinary row or relay reconnect can clear the same peer's live
 // capability after the cache lease was activated, before its Offer or ACK.
 sender.capability(peer,0,false);Blob offered;
 for(unsigned now=9;now<20&&offered.empty();++now)sender.tickScoped(now,[&](const Snapshot& row){if(row.purpose==Purpose::PeerFile){sender.capability(peer,0,false);return std::uint64_t(0);}return std::uint64_t(2);},[&](const Snapshot& row,const Blob& frame){check(row.purpose==Purpose::DirectCacheCiphertext);offered=frame;return true;});
 protocol::Reader offer(offered);check(offer.type()==protocol::FileTransfer&&offer.number()==2&&offer.number()==static_cast<unsigned>(Kind::Offer));check(offer.string()==cached.id);auto negotiation=offer.number();
 auto pendingOffer=sender.retainedRecords();
 auto cacheFrame=[&](Kind kind){protocol::Writer frame(protocol::FileTransfer);frame.number(2);frame.number(static_cast<unsigned>(kind));frame.string(cached.id);frame.number(negotiation);return frame;};
 sender.capability(peer,0,false);auto ack=cacheFrame(Kind::Resume);ack.number(0);sender.receiveDirectCacheCiphertext(peer,cached,70000,ack.data,20);
 rejected([&]{sender.receive(peer,ack.data,20);});
 rejected([&]{sender.receiveCacheCiphertext(peer,cached,70000,ack.data,20);});
 rejected([&]{sender.receiveDirectCacheCiphertext(std::string(64,'c'),cached,70000,ack.data,20);});
 auto rows=sender.snapshots();auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& value){return value.metadata.id==cached.id;});check(row!=rows.end()&&row->state==State::Transferring&&row->done==0);
 unsigned chunks=0;for(unsigned now=21;now<25;++now)sender.tickScoped(now,[](const Snapshot& row){return row.purpose==Purpose::DirectCacheCiphertext?2:0;},[&](const Snapshot&,const Blob& frame){protocol::Reader r(frame);check(r.number()==2);if(r.number()==static_cast<unsigned>(Kind::Chunk))++chunks;return true;});check(chunks==2);
 struct Journal:StateStore {
  std::vector<DurableState> rows;Blob ciphertext;bool failReads=false;
  bool supports(Purpose)const override{return true;}std::uint64_t wallTime()const override{return 100;}
  std::vector<DurableState> load()override{return rows;}void save(const std::vector<DurableState>& value)override{rows=value;}
  std::map<std::string,Options> loadOptions()override{return {{"",{ExtendedChunkBytes,4,0}}};}
  std::unique_ptr<Source> source(const DurableState& d)override{check(d.purpose==Purpose::DirectCacheCiphertext);struct Input:MemorySource{bool& failed;Input(Blob bytes,const std::string& name,bool& flag):MemorySource(std::move(bytes),name),failed(flag){}Blob read(std::uint64_t offset,size_t maximum)override{if(failed)throw std::runtime_error("Injected retained source read failure");return MemorySource::read(offset,maximum);}};return std::make_unique<Input>(ciphertext,d.metadata.name,failReads);}
  std::unique_ptr<Sink> sink(const DurableState&)override{throw std::runtime_error("Unexpected receive restore");}
 } journal;journal.rows=pendingOffer;journal.ciphertext=ciphertext;
 for(auto& saved:journal.rows){saved.created=1;saved.updated=1;saved.expires=RetentionMilliseconds+1;}
 TransferManager restarted(hash,&journal);restarted.restore(100);restarted.cacheCapability(Purpose::DirectCacheCiphertext,peer,cached.id,4,true);Blob restoredOffer;
 for(unsigned now=101;now<120&&restoredOffer.empty();++now)restarted.tickScoped(now,[&](const Snapshot& row){if(row.purpose==Purpose::PeerFile){restarted.capability(peer,0,false);return std::uint64_t(0);}return std::uint64_t(4);},[&](const Snapshot&,const Blob& frame){restoredOffer=frame;return true;});
 protocol::Reader restored(restoredOffer);check(restored.type()==protocol::FileTransfer&&restored.number()==2&&restored.number()==static_cast<unsigned>(Kind::Offer)&&restored.string()==cached.id);negotiation=restored.number();
 restarted.capability(peer,0,false);ack=cacheFrame(Kind::Resume);ack.number(0);restarted.receiveDirectCacheCiphertext(peer,cached,70000,ack.data,121);
 rows=restarted.snapshots();row=std::find_if(rows.begin(),rows.end(),[&](const auto& value){return value.metadata.id==cached.id;});check(row!=rows.end()&&row->state==State::Transferring&&row->done==0);
 // A suspended source and a sender with outstanding chunks both retain their
 // scoped credit when stale capability registrations fill the bounded map.
 journal.failReads=true;restarted.tickScoped(122,[](const Snapshot& value){return value.purpose==Purpose::DirectCacheCiphertext?4:0;},sink);
 rows=restarted.snapshots();row=std::find_if(rows.begin(),rows.end(),[&](const auto& value){return value.metadata.id==cached.id;});check(row!=rows.end()&&row->state==State::Suspended);
 for(unsigned i=0;i<130;++i){auto suffix=std::to_string(i);auto unused=std::string(32-suffix.size(),'f')+suffix;sender.cacheCapability(Purpose::DirectCacheCiphertext,peer,unused,8,true);restarted.cacheCapability(Purpose::DirectCacheCiphertext,peer,unused,8,true);}
 ack=cacheFrame(Kind::Resume);ack.number(0);restarted.receiveDirectCacheCiphertext(peer,cached,70000,ack.data,123);
 // The original sender still owns its two unacknowledged payload chunks.
 protocol::Reader original(offered);original.number();original.number();original.string();negotiation=original.number();ack=cacheFrame(Kind::Resume);ack.number(65536);sender.receiveDirectCacheCiphertext(peer,cached,70000,ack.data,124);
 std::cout<<"PASS cached v2 Offer/Resume and payload credit survive completed ordinary rows and live capability loss for the same peer\n";
 std::cout<<"PASS restored pending cache Offer revalidates its retained source and resumes beside completed ordinary same-peer history; cache purpose/peer isolation preserved\n";
 std::cout<<"PASS bounded scoped capability eviction preserves suspended source recovery and outstanding payload credit\n";
}
static void cacheOnlyBorrowTurns(){
 unsigned ordinaryReads=0;struct TrackedSource:MemorySource{unsigned& reads;TrackedSource(unsigned& n):MemorySource(data(1048576)),reads(n){}Blob read(std::uint64_t off,size_t count)override{++reads;return MemorySource::read(off,count);}};
 TransferManager manager([]{return std::make_unique<windows::Sha256>();});manager.configure("",{ExtendedChunkBytes,16,0});
 const std::string peer(64,'b'),ordinaryId(32,'1'),receiveId(32,'2');manager.offer(peer,ordinaryId,std::make_unique<TrackedSource>(ordinaryReads),1);
 Metadata incoming{receiveId,"ordinary.bin",1,{}};manager.receive(peer,control(Kind::Offer,incoming),1);Result result;manager.accept(receiveId,std::make_unique<MemorySink>(result),1);check(manager.hasPendingControl(receiveId));
 const std::uint64_t plain=2*1024*1024;auto payload=data(20+plain+((plain+32767)/32768)*16);windows::Sha256 hash;for(size_t off=0;off<payload.size();off+=ChunkBytes)hash.update(payload.data()+off,std::min(ChunkBytes,payload.size()-off));
 Metadata direct{std::string(32,'3'),"ciphertext.peerchat",payload.size(),hash.finish()},group=direct;group.id=std::string(32,'4');
 manager.cacheCapability(Purpose::DirectCacheCiphertext,peer,direct.id,2,true);manager.cacheCapability(Purpose::GroupCacheCiphertext,peer,group.id,3,true);
 manager.offerDirectCacheCiphertext(peer,direct,plain,std::make_unique<MemorySource>(payload,direct.name),1);manager.offerCacheCiphertext(peer,group,plain,std::make_unique<MemorySource>(payload,group.name),1);
 std::map<std::string,std::uint64_t> negotiations;std::map<std::string,unsigned> chunks;unsigned ordinaryRoutes=0;
 auto route=[&](const Snapshot& row)->std::uint64_t{if(row.purpose==Purpose::PeerFile){++ordinaryRoutes;return 99;}return row.purpose==Purpose::DirectCacheCiphertext?2:3;};
 auto send=[&](const Snapshot& row,const Blob& bytes){check(row.purpose!=Purpose::PeerFile);protocol::Reader frame(bytes);check(frame.number()==2);auto kind=static_cast<Kind>(frame.number());check(frame.string()==row.metadata.id);auto token=frame.number();if(kind==Kind::Offer)negotiations[row.metadata.id]=token;else if(kind==Kind::Chunk){if(chunks[row.metadata.id]>=8)return false;++chunks[row.metadata.id];}return true;};
 for(unsigned turn=0;turn<200&&negotiations.size()<2;++turn)manager.tickCacheScoped(100,route,send);
 check(negotiations.size()==2&&ordinaryReads==0&&ordinaryRoutes==0&&manager.hasPendingControl(receiveId));
 for(const auto& context:{direct,group}){protocol::Writer ack(protocol::FileTransfer);ack.number(2);ack.number(static_cast<unsigned>(Kind::Resume));ack.string(context.id);ack.number(negotiations.at(context.id));ack.number(0);if(context.id==direct.id)manager.receiveDirectCacheCiphertext(peer,context,plain,ack.data,100);else manager.receiveCacheCiphertext(peer,context,plain,ack.data,100);}
 manager.mediaActive(true);for(unsigned turn=0;turn<10;++turn)manager.tickCacheScoped(100,route,send);check(chunks[direct.id]==1&&chunks[group.id]==1);
 manager.mediaActive(false);for(unsigned turn=0;turn<12;++turn)manager.tickCacheScoped(100,route,send);check(chunks[direct.id]==8&&chunks[group.id]==8&&ordinaryReads==0&&ordinaryRoutes==0&&manager.hasPendingControl(receiveId)&&result.bytes.empty());
 auto rows=manager.snapshots();auto ordinary=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.metadata.id==ordinaryId;});check(ordinary!=rows.end()&&ordinary->state==State::Preparing&&ordinary->done==0);
 std::cout<<"PASS same-clock cache-only turns hash and Offer both cache purposes then fill eight credits each; ordinary reads/routes/pending controls untouched and media-active limit remains one\n";
}
static void boundedCacheRateCredit(){
 auto run=[](std::uint64_t aggregate,std::uint64_t individual,bool media,bool ordinary,bool shared){
  TransferManager manager([]{return std::make_unique<windows::Sha256>();});manager.configure("",{ExtendedChunkBytes,16,aggregate});manager.mediaActive(media);const std::string peer(64,'b');const std::uint64_t plain=2*1024*1024;auto payload=data(20+plain+((plain+32767)/32768)*16);windows::Sha256 hash;for(size_t off=0;off<payload.size();off+=ChunkBytes)hash.update(payload.data()+off,std::min(ChunkBytes,payload.size()-off));
  Metadata cached{std::string(32,'5'),"ciphertext.peerchat",payload.size(),hash.finish()},other=cached;other.id=std::string(32,'6');
  manager.capability(peer,9,true);manager.cacheCapability(Purpose::DirectCacheCiphertext,peer,cached.id,2,true);
  if(ordinary)manager.offer(peer,cached.id,std::make_unique<MemorySource>(payload,cached.name),1);else manager.offerDirectCacheCiphertext(peer,cached,plain,std::make_unique<MemorySource>(payload,cached.name),1);
  manager.configure(cached.id,{ExtendedChunkBytes,16,individual});if(shared){manager.offer(peer,other.id,std::make_unique<MemorySource>(payload,other.name),1);manager.configure(other.id,{ExtendedChunkBytes,16,0});}
  std::map<std::string,std::uint64_t> negotiations;std::map<std::string,unsigned> chunks;auto route=[](const Snapshot& row)->std::uint64_t{return row.purpose==Purpose::PeerFile?9:2;};
  auto send=[&](const Snapshot& row,const Blob& bytes){protocol::Reader frame(bytes);check(frame.number()==2);auto kind=static_cast<Kind>(frame.number());check(frame.string()==row.metadata.id);auto negotiation=frame.number();if(kind==Kind::Offer)negotiations[row.metadata.id]=negotiation;else if(kind==Kind::Chunk)++chunks[row.metadata.id];return true;};
  for(unsigned n=0;n<200&&negotiations.size()<(shared?2u:1u);++n)manager.tickScoped(100,route,send);check(negotiations.size()==(shared?2u:1u));
  for(const auto& context:{cached,other}){if(!negotiations.count(context.id))continue;protocol::Writer ack(protocol::FileTransfer);ack.number(2);ack.number(static_cast<unsigned>(Kind::Resume));ack.string(context.id);ack.number(negotiations.at(context.id));ack.number(0);if(context.id==cached.id&&!ordinary)manager.receiveDirectCacheCiphertext(peer,context,plain,ack.data,100);else manager.receive(peer,ack.data,100);}
  for(unsigned n=0;n<20;++n)manager.tickScoped(100,route,send);check(chunks[cached.id]==(aggregate?0u:1u));
  for(unsigned n=0;n<20;++n)manager.tickScoped(600,route,send);
  auto total=chunks[cached.id]+chunks[other.id];if(aggregate)check(total*ExtendedChunkBytes<=500*aggregate/8000);else check(total*ExtendedChunkBytes<=ExtendedChunkBytes+500*individual/8000);
  if(media)check(total==1);else if(shared)check(chunks[other.id]==1&&total<=8);else if(ordinary)check(total==2);else check(total==(aggregate?8u:9u));
  auto before=total;for(unsigned n=0;n<20;++n)manager.tickScoped(600,route,send);check(chunks[cached.id]+chunks[other.id]==before);
 };
 run(10000000,0,false,false,false);run(0,10000000,false,false,false);run(10000000,0,true,false,false);run(0,10000000,false,true,false);run(10000000,0,false,false,true);
 std::cout<<"PASS 10Mbps cache aggregate and individual rates accumulate at most eight chunks across network wait; no same-clock minted credit, shared ordinary rate/fairness and one-chunk ordinary/media bursts retained\n";
}
int main(){try{
 cacheCapabilityInterference();cacheOnlyBorrowTurns();boundedCacheRateCredit();
 static_assert(MaxFileBytes==5ull*1024*1024*1024);static_assert(MaxCacheCipherBytes==group_attachments::MaxCipherBytes);
 std::string peer(64,'b');Metadata maximum{std::string(32,'1'),"ciphertext.peerchat",MaxCacheCipherBytes,{}};
 TransferManager manager([]{return std::make_unique<windows::Sha256>();});
 auto tooLarge=maximum;tooLarge.size=MaxFileBytes+1;
 rejected([&]{manager.offer(peer,tooLarge.id,std::make_unique<SizedSource>(tooLarge.size),1);});
 rejected([&]{manager.receive(peer,control(Kind::Offer,tooLarge),1);});check(manager.snapshots().empty());
 rejected([&]{archive::encodeAttachmentFetch({tooLarge.id,std::string(32,'9'),tooLarge});});
 manager.offerCacheCiphertext(peer,maximum,MaxCachedFileBytes,std::make_unique<SizedSource>(maximum.size),1);check(manager.snapshots()[0].purpose==Purpose::GroupCacheCiphertext);manager.cancel(maximum.id);
 auto over=maximum;over.size++;over.id=std::string(32,'2');rejected([&]{manager.offerCacheCiphertext(peer,over,MaxCachedFileBytes,std::make_unique<SizedSource>(over.size),2);});
 TransferManager receiver([]{return std::make_unique<windows::Sha256>();});receiver.receiveCacheCiphertext(peer,maximum,MaxCachedFileBytes,control(Kind::Offer,maximum),1);
 rejected([&]{receiver.receive(peer,control(Kind::Cancel,maximum),2);});check(receiver.snapshots()[0].state==State::AwaitingAcceptance);
 auto wrong=maximum;wrong.digest[0]^=1;rejected([&]{receiver.receiveCacheCiphertext(peer,wrong,MaxCachedFileBytes,control(Kind::Cancel,maximum),2);});
 receiver.receiveCacheCiphertext(peer,maximum,MaxCachedFileBytes,control(Kind::Cancel,maximum),3);check(receiver.snapshots()[0].state==State::Cancelled);
 TransferManager legacy([]{return std::make_unique<windows::Sha256>();});auto small=maximum;small.size=37;legacy.receive(peer,control(Kind::Offer,small),1);
 rejected([&]{legacy.receiveCacheCiphertext(peer,small,1,control(Kind::Cancel,small),2);});check(legacy.snapshots()[0].purpose==Purpose::PeerFile);
 // Existing real protected journal must refuse cache entries, not erase purpose
 // or make older versions unable to read their legacy transfer history.
 auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/unicode::decode(std::string(32,'c'));
 root+=std::to_wstring(GetTickCount64());std::filesystem::create_directories(root);windows::DurableJournal journal(root.wstring(),peer);
 TransferManager protectedManager([]{return std::make_unique<windows::Sha256>();},&journal);protectedManager.restore(1);
 rejected([&]{protectedManager.offerCacheCiphertext(peer,small,1,std::make_unique<SizedSource>(37),2);});check(protectedManager.snapshots().empty()&&journal.load().empty());
 // The upper-bound source is sparse: verify 64-bit file accounting without
 // allocating or transmitting a 1.5-GiB fixture.
 auto sparse=root/L"maximum-ciphertext.bin";HANDLE file=CreateFileW(sparse.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);check(file!=INVALID_HANDLE_VALUE);DWORD returned=0;
 check(DeviceIoControl(file,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&returned,nullptr));LARGE_INTEGER end{};end.QuadPart=MaxCacheCipherBytes;check(SetFilePointerEx(file,end,nullptr,FILE_BEGIN)&&SetEndOfFile(file)&&FlushFileBuffers(file));CloseHandle(file);
 {windows::FileSource ordinary(sparse.wstring());check(ordinary.size()==MaxCacheCipherBytes);}
 {windows::FileSource encryptedSource(sparse.wstring(),"ciphertext.peerchat",Purpose::GroupCacheCiphertext);check(encryptedSource.size()==MaxCacheCipherBytes);DurableState saved;saved.purpose=Purpose::GroupCacheCiphertext;saved.location=encryptedSource.resumeToken();auto reopened=windows::FileSource::reopen(saved);check(reopened->size()==MaxCacheCipherBytes&&reopened->name()=="ciphertext.peerchat");saved.purpose=Purpose::PeerFile;check(windows::FileSource::reopen(saved)->size()==MaxCacheCipherBytes);}
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
 std::cout<<"PASS explicit cache purpose, exact 1.5-GiB plaintext/AEAD bounds, DM/archive isolation, legacy-journal rejection, production TransferManager encrypted-object round trip and separate plaintext verification; cache transport remains gated\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
