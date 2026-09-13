#include "node.hpp"
#include "direct_transport_adapter.hpp"
#include "protocol.hpp"
#include "session_transport_manager.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <thread>

using namespace peerchat;
using Clock=std::chrono::steady_clock;
struct Seen {NodeEvent event;Clock::time_point at;};
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void drain(Node& node,std::vector<Seen>& seen){for(auto& event:node.poll())seen.push_back({std::move(event),Clock::now()});}
template<class Check> static void until(Node& node,std::vector<Seen>& seen,Check check,const char* message,int milliseconds=5000){auto deadline=Clock::now()+std::chrono::milliseconds(milliseconds);while(Clock::now()<deadline){drain(node,seen);if(check())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));}throw std::runtime_error(message);}
static std::vector<Seen> attempts(const std::vector<Seen>& seen,const std::string& peer){std::vector<Seen> result;for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ConnectionAttempt&&value.event.expectedPeerId==peer)result.push_back(value);return result;}
static size_t schedules(const std::vector<Seen>& seen,const std::string& peer){size_t result=0;for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ReconnectScheduled&&value.event.expectedPeerId==peer)result++;return result;}

int main(int argc,char** argv){
    try{
        // Stage 2 is an observation boundary only: the legacy Node remains the
        // direct transport authority and tells the projection what it decided.
        session::SessionEpoch epoch{41};
        session::RouteObservation route{{3},"127.0.0.1:29761","test","direct",false};
        session::SessionTransportManager manager("profile-a",std::string(64,'1'),epoch);
        session::DirectTransportAdapter adapter(epoch,{7},route,std::string(64,'2'));
        require(manager.observeLegacyDecision(adapter.connecting()),"manager rejected current session epoch");
        require(manager.find(std::string(64,'2'))==nullptr,"unverified attempt created a logical PeerSession");
        require(!adapter.sendAccepted(12).accepted(),"inactive direct wrapper accepted a send");
        require(manager.observeLegacyDecision(adapter.identityPresented(std::string(64,'2'),protocol::CapabilityMutualTls|protocol::CapabilityLogicalOrdering)),"identity observation failed");
        require(manager.find(std::string(64,'2'))==nullptr,"presented identity was treated as verified");
        require(manager.observeLegacyDecision(adapter.locallyTrusted()),"trust observation failed");
        require(manager.observeLegacyDecision(adapter.activated()),"legacy activation observation failed");
        auto projected=manager.find(std::string(64,'2'));
        require(projected&&projected->observedBinding(session::ChannelKind::Control)&&projected->observedBinding(session::ChannelKind::Control)->value==7,"legacy control binding was not projected");
        require(projected->observedBinding(session::ChannelKind::ReliableMessages)&&projected->observedBinding(session::ChannelKind::ReliableMessages)->value==7&&!projected->observedBinding(session::ChannelKind::RealtimeMedia),"legacy direct binding did not preserve channel separation");
        require(adapter.sendAccepted(12).accepted(),"active direct wrapper rejected send acceptance");
        require(adapter.sendAccepted(protocol::MaxPacket+1).acceptance==session::SendAcceptance::RejectedPayloadTooLarge,"direct wrapper ignored its payload limit");
        require(manager.observeLegacyDecision(adapter.observedReceive(24)),"receive observation failed");
        session::DirectTransportAdapter staleAdapter({40},{9},route,std::string(64,'2'));
        auto stale=staleAdapter.observedReceive(1);
        require(!manager.observeLegacyDecision(stale),"stale session epoch changed the projection");
        session::DirectTransportAdapter replacement(epoch,{8},route,std::string(64,'2'));
        manager.observeLegacyDecision(replacement.identityPresented(std::string(64,'2'),protocol::CapabilityMutualTls));
        manager.observeLegacyDecision(replacement.locallyTrusted());
        manager.observeLegacyDecision(replacement.activated());
        manager.observeLegacyDecision(adapter.closed("legacy duplicate retired",false));
        projected=manager.find(std::string(64,'2'));
        require(projected&&projected->observedBinding(session::ChannelKind::Control)&&projected->observedBinding(session::ChannelKind::Control)->value==8,"old transport close cleared a newer observed binding");
        session::DirectTransportAdapter closing(epoch,{9},route,std::string(64,'3'));
        manager.observeLegacyDecision(closing.closing("legacy close requested"));
        require(closing.sendAccepted(1).acceptance==session::SendAcceptance::RejectedClosing,"closing direct wrapper accepted a send");
        manager.observeLegacyDecision(closing.closed("legacy close completed",false));
        require(closing.state()==session::TransportState::Closed,"direct wrapper did not report close completion");
        session::ChannelRequirements messages{true,true,false,protocol::MaxPacket};
        session::ChannelRequirements media{false,false,true,1200};
        require(messages.reliable&&messages.ordered&&!messages.lowLatency&&media.lowLatency&&!media.ordered,"reliable and realtime channel requirements collapsed together");
        protocol::Writer accepted(protocol::Accepted);
        const Blob acceptedWire=accepted.frame();
        const Blob expectedAcceptedWire={0,0,0,4,'P','T',1,static_cast<unsigned char>(protocol::Accepted)};
        require(acceptedWire==expectedAcceptedWire,"legacy protocol framing changed");
        if(argc==2&&std::string(argv[1])=="--contract-only"){
            std::cout<<"PASS: Stage 2 session/direct-wrapper identities, epochs, route revisions, channel separation, immutable events, send limits, close completion, and unchanged protocol framing.\n";
            return 0;
        }

        auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::filesystem::path(L"reconnect-"+std::to_wstring(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        const std::string peerA(64,'a'),peerB(64,'b');Node node;std::vector<Seen> seen;node.start((root/L"main").wstring(),29761,L"Reconnect test");until(node,seen,[&]{for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::Ready)return true;return false;},"node did not start");
        node.saveContact({peerA,L"Peer A",{},{{L"127.0.0.1",29881}},true});node.saveContact({peerB,L"Peer B",{},{{L"127.0.0.1",29882}},true});
        until(node,seen,[&]{return attempts(seen,peerA).size()>=3&&attempts(seen,peerB).size()>=3;},"two peers did not retry with backoff");
        auto a=attempts(seen,peerA),b=attempts(seen,peerB);require(a[0].event.candidate==L"127.0.0.1:29881"&&b[0].event.candidate==L"127.0.0.1:29882","simultaneous peer diagnostics crossed candidates");require(a[0].event.candidateSource==L"Saved contact"&&a[0].event.route==L"automatic reconnect","automatic retry metadata was not propagated");
        auto gapOne=std::chrono::duration_cast<std::chrono::milliseconds>(a[1].at-a[0].at).count(),gapTwo=std::chrono::duration_cast<std::chrono::milliseconds>(a[2].at-a[1].at).count();require(gapOne>=50&&gapTwo>=110&&gapTwo>gapOne,"reconnect delay did not grow exponentially");
        for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ReconnectScheduled)require(value.event.retryDelayMilliseconds>0&&value.event.retryDelayMilliseconds<=800,"reconnect jitter exceeded the configured cap");
        until(node,seen,[&]{return schedules(seen,peerA)>=3&&schedules(seen,peerB)>=3;},"failed attempts did not enter backoff");size_t before=attempts(seen,peerA).size();node.connect(L"127.0.0.1",29881,peerA,L"Saved contact",L"manual retry",true);until(node,seen,[&]{return attempts(seen,peerA).size()>before;},"manual retry did not force an immediate attempt",300);auto manual=attempts(seen,peerA).back();require(manual.event.route==L"manual retry","manual retry metadata was lost");
        until(node,seen,[&]{return schedules(seen,peerB)>=3;},"peer B was not waiting in backoff");before=attempts(seen,peerB).size();node.connect(L"127.0.0.1",29883,peerB,L"Discovery LAN candidate",L"candidate update",true);until(node,seen,[&]{return attempts(seen,peerB).size()>before;},"candidate update did not force an immediate attempt",300);auto presence=attempts(seen,peerB).back();require(presence.event.candidate==L"127.0.0.1:29883"&&presence.event.route==L"candidate update","presence candidate retry was not correlated");until(node,seen,[&]{return schedules(seen,peerA)>=4;},"peer A did not return to backoff before route invalidation");before=attempts(seen,peerA).size();node.invalidateRoute(peerA,L"127.0.0.1",29881);auto invalidatedUntil=Clock::now()+std::chrono::milliseconds(1100);while(Clock::now()<invalidatedUntil){drain(node,seen);std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(attempts(seen,peerA).size()==before,"stale saved route retried after runtime network invalidation");node.stop();

        Node routeA,routeB;std::vector<Seen> routeAEvents,routeBEvents;routeA.start((root/L"route-a").wstring(),29764,L"Route A");routeB.start((root/L"route-b").wstring(),29765,L"Route B");std::string routeAId,routeBId;until(routeA,routeAEvents,[&]{for(const auto& value:routeAEvents)if(value.event.kind==NodeEvent::Kind::Ready){routeAId=value.event.peerId;return true;}return false;},"route A did not start");until(routeB,routeBEvents,[&]{for(const auto& value:routeBEvents)if(value.event.kind==NodeEvent::Kind::Ready){routeBId=value.event.peerId;return true;}return false;},"route B did not start");routeA.connect(L"127.0.0.1",29765,routeBId,L"Current public candidate",L"network change");bool routeAOnline=false,routeBOnline=false;auto routeDeadline=Clock::now()+std::chrono::seconds(5);while(Clock::now()<routeDeadline&&(!routeAOnline||!routeBOnline)){for(auto& event:routeA.poll()){routeAEvents.push_back({event,Clock::now()});if(event.kind==NodeEvent::Kind::TrustRequired)routeA.approve(event.peerId,true);if(event.kind==NodeEvent::Kind::Connected&&event.peerId==routeBId)routeAOnline=true;}for(auto& event:routeB.poll()){routeBEvents.push_back({event,Clock::now()});if(event.kind==NodeEvent::Kind::TrustRequired)routeB.approve(event.peerId,true);if(event.kind==NodeEvent::Kind::Connected&&event.peerId==routeAId)routeBOnline=true;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(routeAOnline&&routeBOnline,"current public route did not authenticate");size_t routeADisconnects=0;for(const auto& value:routeAEvents)if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.peerId==routeBId)++routeADisconnects;routeA.invalidateRoute(routeBId,L"192.168.1.17",7777);auto delayedOldGeneration=Clock::now()+std::chrono::milliseconds(500);while(Clock::now()<delayedOldGeneration){drain(routeA,routeAEvents);drain(routeB,routeBEvents);std::this_thread::sleep_for(std::chrono::milliseconds(10));}size_t afterOldGeneration=0;for(const auto& value:routeAEvents)if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.peerId==routeBId)++afterOldGeneration;require(afterOldGeneration==routeADisconnects,"delayed old-generation invalidation closed the new authenticated route");routeA.invalidateRoute(routeBId,L"127.0.0.1",29765);auto endpointRefresh=Clock::now()+std::chrono::milliseconds(500);while(Clock::now()<endpointRefresh){drain(routeA,routeAEvents);drain(routeB,routeBEvents);std::this_thread::sleep_for(std::chrono::milliseconds(10));}size_t afterEndpointRefresh=0;bool retainedDiagnostic=false;for(const auto& value:routeAEvents){if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.peerId==routeBId)++afterEndpointRefresh;if(value.event.kind==NodeEvent::Kind::ConnectionLifecycle&&value.event.peerId==routeBId&&value.event.text.find(L"live verified session retained")!=std::wstring::npos)retainedDiagnostic=true;}require(afterEndpointRefresh==routeADisconnects,"authoritative endpoint refresh closed the healthy authenticated winner");require(retainedDiagnostic,"live-route retention was not diagnosed");require(!routeA.send(routeBId,L"new route remains live").empty(),"new authenticated route was unusable after stale invalidation");routeA.stop();routeB.stop();

        Node target,verifier;std::vector<Seen> targetEvents,verifierEvents;target.start((root/L"target").wstring(),29762,L"Actual peer");verifier.start((root/L"verifier").wstring(),29763,L"Verifier");std::string targetId;until(target,targetEvents,[&]{for(const auto& value:targetEvents)if(value.event.kind==NodeEvent::Kind::Ready){targetId=value.event.peerId;return true;}return false;},"target did not start");until(verifier,verifierEvents,[&]{for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::Ready)return true;return false;},"verifier did not start");const std::string expected(64,'e');verifier.saveContact({expected,L"Expected peer",{},{{L"127.0.0.1",29762}},true});
        NodeEvent mismatch;until(verifier,verifierEvents,[&]{drain(target,targetEvents);for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::TrustRequired&&value.event.expectedPeerId==expected){mismatch=value.event;return true;}return false;},"wrong identity did not reach verification");require(mismatch.peerId==targetId&&mismatch.candidate==L"127.0.0.1:29762"&&mismatch.route==L"automatic reconnect","wrong-identity attempt lost correlation");size_t pendingAttempts=attempts(verifierEvents,expected).size();verifier.connect(L"127.0.0.1",29762,expected,L"Discovery LAN candidate",L"candidate update - same LAN",true);std::this_thread::sleep_for(std::chrono::milliseconds(30));drain(verifier,verifierEvents);verifier.connect(L"127.0.0.1",29762,expected,L"Refreshed candidate",L"manual retry",true);until(verifier,verifierEvents,[&]{drain(target,targetEvents);return attempts(verifierEvents,expected).size()>pendingAttempts;},"Manual repair was blocked by outstanding trust/transport state");bool refreshedMetadata=false;for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.expectedPeerId==expected&&value.event.candidateSource==L"Discovery LAN candidate"&&value.event.route==L"candidate update - same LAN")refreshedMetadata=true;require(refreshedMetadata,"Authoritative same-endpoint refresh left the active attempt labeled Saved contact");until(verifier,verifierEvents,[&]{drain(target,targetEvents);int prompts=0;for(const auto& value:verifierEvents){require(value.event.kind!=NodeEvent::Kind::Connected,"Repair bypassed identity verification");if(value.event.kind==NodeEvent::Kind::TrustRequired&&value.event.expectedPeerId==expected)++prompts;}return prompts>=2;},"Retried wrong identity did not require verification");verifier.approve(targetId,false);size_t mismatchAttempts=attempts(verifierEvents,expected).size();auto quietUntil=Clock::now()+std::chrono::milliseconds(1100);while(Clock::now()<quietUntil){drain(verifier,verifierEvents);drain(target,targetEvents);std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(attempts(verifierEvents,expected).size()==mismatchAttempts,"wrong-identity endpoint retried without a manual/presence trigger");target.stop();verifier.stop();
        std::cout<<"PASS: Stage 2 direct-wrapper/session projection, unchanged wire framing, channel separation, stale-epoch rejection, two-peer reconnect backoff, route invalidation, retry diagnostics, and wrong-identity suspension.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
