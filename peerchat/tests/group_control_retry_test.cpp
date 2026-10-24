#include "../portable/relay_requests.hpp"
#include <iostream>
#include <stdexcept>
using namespace peerchat;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
 const std::string local(64,'a'),remote(64,'b'),group(32,'d');
 session::SessionTransportManager owner(local,local,{1});reliable::AuthenticatedFriendSnapshot friends;friends.replace(10,1,{});
 session::ReliableAuthorization scope{session::ReliableAuthorizationScope::GroupControl,group,1,remote};
 std::uint64_t next=1,now=10,network=1;bool authorized=true;
 portable::RelayRequests requests(owner,friends,{1},next,{},[&](const auto& value){return authorized&&value.scope==scope.scope&&value.groupId==scope.groupId&&value.rosterGeneration==scope.rosterGeneration&&value.expectedRemoteDeviceId==scope.expectedRemoteDeviceId;});
 check(requests.authenticatedNetwork(1,network),"Initial network rejected");
 auto demand=[&](std::uint64_t time){check(owner.demandGroupRoute(scope,time+2000,network,time),"Scoped demand rejected");};
 demand(now);auto first=requests.take(now,0,32);check(first.size()==1,"Initial group attempt missing");
 check(requests.close(first.front().id,20,0,true),"Failed group attempt not retired");
 auto snapshot=owner.snapshot(scope.continuityKey());check(snapshot&&snapshot->retryDelayMilliseconds==4000,"Initial existing retry delay changed");
 // The production demand lasts2s. A busy native turn can renew it after expiry
 // while the SAME failed scope still has a future5s exponential retry.
 demand(2011);check(requests.take(2011,0,32).empty(),"Expired identical group demand bypassed existing failed-attempt backoff");
 check(requests.take(4019,0,32).empty(),"Group retry happened before due time");
 now=4020;
 for(unsigned failure=2;failure<=12;++failure){
  demand(now);auto attempt=requests.take(now,0,32);check(attempt.size()==1,"Due group retry missing");
  check(requests.close(attempt.front().id,now+1,40,true),"Retry failure not retired");
  std::uint64_t base=5000;for(unsigned n=1;n<failure&&base<300000;++n)base=std::min<std::uint64_t>(base*2,300000);
  auto delay=std::min<std::uint64_t>(base*120/100,300000);snapshot=owner.snapshot(scope.continuityKey());check(snapshot&&snapshot->retryDelayMilliseconds==delay,"Existing exponential retry/cap changed");
  const auto due=now+1+delay;demand(now+2002);check(requests.take(now+2002,0,32).empty(),"Repeated expired group demand reset exponential retry");
  demand(due-1);check(requests.take(due-1,0,32).empty(),"Repeated group poll skipped final retry bound");now=due;
 }
 // New authenticated network resets ONLY this group's stale failure series.
 ++network;check(requests.authenticatedNetwork(1,network),"New network rejected");demand(now);snapshot=owner.snapshot(scope.continuityKey());check(snapshot&&snapshot->retryDelayMilliseconds==0,"Network change retained stale group failure count");
 auto fresh=requests.take(now,0,32);check(fresh.size()==1&&fresh.front().route.networkRevision==network,"New network did not start a fresh scoped attempt");
 check(requests.close(fresh.front().id,now+1,0,true),"Fresh network failure missing");snapshot=owner.snapshot(scope.continuityKey());check(snapshot&&snapshot->retryDelayMilliseconds==4000,"Network reset did not restart first failure delay");
 owner.invalidateGroupControl({1},group,2);++scope.rosterGeneration;demand(now+2);fresh=requests.take(now+2,0,32);check(fresh.size()==1,"New authorized roster inherited old retry");
 auto attached=requests.attach(fresh.front().id,remote);check(attached.has_value(),"New roster attempt attach rejected");
 owner.finishGroupRoute(scope);demand(now+3);fresh=requests.take(now+3,0,32);check(fresh.size()==1,"Completed demand blocked new explicit group work");
 // A pending OLD network attempt must not install backoff into the fresh plan.
 ++network;check(requests.authenticatedNetwork(1,network),"Pending network change rejected");demand(now+4);auto moved=requests.take(now+4,0,32);check(moved.size()==1&&moved.front().route.networkRevision==network,"Old network failure delayed current group demand");
 authorized=false;check(requests.take(now+1000000,0,32).empty(),"Revoked group scope retried");
 // Keep ordinary friend demand's existing explicit revival behavior unchanged.
 session::SessionTransportManager friendOwner(local,local,{1});reliable::AuthenticatedFriendSnapshot friendPins;friendPins.replace(10,1,{{20,remote,{remote}}});std::uint64_t friendNext=1;portable::RelayRequests friendRequests(friendOwner,friendPins,{1},friendNext);
 check(friendRequests.presence(remote,true,1,1,10),"Friend presence rejected");friendOwner.demandLiveRoute(remote,2010,10);auto friendFirst=friendRequests.take(10,0,32);check(friendFirst.size()==1,"Friend demand missing");check(friendRequests.close(friendFirst.front().id,20,0,true),"Friend failure missing");friendOwner.demandLiveRoute(remote,4011,2011);check(friendRequests.take(2011,0,32).size()==1,"Group fix changed ordinary friend explicit demand revival");
 std::cout<<"PASS group-control failed-scope backoff survives expired2s polling; existing exponential300s cap; network/roster/completed-demand resets; stale network no penalty; revocation and friend behavior unchanged\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
