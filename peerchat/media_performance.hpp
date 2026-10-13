#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace peerchat {
// Observers only. No media bytes, queue, retry, scheduler or disk logging.
class MediaPerformance {
public:
    enum Stage {CaptureInterval,PlaybackInterval,CaptureWork,ApmCapture,ApmRender,
        OpusEncode,ReceivePull,ScreenCapture,ScreenInput,ScreenOutput,
        ScreenDecode,ScreenPresent,ScreenQueueAge,RtpArrival,ScreenCaptureWait,ScreenEncodedWait,Count};
    static std::uint64_t nowUs(){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    struct Value {std::uint64_t count=0,total=0,last=0,maximum=0,misses=0;};
    struct Event {std::uint64_t time=0,duration=0,ordinal=0;unsigned stage=0,queue=0;};
    void observe(Stage stage,std::uint64_t us,std::uint64_t budget,unsigned queue=0){
        auto& m=metrics_[stage];++m.count;m.total+=us;m.last=us;auto peak=m.maximum.load();while(peak<us&&!m.maximum.compare_exchange_weak(peak,us)){}
        if(!budget||us<=budget)return;
        ++m.misses;
        // A callback never waits behind a diagnostics reader.
        std::unique_lock<std::mutex> lock(eventsMutex_,std::try_to_lock);
        if(!lock.owns_lock()){++eventsSkipped_;return;}
        auto ordinal=++next_;events_[(ordinal-1)%events_.size()]={nowUs(),us,ordinal,static_cast<unsigned>(stage),queue};
    }
    Value value(Stage stage)const{const auto& m=metrics_[stage];return {m.count.load(),m.total.load(),m.last.load(),m.maximum.load(),m.misses.load()};}
    void reset(){for(auto& m:metrics_){m.count=0;m.total=0;m.last=0;m.maximum=0;m.misses=0;}std::lock_guard<std::mutex> lock(eventsMutex_);events_={};next_=0;eventsSkipped_=0;}
    std::wstring text()const{
        static const wchar_t* names[]={L"capture_interval",L"playback_interval",L"capture_work",L"apm_capture",L"apm_render",L"opus_encode",L"receive_pull",L"screen_capture",L"screen_input",L"screen_output",L"screen_decode_and_present",L"screen_present",L"screen_queue_age",L"rtp_arrival_interval",L"screen_capture_wait",L"screen_encoded_wait"};
        std::wostringstream out;
        for(unsigned i=0;i<Count;++i){auto v=value(static_cast<Stage>(i));if(v.count)out<<L"\nperf_"<<names[i]<<L"=count:"<<v.count<<L";last_us:"<<v.last<<L";max_us:"<<v.maximum<<L";mean_us:"<<v.total/v.count<<L";deadline_misses:"<<v.misses;}
        std::array<Event,16> copy;{std::lock_guard<std::mutex> lock(eventsMutex_);copy=events_;}
        std::sort(copy.begin(),copy.end(),[](const Event& a,const Event& b){return a.ordinal<b.ordinal;});auto now=nowUs();
        for(const auto& e:copy)if(e.ordinal&&now>=e.time&&now-e.time<=60000000)out<<L"\nmedia_anomaly=steady_ms:"<<e.time/1000<<L";age_ms:"<<(now-e.time)/1000<<L";stage:"<<names[e.stage]<<L";duration_us:"<<e.duration<<L";queue:"<<e.queue;
        out<<L"\nmedia_anomaly_capacity=16\nmedia_anomaly_skipped_contention="<<eventsSkipped_;return out.str();
    }
    class Span {MediaPerformance& p_;Stage stage_;std::uint64_t start_,budget_;public:Span(MediaPerformance& p,Stage s,std::uint64_t budget):p_(p),stage_(s),start_(nowUs()),budget_(budget){}~Span(){p_.observe(stage_,nowUs()-start_,budget_);}};
private:
    struct Metric {std::atomic<std::uint64_t> count{0},total{0},last{0},maximum{0},misses{0};};
    std::array<Metric,Count> metrics_{};
    mutable std::mutex eventsMutex_;std::array<Event,16> events_{};
    std::uint64_t next_=0;std::atomic<std::uint64_t> eventsSkipped_{0};
};

// Observed RTP sequence/timing; NetEq remains the receive/reorder/PLC owner.
// Missing estimate is not a measured network loss percentage: reordered packets
// can repair gaps, and duplicates older than the 64-packet horizon are unknown.
struct MediaRtpObservation {
    std::uint64_t packets=0,bytes=0,gapPackets=0,recovered=0,duplicates=0,late=0,timestampJumps=0;
    std::uint16_t highest=0;std::uint64_t seen=0,firstUs=0,lastUs=0;std::uint32_t lastTimestamp=0;double jitterUs=0;
    void packet(std::uint16_t seq,std::uint32_t timestamp,std::uint64_t arrival,std::size_t size){
        ++packets;bytes+=size;
        if(!firstUs){firstUs=lastUs=arrival;highest=seq;seen=1;lastTimestamp=timestamp;return;}
        auto step=static_cast<std::int16_t>(seq-highest);
        if(step>0){if(step>1)gapPackets+=step-1;seen=step>=64?1:(seen<<step)|1;highest=seq;}
        else {unsigned age=static_cast<unsigned>(-static_cast<int>(step));if(age>=64)++late;else if(seen&(std::uint64_t(1)<<age))++duplicates;else{seen|=std::uint64_t(1)<<age;++recovered;}}
        auto ticks=static_cast<std::int32_t>(timestamp-lastTimestamp);double transit=double(arrival-lastUs)-double(ticks)*1000000.0/48000;
        jitterUs+=(std::abs(transit)-jitterUs)/16;
        if(step>0&&(ticks<=0||ticks>48000))++timestampJumps;
        lastUs=arrival;lastTimestamp=timestamp;
    }
    std::wstring text()const{std::wostringstream out;out<<L"\nrtp_observed_packets="<<packets<<L"\nrtp_observed_bytes="<<bytes<<L"\nrtp_sequence_gap_packets="<<gapPackets<<L"\nrtp_reordered_recovered="<<recovered<<L"\nrtp_duplicates_within_64="<<duplicates<<L"\nrtp_late_beyond_64="<<late<<L"\nrtp_missing_estimate="<<(gapPackets>=recovered?gapPackets-recovered:0)<<L"\nrtp_arrival_jitter_ms="<<jitterUs/1000<<L"\nrtp_timestamp_discontinuities="<<timestampJumps;return out.str();}
};
}
