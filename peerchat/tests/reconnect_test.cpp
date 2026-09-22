#include "node.hpp"
#include "direct_transport_adapter.hpp"
#include "protocol.hpp"
#include "session_transport_manager.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <thread>

using namespace peerchat;
using Clock=std::chrono::steady_clock;
struct Seen {NodeEvent event;Clock::time_point at;};
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void drain(Node& node,std::vector<Seen>& seen){for(auto& event:node.poll())seen.push_back({std::move(event),Clock::now()});}
template<class Check> static void until(Node& node,std::vector<Seen>& seen,Check check,const char* message,int milliseconds=5000){auto deadline=Clock::now()+std::chrono::milliseconds(milliseconds);while(Clock::now()<deadline){drain(node,seen);if(check())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));}throw std::runtime_error(message);}
static std::vector<Seen> attempts(const std::vector<Seen>& seen,const std::string& peer){std::vector<Seen> result;for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ConnectionAttempt&&value.event.expectedPeerId==peer)result.push_back(value);return result;}
static size_t schedules(const std::vector<Seen>& seen,const std::string& peer){size_t result=0;for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ReconnectScheduled&&value.event.expectedPeerId==peer)result++;return result;}

static void handoffContracts(){
    using namespace session;
    const std::string a(64,'a'),b(64,'b');const SessionEpoch epoch{71};
    for(int dropped=-1;dropped<=7;++dropped){
        SessionTransportManager left("left",a,epoch),right("right",b,epoch);
        struct Wire{bool toLeft;TransportInstanceId target;HandoffEnvelope envelope;};
        std::vector<Wire> wire;bool lost=false;std::uint64_t now=100;
        auto effects=[&](bool fromLeft,const std::vector<HandoffEffect>& values){
            for(const auto& e:values)if(e.kind==HandoffEffect::Kind::Send){
                if(dropped==6&&e.envelope.message!=HandoffMessage::Hello)continue;
                if(dropped==static_cast<int>(e.envelope.message)&&!lost&&e.envelope.message!=HandoffMessage::Hello){lost=true;continue;}
                wire.push_back({!fromLeft,{fromLeft?e.instance.value+100:e.instance.value-100},e.envelope});
            }else if(e.kind==HandoffEffect::Kind::Retire){
                (fromLeft?left:right).closeTransport(epoch,e.instance,e.reason,false);
            }
        };
        auto authenticated=[&](SessionTransportManager& owner,const std::string& peer,std::uint64_t id,bool incoming){
            RouteObservation route{{id},"loopback:"+std::to_string(id),"fixture","direct",incoming,1};
            require(owner.registerAttempt(epoch,{id},peer,route,now),"handoff attempt registration failed");
            DirectTransportAdapter adapter(epoch,{id},route,peer);
            owner.observeTransport(adapter.identityPresented(peer,protocol::CapabilityMutualTls|protocol::CapabilityDirectHandoff),now);
            owner.markLocallyTrusted(epoch,{id});owner.markRemoteAccepted(epoch,{id});
            return owner.arbitrateWinner(epoch,{id},now);
        };
        auto pump=[&]{
            for(int rounds=0;!wire.empty()&&rounds<30;++rounds){auto batch=std::move(wire);wire.clear();for(const auto& w:batch)
                effects(w.toLeft,(w.toLeft?left:right).receiveHandoff(epoch,w.target,w.envelope,now));}
            require(wire.empty(),"handoff messages formed an unbounded reply loop");
        };
        require(authenticated(left,b,1,false).activated()&&authenticated(right,a,101,true).activated(),"initial handoff binding failed");
        effects(true,left.attachHandoff(epoch,{1},std::string(32,'1'),now));effects(false,right.attachHandoff(epoch,{101},std::string(32,'2'),now));pump();
        require(left.canPrepareHandoff(b,{{2},"alternate","fixture","direct",false,1}),"alternate route was blocked behind winner");
        require(authenticated(left,b,2,false).disposition==WinnerDisposition::AwaitingHandoff&&authenticated(right,a,102,true).disposition==WinnerDisposition::AwaitingHandoff,"candidate bypassed readiness");
        require(left.snapshot(b)->activeTransport.value==1&&right.snapshot(a)->activeTransport.value==101,"preparation demoted healthy winner");
        effects(true,left.attachHandoff(epoch,{2},std::string(32,'3'),now));effects(false,right.attachHandoff(epoch,{102},std::string(32,'4'),now));
        if(dropped==7){left.observeNetwork(epoch,2);right.observeNetwork(epoch,2);}
        pump();
        for(int step=0;step<5;++step){now+=500;effects(true,left.handoffTick(epoch,now));effects(false,right.handoffTick(epoch,now));pump();}
        if(dropped>=6){
            now+=11000;effects(true,left.handoffTick(epoch,now));effects(false,right.handoffTick(epoch,now));pump();
            require(left.snapshot(b)->activeTransport.value==1&&right.snapshot(a)->activeTransport.value==101,"lost coordination tore down healthy original binding");
            continue;
        }
        if(dropped>0)require(lost,"loss fixture never intercepted its handoff phase");
        require(left.snapshot(b)->activeTransport.value==2&&right.snapshot(a)->activeTransport.value==102,"lost/replayed promotion did not converge");
        HandoffEnvelope stale{HandoffMessage::Commit,std::string(32,'3')+std::string(32,'4'),std::string(32,'1')+std::string(32,'2')};
        require(right.receiveHandoff({70},{102},stale,now).empty(),"old profile lifetime mutated handoff");
        require(right.receiveHandoff(epoch,{101},stale,now).empty(),"handoff replay crossed transport instances");
        if(dropped==-1){
            left.closeTransport(epoch,{2},"new winner failed",true);effects(true,left.handoffTick(epoch,now));
            require(left.snapshot(b)->activeTransport.value==1,"retained trusted route was not restored after immediate candidate failure");
            right.closeTransport(epoch,{102},"remote candidate EOF",true);effects(false,right.handoffTick(epoch,now));
            require(right.snapshot(a)->activeTransport.value==101,"remote peer did not restore its retained binding after candidate EOF");
        }else if(dropped==0){
            left.removePeer(b);left.closeTransport(epoch,{2},"new winner failed after trust revocation",true);effects(true,left.handoffTick(epoch,now));
            require(!left.snapshot(b)->connected,"fallback promoted a route after trust revocation");
        }else{
            now+=4000;effects(true,left.handoffTick(epoch,now));effects(false,right.handoffTick(epoch,now));pump();
            require(left.snapshot(b)->activeTransport.value==2&&right.snapshot(a)->activeTransport.value==102,"bounded old drain cleared replacement");
            require(!left.draining({1})&&!right.draining({101}),"draining ownership was not bounded");
        }
    }
    // Legacy peers advertise no new bit, so the manager must never emit handoff.
    SessionTransportManager legacy("legacy",a,epoch);RouteObservation route{{1},"legacy:1","fixture","direct",false,1};
    legacy.registerAttempt(epoch,{1},b,route,0);DirectTransportAdapter adapter(epoch,{1},route,b);
    legacy.observeTransport(adapter.identityPresented(b,protocol::CapabilityMutualTls),0);legacy.markLocallyTrusted(epoch,{1});legacy.markRemoteAccepted(epoch,{1});
    require(legacy.arbitrateWinner(epoch,{1},0).activated(),"legacy peer did not activate");
    require(legacy.attachHandoff(epoch,{1},std::string(32,'1'),0).empty()&&legacy.handoffTick(epoch,1).empty(),"new packets were sent to a legacy peer");
}

