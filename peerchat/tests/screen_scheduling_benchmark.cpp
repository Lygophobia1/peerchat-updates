#include "screen_share_media.hpp"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <stdexcept>
#include <condition_variable>
using Clock=std::chrono::steady_clock;
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct Route {
    std::unique_ptr<peerchat::ScreenShareMedia> sender,receiver;
    bool answered=false;
    std::vector<std::pair<std::string,std::string>> candidates;
    void pump(){std::string error;
        for(auto& e:sender->poll()){
            if(e.kind==peerchat::ScreenShareMedia::EventKind::Description&&e.detail=="offer"){
                check(receiver->startAnswer(e.value,error),error.c_str());answered=true;
                for(auto& c:candidates)check(receiver->addCandidate(c.first,c.second),"Candidate rejected");
                candidates.clear();
            }else if(e.kind==peerchat::ScreenShareMedia::EventKind::Candidate){if(answered)check(receiver->addCandidate(e.value,e.detail),"Candidate rejected");else candidates.emplace_back(e.value,e.detail);}
            else if(e.kind==peerchat::ScreenShareMedia::EventKind::Failed)throw std::runtime_error(e.value);
        }
        for(auto& e:receiver->poll()){
            if(e.kind==peerchat::ScreenShareMedia::EventKind::Description&&e.detail=="answer")check(sender->setAnswer(e.value),"Answer rejected");
            else if(e.kind==peerchat::ScreenShareMedia::EventKind::Candidate)check(sender->addCandidate(e.value,e.detail),"Candidate rejected");
            else if(e.kind==peerchat::ScreenShareMedia::EventKind::Failed)throw std::runtime_error(e.value);
        }
    }
};
static unsigned long long cpu(){FILETIME a,b,k,u;check(GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u)!=0,"CPU query failed");ULARGE_INTEGER x{},y{};x.LowPart=k.dwLowDateTime;x.HighPart=k.dwHighDateTime;y.LowPart=u.dwLowDateTime;y.HighPart=u.dwHighDateTime;return x.QuadPart+y.QuadPart;}
int main(int argc,char** argv){HWND source=nullptr,content=nullptr;std::vector<HWND> windows;
try{
    unsigned viewers=argc>1?std::stoul(argv[1]):1,pollMs=argc>2?std::stoul(argv[2]):20;
    check(viewers>=1&&viewers<=3&&pollMs>=1&&pollMs<=60,"Bounded benchmark arguments required");
    source=CreateWindowExW(0,L"STATIC",L"Screen scheduling fixture",WS_OVERLAPPEDWINDOW|WS_VISIBLE,20,20,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(source,"Source unavailable");
    content=CreateWindowExW(0,L"STATIC",L"Frame",WS_CHILD|WS_VISIBLE,20,40,500,200,source,nullptr,GetModuleHandleW(nullptr),nullptr);check(content,"Content unavailable");
    std::vector<Route> routes(viewers);
    for(unsigned i=0;i<viewers;++i){auto w=CreateWindowExW(0,L"STATIC",L"Scheduling viewer",WS_OVERLAPPEDWINDOW|WS_VISIBLE,700,20+250*i,400,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(w,"Viewer unavailable");windows.push_back(w);
        peerchat::ScreenShareMedia::Options o;o.sender=true;o.externallyEncoded=true;o.fps=30;o.width=640;o.height=480;o.backend=peerchat::screen::Backend::WebRtcCompatibility;o.ice=peerchat::MediaIceConfiguration::directForTests();
        routes[i].sender=std::make_unique<peerchat::ScreenShareMedia>(o);o.sender=false;o.presentationWindow=reinterpret_cast<std::uint64_t>(w);routes[i].receiver=std::make_unique<peerchat::ScreenShareMedia>(o);std::string error;check(routes[i].sender->startOffer(error),error.c_str());
    }
    peerchat::ScreenCaptureEncodeBackend capture;pc_screen_encoder_options o{};o.structure_size=sizeof(o);o.source_kind=PC_SCREEN_WINDOW;o.source_handle=reinterpret_cast<std::uint64_t>(source);o.frames_per_second=30;o.bitrate_bits_per_second=4000000;o.backend=PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY;check(capture.open(o),"Capture failed");
    const auto start=Clock::now(),finish=start+std::chrono::seconds(12);auto nextPaint=start,nextKeyframe=start,nextPump=start;
    pc_screen_encoder_status baseline{};baseline.structure_size=sizeof(baseline);std::vector<peerchat::ScreenShareMedia::Statistics> before(viewers);bool sampled=false;auto measured=start;unsigned updates=0;double pumpPeak=0;unsigned long long cpuStart=0;
    std::mutex mutex;std::condition_variable wake;
    while(Clock::now()<finish){auto now=Clock::now();MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        if(now>=nextPaint){SetWindowTextW(content,(L"Animated fixture "+std::to_wstring(++updates)).c_str());RedrawWindow(source,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);nextPaint=now+std::chrono::milliseconds(16);}
        if(now>=nextPump){auto began=Clock::now();for(auto& r:routes)r.pump();for(unsigned n=0;n<4;++n){pc_screen_encoded_frame f{};f.structure_size=sizeof(f);int pop=capture.pop(f);check(pop>=0,"Encoder failed");if(!pop)break;for(auto& r:routes)r.sender->sendEncoded(capture.data(),f);}pumpPeak=std::max(pumpPeak,std::chrono::duration<double,std::milli>(Clock::now()-began).count());nextPump=now+std::chrono::milliseconds(pollMs);}
        if(now>=nextKeyframe){capture.requestKeyframe();nextKeyframe=now+std::chrono::seconds(2);}
        if(!sampled&&now-start>=std::chrono::seconds(4)){check(capture.status(baseline)!=0,"Status unavailable");for(unsigned i=0;i<viewers;++i)before[i]=routes[i].receiver->statistics();measured=now;cpuStart=cpu();sampled=true;}
        std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::milliseconds(1));
    }
    const double seconds=std::chrono::duration<double>(Clock::now()-measured).count();pc_screen_encoder_status s{};s.structure_size=sizeof(s);check(capture.status(s)!=0&&sampled,"Final status unavailable");
    std::cout<<"MEASURE viewers="<<viewers<<" poll_ms="<<pollMs<<" target_fps=30 seconds="<<seconds<<" capture_fps="<<(s.captured_frames-baseline.captured_frames)/seconds<<" encoded_fps="<<(s.encoded_frames-baseline.encoded_frames)/seconds<<" output_queue="<<s.encoded_queue_depth<<" capture_drops="<<s.dropped_before_encode<<" output_drops="<<s.dropped_encoded_queue<<" pump_peak_ms="<<pumpPeak<<" process_cpu_ms="<<(cpu()-cpuStart)/10000.<<"\n";
    std::wcout<<s.performance<<L"\n";
    for(unsigned i=0;i<viewers;++i){auto r=routes[i].receiver->statistics();check(r.presented>before[i].presented,"No measured presented frames");std::cout<<"MEASURE viewer="<<i<<" received_au_fps="<<(r.accessUnitsDelivered-before[i].accessUnitsDelivered)/seconds<<" decoded_fps="<<(r.decoded-before[i].decoded)/seconds<<" presented_fps="<<(r.presented-before[i].presented)/seconds<<" queue="<<r.decoderQueueDepth<<" oldest_ms="<<r.decoderOldestQueueAgeMs<<" queue_refused="<<r.droppedDecodeQueue<<"\n";std::wcout<<routes[i].receiver->diagnostics()<<L"\n";}
    capture.close();for(auto& r:routes){r.sender->stop();r.receiver->stop();}for(auto w:windows)DestroyWindow(w);DestroyWindow(source);return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";for(auto w:windows)DestroyWindow(w);if(source)DestroyWindow(source);return 1;}}
