#include "node.hpp"
#include "call_manager.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct CallView {std::wstring state;std::string callId,peer;std::uint64_t lifetime=0,mediaGeneration=0;std::uint32_t mediaState=0,mediaRoute=0;bool muted=false,deafened=false,controlAvailable=true;int connectedCount=0,idleCount=0;};
}

int main(){
    using namespace peerchat;using Clock=std::chrono::steady_clock;
    std::array<Node,2> nodes;std::array<std::string,2> identities;std::array<bool,2> ready{},online{};std::array<CallView,2> calls;int textReceived=0;
    auto token=Storage::newMessageId();auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/std::wstring(token.begin(),token.end());unsigned short base=static_cast<unsigned short>(24000+std::stoul(token.substr(0,4),nullptr,16)%15000);
    auto collect=[&]{for(size_t index=0;index<nodes.size();++index)for(auto& event:nodes[index].poll()){
        if(event.kind==NodeEvent::Kind::Ready){identities[index]=event.peerId;ready[index]=true;}
        else if(event.kind==NodeEvent::Kind::TrustRequired)nodes[index].approve(event.peerId,true);
        else if(event.kind==NodeEvent::Kind::Connected)online[index]=true;
        else if(event.kind==NodeEvent::Kind::Disconnected)online[index]=false;
        else if(event.kind==NodeEvent::Kind::Message)textReceived+=static_cast<int>(event.messages.size());
        else if(event.kind==NodeEvent::Kind::VoiceCall){calls[index].state=event.text;calls[index].callId.assign(event.detail.begin(),event.detail.end());calls[index].peer=event.peerId;calls[index].lifetime=event.callLifetime;calls[index].mediaGeneration=event.mediaGeneration;calls[index].mediaState=event.mediaState;calls[index].mediaRoute=event.mediaRoute;calls[index].muted=event.callMuted;calls[index].deafened=event.callDeafened;calls[index].controlAvailable=event.callControlAvailable;std::wcerr<<L"Voice test peer "<<index<<L" state: "<<event.text<<L" media="<<event.mediaState<<L" generation="<<event.mediaGeneration<<L'\n';if(event.mediaState==static_cast<std::uint32_t>(calls::MediaState::Connected))calls[index].connectedCount++;if(event.text.rfind(L"Idle",0)==0)calls[index].idleCount++;}
        else if(event.kind==NodeEvent::Kind::Error)std::wcerr<<L"Voice test peer "<<index<<L": "<<event.text<<L'\n';
    }};
    auto wait=[&](auto predicate,const char* failure){auto deadline=Clock::now()+std::chrono::seconds(20);while(Clock::now()<deadline){collect();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(20));}throw std::runtime_error(failure);};
    auto state=[&](size_t index,const wchar_t* prefix){return calls[index].state.rfind(prefix,0)==0;};
    try{
        calls::CallManager ownership;
        require(ownership.beginOutgoing({"call-one"},calls::CallType::Voice,{"peer-one"},1000),"CallManager rejected a valid 1:1 call");
        const auto firstLifetime=ownership.lifetime();
        require(ownership.active()->participants.size()==1&&ownership.active()->type==calls::CallType::Voice,"Call ownership is not participant-oriented or typed");
        require(ownership.matches(firstLifetime,"call-one","peer-one"),"Current call lifetime was not accepted");
        ownership.finish(calls::State::Ended);
        require(ownership.beginOutgoing({"call-two"},calls::CallType::Voice,{"peer-one"},2000),"A new call could not start after teardown");
        require(ownership.lifetime()>firstLifetime&&!ownership.matches(firstLifetime,"call-one","peer-one"),"An older asynchronous call completion matched the replacement call");
        ownership.finish(calls::State::Ended);
        require(ownership.beginOutgoing({"media-call"},calls::CallType::Voice,{"peer-one"},5000),"Media recovery ownership call could not start");
        ownership.setMediaRecoveryCapable("peer-one",true);ownership.beginInitialMedia("media-call");auto mediaLifetime=ownership.lifetime();
        require(ownership.interruptMedia(100,1000),"Media interruption was not owned by CallManager");auto replacementGeneration=ownership.beginMediaRecovery(100,100);
        require(replacementGeneration==2&&ownership.matchesMedia(mediaLifetime,"media-call","peer-one",2),"Replacement media generation was not accepted");
        require(!ownership.matchesMedia(mediaLifetime,"media-call","peer-one",1)&&!ownership.transitionMedia(1,calls::MediaState::Connected),"Stale media completion was allowed to mutate the replacement generation");
        require(ownership.transitionMedia(2,calls::MediaState::Connected,calls::MediaRoute::NatTraversedIce),"Current replacement media completion was rejected");
        require(ownership.active()->mediaRoute==calls::MediaRoute::NatTraversedIce,"Authoritative media route was not retained by CallManager");ownership.observeControl(false,300);require(!ownership.controlAndMediaTimedOut(400)&&ownership.active()->mediaState==calls::MediaState::Connected,"Temporary control loss terminated healthy media");ownership.observeControl(true);require(ownership.active()->controlAvailable,"Recovered control was not reflected in the active call");require(ownership.interruptMedia(500,1000)&&ownership.beginMediaRecovery(500,100)==3,"A subsequent bounded recovery could not start");require(!ownership.mediaTimedOut(999)&&ownership.mediaTimedOut(1000),"Media recovery deadline was not bounded");ownership.finish(calls::State::Ended);

        for(size_t index=0;index<nodes.size();++index){auto path=(root/std::to_wstring(index)).wstring();{Storage store(path);auto profile=store.profile();profile.listenPort=static_cast<unsigned short>(base+index);profile.name=index?L"Beta":L"Alpha";store.saveProfile(profile);}nodes[index].start(path);}
        wait([&]{return ready[0]&&ready[1];},"Voice peers did not start");nodes[0].setNetworkContext(2,L"10.211.241.175/255.255.255.0");nodes[1].setNetworkContext(1,L"192.168.1.17/255.255.255.0");nodes[0].connect(L"127.0.0.1",static_cast<unsigned short>(base+1),identities[1]);wait([&]{return online[0]&&online[1];},"Voice peers did not establish verified mutual TLS");

        std::string first=nodes[0].startVoiceCall(identities[1]);require(!first.empty(),"Outgoing call command was rejected");wait([&]{return state(0,L"Outgoing")&&state(1,L"Incoming")&&calls[1].callId==first;},"Outgoing call did not ring the verified peer");require(nodes[1].answerVoiceCall(first),"Answer command was rejected");wait([&]{return state(0,L"Connected")&&state(1,L"Connected");},"Answered call did not reach Connected on both peers");require(calls[0].mediaRoute!=0&&calls[1].mediaRoute!=0,"Connected ICE media did not expose an authoritative route class");auto callerDiagnostics=nodes[0].voiceDiagnostics(),answerDiagnostics=nodes[1].voiceDiagnostics();require(callerDiagnostics.find(L"winning_peerchat_connection_id_at_call_start=0")==std::wstring::npos&&callerDiagnostics.find(L"sdp_offer_sent_connection_id=0")==std::wstring::npos&&callerDiagnostics.find(L"sdp_answer_received_connection_id=0")==std::wstring::npos,"Caller signaling was not correlated to the authenticated winner");require(answerDiagnostics.find(L"call_request_received_connection_id=0")==std::wstring::npos&&answerDiagnostics.find(L"sdp_answer_sent_connection_id=0")==std::wstring::npos,"Answer signaling was not correlated to the authenticated winner");require(callerDiagnostics.find(L"network_generation_at_media_creation=2")!=std::wstring::npos&&callerDiagnostics.find(L"fresh_peer_connection=true")!=std::wstring::npos,"Call did not create fresh media state for the current network generation");
        require(nodes[0].setVoiceMuted(true)&&nodes[1].setVoiceDeafened(true),"Voice controls could not be set before recovery");nodes[0].setNetworkContext(3,L"10.211.241.176/255.255.255.0");wait([&]{return calls[0].mediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&calls[1].mediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&calls[0].mediaGeneration>=2&&calls[1].mediaGeneration==calls[0].mediaGeneration;},"Media did not recover in-place after a network generation change");require(calls[0].callId==first&&calls[1].callId==first,"Media recovery replaced the logical CallId");require(calls[0].muted&&calls[1].deafened,"Mute or deafen state was lost across media replacement");require(nodes[0].setVoiceMuted(false)&&nodes[1].setVoiceDeafened(false),"Voice controls could not be restored after recovery");
        auto firstRecovery=calls[0].mediaGeneration;nodes[1].setNetworkContext(2,L"192.168.1.18/255.255.255.0");wait([&]{return calls[0].mediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&calls[1].mediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&calls[0].mediaGeneration>firstRecovery&&calls[1].mediaGeneration==calls[0].mediaGeneration;},"A repeated media interruption did not create and connect a fresh generation");require(calls[0].callId==first&&calls[1].callId==first,"Repeated media recovery replaced the logical CallId");
        require(!nodes[0].send(identities[1],L"Text during voice call").empty(),"Text was blocked during a call");wait([&]{return textReceived>0;},"Text did not continue during a call");require(nodes[1].endVoiceCall(first),"Remote hang-up was rejected");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Hang-up did not return both peers to Idle");

        std::string declined=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==declined;},"Second call did not ring");require(nodes[1].declineVoiceCall(declined),"Decline command was rejected");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Decline did not clear both call states");

        std::string cancelled=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==cancelled;},"Caller-cancel test did not ring");require(nodes[0].endVoiceCall(cancelled),"Caller cancel before answer was rejected");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Caller cancel before answer did not clear both call states");

        std::string timedOut=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==timedOut;},"Timeout test did not ring");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Unanswered call did not time out on both peers");

        std::string abandoned=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==abandoned;},"Disconnect-during-ringing test did not ring");nodes[1].stop();wait([&]{return state(0,L"Idle");},"Peer disappearance during ringing did not clean the caller");ready[1]=online[1]=false;calls[1]={};nodes[1].start((root/L"1").wstring());wait([&]{return ready[1]&&identities[1].size()==64&&online[0]&&online[1];},"Stopped profile did not restart and reconnect after ringing cleanup");

        std::string left=nodes[0].startVoiceCall(identities[1]);std::string right=nodes[1].startVoiceCall(identities[0]);require(left!=right,"Independent simultaneous calls reused an ID");wait([&]{return (state(0,L"Incoming")&&state(1,L"Outgoing"))||(state(1,L"Incoming")&&state(0,L"Outgoing"));},"Simultaneous calls did not converge to one ringing call");size_t receiver=state(0,L"Incoming")?0:1;std::string surviving=calls[receiver].callId;require(nodes[receiver].answerVoiceCall(surviving),"Simultaneous-call winner could not be answered");wait([&]{return state(0,L"Connected")&&state(1,L"Connected");},"Simultaneous call resolution did not connect");

        auto connectedGeneration=calls[0].mediaGeneration;nodes[0].setNetworkContext(4,L"10.211.241.177/255.255.255.0");wait([&]{return calls[0].mediaGeneration>connectedGeneration&&calls[0].mediaState!=static_cast<std::uint32_t>(calls::MediaState::Connected);},"Remote-hangup test did not enter a replacement media generation");require(nodes[1].endVoiceCall(surviving),"Remote hang-up was rejected while the peer recovered media");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Remote hang-up during recovery did not stop both call lifetimes");

        std::string localRecovery=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==localRecovery;},"Local-recovery-hangup test did not ring");require(nodes[1].answerVoiceCall(localRecovery),"Local-recovery-hangup call could not be answered");wait([&]{return calls[0].mediaState==static_cast<std::uint32_t>(calls::MediaState::Connected)&&calls[1].mediaState==static_cast<std::uint32_t>(calls::MediaState::Connected);},"Local-recovery-hangup call did not connect");auto localRecoveryGeneration=calls[0].mediaGeneration;nodes[0].setNetworkContext(5,L"10.211.241.178/255.255.255.0");wait([&]{return calls[0].mediaGeneration>localRecoveryGeneration&&calls[0].mediaState!=static_cast<std::uint32_t>(calls::MediaState::Connected);},"Local-hangup test did not enter recovery");require(nodes[0].endVoiceCall(localRecovery),"Local hang-up was rejected during media recovery");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Local hang-up during recovery did not stop both call lifetimes");nodes[1].stop();nodes[0].stop();
        require(calls[0].connectedCount>=2&&calls[0].idleCount>=6,"Call lifecycle events were incomplete");
        std::cout<<"PASS: verified-peer WebRTC voice, stable CallId media recovery, repeated replacement, stale-generation rejection, control grace, bounded recovery timeout, hang-up during recovery, call controls, simultaneous-call convergence, text coexistence and shutdown cleanup.\n";return 0;
    }catch(const std::exception& error){for(auto& node:nodes)node.stop();std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
