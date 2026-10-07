#include "node.hpp"
#include "call_manager.hpp"
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

static void relayManagerContracts(){
    using namespace session;const std::string local(64,'a'),peer(64,'b');SessionEpoch epoch{81};SessionTransportManager manager("relay-profile",local,epoch,40,400);
    require(manager.observeNetwork(epoch,7),"relay manager rejected current network generation");
    RouteObservation lan{{1},"192.168.1.9:7777","discovery","LAN",false,7,TransportKind::Direct,RouteClass::DirectLan};
    RouteObservation internet{{2},"203.0.113.9:7777","discovery","public",false,7,TransportKind::Direct,RouteClass::DirectInternet};
    require(manager.replaceCandidates(epoch,peer,{lan,internet},true,7,100),"candidate plan was rejected");
    require(manager.takeDueRetries(100).empty(),"ordinary discovery candidates initiated a direct probe under ServerFirst");
    auto relays=manager.takeDueRelayAttempts(100);require(relays.size()==1&&relays[0].remoteDeviceId==peer,"server-first relay was not manager-owned");
    RouteObservation relay{{3},"relay:"+peer,"server","opaque relay",false,7,TransportKind::Relay,RouteClass::ServerRelay};
    require(manager.registerAttempt(epoch,{30},peer,relay,100),"relay attempt was not registered");DirectTransportAdapter relayAdapter(epoch,{30},relay,peer);
    manager.observeTransport(relayAdapter.identityPresented(peer,protocol::CapabilityMutualTls|protocol::CapabilityDirectHandoff),101);manager.markLocallyTrusted(epoch,{30});manager.markRemoteAccepted(epoch,{30});require(manager.arbitrateWinner(epoch,{30},102).activated(),"authenticated relay did not activate");
    require(manager.replaceCandidates(epoch,peer,{lan,internet},true,7,110),"authoritative refresh rejected an unchanged relay plan");
    require(manager.takeDueRetries(110).empty(),"refresh bypassed relay-only privacy policy");
    const std::string authorization(43,'p');
    auto staleScoped=lan;staleScoped.networkRevision=6;require(!manager.beginScopedP2pProbe(epoch,peer,authorization,190,staleScoped,110),"stale scoped candidate generation was accepted");
    require(manager.beginScopedP2pProbe(epoch,peer,authorization,190,lan,110),"authorized temporary candidate did not start bounded probe");
    auto probe=manager.takeDueRetries(110);require(probe.size()==1&&probe.front().route.endpointUtf8==lan.endpointUtf8,"authorized direct probe was not scheduled once");
    require(manager.registerAttempt(epoch,{31},peer,probe.front().route,111),"direct probe was not registered beside relay winner");
    auto failedProbe=manager.closeTransport(epoch,{31},"local authentication timeout",true);require(failedProbe.accepted&&!failedProbe.wasActiveWinner&&failedProbe.hasActiveWinner,"failed direct probe downgraded the live relay winner");
    auto relaySnapshot=manager.snapshot(peer);require(relaySnapshot&&relaySnapshot->connected&&relaySnapshot->activeTransport.value==30&&relaySnapshot->activeRoute.routeClass==RouteClass::ServerRelay&&relaySnapshot->latestFailedTransport.value==31,"failed direct probe disturbed relay winner or lost failure correlation");require(manager.takeDueRelayAttempts(10000).empty(),"failed direct probe started a second relay while a healthy relay winner existed");
    require(manager.cancelScopedP2p(epoch,peer).empty(),"cancel resurrected an already-closed direct route");
    require(!manager.beginScopedP2pProbe(epoch,peer,authorization,120,lan,121),"expired authorization started a probe");
    const std::string secondAuthorization(43,'q');require(manager.beginScopedP2pProbe(epoch,peer,secondAuthorization,300,lan,130),"replacement authorization was rejected");
    probe=manager.takeDueRetries(130);require(probe.size()==1,"replacement scoped probe was not scheduled");require(manager.registerAttempt(epoch,{32},peer,probe.front().route,131),"standby probe registration failed");DirectTransportAdapter standby(epoch,{32},probe.front().route,peer);manager.observeTransport(standby.identityPresented(peer,protocol::CapabilityMutualTls|protocol::CapabilityDirectHandoff),132);manager.markLocallyTrusted(epoch,{32});manager.markRemoteAccepted(epoch,{32});require(manager.arbitrateWinner(epoch,{32},133).disposition==WinnerDisposition::VerifiedStandby,"verified direct probe auto-promoted instead of becoming standby");relaySnapshot=manager.snapshot(peer);require(relaySnapshot&&relaySnapshot->activeTransport.value==30&&relaySnapshot->p2pAvailability==P2pAvailability::Available&&relaySnapshot->p2pStandbyTransport.value==32,"standby state was not projected authoritatively");
    auto promotion=manager.promoteVerifiedP2P(epoch,peer,secondAuthorization,134);require(promotion.size()==1&&promotion.front().kind==HandoffEffect::Kind::Send,"explicit P2P promotion did not begin the existing authenticated handoff");relaySnapshot=manager.snapshot(peer);require(relaySnapshot&&relaySnapshot->p2pAvailability==P2pAvailability::Promoted&&relaySnapshot->activeTransport.value==30,"promotion preflight changed delivery winner before handoff completion");
    require(manager.cancelScopedP2p(epoch,peer).size()==1,"disabling P2P did not close verified standby");relaySnapshot=manager.snapshot(peer);require(relaySnapshot&&relaySnapshot->activeTransport.value==30,"disabling P2P disturbed relay winner");
    // Drive an explicit relay -> verified-direct promotion on two independent
    // managers. The existing peer handoff frames remain the coordinator; this
    // new policy only prevents their first Hello from being sent automatically.
    SessionTransportManager left("left",local,{83},40,400),right("right",peer,{83},40,400);left.observeNetwork({83},1);right.observeNetwork({83},1);
    RouteObservation leftRelay{{10},"relay:"+peer,"server","relay",false,1,TransportKind::Relay,RouteClass::ServerRelay},rightRelay{{110},"relay:"+local,"server","relay",true,1,TransportKind::Relay,RouteClass::ServerRelay};
    auto activate=[&](SessionTransportManager& owner,const std::string& remote,TransportInstanceId id,RouteObservation route){require(owner.registerAttempt({83},id,remote,route,0),"relay registration failed");DirectTransportAdapter adapter({83},id,route,remote);owner.observeTransport(adapter.identityPresented(remote,protocol::CapabilityMutualTls|protocol::CapabilityDirectHandoff),0);owner.markLocallyTrusted({83},id);owner.markRemoteAccepted({83},id);require(owner.arbitrateWinner({83},id,1).activated(),"relay activation failed");};
    activate(left,peer,{10},leftRelay);activate(right,local,{110},rightRelay);
    auto leftRelayHello=left.attachHandoff({83},{10},std::string(32,'1'),2);auto rightRelayHello=right.attachHandoff({83},{110},std::string(32,'2'),2);
    require(leftRelayHello.size()==1&&rightRelayHello.size()==1,"relay handoff capability was not armed");
    left.receiveHandoff({83},{10},rightRelayHello.front().envelope,2);right.receiveHandoff({83},{110},leftRelayHello.front().envelope,2);
    RouteObservation leftDirect{{11},"192.168.1.9:7777","scoped","P2P",false,1,TransportKind::Direct,RouteClass::DirectLan},rightDirect{{111},"192.168.1.8:7777","scoped","P2P",true,1,TransportKind::Direct,RouteClass::DirectLan};const std::string paired(43,'r');
    require(left.beginScopedP2pProbe({83},peer,paired,10000,leftDirect,2)&&right.beginScopedP2pProbe({83},local,paired,10000,rightDirect,2),"paired scoped probes were rejected");
    auto authenticateStandby=[&](SessionTransportManager& owner,const std::string& remote,TransportInstanceId id,RouteObservation route){auto due=owner.takeDueRetries(2);require(due.size()==1,"scoped probe was not bounded to one route");require(owner.registerAttempt({83},id,remote,route,2),"scoped transport registration failed");DirectTransportAdapter adapter({83},id,route,remote);owner.observeTransport(adapter.identityPresented(remote,protocol::CapabilityMutualTls|protocol::CapabilityDirectHandoff),2);owner.markLocallyTrusted({83},id);owner.markRemoteAccepted({83},id);require(owner.arbitrateWinner({83},id,3).disposition==WinnerDisposition::VerifiedStandby,"direct candidate did not remain verified standby");};
    authenticateStandby(left,peer,{11},leftDirect);authenticateStandby(right,local,{111},rightDirect);
    struct Wire{bool toLeft;HandoffEnvelope envelope;};std::vector<Wire> wire;
    auto apply=[&](bool fromLeft,const std::vector<HandoffEffect>& effects){for(const auto& effect:effects)if(effect.kind==HandoffEffect::Kind::Send)wire.push_back({!fromLeft,effect.envelope});};
    apply(true,left.promoteVerifiedP2P({83},peer,paired,4));
    for(std::uint64_t now=4;now<9000&&(!wire.empty()||left.snapshot(peer)->activeTransport.value!=11||right.snapshot(local)->activeTransport.value!=111);now+=500){auto batch=std::move(wire);wire.clear();for(const auto& item:batch)apply(item.toLeft,item.toLeft?left.receiveHandoff({83},{11},item.envelope,now):right.receiveHandoff({83},{111},item.envelope,now));apply(true,left.handoffTick({83},now));apply(false,right.handoffTick({83},now));}
    require(left.snapshot(peer)->activeTransport.value==11&&right.snapshot(local)->activeTransport.value==111,"explicit promotion did not atomically move both delivery winners to direct");
    left.closeTransport({83},{11},"direct failed",true);right.closeTransport({83},{111},"direct failed",true);require(left.snapshot(peer)->activeTransport.value==10,"left direct failure did not restore retained relay winner");require(right.snapshot(local)->activeTransport.value==110,"right direct failure did not restore retained relay winner");
    RouteObservation stale=lan;stale.networkRevision=6;require(!manager.replaceCandidates(epoch,peer,{stale},true,6,600),"stale candidate generation replaced current plan");
    require(manager.observeNetwork(epoch,8),"new network generation was rejected");require(!manager.attachablePeer(epoch,{30}),"old-generation relay completion remained attachable");
    SessionTransportManager directWinner("direct-profile",local,{82},40,400);directWinner.observeNetwork({82},1);require(directWinner.registerAttempt({82},{1},peer,lan,0),"direct winner registration failed");DirectTransportAdapter directAdapter({82},{1},lan,peer);directWinner.observeTransport(directAdapter.identityPresented(peer,protocol::CapabilityMutualTls|protocol::CapabilityDirectHandoff),0);directWinner.markLocallyTrusted({82},{1});directWinner.markRemoteAccepted({82},{1});require(directWinner.arbitrateWinner({82},{1},0).activated(),"direct winner did not activate");require(directWinner.replaceCandidates({82},peer,{lan},true,1,1),"direct winner plan failed");require(directWinner.takeDueRelayAttempts(10000).empty(),"server/relay availability disturbed healthy direct winner");
    require(directWinner.registerAttempt({82},{2},peer,relay,2),"delayed relay registration failed");DirectTransportAdapter lateRelay({82},{2},relay,peer);directWinner.observeTransport(lateRelay.identityPresented(peer,protocol::CapabilityMutualTls|protocol::CapabilityDirectHandoff),2);directWinner.markLocallyTrusted({82},{2});directWinner.markRemoteAccepted({82},{2});require(directWinner.arbitrateWinner({82},{2},3).disposition==WinnerDisposition::RejectedDuplicate,"relay replaced a healthy direct winner");
}

