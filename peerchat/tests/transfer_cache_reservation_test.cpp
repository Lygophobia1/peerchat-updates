#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#define require(value) do{if(!(value))throw std::runtime_error("Cache reservation regression line "+std::to_string(__LINE__));}while(false)
struct CacheReservationFixture {
 struct Reads {unsigned calls=0,failAt=0;};
 struct Input:MemorySource {Reads& reads;Input(const Blob& bytes,Reads& r):MemorySource(bytes,"ciphertext.peerchat"),reads(r){}Blob read(std::uint64_t offset,size_t count)override{if(++reads.calls==reads.failAt)throw std::runtime_error("Injected staged read failure");return MemorySource::read(offset,count);}};
 struct Journal:StateStore {
  Blob& bytes;Reads& reads;std::vector<DurableState> rows;bool failBefore=false,failAfter=false;unsigned highWaterCommits=0,doneCommits=0;
  Journal(Blob& b,Reads& r):bytes(b),reads(r){}bool supports(Purpose)const override{return true;}std::uint64_t wallTime()const override{return 100;}
  std::map<std::string,Options> loadOptions()override{return {{"",{ExtendedChunkBytes,16,0}}};}std::vector<DurableState> load()override{return rows;}
  void save(const std::vector<DurableState>& next)override{if(failBefore){failBefore=false;throw std::runtime_error("Injected before journal commit");}if(!next.empty()&&(rows.empty()||rows.front().highWaterSent!=next.front().highWaterSent))++highWaterCommits;if(!next.empty()&&(rows.empty()?next.front().done!=0:rows.front().done!=next.front().done))++doneCommits;rows=next;if(failAfter){failAfter=false;throw std::runtime_error("Injected after journal commit");}}
  std::unique_ptr<Source> source(const DurableState&)override{return std::make_unique<Input>(bytes,reads);}std::unique_ptr<Sink> sink(const DurableState&)override{throw std::runtime_error("Unexpected cache receiver");}
 };
 Blob bytes;Reads reads;Journal journal;TransferManager manager;Metadata metadata;std::string peer=std::string(64,'b');std::uint64_t negotiation=0;unsigned accepted=0;std::vector<Blob> exposed;
 static Blob ciphertext(){const size_t plain=2097152;return data(20+plain+((plain+32767)/32768)*16);}
 CacheReservationFixture():bytes(ciphertext()),journal(bytes,reads),manager([]{return std::make_unique<windows::Sha256>();},&journal){
  windows::Sha256 hash;for(size_t offset=0;offset<bytes.size();offset+=ChunkBytes)hash.update(bytes.data()+offset,std::min(ChunkBytes,bytes.size()-offset));metadata={std::string(32,'1'),"ciphertext.peerchat",bytes.size(),hash.finish()};manager.restore(100);manager.cacheCapability(Purpose::DirectCacheCiphertext,peer,metadata.id,2,true);manager.offerDirectCacheCiphertext(peer,metadata,2097152,std::make_unique<Input>(bytes,reads),100);
  for(unsigned n=0;n<100&&!negotiation;++n)manager.tickCacheScoped(100,[](const Snapshot&){return 2;},[&](const Snapshot&,const Blob& frame){protocol::Reader r(frame);require(r.number()==2&&r.number()==static_cast<unsigned>(Kind::Offer)&&r.string()==metadata.id);negotiation=r.number();return true;});require(negotiation);resume(manager,0,100,negotiation);reads.calls=0;journal.highWaterCommits=0;
 }
 void resume(TransferManager& target,std::uint64_t offset,std::uint64_t now,std::uint64_t token){protocol::Writer ack(protocol::FileTransfer);ack.number(2);ack.number(static_cast<unsigned>(Kind::Resume));ack.string(metadata.id);ack.number(token);ack.number(offset);target.receiveDirectCacheCiphertext(peer,metadata,2097152,ack.data,now);}
 void pump(size_t credit=8,unsigned acceptLimit=8,std::uint64_t now=100){manager.tickCacheScoped(now,[](const Snapshot&){return 2;},[&](const Snapshot&,const Blob& frame){protocol::Reader r(frame);require(r.number()==2&&r.number()==static_cast<unsigned>(Kind::Chunk)&&r.string()==metadata.id);r.number();auto offset=r.number();auto payload=r.bytes();r.end();require(journal.rows.front().highWaterSent>=offset+payload.size());if(accepted>=acceptLimit)return false;++accepted;exposed.push_back(frame);return true;},[&](const Snapshot&){return credit;});}
};
static std::vector<Blob> resumeWindow(const CacheReservationFixture& f){std::vector<Blob> frames;for(const auto& chunk:f.exposed){protocol::Reader r(chunk);r.number();r.number();r.string();r.number();auto offset=r.number();auto payload=r.bytes();protocol::Writer ack(protocol::FileTransfer);ack.number(2);ack.number(static_cast<unsigned>(Kind::Resume));ack.string(f.metadata.id);ack.number(f.negotiation);ack.number(offset+payload.size());frames.push_back(std::move(ack.data));}return frames;}
static void atomicResumeWindows(){
 {CacheReservationFixture f;f.pump();auto frames=resumeWindow(f);f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,frames,101);require(f.manager.snapshots().front().done==8*ExtendedChunkBytes&&f.manager.snapshots().front().statisticsBytes==8*ExtendedChunkBytes&&f.journal.doneCommits==1);}
 for(unsigned position=0;position<8;++position)for(unsigned fault=0;fault<3;++fault){CacheReservationFixture f;f.pump();auto valid=resumeWindow(f),damaged=valid;auto& frame=damaged[position];protocol::Reader r(frame);r.number();r.number();r.string();auto tokenAt=r.cursor;r.number();auto offsetAt=r.cursor;if(fault==0)frame[offsetAt]^=1;else if(fault==1)frame[tokenAt]^=1;else frame[4]=1;
  rejected([&]{f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,damaged,101);});require(f.manager.snapshots().front().done==0&&f.manager.snapshots().front().statisticsBytes==0&&f.journal.rows.front().done==0&&f.journal.doneCommits==0);f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,valid,102);require(f.manager.snapshots().front().done==8*ExtendedChunkBytes&&f.journal.doneCommits==1);
 }
 {CacheReservationFixture f;f.pump();auto frames=resumeWindow(f);f.journal.failBefore=true;rejected([&]{f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,frames,101);});require(f.manager.snapshots().front().done==0&&f.manager.snapshots().front().statisticsBytes==0&&f.journal.rows.front().done==0);f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,frames,351);require(f.manager.snapshots().front().done==8*ExtendedChunkBytes&&f.journal.doneCommits==1);}
 {CacheReservationFixture f;f.pump();auto frames=resumeWindow(f);f.manager.cacheCapability(Purpose::DirectCacheCiphertext,f.peer,f.metadata.id,3,true);rejected([&]{f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,frames,101);});require(f.manager.snapshots().front().done==0&&f.journal.rows.front().done==0);f.manager.cacheCapability(Purpose::DirectCacheCiphertext,f.peer,f.metadata.id,2,true);f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,frames,102);require(f.journal.doneCommits==1);}
 {CacheReservationFixture f;f.pump();auto frames=resumeWindow(f);f.journal.failAfter=true;rejected([&]{f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,frames,101);});require(f.manager.snapshots().front().done==0&&f.manager.snapshots().front().statisticsBytes==0&&f.journal.rows.front().done==8*ExtendedChunkBytes);
  TransferManager restored([]{return std::make_unique<windows::Sha256>();},&f.journal);restored.restore(400);restored.cacheCapability(Purpose::DirectCacheCiphertext,f.peer,f.metadata.id,3,true);std::uint64_t token=0;for(unsigned n=0;n<100&&!token;++n)restored.tickCacheScoped(401,[](const Snapshot&){return 3;},[&](const Snapshot&,const Blob& frame){protocol::Reader r(frame);r.number();require(r.number()==static_cast<unsigned>(Kind::Offer));r.string();token=r.number();return true;});require(token);f.resume(restored,8*ExtendedChunkBytes,402,token);require(restored.snapshots().front().done==8*ExtendedChunkBytes);
 }
 {CacheReservationFixture f;unsigned chunks=0,windows=0;while(f.manager.snapshots().front().done<f.bytes.size()){f.accepted=0;f.exposed.clear();f.pump(8,8,100+windows);require(f.accepted>0&&f.accepted<=8);auto frames=resumeWindow(f);f.manager.receiveDirectCacheResumeWindow(f.peer,f.metadata,2097152,frames,101+windows);chunks+=f.accepted;++windows;}require(chunks==33&&windows==5&&f.journal.highWaterCommits==5&&f.journal.doneCommits==5);std::cout<<"PASS2MiB atomic upload: highWater commits5 + ACK commits5,33 exact-offset validations preserved\n";}
 std::cout<<"PASS atomic8 ACK apply: one commit; every-position offset/negotiation/version tamper rolls back all progress and credit; route swap, pre/postcommit failure, safe retry and ambiguous-commit restart preserved\n";
}
int main(){try{
 atomicResumeWindows();
 {CacheReservationFixture f;f.pump();require(f.accepted==8&&f.reads.calls==16&&f.journal.highWaterCommits==1&&f.journal.rows.front().highWaterSent==8*ExtendedChunkBytes);auto before=f.journal.highWaterCommits;f.pump(0);require(f.accepted==8&&f.journal.highWaterCommits==before);}
 for(unsigned failedRead=1;failedRead<=16;++failedRead){CacheReservationFixture f;f.reads.failAt=failedRead;f.pump();require(f.accepted==0&&f.exposed.empty()&&f.journal.highWaterCommits==0&&f.journal.rows.front().highWaterSent==0&&f.manager.snapshots().front().state==State::Suspended);}
 {CacheReservationFixture f;f.journal.failBefore=true;rejected([&]{f.pump();});require(f.accepted==0&&f.exposed.empty()&&f.journal.rows.front().highWaterSent==0);f.pump(8,8,351);require(f.accepted==8&&f.journal.highWaterCommits==1);}
 {CacheReservationFixture f;f.pump(8,2);require(f.accepted==2&&f.journal.rows.front().highWaterSent==8*ExtendedChunkBytes);rejected([&]{f.resume(f.manager,3*ExtendedChunkBytes,101,f.negotiation);});require(f.manager.snapshots().front().done==0);f.resume(f.manager,2*ExtendedChunkBytes,102,f.negotiation);require(f.manager.snapshots().front().done==2*ExtendedChunkBytes);}
 for(unsigned prefixChunks:{0u,2u,8u}){
  CacheReservationFixture f;f.journal.failAfter=true;rejected([&]{f.pump();});require(f.exposed.empty()&&f.journal.rows.front().highWaterSent==8*ExtendedChunkBytes);
  // The committed reservation survives a crash before exposure, a lost ACK
  // after a real server prefix, and loss of every reply in the whole window.
  if(prefixChunks){f.pump(8,8,351);require(f.accepted==8);}
  TransferManager restored([]{return std::make_unique<windows::Sha256>();},&f.journal);f.reads.calls=0;restored.restore(400);restored.cacheCapability(Purpose::DirectCacheCiphertext,f.peer,f.metadata.id,3,true);std::uint64_t token=0;unsigned hashReads=0;
  for(unsigned n=0;n<100&&!token;++n)restored.tickCacheScoped(401,[](const Snapshot&){return 3;},[&](const Snapshot&,const Blob& frame){protocol::Reader r(frame);require(r.number()==2&&r.number()==static_cast<unsigned>(Kind::Offer)&&r.string()==f.metadata.id);token=r.number();hashReads=f.reads.calls;return true;});require(token&&hashReads>0);f.resume(restored,prefixChunks*ExtendedChunkBytes,402,token);require(restored.snapshots().front().done==prefixChunks*ExtendedChunkBytes);
  std::uint64_t first=~std::uint64_t(0);restored.tickCacheScoped(403,[](const Snapshot&){return 3;},[&](const Snapshot&,const Blob& frame){protocol::Reader r(frame);r.number();require(r.number()==static_cast<unsigned>(Kind::Chunk));r.string();r.number();first=r.number();return true;},[](const Snapshot&){return 1;});require(first==prefixChunks*ExtendedChunkBytes);
 }
 {CacheReservationFixture f;unsigned chunks=0,windows=0;while(f.manager.snapshots().front().done<f.bytes.size()){
  f.accepted=0;f.exposed.clear();f.pump(8,8,100+windows);require(f.accepted>0&&f.accepted<=8);++windows;
  for(const auto& frame:f.exposed){protocol::Reader r(frame);r.number();r.number();r.string();r.number();auto offset=r.number();auto payload=r.bytes();f.resume(f.manager,offset+payload.size(),100+windows,f.negotiation);++chunks;}
 }require(chunks==33&&windows==5&&f.journal.highWaterCommits==5&&f.journal.doneCommits==33);std::cout<<"PASS2MiB upload: highWater durable commits33->5; all33 ACK progress commits retained\n";}
 {CacheReservationFixture f;f.manager.mediaActive(true);f.pump();require(f.accepted==1&&f.reads.calls==2&&f.journal.rows.front().highWaterSent==ExtendedChunkBytes);}
 {CacheReservationFixture f;f.pump(3);require(f.accepted==3&&f.journal.rows.front().highWaterSent==3*ExtendedChunkBytes&&f.reads.calls==6);}
 {CacheReservationFixture f;f.manager.configure("",{ExtendedChunkBytes,16,10000000});f.pump();require(f.accepted==0);f.pump(8,8,600);require(f.accepted==8&&f.journal.highWaterCommits==1);auto count=f.accepted;f.pump(8,16,600);require(f.accepted==count);}
 std::cout<<"PASS bounded eight-chunk cache reservation commits once before exposure; every staging-read fault, pre/post-commit failure, partial enqueue, live forged Resume, restart server prefix/full source rehash, adapter credit, media and shared10Mbps limits retained\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
