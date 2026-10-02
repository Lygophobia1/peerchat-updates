#define NOMINMAX
#include "../voice_media_topology.hpp"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#define CHECK(v) do{if(!(v)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#v);std::exit(1);}}while(0)
int main(){
    using namespace peerchat;RelayMeshVoiceTopology topology;MediaIceConfiguration ice;
    auto incoming=[](const std::string&,std::uint64_t,const unsigned char*,std::size_t){};
    CHECK(!topology.start(std::string(64,'a'),ice,incoming));
    ice.policy=MediaIceConfiguration::Policy::DirectTestOnly;CHECK(!topology.start(std::string(64,'a'),ice,incoming));
    ice.policy=MediaIceConfiguration::Policy::RelayOnly;ice.host="127.0.0.1";ice.port=3478;ice.username="synthetic";ice.password="synthetic";
    ice.expiresUtc=std::time(nullptr)+600;ice.profileSessionGeneration=ice.discoveryGeneration=ice.generation=1;
    CHECK(topology.start(std::string(64,'a'),ice,incoming));
    AuthorizedVoiceSource b{2,std::string(64,'b'),std::string(32,'a')},c{3,std::string(64,'c'),std::string(32,'b')};
    CHECK(topology.reconcile({b,c}));auto rows=topology.snapshot();CHECK(rows.size()==2);
    auto first=rows[0].routeId;CHECK(first.size()==32);
    CHECK(!topology.signaling({"unauthorized",first,"offer","v=0\r\n"}));
    CHECK(!topology.signaling({b.key(),std::string(32,'e'),"answer","v=0\r\n"}));
    CHECK(!topology.signaling({b.key(),first,"answer","v=0\r\na=candidate:1 1 UDP 1 127.0.0.1 1 typ host\r\n"}));
    CHECK(!topology.signaling({b.key(),first,"candidate","candidate:1 1 UDP 1 127.0.0.1 1 typ srflx"}));
    CHECK(!topology.signaling({b.key(),first,"candidate","candidate:1 1 UDP 1 127.0.0.1 1 typ relay","0",2}));
    CHECK(topology.reconcile({b}));rows=topology.snapshot();CHECK(rows.size()==1&&rows[0].routeId==first);
    CHECK(topology.reconcile({}));CHECK(topology.snapshot().empty());
    CHECK(!topology.signaling({b.key(),first,"answer","v=0\r\n"}));
    for(unsigned cycle=0;cycle<15;++cycle){topology.stop();CHECK(topology.start(std::string(64,'a'),ice,incoming));CHECK(topology.reconcile({b}));topology.stop();}
    std::puts("PASS: topology fail-closed relay policy, device/source/route generation guards, inline direct SDP rejection, retire-one/preserve-other and 15 RTC lifetimes; no live TURN success claimed");
}
