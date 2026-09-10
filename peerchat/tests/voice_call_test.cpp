#include "node.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct CallView {std::wstring state;std::string callId,peer;int connectedCount=0,idleCount=0;};
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
        else if(event.kind==NodeEvent::Kind::VoiceCall){calls[index].state=event.text;calls[index].callId.assign(event.detail.begin(),event.detail.end());calls[index].peer=event.peerId;std::wcerr<<L"Voice test peer "<<index<<L" state: "<<event.text<<L'\n';if(event.text.rfind(L"Connected",0)==0)calls[index].connectedCount++;if(event.text.rfind(L"Idle",0)==0)calls[index].idleCount++;}
        else if(event.kind==NodeEvent::Kind::Error)std::wcerr<<L"Voice test peer "<<index<<L": "<<event.text<<L'\n';
    }};
    auto wait=[&](auto predicate,const char* failure){auto deadline=Clock::now()+std::chrono::seconds(20);while(Clock::now()<deadline){collect();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(20));}throw std::runtime_error(failure);};
    auto state=[&](size_t index,const wchar_t* prefix){return calls[index].state.rfind(prefix,0)==0;};
    try{
        for(size_t index=0;index<nodes.size();++index){auto path=(root/std::to_wstring(index)).wstring();{Storage store(path);auto profile=store.profile();profile.listenPort=static_cast<unsigned short>(base+index);profile.name=index?L"Beta":L"Alpha";store.saveProfile(profile);}nodes[index].start(path);}
        wait([&]{return ready[0]&&ready[1];},"Voice peers did not start");nodes[0].connect(L"127.0.0.1",static_cast<unsigned short>(base+1),identities[1]);wait([&]{return online[0]&&online[1];},"Voice peers did not establish verified mutual TLS");

        std::string first=nodes[0].startVoiceCall(identities[1]);require(!first.empty(),"Outgoing call command was rejected");wait([&]{return state(0,L"Outgoing")&&state(1,L"Incoming")&&calls[1].callId==first;},"Outgoing call did not ring the verified peer");require(nodes[1].answerVoiceCall(first),"Answer command was rejected");wait([&]{return state(0,L"Connected")&&state(1,L"Connected");},"Answered call did not reach Connected on both peers");
        require(!nodes[0].send(identities[1],L"Text during voice call").empty(),"Text was blocked during a call");wait([&]{return textReceived>0;},"Text did not continue during a call");require(nodes[1].endVoiceCall(first),"Remote hang-up was rejected");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Hang-up did not return both peers to Idle");

        std::string declined=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==declined;},"Second call did not ring");require(nodes[1].declineVoiceCall(declined),"Decline command was rejected");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Decline did not clear both call states");

        std::string timedOut=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==timedOut;},"Timeout test did not ring");wait([&]{return state(0,L"Idle")&&state(1,L"Idle");},"Unanswered call did not time out on both peers");

        std::string abandoned=nodes[0].startVoiceCall(identities[1]);wait([&]{return state(1,L"Incoming")&&calls[1].callId==abandoned;},"Disconnect-during-ringing test did not ring");nodes[1].stop();wait([&]{return state(0,L"Idle");},"Peer disappearance during ringing did not clean the caller");ready[1]=online[1]=false;calls[1]={};nodes[1].start((root/L"1").wstring());wait([&]{return ready[1]&&identities[1].size()==64&&online[0]&&online[1];},"Stopped profile did not restart and reconnect after ringing cleanup");

        std::string left=nodes[0].startVoiceCall(identities[1]);std::string right=nodes[1].startVoiceCall(identities[0]);require(left!=right,"Independent simultaneous calls reused an ID");wait([&]{return (state(0,L"Incoming")&&state(1,L"Outgoing"))||(state(1,L"Incoming")&&state(0,L"Outgoing"));},"Simultaneous calls did not converge to one ringing call");size_t receiver=state(0,L"Incoming")?0:1;std::string surviving=calls[receiver].callId;require(nodes[receiver].answerVoiceCall(surviving),"Simultaneous-call winner could not be answered");wait([&]{return state(0,L"Connected")&&state(1,L"Connected");},"Simultaneous call resolution did not connect");

        nodes[1].stop();wait([&]{return state(0,L"Idle");},"Peer disappearance did not clean the active call");nodes[0].stop();
        require(calls[0].connectedCount>=2&&calls[0].idleCount>=5,"Call lifecycle events were incomplete");
        std::cout<<"PASS: verified-peer WebRTC voice, ring/answer/decline/timeout/hang-up, ringing disconnect/restart, simultaneous-call convergence, text coexistence, active disconnect and shutdown cleanup.\n";return 0;
    }catch(const std::exception& error){for(auto& node:nodes)node.stop();std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
