#define main retained_transfer_fixture_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include <psapi.h>
struct CountingHash:Hash {std::uint64_t total=0;void update(const unsigned char*,size_t n)override{total+=n;}Digest finish()override{Digest d{};for(unsigned i=0;i<8;++i)d[i]=static_cast<unsigned char>(total>>(8*i));return d;}};
struct VirtualSource:Source {std::uint64_t length;size_t& largest;VirtualSource(std::uint64_t n,size_t& l):length(n),largest(l){}std::uint64_t size()const override{return length;}std::string name()const override{return "large.bin";}Blob read(std::uint64_t at,size_t n)override{check(at<=length&&n<=ChunkBytes);largest=std::max(largest,n);return Blob(static_cast<size_t>(std::min<std::uint64_t>(n,length-at)));}};
struct VirtualResult {std::uint64_t size=0;bool committed=false,discarded=false;};
struct VirtualSink:Sink {VirtualResult& result;explicit VirtualSink(VirtualResult& r):result(r){}void append(const Blob& b)override{check(b.size()<=ChunkBytes);result.size+=b.size();}void commit()override{result.committed=true;}void discard()override{result.discarded=true;result.size=0;}};
int main(int argc,char** argv){try{
 const bool disk=argc==2;std::filesystem::path root;if(disk){root=std::filesystem::absolute(argv[1]);check(!std::filesystem::exists(root));std::filesystem::create_directories(root);std::ofstream file(root/"large.bin",std::ios::binary);Blob block(ChunkBytes);for(size_t i=0;i<block.size();++i)block[i]=static_cast<unsigned char>(i*29+71);for(std::uint64_t n=0;n<MaxFileBytes/ChunkBytes;++n)file.write(reinterpret_cast<const char*>(block.data()),block.size());file.close();check(bool(file));}
 const std::string a(64,'a'),b(64,'b'),id(32,'1');size_t largest=0;VirtualResult result;
 auto hashes=[disk]()->std::unique_ptr<Hash>{if(disk)return std::make_unique<peerchat::transfer::windows::Sha256>();return std::make_unique<CountingHash>();};TransferManager sender(hashes),receiver(hashes);
 sender.capability(b,1,true,true);receiver.capability(a,1,true,true);
 if(disk)sender.offer(b,id,std::make_unique<peerchat::transfer::windows::FileSource>((root/"large.bin").wstring()),1);else sender.offer(b,id,std::make_unique<VirtualSource>(MaxFileBytes,largest),1);
 struct Frame{bool incoming;Blob bytes;};std::deque<Frame> wire;std::uint64_t now=2,route=1;bool accepted=false,reconnected=false;size_t maxQueued=0;
 for(;now<600000&&sender.snapshots().front().state!=State::Complete;++now){
  sender.tick(now,[&](const std::string&){return route;},[&](const std::string&,const Blob& frame){wire.push_back({true,frame});return true;});
  receiver.tick(now,[&](const std::string&){return route;},[&](const std::string&,const Blob& frame){wire.push_back({false,frame});return true;});maxQueued=std::max(maxQueued,wire.size());
  while(!wire.empty()){auto f=std::move(wire.front());wire.pop_front();if(f.incoming)receiver.receive(a,f.bytes,now);else sender.receive(b,f.bytes,now);}
  if(!accepted&&!receiver.snapshots().empty()){check(now==2&&sender.snapshots().front().state==State::Preparing);if(disk){auto m=receiver.snapshots().front().metadata;receiver.accept(id,std::make_unique<peerchat::transfer::windows::FileSink>(root.wstring(),m),now);}else receiver.accept(id,std::make_unique<VirtualSink>(result),now);accepted=true;}
  if(disk&&!receiver.snapshots().empty()){result.size=receiver.snapshots().front().done;result.committed=receiver.snapshots().front().state==State::Complete;}
  if(!reconnected&&result.size>4ull*1024*1024*1024){route=2;sender.capability(b,route,true,true);receiver.capability(a,route,true,true);reconnected=true;}
 }
 check(accepted&&reconnected&&result.committed&&result.size==5ull*1024*1024*1024&&sender.snapshots().front().done==result.size&&largest<=32768&&maxQueued<=2);
 if(disk){auto rows=receiver.snapshots();check(rows.front().state==State::Complete);std::uint64_t files=0;for(const auto& item:std::filesystem::directory_iterator(root))if(item.is_regular_file()&&item.file_size()==MaxFileBytes)++files;check(files==2);std::cout<<"PASS actual 5-GiB source and destination files with production SHA-256 integrity, early acceptance, and reconnect above 4 GiB. Fixture: "<<root.string()<<"\n";}
 PROCESS_MEMORY_COUNTERS memory{};memory.cb=sizeof(memory);check(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory))!=0&&memory.PeakWorkingSetSize<128ull*1024*1024);
 // A legacy peer sees no provisional packet; cancellation before hash completion
 // retires the source and cannot be upgraded into a completed transfer.
 TransferManager legacy([]{return std::make_unique<CountingHash>();});legacy.capability(b,1,true);legacy.offer(b,id,std::make_unique<VirtualSource>(2*1024*1024,largest),1);unsigned packets=0;legacy.tick(2,[](const auto&){return 1;},[&](const auto&,const auto&){++packets;return true;});check(packets==0);legacy.cancel(id);check(legacy.snapshots().front().state==State::Cancelled);
 std::cout<<"PASS: early acceptance while hashing; full 5-GiB "<<(disk?"disk source with production SHA-256":"virtual source with counting hash")<<" through 64-bit wire counters and reconnect above 4 GiB; bounded 32-KiB reads, <=2 queued frames, peak working set="<<memory.PeakWorkingSetSize<<"; legacy fallback and preparation cancellation.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
