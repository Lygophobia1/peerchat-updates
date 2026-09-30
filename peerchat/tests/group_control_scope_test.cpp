#include "session_transport_manager.hpp"
#include "direct_transport_adapter.hpp"
#include <iostream>
#include <stdexcept>

using namespace peerchat::session;
static void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}

int main(){
    try{
        const std::string local(64,'a'),remote(64,'b'),wrong(64,'c');
        const std::string group(32,'d'),otherGroup(32,'e');
        const SessionEpoch epoch{7};
        SessionTransportManager manager("group-scope-test",local,epoch);
        RouteObservation relay{{1},"relay:"+remote,"server","peer TLS over relay",false,0,
                               TransportKind::Relay,RouteClass::ServerRelay};
        ReliableAuthorization scope{ReliableAuthorizationScope::GroupControl,group,4,remote};
        require(scope.valid()&&!scope.permitsDirectMessage()&&scope.permitsGroupKeyControl(),"group purpose was not restricted");
        require(scope.continuityKey()!=remote,"group authorization reused friend continuity");
        require(manager.registerGroupControlAttempt(epoch,{1},scope,relay,0),"group attempt was rejected");
        require(!manager.registerGroupControlAttempt(epoch,{2},{ReliableAuthorizationScope::GroupControl,otherGroup,0,remote},relay,0),"invalid roster epoch was accepted");
        auto observed=manager.authorization(epoch,{1});
        require(observed&&observed->groupId==group&&observed->rosterGeneration==4,"scope was not retained on the transport");
        DirectTransportAdapter adapter(epoch,{1},relay,remote);
        require(!manager.observeTransport(adapter.identityPresented(wrong,0),1),
                "wrong presented Peer ID was accepted as a group transport event");
        require(!manager.registerAttempt(epoch,{1},remote,relay,1),
                "friend registration reused a group-control transport instance");
        auto direct=relay;direct.kind=TransportKind::Direct;direct.routeClass=RouteClass::DirectLan;
        DirectTransportAdapter directAdapter(epoch,{1},direct,remote);
        require(!manager.observeTransport(directAdapter.identityPresented(remote,0),1),
                "direct route was accepted for group control");
        manager.markLocallyTrusted(epoch,{1});manager.markRemoteAccepted(epoch,{1});
        require(!manager.arbitrateWinner(epoch,{1},2).activated(),"wrong presented Peer ID activated group control");
        manager.closeTransport(epoch,{1},"wrong identity",true);
        require(manager.registerGroupControlAttempt(epoch,{3},scope,relay,3),"replacement scoped attempt was rejected");
        DirectTransportAdapter replacement(epoch,{3},relay,remote);
        manager.observeTransport(replacement.identityPresented(remote,0),4);
        manager.markLocallyTrusted(epoch,{3});manager.markRemoteAccepted(epoch,{3});
        require(manager.arbitrateWinner(epoch,{3},5).activated(),"authenticated group scope did not activate");
        require(!manager.snapshot(remote)->connected,"group control incorrectly became the friend conversation winner");
        require(manager.snapshot(scope.continuityKey())->connected,"group continuity was not isolated");
        require(manager.registerGroupControlAttempt(epoch,{4},{ReliableAuthorizationScope::GroupControl,otherGroup,4,remote},relay,6),"other group attempt was rejected");
        DirectTransportAdapter other(epoch,{4},relay,remote);
        manager.observeTransport(other.identityPresented(remote,0),7);
        manager.markLocallyTrusted(epoch,{4});manager.markRemoteAccepted(epoch,{4});
        require(manager.arbitrateWinner(epoch,{4},8).activated(),"independent group could not activate");
        require(manager.snapshot(scope.continuityKey())->activeTransport.value==3,"other group replaced the wrong scoped winner");
        auto invalidated=manager.invalidateGroupControl(epoch,group,5);
        require(invalidated.size()==1&&invalidated[0].value==3,"roster rotation did not revoke the old scoped relay");
        require(!manager.authorization(epoch,{3})&& !manager.snapshot(scope.continuityKey())->connected,
                "revoked group session remained eligible for delivery");
        require(manager.snapshot((ReliableAuthorization{ReliableAuthorizationScope::GroupControl,otherGroup,4,remote}).continuityKey())->connected,
                "one group invalidation closed an unrelated group session");
        std::cout<<"PASS: group-control scope remains device/group-bound and separate from friend continuity.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
