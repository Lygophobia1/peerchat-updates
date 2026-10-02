#include "screen_share_media.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

// Test-only local ICE. Production group topology continues to reject direct
// policy. Exercises real shared WGC/software H.264 -> two independent accepted
// RTC/receive/recovery/MFT paths, without a second capture or encoder.
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct Route {
    std::unique_ptr<peerchat::ScreenShareMedia> sender,receiver;
    bool answered=false;
    std::vector<std::pair<std::string,std::string>> candidates;
    void pump(){std::string error;
        for(auto& e:sender->poll()){
            if(e.kind==peerchat::ScreenShareMedia::EventKind::Description&&e.detail=="offer"){
                check(receiver->startAnswer(e.value,error),error.c_str());answered=true;
                for(auto& c:candidates)check(receiver->addCandidate(c.first,c.second),"Pending candidate rejected");
                candidates.clear();
            }else if(e.kind==peerchat::ScreenShareMedia::EventKind::Candidate){if(answered)check(receiver->addCandidate(e.value,e.detail),"Candidate rejected");else candidates.emplace_back(e.value,e.detail);}
            else if(e.kind==peerchat::ScreenShareMedia::EventKind::Failed)throw std::runtime_error(e.value);
        }
        for(auto& e:receiver->poll()){
            if(e.kind==peerchat::ScreenShareMedia::EventKind::Description&&e.detail=="answer")check(sender->setAnswer(e.value),"Answer rejected");
            else if(e.kind==peerchat::ScreenShareMedia::EventKind::Candidate)check(sender->addCandidate(e.value,e.detail),"Reverse candidate rejected");
            else if(e.kind==peerchat::ScreenShareMedia::EventKind::Failed)throw std::runtime_error(e.value);
        }
    }
};
int main(){HWND source=nullptr,viewers[2]{};try{
    source=CreateWindowExW(0,L"STATIC",L"Shared software H.264 source",WS_OVERLAPPEDWINDOW|WS_VISIBLE,20,20,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(source!=nullptr,"Source window unavailable");
    Route routes[2];
    for(unsigned i=0;i<2;++i){viewers[i]=CreateWindowExW(0,L"STATIC",L"Shared fanout receiver",WS_OVERLAPPEDWINDOW|WS_VISIBLE,700,20+i*300,400,280,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(viewers[i]!=nullptr,"Viewer window unavailable");
        peerchat::ScreenShareMedia::Options o;o.sender=true;o.externallyEncoded=true;o.fps=30;o.width=1280;o.height=720;o.backend=peerchat::screen::Backend::WebRtcCompatibility;o.ice=peerchat::MediaIceConfiguration::directForTests();
        routes[i].sender=std::make_unique<peerchat::ScreenShareMedia>(o);o.sender=false;o.presentationWindow=reinterpret_cast<std::uint64_t>(viewers[i]);routes[i].receiver=std::make_unique<peerchat::ScreenShareMedia>(o);
        std::string error;check(routes[i].sender->startOffer(error),error.c_str());
    }
    peerchat::ScreenCaptureEncodeBackend capture;pc_screen_encoder_options options{};options.structure_size=sizeof(options);options.source_kind=PC_SCREEN_WINDOW;options.source_handle=reinterpret_cast<std::uint64_t>(source);options.frames_per_second=30;options.bitrate_bits_per_second=4000000;options.backend=PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY;
    check(capture.open(options),"Shared capture failed");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);unsigned frames=0;bool retired=false;std::uint64_t remainingBefore=0,retiredPresented=0;
    while(std::chrono::steady_clock::now()<deadline){
        SetWindowTextW(source,(L"Shared source frame "+std::to_wstring(++frames)).c_str());RedrawWindow(source,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
        MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        if(!retired)routes[0].pump();
        routes[1].pump();
        for(unsigned n=0;n<4;++n){pc_screen_encoded_frame f{};f.structure_size=sizeof(f);int pop=capture.pop(f);check(pop>=0,"Shared encoder failed");if(!pop)break;
            if(!retired)routes[0].sender->sendEncoded(capture.data(),f);
            routes[1].sender->sendEncoded(capture.data(),f);
        }
        if(frames%15==0)capture.requestKeyframe();
        auto a=routes[0].receiver->statistics(),b=routes[1].receiver->statistics();
        if(!retired&&a.presented>=4&&b.presented>=4){retiredPresented=a.presented;remainingBefore=b.presented;routes[0].sender->stop();routes[0].receiver->stop();retired=true;}
        if(retired&&b.presented>=remainingBefore+4){std::cout<<"PASS: one capture/software encoder; two real RTP/MFT receivers; 1280x720 bootstrap renegotiates actual capture; viewer removal preserves remaining route; presented="<<retiredPresented<<","<<b.presented<<"\n";capture.close();routes[1].sender->stop();routes[1].receiver->stop();for(auto w:viewers)DestroyWindow(w);DestroyWindow(source);return 0;}
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    for(auto& r:routes){auto s=r.receiver->statistics();std::cerr<<"receiver RTP="<<s.rtpReceived<<" AU="<<s.accessUnitsDelivered<<" decoded="<<s.decoded<<" presented="<<s.presented<<"\n";}
    throw std::runtime_error("Shared fanout did not sustain frames before timeout");
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";for(auto w:viewers)if(w)DestroyWindow(w);if(source)DestroyWindow(source);return 1;}}