static void relayNodeContracts(){
    // A production relay-only profile must open local encrypted state without
    // allocating a direct TCP listener or publishing a usable listener port.
    Node relayOnly;std::vector<Seen> relayOnlyEvents;auto relayOnlyRoot=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/("relay-only-"+Storage::newMessageId());relayOnly.start(relayOnlyRoot.wstring(),0,L"Relay only",false);until(relayOnly,relayOnlyEvents,[&]{for(const auto& event:relayOnlyEvents)if(event.event.kind==NodeEvent::Kind::Ready){require(event.event.port==0,"relay-only startup opened a direct listener");return true;}return false;},"relay-only node did not start");relayOnly.stop();
    Node left,right;std::string leftId,rightId;std::wstring rightCallId;std::uint64_t leftAttempt=0,rightAttempt=0,leftMediaGeneration=0,rightMediaGeneration=0;std::uint32_t leftMediaState=0,rightMediaState=0;bool leftOnline=false,rightOnline=false,leftSessionDisconnected=false,rightSessionDisconnected=false,leftReceived=false,rightReceived=false,leftDelivered=false,rightDelivered=false,rightRinging=false,leftIdle=false,rightIdle=false;std::set<std::string> leftReceivedMessages,rightReceivedMessages;
    bool temporaryListenerOpened=false,temporaryListenerClosed=false;std::uint16_t temporaryListenerPort=0;
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/("relay-"+Storage::newMessageId());left.start((root/L"left").wstring(),0,L"Relay left",false);right.start((root/L"right").wstring(),0,L"Relay right",false);
    auto poll=[&]{for(auto& event:left.poll()){if(event.kind==NodeEvent::Kind::Ready)leftId=event.peerId;if(event.kind==NodeEvent::Kind::ConnectionLifecycle&&event.text==L"Temporary reliable direct listener opened"){temporaryListenerOpened=true;temporaryListenerPort=event.port;}if(event.kind==NodeEvent::Kind::ConnectionLifecycle&&event.text==L"Temporary reliable direct listener closed")temporaryListenerClosed=true;if(event.kind==NodeEvent::Kind::RelayRequested)leftAttempt=event.attemptId;if(event.kind==NodeEvent::Kind::TrustRequired)left.approve(event.peerId,true);if(event.kind==NodeEvent::Kind::Connected)leftOnline=true;if(event.kind==NodeEvent::Kind::Disconnected)leftOnline=false;if(event.kind==NodeEvent::Kind::SessionSnapshot&&!event.sessionConnected)leftSessionDisconnected=true;if(event.kind==NodeEvent::Kind::Message){leftReceived=true;for(const auto& message:event.messages)leftReceivedMessages.insert(message.id);}if(event.kind==NodeEvent::Kind::Delivered)leftDelivered=true;if(event.kind==NodeEvent::Kind::VoiceCall){leftMediaState=event.mediaState;leftMediaGeneration=event.mediaGeneration;if(event.callState==0)leftIdle=true;}}for(auto& event:right.poll()){if(event.kind==NodeEvent::Kind::Ready)rightId=event.peerId;if(event.kind==NodeEvent::Kind::RelayRequested)rightAttempt=event.attemptId;if(event.kind==NodeEvent::Kind::TrustRequired)right.approve(event.peerId,true);if(event.kind==NodeEvent::Kind::Connected)rightOnline=true;if(event.kind==NodeEvent::Kind::Disconnected)rightOnline=false;if(event.kind==NodeEvent::Kind::SessionSnapshot&&!event.sessionConnected)rightSessionDisconnected=true;if(event.kind==NodeEvent::Kind::Message){rightReceived=true;for(const auto& message:event.messages)rightReceivedMessages.insert(message.id);}if(event.kind==NodeEvent::Kind::Delivered)rightDelivered=true;if(event.kind==NodeEvent::Kind::VoiceCall){rightMediaState=event.mediaState;rightMediaGeneration=event.mediaGeneration;if(event.callState==2){rightRinging=true;rightCallId=event.detail;}if(event.callState==0)rightIdle=true;}}};
    auto untilLocal=[&](auto predicate,const char* failure,int milliseconds=5000){auto deadline=Clock::now()+std::chrono::milliseconds(milliseconds);while(Clock::now()<deadline){poll();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error(failure);};
    // Explicit incoming/outgoing control demand, not an idle contact observation.
    untilLocal([&]{return !leftId.empty()&&!rightId.empty();},"relay nodes did not start");left.setNetworkContext(1,L"relay-fixture");right.setNetworkContext(1,L"relay-fixture");left.updateCandidates(rightId,L"",0,L"",0,true,1,true);right.updateCandidates(leftId,L"",0,L"",0,true,1,true);
    untilLocal([&]{return leftAttempt&&rightAttempt;},"manager did not request bounded relay fallback",2500);require(left.relayReady(leftAttempt,leftId>rightId)&&right.relayReady(rightAttempt,rightId>leftId),"relay physical adapter rejected current attempts");
    auto pump=[&]{unsigned char bytes[64*1024];for(;;){int count=left.takeRelayOutput(leftAttempt,bytes,sizeof(bytes),0);if(count<=0)break;require(right.relayReceive(rightAttempt,bytes,count),"right relay input rejected opaque peer TLS");}for(;;){int count=right.takeRelayOutput(rightAttempt,bytes,sizeof(bytes),0);if(count<=0)break;require(left.relayReceive(leftAttempt,bytes,count),"left relay input rejected opaque peer TLS");}poll();};
    auto pumpUntil=[&](auto predicate,const char* failure){auto deadline=Clock::now()+std::chrono::seconds(8);while(Clock::now()<deadline){pump();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}throw std::runtime_error(failure);};
    pumpUntil([&]{return leftOnline&&rightOnline;},"inner peer TLS/trust did not authenticate over opaque relay");require(!left.send(rightId,L"relay left to right").empty()&&!right.send(leftId,L"relay right to left").empty(),"relay winner rejected durable messages");pumpUntil([&]{return leftReceived&&rightReceived&&leftDelivered&&rightDelivered;},"message/ACK delivery did not survive relay transport");
    const auto oldLeftAttempt=leftAttempt,oldRightAttempt=rightAttempt;leftSessionDisconnected=rightSessionDisconnected=false;
    left.relayClosed(oldLeftAttempt,L"test remote relay EOF");
    untilLocal([&]{return !leftOnline&&leftSessionDisconnected;},"first relay EOF did not retire the local authoritative winning session");
    const auto queuedOffline=left.send(rightId,L"queued during disconnected relay");
    require(!queuedOffline.empty(),"verified disconnected relay rejected durable offline enqueue");
    require(rightReceivedMessages.count(queuedOffline)==0,"offline message was projected as remotely received before delivery");
    untilLocal([&]{return leftAttempt!=oldLeftAttempt;},"relay-only idle loop did not request a replacement while its peer was still stale",7000);
    require(rightOnline&&rightAttempt==oldRightAttempt,"test did not preserve the asymmetric stale winning session");
    require(left.relayReady(leftAttempt,leftId>rightId),"early replacement relay attempt was rejected");
    right.relayClosed(oldRightAttempt,L"test paired relay EOF");
    untilLocal([&]{return !rightOnline&&rightSessionDisconnected;},"paired relay EOF did not retire the stale remote winning session");
    untilLocal([&]{return rightAttempt!=oldRightAttempt;},"stale remote session blocked a fresh authenticated relay request",7000);
    require(right.relayReady(rightAttempt,rightId>leftId),"replacement relay attempt was rejected");
    leftOnline=rightOnline=false;pumpUntil([&]{return leftOnline&&rightOnline;},"fresh relay sessions did not reauthenticate without restarting either node");
    pumpUntil([&]{return rightReceivedMessages.count(queuedOffline)!=0;},"existing outbox did not drain the exact offline ID after reconnect");
    const auto afterReconnect=left.send(rightId,L"message after asymmetric relay recovery");require(!afterReconnect.empty(),"reconnected relay winner rejected a message");
    pumpUntil([&]{return rightReceivedMessages.count(afterReconnect)!=0;},"reconnected relay did not carry a new message");
    require(left.beginScopedP2pProbe(rightId,std::string(43,'s'),L"127.0.0.1",1,UINT64_MAX,1),"authorized scoped probe was rejected before temporary listener allocation");untilLocal([&]{return temporaryListenerOpened;},"scoped P2P did not open a temporary listener");require(temporaryListenerPort!=0,"temporary listener did not report its OS-selected port");left.cancelScopedP2p(rightId);untilLocal([&]{return temporaryListenerClosed;},"cancelling scoped P2P did not close the temporary listener");
    const auto relayCall=left.startVoiceCall(rightId);const std::wstring relayCallWide(relayCall.begin(),relayCall.end());require(!relayCall.empty(),"relay control winner was incorrectly treated as voice-incapable");pumpUntil([&]{return rightRinging&&rightCallId==relayCallWide;},"call-control signaling did not cross the authenticated opaque relay");require(right.answerVoiceCall(relayCall),"relayed incoming call could not be answered");pumpUntil([&]{return leftMediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&rightMediaState==static_cast<std::uint32_t>(calls::MediaState::Connected);},"independent ICE media did not connect while call control used the server relay");auto originalGeneration=leftMediaGeneration;left.setNetworkContext(2,L"relay-fixture-changed");pumpUntil([&]{return leftMediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&rightMediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&leftMediaGeneration>originalGeneration&&rightMediaGeneration==leftMediaGeneration;},"media recovery signaling did not cross the authenticated opaque relay");require(leftOnline&&rightOnline,"media recovery disturbed the healthy relay messaging winner");require(right.endVoiceCall(relayCall),"relayed active call could not be ended");pumpUntil([&]{return leftIdle&&rightIdle;},"relayed call-control teardown did not converge");
    left.stop();right.stop();
}

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
        relayManagerContracts();
        relayNodeContracts();
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
        std::cout<<"PASS: Server-first reliable relay, asymmetric relay EOF retirement/reconnect, scoped P2P standby/promotion/fallback, Stage 5 direct-text handoff and opaque relay control, Stage 7 ICE media recovery signaled over relay, ACK/dedup, reconnect and identity safeguards.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
