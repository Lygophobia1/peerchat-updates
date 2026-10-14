#include "media_performance.hpp"
#include "screen_frame_timing.hpp"
#include <thread>
#include <iostream>
#include <stdexcept>
void check(bool b){if(!b)throw std::runtime_error("media observer assertion");}
int main(){
    using peerchat::MediaPerformance;MediaPerformance p;
    peerchat::screen::FrameTiming gaps;gaps.observe(1000,33333);gaps.observe(34333,33333);gaps.observe(226882,33333);
    auto gapText=gaps.text(L"gap");check(gapText.find(L"samples:2")!=std::wstring::npos&&gapText.find(L"max_us:192549")!=std::wstring::npos&&gapText.find(L"over_1_5_period:1")!=std::wstring::npos);
    for(unsigned n=1;n<=1000;++n)gaps.observe(226882+n*33333ull,33333);
    check(gaps.text(L"gap").find(L"recent_p95_us:33333")!=std::wstring::npos&&gaps.text(L"gap").size()<256);
    p.observe(MediaPerformance::OpusEncode,1000,20000);p.observe(MediaPerformance::OpusEncode,30000,20000);
    auto v=p.value(MediaPerformance::OpusEncode);check(v.count==2&&v.total==31000&&v.maximum==30000&&v.misses==1);
    std::array<std::thread,4> workers;for(auto& t:workers)t=std::thread([&]{for(int i=0;i<10000;++i)p.observe(MediaPerformance::ScreenDecode,50000,33000,8);});for(auto& t:workers)t.join();
    check(p.value(MediaPerformance::ScreenDecode).count==40000);auto text=p.text();unsigned events=0;std::size_t at=0;while((at=text.find(L"\nmedia_anomaly=",at))!=std::wstring::npos){++events;++at;}check(events>0&&events<=16&&text.size()<4096);
    p.reset();check(p.value(MediaPerformance::ScreenDecode).count==0&&p.text().find(L"\nmedia_anomaly=")==std::wstring::npos);
    p.observe(MediaPerformance::ScreenCaptureWait,100,33333);
    p.observe(MediaPerformance::ScreenEncodedWait,60000,33333,2);
    check(p.value(MediaPerformance::ScreenCaptureWait).misses==0&&p.value(MediaPerformance::ScreenEncodedWait).misses==1);
    check(p.text().find(L"perf_screen_encoded_wait=")!=std::wstring::npos);
    peerchat::MediaRtpObservation r;
    r.packet(65534,0xfffff000,1000000,100);r.packet(65535,0xfffff3c0,1020000,100);
    r.packet(1,0xfffffb40,1060000,100);check(r.gapPackets==1);
    r.packet(0,0xfffff780,1070000,100);check(r.recovered==1&&r.gapPackets-r.recovered==0);
    r.packet(0,0xfffff780,1080000,100);check(r.duplicates==1&&r.recovered==1);
    r.packet(3,0x2c0,1100000,100);check(r.gapPackets==2&&r.timestampJumps==0);
    r.packet(4,0x680,1600000,100);check(r.jitterUs>10000);
    r.packet(65000,0,1620000,100);check(r.late==1);
    std::cout<<"PASS: fixed 16-event ring, concurrent timing/peaks, reset, RTP wrap/reorder/recovery/duplicate/loss/jitter observations\n";
}
