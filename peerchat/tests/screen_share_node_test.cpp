#include "node.hpp"
#include "screen_share_manager.hpp"
#include "windows_media/peerchat_screen_media.h"
#include <windows.h>
#include <array>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <fstream>
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
    const bool relayFixture=GetEnvironmentVariableW(L"PEERCHAT_SCREEN_TEST_RELAY",nullptr,0)>0;
    const auto backend=GetEnvironmentVariableW(L"PEERCHAT_SCREEN_TEST_BACKEND",nullptr,0)>0?
        screen::Backend::WebRtcCompatibility:screen::Backend::NativeWindows;
    const auto nextBackend=backend==screen::Backend::NativeWindows?
        screen::Backend::WebRtcCompatibility:screen::Backend::NativeWindows;
    const auto receiverRequirement=GetEnvironmentVariableW(L"PEERCHAT_SCREEN_TEST_RECEIVER_REQUIREMENT",nullptr,0)>0?
        screen::Backend::WebRtcCompatibility:screen::Backend::NativeWindows;
    std::array<Node,2> nodes;
    std::array<std::string,2> peers;
    std::array<bool,2> ready{},connected{};
    std::array<std::uint64_t,2> relayAttempts{};bool relayPaired=false;
    std::array<std::uint32_t,2> shareState{};
    std::array<std::uint64_t,2> shareGeneration{};
    std::array<std::uint32_t,2> shareRoute{},shareWidth{},shareHeight{};
    std::array<std::string,2> shareId;
    std::array<std::wstring,2> shareSummary;
    std::array<std::uint32_t,2> printedState{},printedRoute{},printedWidth{},printedHeight{};
    std::array<std::uint64_t,2> printedGeneration{};
    int deliveredText=0;
    unsigned reliableDisconnects=0;
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
            else if(event.kind==NodeEvent::Kind::TrustRequired){if(relayFixture)require(nodes[i].pinInitialAuthenticatedFriendIdentity(event.peerId,event.attemptId,1),"constrained relay identity pin rejected");else nodes[i].approve(event.peerId,true);}
            else if(event.kind==NodeEvent::Kind::RelayRequested)relayAttempts[i]=event.attemptId;
            else if(event.kind==NodeEvent::Kind::Connected)connected[i]=true;
            else if(event.kind==NodeEvent::Kind::Disconnected){connected[i]=false;++reliableDisconnects;}
            else if(event.kind==NodeEvent::Kind::ScreenShare){shareState[i]=event.shareState;shareGeneration[i]=event.shareGeneration;shareRoute[i]=event.screenRoute;shareWidth[i]=event.screenWidth;shareHeight[i]=event.screenHeight;shareId[i].assign(event.detail.begin(),event.detail.end());shareSummary[i]=event.screenSummary;if(printedState[i]!=event.shareState||printedGeneration[i]!=event.shareGeneration||printedRoute[i]!=event.screenRoute||printedWidth[i]!=event.screenWidth||printedHeight[i]!=event.screenHeight){std::wcerr<<L"screen peer "<<i<<L" state="<<event.shareState<<L" generation="<<event.shareGeneration<<L" route="<<event.screenRoute<<L" surface="<<event.screenWidth<<L'x'<<event.screenHeight<<L" "<<event.text<<L'\n';printedState[i]=event.shareState;printedGeneration[i]=event.shareGeneration;printedRoute[i]=event.screenRoute;printedWidth[i]=event.screenWidth;printedHeight[i]=event.screenHeight;}}
            else if(event.kind==NodeEvent::Kind::Message)deliveredText+=static_cast<int>(event.messages.size());
            else if(event.kind==NodeEvent::Kind::Error)std::wcerr<<L"screen peer "<<i<<L" error="<<event.text<<L'\n';
        }
        if(relayFixture&&relayAttempts[0]&&relayAttempts[1]){
            if(!relayPaired){require(nodes[0].relayReady(relayAttempts[0],peers[0]>peers[1])&&nodes[1].relayReady(relayAttempts[1],peers[1]>peers[0]),"opaque relay roles rejected");relayPaired=true;}
            unsigned char bytes[65536];for(unsigned side=0;side<2;++side)for(unsigned n=0;n<8;++n){int count=nodes[side].takeRelayOutput(relayAttempts[side],bytes,sizeof(bytes),0);if(count<=0)break;require(nodes[1-side].relayReceive(relayAttempts[1-side],bytes,count),"opaque relay bytes rejected");}
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
        const bool realMonitor=GetEnvironmentVariableW(L"PEERCHAT_SCREEN_TEST_REAL_MONITOR",nullptr,0)>0;
        const HMONITOR monitor=realMonitor?MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY):nullptr;
        MONITORINFO monitorInfo{};monitorInfo.cbSize=sizeof(monitorInfo);
        require(!realMonitor||(monitor&&GetMonitorInfoW(monitor,&monitorInfo)),
            "Primary desktop monitor was unavailable for real-screen capture");
        wchar_t resolution[16]{};
        GetEnvironmentVariableW(L"PEERCHAT_SCREEN_TEST_RESOLUTION",resolution,16);
        wchar_t holdText[16]{};
        GetEnvironmentVariableW(L"PEERCHAT_SCREEN_TEST_HOLD_SECONDS",holdText,16);
        const auto requestedHold=std::wcstol(holdText,nullptr,10);
        const int holdSeconds=requestedHold>=1&&requestedHold<=120?
            static_cast<int>(requestedHold):12;
        const int sourceWidth=realMonitor?
            monitorInfo.rcMonitor.right-monitorInfo.rcMonitor.left:
            std::wcscmp(resolution,L"720")==0?1280:
            std::wcscmp(resolution,L"1080")==0?1920:
            std::wcscmp(resolution,L"1440")==0?2560:heavy?1920:640;
        const int sourceHeight=realMonitor?
            monitorInfo.rcMonitor.bottom-monitorInfo.rcMonitor.top:
            std::wcscmp(resolution,L"720")==0?720:
            std::wcscmp(resolution,L"1080")==0?1080:
            std::wcscmp(resolution,L"1440")==0?1440:heavy?1080:480;
        const std::uint32_t fps=heavy||realMonitor?60:30,
            bitrate=heavy||realMonitor?25000000:8000000;
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
            // This direct-only fixture uses explicit loopback ports. Production
            // relay-only startup still has no reliable direct listener.
            if(relayFixture)nodes[i].start(path,0,L"Relay media fixture",false);else nodes[i].start(path,static_cast<unsigned short>(base+i));
        }
        wait([&]{return ready[0]&&ready[1];},"Screen peers did not start");
        nodes[0].replaceAuthenticatedFriends(71,1,{{72,peers[1],{peers[1]}}});
        nodes[1].replaceAuthenticatedFriends(72,1,{{71,peers[0],{peers[0]}}});
        nodes[0].setNetworkContext(1,L"lan-a");nodes[1].setNetworkContext(1,L"lan-b");
        if(relayFixture){nodes[0].updateCandidates(peers[1],L"",0,L"",0,true,1,true);nodes[1].updateCandidates(peers[0],L"",0,L"",0,true,1,true);}else nodes[0].connect(L"127.0.0.1",static_cast<unsigned short>(base+1),peers[1]);
        wait([&]{return connected[0]&&connected[1];},"Screen peers did not establish verified peer TLS");
        require(nodes[1].setScreenShareBackend(receiverRequirement),
            "Receiver backend requirement could not be configured");
        const auto captureKind=realMonitor?PC_SCREEN_MONITOR:PC_SCREEN_WINDOW;
        const auto captureHandle=reinterpret_cast<std::uint64_t>(realMonitor?
            static_cast<HANDLE>(monitor):static_cast<HANDLE>(source));
        auto id=nodes[0].startScreenShare(peers[1],captureKind,
            captureHandle,fps,bitrate,true,backend);
        require(!id.empty(),"Screen share start command was rejected");
        wait([&]{return shareState[1]==static_cast<unsigned>(screen::State::Incoming)&&shareId[1]==id;},
            "Authenticated screen offer did not reach the verified peer");
        require(nodes[1].answerScreenShare(id,reinterpret_cast<std::uint64_t>(viewer),backend),
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
        const auto expectedBackend=screen::negotiateBackend(backend,receiverRequirement);
        const auto expectedLabel=expectedBackend==screen::Backend::WebRtcCompatibility?
            L"negotiated_backend=WebRTC Compatibility":L"negotiated_backend=Native Windows";
        require(senderTelemetry.find(expectedLabel)!=std::wstring::npos&&
            receiverTelemetry.find(expectedLabel)!=std::wstring::npos,
            "Peers did not retain the same negotiated backend");
        require(counter(receiverTelemetry,L"remote_rtp_callback_packets")>0&&
            counter(receiverTelemetry,L"valid_rtp_packets_received")>0,
            "Receiver did not observe raw and valid RTP callbacks");
        require(nodes[0].setScreenShareBackend(nextBackend)&&nodes[1].setScreenShareBackend(nextBackend),
            "Changing the next-share backend was rejected");
        wait([&]{return nodes[0].screenShareDiagnostics().find(
            nextBackend==screen::Backend::NativeWindows?L"configured_backend=Native Windows":L"configured_backend=WebRTC Compatibility")!=std::wstring::npos;},
            "Configured backend did not update");
        require(nodes[0].screenShareDiagnostics().find(
            expectedBackend==screen::Backend::NativeWindows?L"active_backend=Native Windows":L"active_backend=WebRTC Compatibility")!=std::wstring::npos,
            "Changing the preference switched an active share");
        require(receiverTelemetry.find(L"decoder_worker_stage=")!=std::wstring::npos&&
            receiverTelemetry.find(L"decoder_queue_depth=")!=std::wstring::npos,
            "Decoder worker and queue diagnostics were not surfaced");
        if(resolution[0]||realMonitor){
            auto until=Clock::now()+std::chrono::seconds(holdSeconds);
            auto nextReport=Clock::now()+std::chrono::seconds(10);
            auto lastPresent=counter(receiverTelemetry,L"presented_frames");
            auto lastProgress=Clock::now();
            while(Clock::now()<until){
                collect();
                receiverTelemetry=nodes[1].screenShareDiagnostics();
                const auto current=counter(receiverTelemetry,L"presented_frames");
                if(current>lastPresent){lastPresent=current;lastProgress=Clock::now();}
                if(Clock::now()-lastProgress>std::chrono::seconds(realMonitor?8:4)){
                    std::wcerr<<L"receiver stalled at "<<sourceWidth<<L'x'<<sourceHeight
                        <<L":\n"<<receiverTelemetry<<L'\n';
                    throw std::runtime_error("Receiver presentation stopped progressing");
                }
                if(Clock::now()>=nextReport){
                    const auto senderNow=nodes[0].screenShareDiagnostics();
                    std::wcout<<L"active_share="<<sourceWidth<<L'x'<<sourceHeight
                        <<L" captured="<<counter(senderNow,L"captured_frames")
                        <<L" encoded="<<counter(senderNow,L"encoded_frames")
                        <<L" submitted="<<counter(senderNow,L"encoded_submit_api_accepted")
                        <<L" receiver_rtp="<<counter(receiverTelemetry,L"remote_rtp_callback_packets")
                        <<L" accepted="<<counter(receiverTelemetry,L"complete_access_units_delivered")
                        <<L" decoded="<<counter(receiverTelemetry,L"decoded_frames")
                        <<L" presented="<<current
                        <<L" decode_queue="<<counter(receiverTelemetry,L"decoder_queue_depth")
                        <<L" queue_dropped="<<counter(receiverTelemetry,L"dropped_decode_queue")
                        <<L" worker_stage="<<counter(receiverTelemetry,L"decoder_worker_stage")
                        <<L" stage_ms="<<counter(receiverTelemetry,L"decoder_worker_stage_elapsed_ms")
                        <<L'\n';
                    nextReport=Clock::now()+std::chrono::seconds(10);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            std::wcout<<L"resolution="<<sourceWidth<<L'x'<<sourceHeight
                <<L" presented="<<lastPresent<<L" queue_dropped="
                <<counter(receiverTelemetry,L"dropped_decode_queue")<<L'\n';
            std::wcout<<L"sender_final_diagnostics:\n"<<nodes[0].screenShareDiagnostics()
                <<L"\nreceiver_final_diagnostics:\n"<<receiverTelemetry<<L'\n';
        }
        require(counter(senderTelemetry,L"captured_frames")>0&&
            counter(senderTelemetry,L"encoded_bytes_submit_api_accepted")>0&&
            counter(senderTelemetry,L"capture_callback_count")>0&&
            senderTelemetry.find(L"encoder_worker_stage=")!=std::wstring::npos&&
            senderTelemetry.find(L"capture_callback_stage=")!=std::wstring::npos&&
            counter(receiverTelemetry,L"media_rtp_bytes_received")>0&&
            receiverTelemetry.find(L"media_rtt_ms=Unavailable")!=std::wstring::npos,
            "Screen telemetry did not distinguish measured counters from unavailable ICE RTT");
        if(!relayFixture){nodes[1].setNetworkContext(2,L"new-network-b");
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
            "An unchanged network fingerprint unnecessarily tore down the screen media");}
        require(!nodes[0].send(peers[1],L"Text during screen sharing").empty(),
            "Text send failed during screen sharing");
        wait([&]{return deliveredText>0;},"Text delivery stopped during screen sharing");
        // Existing authenticated peer TLS carries the file independently of
        // H.264 DTLS-SRTP. Stop media while a verified durable prefix is active.
        Blob fileBytes(10*1024*1024);for(size_t n=0;n<fileBytes.size();++n)fileBytes[n]=static_cast<unsigned char>(n*19);
        auto fileSource=root/L"concurrent-media.bin",fileDestination=root/L"concurrent-download";
        std::filesystem::create_directory(fileDestination);
        {std::ofstream file(fileSource,std::ios::binary);file.write(reinterpret_cast<const char*>(fileBytes.data()),fileBytes.size());require(bool(file),"fixture source write failed");}
        if(auto tuning=std::getenv("PEERCHAT_TEST_TRANSFER_TUNING"))nodes[0].fileTransfer(13,"","",unicode::decode(tuning));
        auto fileId=nodes[0].fileTransfer(1,peers[1],"",fileSource.wstring());
        std::string fileKey(fileId.begin(),fileId.end());
        wait([&]{return nodes[1].fileTransfer(0).find(fileId)!=std::wstring::npos;},"file offer during media missing");
        nodes[1].fileTransfer(2,"",fileKey,fileDestination.wstring());
        wait([&]{auto rows=nodes[1].fileTransfer(0);return rows.find(L"\"state\":3")!=std::wstring::npos&&rows.find(L"\"done\":0,")==std::wstring::npos;},"concurrent transfer did not establish durable payload",30);
        auto disconnectsBeforeStop=reliableDisconnects;
        bool mailboxRejected=false;try{nodes[0].mailbox(1,{1});}catch(...){mailboxRejected=true;}
        require(mailboxRejected,"invalid local mailbox operation did not fail closed");
        require(nodes[0].endScreenShare(id),"Sender could not stop the screen share");
        wait([&]{return shareState[0]==0&&shareState[1]==0;},
            "Both peers did not return to idle after screen share");
        require(connected[0]&&connected[1]&&reliableDisconnects==disconnectsBeforeStop,"media stop closed the reliable owner");
        if(relayFixture){
            const auto previous=relayAttempts;
            nodes[0].relayClosed(relayAttempts[0],L"isolated fixture interruption");nodes[1].relayClosed(relayAttempts[1],L"isolated fixture interruption");relayPaired=false;relayAttempts={};connected={};
            wait([&]{return relayPaired&&connected[0]&&connected[1]&&relayAttempts[0]!=previous[0]&&relayAttempts[1]!=previous[1];},"active file did not reestablish a fresh authenticated relay",30);
        }
        wait([&]{return nodes[0].fileTransfer(0).find(L"\"state\":4")!=std::wstring::npos&&nodes[1].fileTransfer(0).find(L"\"state\":4")!=std::wstring::npos;},"media-stop concurrent file did not complete",60);
        auto receivedPath=fileDestination/(fileId+L".peerchat-received")/L"concurrent-media.bin";
        require(receivedPath.filename()==fileSource.filename(),"received original filename changed");
        std::ifstream received(receivedPath,std::ios::binary);
        Blob actual(std::istreambuf_iterator<char>(received),{});
        require(actual==fileBytes&&connected[0]&&connected[1]&&(relayFixture||reliableDisconnects==disconnectsBeforeStop),"media stop invalidated reliable transport or corrupted active file");
        std::cout<<"PASS: 10 MiB durable verified transfer across actual screen-media stop and local mailbox rejection; "<<(relayFixture?"fresh opaque relay pairing and confirmed-prefix recovery":"direct reliable route retained")<<". HOST PEER TLS / WEBRTC UDP ONLY; NO PI\n";
        require(nodes[0].screenShareDiagnostics().find(L"screen_stop_stage=10")!=std::wstring::npos,
            "Completed sender teardown did not expose its stop stage");
        auto repeated=nodes[0].startScreenShare(peers[1],captureKind,
            captureHandle,fps,bitrate,true,nextBackend);
        require(!repeated.empty(),"A second active screen share could not start after teardown");
        wait([&]{return shareState[1]==static_cast<unsigned>(screen::State::Incoming)&&shareId[1]==repeated;},
            "Second active share did not reach the verified peer");
        require(nodes[1].answerScreenShare(repeated,reinterpret_cast<std::uint64_t>(viewer),nextBackend),
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
