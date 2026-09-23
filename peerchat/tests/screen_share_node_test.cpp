#include "node.hpp"
#include "screen_share_manager.hpp"
#include "windows_media/peerchat_screen_media.h"
#include <windows.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
std::uint64_t counter(const std::wstring& text,const std::wstring& name){
    const auto start=text.find(name+L"=");
    if(start==std::wstring::npos)return 0;
    try{return std::stoull(text.substr(start+name.size()+1));}catch(...){return 0;}
}
}

int main(){
    using namespace peerchat;
    using Clock=std::chrono::steady_clock;
    std::array<Node,2> nodes;
    std::array<std::string,2> peers;
    std::array<bool,2> ready{},connected{};
    std::array<std::uint32_t,2> shareState{};
    std::array<std::uint64_t,2> shareGeneration{};
    std::array<std::uint32_t,2> shareRoute{},shareWidth{},shareHeight{};
    std::array<std::string,2> shareId;
    std::array<std::wstring,2> shareSummary;
    std::array<std::uint32_t,2> printedState{},printedRoute{},printedWidth{},printedHeight{};
    std::array<std::uint64_t,2> printedGeneration{};
    int deliveredText=0;
    HWND source=nullptr,viewer=nullptr,content=nullptr;
    auto seed=Storage::newMessageId();
    auto root=std::filesystem::current_path()/L"build"/L"tests"/L"runtime"/
        std::wstring(seed.begin(),seed.end());
    auto base=static_cast<unsigned short>(27000+std::stoul(seed.substr(0,4),nullptr,16)%12000);
    auto collect=[&]{
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        for(size_t i=0;i<2;++i)for(auto& event:nodes[i].poll()){
            if(event.kind==NodeEvent::Kind::Ready){peers[i]=event.peerId;ready[i]=true;}
            else if(event.kind==NodeEvent::Kind::TrustRequired)nodes[i].approve(event.peerId,true);
            else if(event.kind==NodeEvent::Kind::Connected)connected[i]=true;
            else if(event.kind==NodeEvent::Kind::ScreenShare){shareState[i]=event.shareState;shareGeneration[i]=event.shareGeneration;shareRoute[i]=event.screenRoute;shareWidth[i]=event.screenWidth;shareHeight[i]=event.screenHeight;shareId[i].assign(event.detail.begin(),event.detail.end());shareSummary[i]=event.screenSummary;if(printedState[i]!=event.shareState||printedGeneration[i]!=event.shareGeneration||printedRoute[i]!=event.screenRoute||printedWidth[i]!=event.screenWidth||printedHeight[i]!=event.screenHeight){std::wcerr<<L"screen peer "<<i<<L" state="<<event.shareState<<L" generation="<<event.shareGeneration<<L" route="<<event.screenRoute<<L" surface="<<event.screenWidth<<L'x'<<event.screenHeight<<L" "<<event.text<<L'\n';printedState[i]=event.shareState;printedGeneration[i]=event.shareGeneration;printedRoute[i]=event.screenRoute;printedWidth[i]=event.screenWidth;printedHeight[i]=event.screenHeight;}}
            else if(event.kind==NodeEvent::Kind::Message)deliveredText+=static_cast<int>(event.messages.size());
            else if(event.kind==NodeEvent::Kind::Error)std::wcerr<<L"screen peer "<<i<<L" error="<<event.text<<L'\n';
        }
    };
    auto wait=[&](auto predicate,const char* failure,int seconds=30){
        auto deadline=Clock::now()+std::chrono::seconds(seconds);
        unsigned frame=0;
        while(Clock::now()<deadline){
            if(content){SetWindowTextW(content,(L"Live screen frame "+std::to_wstring(++frame)).c_str());RedrawWindow(source,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);}
            collect();if(predicate())return;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        throw std::runtime_error(failure);
    };
    try{
        const bool heavy=GetEnvironmentVariableW(L"PEERCHAT_SCREEN_TEST_HEAVY",nullptr,0)>0;
        const int sourceWidth=heavy?1920:640,sourceHeight=heavy?1080:480;
        const std::uint32_t fps=heavy?60:30,bitrate=heavy?25000000:8000000;
        source=CreateWindowExW(0,L"STATIC",L"PeerChat authenticated screen sender",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,sourceWidth,sourceHeight,
            nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        viewer=CreateWindowExW(0,L"STATIC",L"PeerChat authenticated screen receiver",
            WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,800,600,
            nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(source&&viewer,"Could not create screen integration test windows");
        content=CreateWindowExW(0,L"STATIC",L"Frame 0",WS_CHILD|WS_VISIBLE|SS_CENTER,
            30,50,500,120,source,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(content!=nullptr,"Could not create animated screen content");
        SetWindowPos(source,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        for(size_t i=0;i<2;++i){auto path=(root/std::to_wstring(i)).wstring();
            {Storage storage(path);auto profile=storage.profile();profile.listenPort=static_cast<unsigned short>(base+i);storage.saveProfile(profile);}
            nodes[i].start(path);
        }
        wait([&]{return ready[0]&&ready[1];},"Screen peers did not start");
        nodes[0].setNetworkContext(1,L"lan-a");nodes[1].setNetworkContext(1,L"lan-b");
        nodes[0].connect(L"127.0.0.1",static_cast<unsigned short>(base+1),peers[1]);
        wait([&]{return connected[0]&&connected[1];},"Screen peers did not establish verified peer TLS");
        auto id=nodes[0].startScreenShare(peers[1],PC_SCREEN_WINDOW,
            reinterpret_cast<std::uint64_t>(source),fps,bitrate,true);
        require(!id.empty(),"Screen share start command was rejected");
        wait([&]{return shareState[1]==static_cast<unsigned>(screen::State::Incoming)&&shareId[1]==id;},
            "Authenticated screen offer did not reach the verified peer");
        require(nodes[1].answerScreenShare(id,reinterpret_cast<std::uint64_t>(viewer)),
            "Receiver rejected screen share answer command");
        wait([&]{
            if(shareState[0]!=static_cast<unsigned>(screen::State::Connected)||
               shareState[1]!=static_cast<unsigned>(screen::State::Connected))return false;
            return counter(nodes[0].screenShareDiagnostics(),L"encoded_submit_api_accepted")>=8&&
                   counter(nodes[1].screenShareDiagnostics(),L"presented_frames")>=1;
        },"Authenticated screen share did not encode, deliver and present",35);
        wait([&]{return shareRoute[0]!=0&&shareRoute[1]!=0&&shareWidth[0]>0&&shareHeight[0]>0&&shareWidth[1]>0&&shareHeight[1]>0;},
            "Authoritative ICE route or effective video dimensions were not surfaced");
        wait([&]{return shareSummary[0].find(L"Mbps submitted")!=std::wstring::npos&&
            shareSummary[1].find(L"Mbps received")!=std::wstring::npos;},
            "Rolling sender/receiver video telemetry was not surfaced");
        auto senderTelemetry=nodes[0].screenShareDiagnostics();
        auto receiverTelemetry=nodes[1].screenShareDiagnostics();
        require(counter(senderTelemetry,L"captured_frames")>0&&
            counter(senderTelemetry,L"encoded_bytes_submit_api_accepted")>0&&
            counter(receiverTelemetry,L"media_rtp_bytes_received")>0&&
            receiverTelemetry.find(L"media_rtt_ms=Unavailable")!=std::wstring::npos,
            "Screen telemetry did not distinguish measured counters from unavailable ICE RTT");
        nodes[1].setNetworkContext(2,L"new-network-b");
        wait([&]{return shareState[0]==static_cast<unsigned>(screen::State::Connected)&&
            shareState[1]==static_cast<unsigned>(screen::State::Connected)&&
            shareGeneration[0]>=2&&shareGeneration[1]==shareGeneration[0]&&
            counter(nodes[1].screenShareDiagnostics(),L"presented_frames")>=1;},
            "Receiver network-change recovery did not preserve the logical share",40);
        const auto recoveredGeneration=shareGeneration[1];
        nodes[1].setNetworkContext(3,L"new-network-b");
        auto sameFingerprintUntil=Clock::now()+std::chrono::milliseconds(700);
        while(Clock::now()<sameFingerprintUntil){collect();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        require(shareState[0]==static_cast<unsigned>(screen::State::Connected)&&
            shareState[1]==static_cast<unsigned>(screen::State::Connected)&&
            shareGeneration[1]==recoveredGeneration,
            "An unchanged network fingerprint unnecessarily tore down the screen media");
        require(!nodes[0].send(peers[1],L"Text during screen sharing").empty(),
            "Text send failed during screen sharing");
        wait([&]{return deliveredText>0;},"Text delivery stopped during screen sharing");
        require(nodes[0].endScreenShare(id),"Sender could not stop the screen share");
        wait([&]{return shareState[0]==0&&shareState[1]==0;},
            "Both peers did not return to idle after screen share");
        auto repeated=nodes[0].startScreenShare(peers[1],PC_SCREEN_WINDOW,
            reinterpret_cast<std::uint64_t>(source),fps,bitrate,true);
        require(!repeated.empty(),"A second active screen share could not start after teardown");
        wait([&]{return shareState[1]==static_cast<unsigned>(screen::State::Incoming)&&shareId[1]==repeated;},
            "Second active share did not reach the verified peer");
        require(nodes[1].answerScreenShare(repeated,reinterpret_cast<std::uint64_t>(viewer)),
            "Second active share answer was rejected");
        wait([&]{return shareState[0]==static_cast<unsigned>(screen::State::Connected)&&
            shareState[1]==static_cast<unsigned>(screen::State::Connected)&&
            counter(nodes[1].screenShareDiagnostics(),L"presented_frames")>=1;},
            "Second active share did not render after repeated start",35);
        ShowWindow(source,SW_HIDE);
        const auto stalledUntil=Clock::now()+std::chrono::milliseconds(350);
        while(Clock::now()<stalledUntil){collect();std::this_thread::sleep_for(std::chrono::milliseconds(20));}
        const auto stopStarted=Clock::now();
        require(nodes[0].endScreenShare(repeated),"Stop was rejected after capture source was hidden");
        wait([&]{return shareState[0]==0&&shareState[1]==0;},
            "Stalled-capture stop did not return both peers to idle",5);
        require(Clock::now()-stopStarted<std::chrono::seconds(5),
            "Stalled-capture teardown exceeded the bounded stop window");
        require(!nodes[0].send(peers[1],L"Text after screen-share teardown").empty(),
            "Messaging was unavailable after screen-share teardown");
        wait([&]{return deliveredText>1;},"Messaging did not recover after screen-share teardown",5);
        ShowWindow(source,SW_SHOW);
        auto declined=nodes[0].startScreenShare(peers[1],PC_SCREEN_WINDOW,
            reinterpret_cast<std::uint64_t>(source),30,8000000,true);
        wait([&]{return shareState[1]==static_cast<unsigned>(screen::State::Incoming)&&shareId[1]==declined;},
            "Second share did not reach the peer after teardown");
        require(nodes[1].declineScreenShare(declined),"Receiver could not decline share");
        wait([&]{return shareState[0]==0&&shareState[1]==0;},
            "Decline did not clean up both share lifetimes");
        for(auto& node:nodes)node.stop();
        DestroyWindow(viewer);DestroyWindow(source);
        std::cout<<"PASS: verified peer TLS screen offer/answer, H264 ICE media, GPU presentation, text coexistence, teardown and repeated decline.\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL before cleanup: "<<error.what()<<'\n';for(size_t i=0;i<2;++i)std::wcerr<<L"screen peer "<<i<<L" diagnostics:\n"<<nodes[i].screenShareDiagnostics()<<L'\n';for(auto& node:nodes)node.stop();if(viewer)DestroyWindow(viewer);if(source)DestroyWindow(source);return 1;}
}
