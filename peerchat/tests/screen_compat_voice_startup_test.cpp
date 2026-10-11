// Reuse the existing real WGC/H.264/WebRTC startup harness, Compatibility only.
// An actual host Opus/DTLS-SRTP voice session is connected before screen startup.
// Injected PCM is not physical microphone or TURN quality acceptance.
#include "voice_media.hpp"
#include <atomic>
#include <cwchar>
#define main screenStartup
#include "screen_media_transport_test.cpp"
#undef main

int main(){
    try{
        using V=peerchat::VoiceMedia;
        V a(peerchat::MediaIceConfiguration::directForTests(),-1,-1,1,"host-a",{},V::ReceiveBinding{std::string(64,'b'),std::string(32,'c'),1,1});
        V b(peerchat::MediaIceConfiguration::directForTests(),-1,-1,1,"host-b",{},V::ReceiveBinding{std::string(64,'a'),std::string(32,'c'),1,1});
        std::wstring error;require(a.startOffer(error),"Voice offer failed");
        bool answered=false,connectedA=false,connectedB=false;
        auto poll=[&]{
            for(auto& e:a.poll()){
                if(e.kind==V::EventKind::Description&&e.detail=="offer"){require(b.startAnswer(e.value,error),"Voice answer failed");answered=true;}
                else if(e.kind==V::EventKind::Candidate&&answered)require(b.addCandidate(e.value,e.detail,error),"Voice candidate failed");
                else if(e.kind==V::EventKind::Connected)connectedA=true;
            }
            for(auto& e:b.poll()){
                if(e.kind==V::EventKind::Description&&e.detail=="answer")require(a.setAnswer(e.value,error),"Voice answer apply failed");
                else if(e.kind==V::EventKind::Candidate)require(a.addCandidate(e.value,e.detail,error),"Voice candidate apply failed");
                else if(e.kind==V::EventKind::Connected)connectedB=true;
            }
        };
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(!(connectedA&&connectedB)&&std::chrono::steady_clock::now()<deadline){poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));}
        require(connectedA&&connectedB,"Voice not connected before screen startup");
        std::atomic<bool> stop{false},failed{false};
        std::thread voice([&]{try{std::vector<unsigned char> pcm(640,0x12);while(!stop){poll();a.testInjectPcm(pcm);b.testInjectPcm(pcm);std::this_thread::sleep_for(std::chrono::milliseconds(20));}}catch(...){failed=true;}});
        char name[]="screen",mode[]="--compat";char* argv[]={name,mode};int result=screenStartup(2,argv);
        stop=true;voice.join();require(!failed&&result==0,"Compatibility startup failed alongside voice");
        auto received=[](const std::wstring& raw){auto pos=raw.find(L"received_packets=");return pos!=std::wstring::npos&&std::stoull(raw.substr(pos+17))>0;};
        const auto da=a.diagnostics(),db=b.diagnostics();
        require(received(da)&&received(db)&&da.find(L"media_state=connected")!=std::wstring::npos&&db.find(L"media_state=connected")!=std::wstring::npos,"Voice did not remain connected/receive during Compatibility screen startup");
        a.stop();b.stop();std::cout<<"PASS: active bidirectional host Opus voice continued through Compatibility capture/RTP/decode/presentation startup.\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
