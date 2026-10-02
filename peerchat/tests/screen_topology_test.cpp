#include "screen_media_topology.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
    peerchat::MediaIceConfiguration ice;ice.host="127.0.0.1";ice.port=9;ice.username="test-only";ice.password="not-a-production-credential";ice.expiresUtc=std::time(nullptr)+3600;ice.profileSessionGeneration=ice.discoveryGeneration=ice.generation=1;
    peerchat::RelayMeshScreenShareTopology topology;
    auto direct=ice;direct.policy=peerchat::MediaIceConfiguration::Policy::DirectTestOnly;
    check(!topology.start(true,direct,0,30),"Production group topology accepted direct policy");
    auto expired=ice;expired.expiresUtc=std::time(nullptr);check(!topology.start(true,expired,0,30),"Expired credentials accepted");
    check(!topology.start(false,ice,0,30),"Viewer without presentation target accepted");
    for(unsigned lifetime=0;lifetime<15;++lifetime){
        check(topology.start(false,ice,1,30),"Viewer route owner initialization failed");
        peerchat::AuthorizedScreenEndpoint peer{2,std::string(64,'a'),std::string(32,'b')};
        check(topology.reconcile({peer}),"Authorized sharer reconciliation failed");
        check(!topology.reconcile({peer,peer}),"Viewer accepted multiple sharers");
        peerchat::ScreenTransportSignal signal{peer.key(),std::string(32,'c'),"candidate","candidate:1 1 UDP 1 127.0.0.1 9 typ relay","0",1};
        auto wrong=signal;wrong.generation=2;check(!topology.signaling(wrong),"Stale/wrong generation accepted");wrong=signal;wrong.sourceKey="wrong";check(!topology.signaling(wrong),"Unauthorized source accepted");wrong=signal;wrong.data="candidate:1 1 UDP 1 127.0.0.1 9 typ host";check(!topology.signaling(wrong),"Direct candidate accepted");
        wrong=signal;wrong.kind="offer";wrong.data="v=0\r\na=candidate:1 1 UDP 1 127.0.0.1 9 typ srflx\r\n";check(!topology.signaling(wrong),"Embedded direct SDP candidate accepted");
        for(unsigned packet=0;packet<32;++packet)check(topology.signaling(signal),"Bounded relay candidate rejected");
        check(!topology.signaling(signal),"Unbounded pending candidates accepted");
        auto snapshot=topology.snapshot();check(snapshot.size()==1&&snapshot[0].state=="Awaiting offer","Wrong unstarted route state");
        topology.stop();check(topology.snapshot().empty()&&!topology.signaling(signal),"Retired route accepted stale signal");
    }
    std::cout<<"PASS: production TURN-only guard, credential expiry, authorized endpoint/generation, embedded candidate rejection, bounded queues, 15 route lifetimes\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
