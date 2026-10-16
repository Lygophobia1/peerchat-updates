#define main legacy_transfer_manager_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "transfer_manager_test.cpp"
#pragma GCC diagnostic pop
#undef main
#include <set>
int main(){try{
 TransferManager send{[]{return std::make_unique<windows::Sha256>();}},receive{[]{return std::make_unique<windows::Sha256>();}};
 std::string a(64,'a'),b(64,'b');std::deque<std::pair<bool,Blob>> wire;std::set<std::string> accepted;std::map<std::string,Result> results;std::uint64_t payload=0,limited=0;
 send.configure("",{65536,8,2000000});send.capability(b,1,true);receive.capability(a,1,true);
 for(char c='1';c<='4';++c){auto id=std::string(32,c);send.offer(b,id,std::make_unique<MemorySource>(data(2097152)),1);send.configure(id,{65536,8,c=='1'?500000u:8000000u});}
 for(std::uint64_t now=1;now<=10000;++now){
  send.tick(now,[](const std::string&){return 1;},[&](const std::string&,const Blob& f){protocol::Reader r(f);auto v=r.number(),kind=r.number();auto id=r.string();if(v==2)r.number();if(kind==3){r.number();auto n=r.bytes().size();payload+=n;if(id==std::string(32,'1'))limited+=n;}wire.emplace_back(true,f);return true;});
  receive.tick(now,[](const std::string&){return 1;},[&](const std::string&,const Blob& f){wire.emplace_back(false,f);return true;});
  while(!wire.empty()){auto item=std::move(wire.front());wire.pop_front();if(item.first)receive.receive(a,item.second,now);else send.receive(b,item.second,now);}
  for(const auto& row:receive.snapshots())if(accepted.insert(row.metadata.id).second)receive.accept(row.metadata.id,std::make_unique<MemorySink>(results[row.metadata.id]),now);
 }
 check(payload<=2500000+65536&&limited<=625000+65536);for(const auto& row:send.snapshots())check(row.done>0);
 std::cout<<"PASS modeled 10-second aggregate 2 Mbps + individual 0.5 Mbps ceilings, four concurrent transfers, bounded burst and fair progress. submitted="<<payload<<" limited="<<limited<<"; not measured network throughput\n";
 {
  Harness h;h.receiver.configureDownload(1000000);h.duplicate=true;h.offer(data(512*1024));h.finish();
  check(h.sender.snapshots()[0].state==State::Complete&&h.result.committed&&h.result.bytes==data(512*1024));
  check(h.now>=4194); // confirmed payload cannot bypass the 1 Mbps receiver ceiling
  h.receiver.configureDownload(0);check(h.receiver.downloadLimit()==0);
  rejected([&]{h.receiver.configureDownload(10000000001ull);});
  std::cout<<"PASS modeled receive ceiling, verified completion, unlimited switch and invalid cap rejection; no network-throughput claim\n";
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
