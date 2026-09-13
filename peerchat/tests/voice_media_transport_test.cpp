#include "voice_media.hpp"
#include <chrono>
#include <cwchar>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool countAtLeast(const std::wstring& diagnostics,const wchar_t* key,std::uint64_t minimum){auto start=diagnostics.find(key);if(start==std::wstring::npos)return false;start+=std::wcslen(key);auto end=diagnostics.find(L'\n',start);return std::stoull(diagnostics.substr(start,end-start))>=minimum;}
}

int main(){
    try{
        for(int cycle=0;cycle<3;++cycle){
        peerchat::VoiceMedia caller(-1,-1,static_cast<std::uint64_t>(cycle+1),cycle==0?"192.168.1.14/24":"10.211.241.175/24");
        peerchat::VoiceMedia answerer(-1,-1,static_cast<std::uint64_t>(cycle+1),"192.168.1.17/24");
        std::wstring error;
        require(caller.startOffer(error),"Could not create caller offer");
        bool answerStarted=false,callerConnected=false,answererConnected=false;
        auto deadline=std::chrono::steady_clock::now()+10s;
        while(std::chrono::steady_clock::now()<deadline&&(!callerConnected||!answererConnected)){
            for(auto& event:caller.poll()){
                if(event.kind==peerchat::VoiceMedia::EventKind::Description&&event.detail=="offer"){
                    require(answerer.startAnswer(event.value,error),"Could not apply offer/create answer");answerStarted=true;
                }else if(event.kind==peerchat::VoiceMedia::EventKind::Candidate&&answerStarted)require(answerer.addCandidate(event.value,event.detail,error),"Answerer rejected caller candidate");
                else if(event.kind==peerchat::VoiceMedia::EventKind::Connected)callerConnected=true;
            }
            for(auto& event:answerer.poll()){
                if(event.kind==peerchat::VoiceMedia::EventKind::Description&&event.detail=="answer")require(caller.setAnswer(event.value,error),"Caller rejected answer");
                else if(event.kind==peerchat::VoiceMedia::EventKind::Candidate)require(caller.addCandidate(event.value,event.detail,error),"Caller rejected answerer candidate");
                else if(event.kind==peerchat::VoiceMedia::EventKind::Connected)answererConnected=true;
            }
            std::this_thread::sleep_for(10ms);
        }
        require(callerConnected&&answererConnected,"Direct ICE/DTLS-SRTP media did not connect");
        std::vector<unsigned char> pcm(640);
        for(std::size_t i=0;i<pcm.size();i+=2){pcm[i]=0x34;pcm[i+1]=0x12;}
        for(int frame=0;frame<12;++frame){caller.testInjectPcm(pcm);answerer.testInjectPcm(pcm);std::this_thread::sleep_for(20ms);}
        deadline=std::chrono::steady_clock::now()+5s;
        while(std::chrono::steady_clock::now()<deadline){caller.poll();answerer.poll();auto a=caller.diagnostics(),b=answerer.diagnostics();if(countAtLeast(a,L"opus_submitted=",12)&&countAtLeast(a,L"received_packets=",12)&&countAtLeast(a,L"decoded_frames=",12)&&countAtLeast(b,L"opus_submitted=",12)&&countAtLeast(b,L"received_packets=",12)&&countAtLeast(b,L"decoded_frames=",12))break;std::this_thread::sleep_for(20ms);}
        auto a=caller.diagnostics(),b=answerer.diagnostics();
        std::wcerr<<L"Caller diagnostics:\n"<<a<<L"\nAnswerer diagnostics:\n"<<b<<L'\n';
        require(countAtLeast(a,L"capture_frames=",12)&&countAtLeast(a,L"encoded_frames=",12)&&countAtLeast(a,L"opus_submitted=",12),"Caller did not capture/encode/submit injected PCM");
        require(countAtLeast(b,L"capture_frames=",12)&&countAtLeast(b,L"encoded_frames=",12)&&countAtLeast(b,L"opus_submitted=",12),"Answerer did not capture/encode/submit injected PCM");
        require(countAtLeast(a,L"received_packets=",12)&&countAtLeast(a,L"decoded_frames=",12)&&countAtLeast(a,L"playback_frames=",12),"Caller did not receive/decode/queue answerer Opus");
        require(countAtLeast(b,L"received_packets=",12)&&countAtLeast(b,L"decoded_frames=",12)&&countAtLeast(b,L"playback_frames=",12),"Answerer did not receive/decode/queue caller Opus");
        require(a.find(L"fresh_peer_connection=true")!=std::wstring::npos&&a.find(L"ice_gathering_state=new")==std::wstring::npos&&a.find(L"local_ice_candidates_generated=0")==std::wstring::npos&&a.find(L"ice_servers=STUN configured; TURN not configured")!=std::wstring::npos,"Fresh media session omitted ICE gathering evidence");
        require(a.find(L"network_generation_at_media_creation="+std::to_wstring(cycle+1))!=std::wstring::npos,"Media session retained an earlier network generation");
        const auto callerLevel=caller.telemetry(),answererLevel=answerer.telemetry();
        require(!callerLevel.outputAvailable||callerLevel.outputLevel>0,"Caller decoded non-silent PCM but output telemetry stayed at zero");
        require(!answererLevel.outputAvailable||answererLevel.outputLevel>0,"Answerer decoded non-silent PCM but output telemetry stayed at zero");
        caller.stop();answerer.stop();
        }
        std::cout<<"PASS: injected PCM crossed direct ICE/DTLS-SRTP as timestamped Opus and reached both playback queues.\n";
        return 0;
    }catch(const std::exception& failure){std::cerr<<"FAIL: "<<failure.what()<<'\n';return 1;}
}