static void liveHandoff(){
    Node left,right;std::string a,b;std::uint64_t leftWinner=0,rightWinner=0;
    std::set<std::string> receivedLeft,receivedRight,deliveredLeft,deliveredRight;
    auto token=Storage::newMessageId();auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/("handoff-"+token);
    auto port=static_cast<std::uint16_t>(31000+std::stoul(token.substr(0,4),nullptr,16)%10000);
    bool requireContinuity=false;int promotions=0;
    std::set<std::pair<bool,std::uint64_t>> retired;
    auto collect=[&]{
        auto receive=[&](Node& node,bool isLeft){for(const auto& e:node.poll()){
            if(e.kind==NodeEvent::Kind::Ready)(isLeft?a:b)=e.peerId;
            if(e.kind==NodeEvent::Kind::TrustRequired)node.approve(e.peerId,true);
            if(e.kind==NodeEvent::Kind::SessionSnapshot){
                if(requireContinuity)require(e.sessionConnected,"candidate attempt downgraded a live text session");
                if(e.sessionConnected)(isLeft?leftWinner:rightWinner)=e.attemptId;
            }
            if(e.kind==NodeEvent::Kind::ConnectionLifecycle){
                if(e.text.find(L"handoff commit authenticated")!=std::wstring::npos||e.text.find(L"handoff promotion acknowledged")!=std::wstring::npos)++promotions;
                if(e.text.find(L"transport closed: ")==0)retired.insert({isLeft,e.attemptId});
            }
            if(e.kind==NodeEvent::Kind::Message)for(const auto& m:e.messages)if(m.recipient==(isLeft?a:b))
                require((isLeft?receivedLeft:receivedRight).insert(m.id).second,"handoff duplicated a visible message");
            if(e.kind==NodeEvent::Kind::Delivered)for(const auto& m:e.messages)(isLeft?deliveredLeft:deliveredRight).insert(m.id);
        }};receive(left,true);receive(right,false);
    };
    auto wait=[&](auto predicate,const char* error){auto deadline=Clock::now()+std::chrono::seconds(15);do{collect();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));}while(Clock::now()<deadline);throw std::runtime_error(error);};
    left.start((root/L"left").wstring(),port,L"Handoff left");right.start((root/L"right").wstring(),port+1,L"Handoff right");
    wait([&]{return !a.empty()&&!b.empty();},"handoff nodes did not start");left.connect(L"127.0.0.1",port+1,b);right.connect(L"127.0.0.1",port,a);
    wait([&]{return leftWinner&&rightWinner;},"initial handoff text route failed");
    // Let the already queued authenticated connection-token exchange arrive.
    auto settle=Clock::now()+std::chrono::milliseconds(250);while(Clock::now()<settle){collect();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    const auto oldLeft=leftWinner,oldRight=rightWinner;requireContinuity=true;
    left.connect(L"localhost",port+1,b,L"Alternate direct candidate",L"manual retry",true);
    right.connect(L"localhost",port,a,L"Alternate direct candidate",L"manual retry",true);
    for(int i=0;i<24;++i){left.send(b,L"synthetic handoff message");right.send(a,L"synthetic handoff message");collect();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    wait([&]{return promotions>=2&&leftWinner!=oldLeft&&rightWinner!=oldRight;},"both peers did not coordinate direct handoff");
    wait([&]{return receivedLeft.size()==24&&receivedRight.size()==24&&deliveredLeft.size()==24&&deliveredRight.size()==24;},"text/ACK continuity failed across simultaneous handoff");
    // Either drain timer can close the shared old socket first; the other end
    // then observes EOF, not necessarily its own timer. Require both closes.
    wait([&]{return retired.count({true,oldLeft})&&retired.count({false,oldRight});},"old direct transports did not drain within the bound");
    left.send(b,L"synthetic after drain");right.send(a,L"synthetic after drain");
    wait([&]{return receivedLeft.size()==25&&receivedRight.size()==25&&deliveredLeft.size()==25&&deliveredRight.size()==25;},"text did not survive old route retirement");
    left.stop();right.stop();
}

int main(int argc,char** argv){
    try{
        handoffContracts();
        // Retain the Stage 2 adapter-contract checks while Stage 3 moves the
        // direct-path continuity decisions into the manager below.
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

        // Stage 3 decisions use explicit time and lifetime tokens so endpoint
        // reuse, old callbacks and delayed network observations are deterministic.
        const std::string continuityPeer(64,'b'),wrongIdentity(64,'c');
        session::SessionEpoch continuityEpoch{51};
        session::SessionTransportManager continuity("profile-stage3",std::string(64,'a'),continuityEpoch,100,800);
        auto activate=[&](std::uint64_t instance,session::RouteObservation observation,std::uint64_t now){
            require(continuity.registerAttempt(continuityEpoch,{instance},continuityPeer,observation,now),"continuity owner rejected attempt registration");
            session::DirectTransportAdapter direct(continuityEpoch,{instance},observation,continuityPeer);
            require(continuity.observeTransport(direct.identityPresented(continuityPeer,protocol::CapabilityMutualTls),now),"continuity owner rejected identity observation");
            require(continuity.markLocallyTrusted(continuityEpoch,{instance})&&continuity.markRemoteAccepted(continuityEpoch,{instance}),"continuity owner rejected authenticated readiness");
            return continuity.arbitrateWinner(continuityEpoch,{instance},now);
        };
        session::RouteObservation activeRoute{{100},"192.168.1.17:7777","discovery","LAN",false,1};
        auto activeDecision=activate(100,activeRoute,1000);require(activeDecision.activated(),"first authenticated transport was not activated");
        auto activeSnapshot=continuity.snapshot(continuityPeer);require(activeSnapshot&&activeSnapshot->connected&&activeSnapshot->activeTransport.value==100,"authoritative active snapshot was not published");
        auto revisionAfterActive=activeSnapshot->revision;

        session::RouteObservation failedCandidate{{101},"198.51.100.7:7777","discovery","public",false,1};
        require(continuity.registerAttempt(continuityEpoch,{101},continuityPeer,failedCandidate,1100),"candidate registration failed");
        auto candidateClose=continuity.closeTransport(continuityEpoch,{101},"candidate failed",true);
        require(candidateClose.accepted&&!candidateClose.wasActiveWinner&&candidateClose.hasActiveWinner,"failed candidate downgraded healthy winner");
        activeSnapshot=continuity.snapshot(continuityPeer);require(activeSnapshot->connected&&activeSnapshot->activeTransport.value==100&&activeSnapshot->latestFailedTransport.value==101,"candidate failure replaced active snapshot state");
        session::RouteObservation wrongRoute{{104},"203.0.113.44:7777","discovery","fallback",false,1};
        continuity.registerAttempt(continuityEpoch,{104},continuityPeer,wrongRoute,1150);session::DirectTransportAdapter wrongDirect(continuityEpoch,{104},wrongRoute,continuityPeer);continuity.observeTransport(wrongDirect.identityPresented(wrongIdentity,protocol::CapabilityMutualTls),1150);
        auto wrongDecision=continuity.arbitrateWinner(continuityEpoch,{104},1150);require(wrongDecision.disposition==session::WinnerDisposition::RejectedNotReady,"wrong identity became eligible without explicit trust");continuity.closeTransport(continuityEpoch,{104},"identity mismatch",true);require(continuity.snapshot(continuityPeer)->activeTransport.value==100,"wrong-identity fallback replaced the verified winner");

        session::RouteObservation duplicateRoute{{102},activeRoute.endpointUtf8,"refreshed","same endpoint",true,1};
        auto duplicateDecision=activate(102,duplicateRoute,1200);require(duplicateDecision.disposition==session::WinnerDisposition::RejectedDuplicate&&duplicateDecision.winner.value==102,"deterministic duplicate loser was not rejected by the owner");
        continuity.closeTransport(continuityEpoch,{102},duplicateDecision.reasonUtf8,false);
        require(continuity.snapshot(continuityPeer)->activeTransport.value==100,"duplicate loser close cleared the winner");

        session::RouteObservation replacementRoute{{103},activeRoute.endpointUtf8,"refreshed","reused endpoint/new lifetime",false,2};
        continuity.observeProgress(continuityEpoch,{100},0);
        auto replacementDecision=activate(103,replacementRoute,4001);require(replacementDecision.disposition==session::WinnerDisposition::ReplacedExisting&&replacementDecision.retire.size()==1&&replacementDecision.retire[0].value==100,"stale winner was not deterministically replaced");
        auto oldClose=continuity.closeTransport(continuityEpoch,{100},"delayed old callback",true);require(oldClose.accepted&&!oldClose.wasActiveWinner&&oldClose.hasActiveWinner,"old callback demoted reused-endpoint replacement");
        require(continuity.snapshot(continuityPeer)->activeTransport.value==103,"old callback changed the new instance token's binding");

        require(continuity.observeNetwork(continuityEpoch,2),"network observation was rejected");
        auto staleCancellation=continuity.cancelRoutes(continuityEpoch,continuityPeer,"",true,1);
        require(staleCancellation.staleObservation&&staleCancellation.cancel.empty()&&continuity.snapshot(continuityPeer)->activeTransport.value==103,"old network invalidation cancelled a new-generation winner");
        auto exactRefresh=continuity.cancelRoutes(continuityEpoch,continuityPeer,replacementRoute.endpointUtf8,false,2);
        require(exactRefresh.retainedActiveWinner&&exactRefresh.cancel.empty(),"presence/endpoint refresh closed a healthy winner");

        const std::string retryPeer(64,'d');session::RouteObservation retryRoute{{200},"203.0.113.9:7777","saved","automatic",false,2};
        auto firstRetry=continuity.scheduleRetry(retryPeer,retryRoute,5000,0);require(firstRetry.scheduled&&firstRetry.delayMilliseconds==80,"retry owner did not apply deterministic bounded jitter");
        continuity.requestImmediateRetry(retryPeer,retryRoute,5001);auto immediate=continuity.takeDueRetries(5001);require(immediate.size()==1&&immediate[0].remoteDeviceId==retryPeer,"presence/manual trigger did not produce one immediate retry");
        continuity.registerAttempt(continuityEpoch,{200},retryPeer,retryRoute,5001);session::DirectTransportAdapter retryDirect(continuityEpoch,{200},retryRoute,retryPeer);continuity.observeTransport(retryDirect.identityPresented(retryPeer,protocol::CapabilityMutualTls),5001);continuity.markLocallyTrusted(continuityEpoch,{200});continuity.markRemoteAccepted(continuityEpoch,{200});require(continuity.arbitrateWinner(continuityEpoch,{200},5001).activated(),"retry transport was not promoted");
        require(continuity.takeDueRetries(6000).empty(),"old retry timer fired while a healthy winner existed");
        require(continuity.snapshot(continuityPeer)->revision>revisionAfterActive,"revisioned snapshot did not advance across owner decisions");

        protocol::Writer accepted(protocol::Accepted);
        const Blob acceptedWire=accepted.frame();
        const Blob expectedAcceptedWire={0,0,0,4,'P','T',1,static_cast<unsigned char>(protocol::Accepted)};
        require(acceptedWire==expectedAcceptedWire,"legacy protocol framing changed");
        if(argc==2&&std::string(argv[1])=="--contract-only"){
            std::cout<<"PASS: Stage 3 continuity plus Stage 5 readiness, lost/replayed handoff, promotion/drain/fallback, stale lifetimes, legacy gating and unchanged legacy framing.\n";
            return 0;
        }
        liveHandoff();

        auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::filesystem::path(L"reconnect-"+std::to_wstring(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        const std::string peerA(64,'a'),peerB(64,'b');Node node;std::vector<Seen> seen;node.start((root/L"main").wstring(),29761,L"Reconnect test");until(node,seen,[&]{for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::Ready)return true;return false;},"node did not start");
        node.saveContact({peerA,L"Peer A",{},{{L"127.0.0.1",29881}},true});node.saveContact({peerB,L"Peer B",{},{{L"127.0.0.1",29882}},true});
        // This includes three real Winsock connect failures, not just the short
        // injected backoff. Keep delay/cap assertions below independent of the
        // OS's variable refusal latency and build-machine scheduling.
        until(node,seen,[&]{return attempts(seen,peerA).size()>=3&&attempts(seen,peerB).size()>=3;},"two peers did not retry with backoff",15000);
        auto a=attempts(seen,peerA),b=attempts(seen,peerB);require(a[0].event.candidate==L"127.0.0.1:29881"&&b[0].event.candidate==L"127.0.0.1:29882","simultaneous peer diagnostics crossed candidates");require(a[0].event.candidateSource==L"Saved contact"&&a[0].event.route==L"automatic reconnect","automatic retry metadata was not propagated");
        auto gapOne=std::chrono::duration_cast<std::chrono::milliseconds>(a[1].at-a[0].at).count(),gapTwo=std::chrono::duration_cast<std::chrono::milliseconds>(a[2].at-a[1].at).count();require(gapOne>=50&&gapTwo>=110&&gapTwo>gapOne,"reconnect delay did not grow exponentially");
        for(const auto& value:seen)if(value.event.kind==NodeEvent::Kind::ReconnectScheduled)require(value.event.retryDelayMilliseconds>0&&value.event.retryDelayMilliseconds<=800,"reconnect jitter exceeded the configured cap");
        until(node,seen,[&]{return schedules(seen,peerA)>=3&&schedules(seen,peerB)>=3;},"failed attempts did not enter backoff");size_t before=attempts(seen,peerA).size();node.connect(L"127.0.0.1",29881,peerA,L"Saved contact",L"manual retry",true);until(node,seen,[&]{return attempts(seen,peerA).size()>before;},"manual retry did not force an immediate attempt",300);auto manual=attempts(seen,peerA).back();require(manual.event.route==L"manual retry","manual retry metadata was lost");
        until(node,seen,[&]{return schedules(seen,peerB)>=3;},"peer B was not waiting in backoff");before=attempts(seen,peerB).size();node.connect(L"127.0.0.1",29883,peerB,L"Discovery LAN candidate",L"candidate update",true);until(node,seen,[&]{return attempts(seen,peerB).size()>before;},"candidate update did not force an immediate attempt",300);auto presence=attempts(seen,peerB).back();require(presence.event.candidate==L"127.0.0.1:29883"&&presence.event.route==L"candidate update","presence candidate retry was not correlated");until(node,seen,[&]{return schedules(seen,peerA)>=4;},"peer A did not return to backoff before route invalidation");before=attempts(seen,peerA).size();node.invalidateRoute(peerA,L"127.0.0.1",29881);auto invalidatedUntil=Clock::now()+std::chrono::milliseconds(1100);while(Clock::now()<invalidatedUntil){drain(node,seen);std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(attempts(seen,peerA).size()==before,"stale saved route retried after runtime network invalidation");node.stop();

        Node routeA,routeB;std::vector<Seen> routeAEvents,routeBEvents;routeA.start((root/L"route-a").wstring(),29764,L"Route A");routeB.start((root/L"route-b").wstring(),29765,L"Route B");std::string routeAId,routeBId;until(routeA,routeAEvents,[&]{for(const auto& value:routeAEvents)if(value.event.kind==NodeEvent::Kind::Ready){routeAId=value.event.peerId;return true;}return false;},"route A did not start");until(routeB,routeBEvents,[&]{for(const auto& value:routeBEvents)if(value.event.kind==NodeEvent::Kind::Ready){routeBId=value.event.peerId;return true;}return false;},"route B did not start");routeA.connect(L"127.0.0.1",29765,routeBId,L"Current public candidate",L"network change");bool routeAOnline=false,routeBOnline=false;auto routeDeadline=Clock::now()+std::chrono::seconds(5);while(Clock::now()<routeDeadline&&(!routeAOnline||!routeBOnline)){for(auto& event:routeA.poll()){routeAEvents.push_back({event,Clock::now()});if(event.kind==NodeEvent::Kind::TrustRequired)routeA.approve(event.peerId,true);if(event.kind==NodeEvent::Kind::Connected&&event.peerId==routeBId)routeAOnline=true;}for(auto& event:routeB.poll()){routeBEvents.push_back({event,Clock::now()});if(event.kind==NodeEvent::Kind::TrustRequired)routeB.approve(event.peerId,true);if(event.kind==NodeEvent::Kind::Connected&&event.peerId==routeAId)routeBOnline=true;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(routeAOnline&&routeBOnline,"current public route did not authenticate");size_t routeADisconnects=0;for(const auto& value:routeAEvents)if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.peerId==routeBId)++routeADisconnects;routeA.invalidateRoute(routeBId,L"192.168.1.17",7777);auto delayedOldGeneration=Clock::now()+std::chrono::milliseconds(500);while(Clock::now()<delayedOldGeneration){drain(routeA,routeAEvents);drain(routeB,routeBEvents);std::this_thread::sleep_for(std::chrono::milliseconds(10));}size_t afterOldGeneration=0;for(const auto& value:routeAEvents)if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.peerId==routeBId)++afterOldGeneration;require(afterOldGeneration==routeADisconnects,"delayed old-generation invalidation closed the new authenticated route");routeA.invalidateRoute(routeBId,L"127.0.0.1",29765);auto endpointRefresh=Clock::now()+std::chrono::milliseconds(500);while(Clock::now()<endpointRefresh){drain(routeA,routeAEvents);drain(routeB,routeBEvents);std::this_thread::sleep_for(std::chrono::milliseconds(10));}size_t afterEndpointRefresh=0;bool retainedDiagnostic=false;for(const auto& value:routeAEvents){if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.peerId==routeBId)++afterEndpointRefresh;if(value.event.kind==NodeEvent::Kind::ConnectionLifecycle&&value.event.peerId==routeBId&&value.event.text.find(L"live verified session retained")!=std::wstring::npos)retainedDiagnostic=true;}require(afterEndpointRefresh==routeADisconnects,"authoritative endpoint refresh closed the healthy authenticated winner");require(retainedDiagnostic,"live-route retention was not diagnosed");require(!routeA.send(routeBId,L"new route remains live").empty(),"new authenticated route was unusable after stale invalidation");routeA.stop();routeB.stop();

        Node target,verifier;std::vector<Seen> targetEvents,verifierEvents;target.start((root/L"target").wstring(),29762,L"Actual peer");verifier.start((root/L"verifier").wstring(),29763,L"Verifier");std::string targetId;until(target,targetEvents,[&]{for(const auto& value:targetEvents)if(value.event.kind==NodeEvent::Kind::Ready){targetId=value.event.peerId;return true;}return false;},"target did not start");until(verifier,verifierEvents,[&]{for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::Ready)return true;return false;},"verifier did not start");const std::string expected(64,'e');verifier.saveContact({expected,L"Expected peer",{},{{L"127.0.0.1",29762}},true});
        NodeEvent mismatch;until(verifier,verifierEvents,[&]{drain(target,targetEvents);for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::TrustRequired&&value.event.expectedPeerId==expected){mismatch=value.event;return true;}return false;},"wrong identity did not reach verification");require(mismatch.peerId==targetId&&mismatch.candidate==L"127.0.0.1:29762"&&mismatch.route==L"automatic reconnect","wrong-identity attempt lost correlation");size_t pendingAttempts=attempts(verifierEvents,expected).size();verifier.connect(L"127.0.0.1",29762,expected,L"Discovery LAN candidate",L"candidate update - same LAN",true);std::this_thread::sleep_for(std::chrono::milliseconds(30));drain(verifier,verifierEvents);verifier.connect(L"127.0.0.1",29762,expected,L"Refreshed candidate",L"manual retry",true);until(verifier,verifierEvents,[&]{drain(target,targetEvents);return attempts(verifierEvents,expected).size()>pendingAttempts;},"Manual repair was blocked by outstanding trust/transport state");bool refreshedMetadata=false;for(const auto& value:verifierEvents)if(value.event.kind==NodeEvent::Kind::Disconnected&&value.event.expectedPeerId==expected&&value.event.candidateSource==L"Discovery LAN candidate"&&value.event.route==L"candidate update - same LAN")refreshedMetadata=true;require(refreshedMetadata,"Authoritative same-endpoint refresh left the active attempt labeled Saved contact");until(verifier,verifierEvents,[&]{drain(target,targetEvents);int prompts=0;for(const auto& value:verifierEvents){require(value.event.kind!=NodeEvent::Kind::Connected,"Repair bypassed identity verification");if(value.event.kind==NodeEvent::Kind::TrustRequired&&value.event.expectedPeerId==expected)++prompts;}return prompts>=2;},"Retried wrong identity did not require verification");verifier.approve(targetId,false);size_t mismatchAttempts=attempts(verifierEvents,expected).size();auto quietUntil=Clock::now()+std::chrono::milliseconds(1100);while(Clock::now()<quietUntil){drain(verifier,verifierEvents);drain(target,targetEvents);std::this_thread::sleep_for(std::chrono::milliseconds(10));}require(attempts(verifierEvents,expected).size()==mismatchAttempts,"wrong-identity endpoint retried without a manual/presence trigger");target.stop();verifier.stop();
        std::cout<<"PASS: Stage 5 direct-text handoff, simultaneous candidates, bidirectional ACK/dedup, bounded drain; Stage 3 reconnect, snapshots, route invalidation, diagnostics and identity suspension.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
