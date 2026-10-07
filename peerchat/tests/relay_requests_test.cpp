#include "../portable/relay_requests.hpp"
#include <iostream>
int main(){using namespace peerchat;try{
    auto check=[](bool value,const char* reason){if(!value)throw std::runtime_error(reason);};
    const std::string alice(64,'a'),bob(64,'b'),other(64,'c');
    for(unsigned cycle=0;cycle<1000;++cycle){
        session::SessionTransportManager owner(alice,alice,{1},100,1000);
        reliable::AuthenticatedFriendSnapshot friends;friends.replace(10,1,{{20,bob,{bob}}});
        std::uint64_t next=1;
        portable::RelayRequests requests(owner,friends,{1},next);
        check(!requests.presence(other,true,1,1,10),"nonfriend presence accepted");
        check(!requests.presence(bob,true,0,1,10),"stale friend snapshot accepted");
        check(requests.presence(bob,true,1,1,10),"presence rejected");
        check(requests.take(10,0,32).empty(),"idle online friend created route demand");
        owner.demandLiveRoute(bob,45000);
        auto first=requests.take(10,0,32);check(first.size()==1&&first[0].peer==bob,"manager relay request missing");
        check(requests.take(11,0,32).empty(),"duplicate pending request");
        check(!requests.attach(first[0].id,other),"wrong-device attachment accepted");
        check(requests.close(first[0].id,20,0,true),"failed rendezvous not retired");
        check(requests.presence(bob,true,1,1,21),"identical presence rejected");
        check(requests.take(21,0,32).empty(),"presence bypassed owner retry delay");
        auto retry=requests.take(1020,0,32);check(retry.size()==1&&retry[0].id.value>first[0].id.value,"retry not manager-owned/monotonic");
        check(!requests.attach(first[0].id,bob),"stale attempt attached");
        check(requests.presence(bob,true,1,2,1021),"new network rejected");
        check(!requests.attach(retry[0].id,bob),"old network attached");
        check(!requests.presence(bob,true,1,1,1022),"stale network accepted");
        auto fresh=requests.take(1022,0,32);check(fresh.empty(),"stale route retirement skipped backoff");
        fresh=requests.take(2022,0,32);check(fresh.size()==1,"fresh network retry missing");
        auto attached=requests.attach(fresh[0].id,bob);check(attached&&attached->route.networkRevision==2,"attachment lost route binding");
        check(!requests.attach(fresh[0].id,bob),"repeated attachment accepted");
        requests.failed(*attached,2023,0,true);
        auto timed=requests.take(4023,0,32);check(timed.size()==1,"timeout fixture missing");
        check(requests.take(24024,0,32).empty(),"timeout did not schedule bounded owner backoff");
        check(!requests.attach(timed[0].id,bob),"timed-out attempt attached");
        friends.replace(0,2,{});requests.revoke();
        check(requests.take(99999,0,32).empty(),"logout reopened relay");
        check(!requests.presence(bob,true,2,3,99999),"logout accepted presence");
    }
    {
        session::SessionTransportManager owner(alice,alice,{1},100,1000);
        reliable::AuthenticatedFriendSnapshot friends;friends.replace(10,1,{{20,bob,{bob}},{30,other,{other}}});
        std::uint64_t next=1;portable::RelayRequests requests(owner,friends,{1},next);
        check(requests.presence(bob,true,1,1,10),"demand fixture presence");
        owner.demandLiveRoute(bob,100,10);
        auto first=requests.take(10,0,32);check(first.size()==1,"explicit incoming demand missing");
        check(requests.close(first[0].id,20,0,true),"demand fixture close");
        check(requests.presence(other,true,1,1,21),"unrelated presence rejected");
        check(requests.take(21,0,32).empty(),"unrelated presence created route or reset backoff");
        check(requests.take(200,0,32).empty(),"expired demand kept retrying");
        owner.demandLiveRoute(bob,500,200);
        check(requests.take(200,0,32).size()==1,"new incoming demand could not revive idle route");
        auto retired=owner.retireIdleRoutes(501);check(retired.size()==1,"idle route was not retired");
        check(requests.take(502,0,32).empty(),"idle retirement created route demand");
    }
    std::cout<<"PASS: 1000 manager-owned relay lifecycles plus idle, incoming demand, expiry, unrelated-presence/backoff and idle retirement. HOST ONLY\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
