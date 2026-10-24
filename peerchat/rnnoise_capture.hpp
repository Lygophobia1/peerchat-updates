#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <cstring>
#include "automatic_noise_gate.hpp"
#include <atomic>
#include <string>
#ifdef __ANDROID__
extern "C" {
#include "third_party/rnnoise/include/rnnoise.h"
}
#else
#include <windows.h>
struct DenoiseState;
#endif
namespace peerchat {
// Android owns one foreground microphone session. Set before media creation.
inline std::atomic<bool> androidAutomaticNoiseControl{true};
inline std::atomic<unsigned> androidAutomaticNoiseControlLevel{1};
struct NoiseTrace {float vad=0,input=0,retained=0,gain=1,floor=80,output=0;bool open=true;float dryMix=.15f,target=.02f;bool background=false;};
// Per-call state. Setup warms the upstream global FFT tables under a mutex;
// processing uses fixed buffers, no allocation, and no additional audio queue.
class RnNoiseCapture {
    using Create = DenoiseState*(*)();
    using Destroy = void(*)(DenoiseState*);
    using Init = int(*)(DenoiseState*);
    using Process = float(*)(DenoiseState*,float*,const float*);
    DenoiseState* state_ = nullptr;
    Destroy destroy_ = nullptr;
    Init init_ = nullptr;
    Process process_ = nullptr;
    std::array<float,63> taps_{}, upHistory_{}, downHistory_{};
    std::array<float,480> input_{}, output_{}, dry_{};
    unsigned upPos_=0, downPos_=0;
    float wet_=.85f,idleWet_=.97f;
    std::atomic<unsigned> level_{1};
    // Independent relaxed meters, not audio/history. Read off the capture thread.
    static_assert(std::atomic<float>::is_always_lock_free,"Audio meters must be lock free");
    std::atomic<float> vad_{0},inputRms_{0},retainedRms_{0},gateGain_{1},noiseFloor_{80},outputRms_{0},dryMix_{.15f},gateTarget_{.02f};
    std::atomic<bool> gateOpen_{true},background_{false};
    AutomaticNoiseGate gate_;
    float filter(float x,std::array<float,63>& history,unsigned& pos) {
        history[pos]=x; float y=0; unsigned at=pos;
        for(unsigned i=0;i<63;++i){y+=taps_[i]*history[at];at=at?at-1:62;}
        pos=(pos+1)%63;return y;
    }
public:
    RnNoiseCapture()=default;
    RnNoiseCapture(const RnNoiseCapture&)=delete;
    RnNoiseCapture& operator=(const RnNoiseCapture&)=delete;
    ~RnNoiseCapture(){close();}
    bool open(unsigned strength=1) {
        close();
        static std::mutex setup;
        std::lock_guard<std::mutex> lock(setup);
        Create create=nullptr;
#ifdef __ANDROID__
        create=&rnnoise_create;destroy_=&rnnoise_destroy;init_=&rnnoise_init;process_=&rnnoise_process_frame;
#else
        // Keep upstream global FFT storage alive for the process lifetime.
        static HMODULE module=LoadLibraryExW(L"PeerChat.RNNoise.dll",nullptr,
            LOAD_LIBRARY_SEARCH_APPLICATION_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module)return false;
        auto symbol=[&](const char* name){return GetProcAddress(module,name);};
        // memcpy avoids incompatible function-pointer cast warnings on MinGW.
        auto assign=[](auto& target,FARPROC source){static_assert(sizeof(target)==sizeof(source));memcpy(&target,&source,sizeof(target));};
        assign(create,symbol("rnnoise_create"));assign(destroy_,symbol("rnnoise_destroy"));
        assign(init_,symbol("rnnoise_init"));assign(process_,symbol("rnnoise_process_frame"));
#endif
        if(!create||!destroy_||!init_||!process_)return false;
        state_=create();if(!state_)return false;
        input_.fill(0);process_(state_,output_.data(),input_.data());init_(state_);
        constexpr double pi=3.14159265358979323846, cutoff=7200.0/48000.0;
        double sum=0;
        for(int i=0;i<63;++i){int n=i-31;double sinc=n?std::sin(2*pi*cutoff*n)/(pi*n):2*cutoff;
            taps_[i]=static_cast<float>(sinc*(0.54-0.46*std::cos(2*pi*i/62)));sum+=taps_[i];}
        for(auto& t:taps_)t=static_cast<float>(t/sum);
        strength=std::min(2u,strength);level_=strength;
        wet_=strength==0?.80f:(strength==1?.85f:.90f);
        idleWet_=strength==0?.90f:(strength==1?.97f:.995f);
        gate_.configure(strength);
        reset();return true;
    }
    void reset(){if(state_)init_(state_);upHistory_.fill(0);downHistory_.fill(0);dry_.fill(0);upPos_=downPos_=0;gate_.reset();vad_=inputRms_=retainedRms_=outputRms_=0;gateGain_=1;noiseFloor_=80;gateOpen_=true;background_=false;dryMix_=1-wet_;gateTarget_=gate_.target();}
    void close(){if(state_&&destroy_)destroy_(state_);state_=nullptr;}
    NoiseTrace trace()const{return {vad_.load(),inputRms_.load(),retainedRms_.load(),gateGain_.load(),noiseFloor_.load(),outputRms_.load(),gateOpen_.load(),dryMix_.load(),gateTarget_.load(),background_.load()};}
    std::wstring diagnostics()const{
        auto s=trace();auto db=[](float x){return std::to_wstring(20*std::log10(std::max(x,0.000001f)/32768));};
        return L"\nautomatic_noise_level="+std::to_wstring(level_.load())+L"\nrnnoise_vad="+std::to_wstring(s.vad)+
            L"\nnoise_gate_open="+(s.open?L"1":L"0")+L"\nnoise_gate_background="+(s.background?L"1":L"0")+
            L"\nnoise_gate_gain="+std::to_wstring(s.gain)+L"\nnoise_gate_target="+std::to_wstring(s.target)+
            L"\nnoise_gate_attenuation_db="+std::to_wstring(-20*std::log10(std::max(s.gain,0.000001f)))+
            L"\nnoise_floor_dbfs="+db(s.floor)+L"\nrnnoise_input_dbfs="+db(s.input)+L"\nrnnoise_output_dbfs="+db(s.retained)+
            L"\nrnnoise_dry_mix="+std::to_wstring(s.dryMix)+L"\nnoise_control_outgoing_dbfs="+db(s.output);
    }
    bool active()const{return state_!=nullptr;}
    bool processFrame(std::int16_t* pcm){
        if(!pcm||!state_)return false;
        for(unsigned block=0;block<2;++block){
            for(unsigned i=0;i<160;++i)for(unsigned phase=0;phase<3;++phase)
                input_[i*3+phase]=filter(phase?0.0f:3.0f*pcm[block*160+i],upHistory_,upPos_);
            const float probability=process_(state_,output_.data(),input_.data());
            double energy=0,retained=0;
            for(unsigned i=0;i<480;++i){energy+=double(dry_[i])*dry_[i];retained+=double(output_[i])*output_[i];}
            const bool gateOpen=gate_.decide(probability,static_cast<float>(std::sqrt(energy/480)),static_cast<float>(std::sqrt(retained/480)));
            for(unsigned i=0;i<480;++i){
                // RNNoise synthesis has one 10 ms block of delay. Match the
                // unprocessed component before mixing suppression strengths.
                const float gain=gate_.next(gateOpen);
                const float wet=wet_+(idleWet_-wet_)*(1-gain);
                float sample=filter((wet*output_[i]+(1-wet)*dry_[i])*gain,downHistory_,downPos_);
                if(i%3==0){
                    // Resampling can overshoot the existing AGC limiter. A
                    // unity-slope soft knee catches only peaks, never boosts
                    // quiet input and adds no second adaptive gain controller.
                    const float magnitude=std::abs(sample);
                    if(magnitude>30000.0f)sample=std::copysign(30000.0f+2700.0f*std::tanh((magnitude-30000.0f)/2700.0f),sample);
                    pcm[block*160+i/3]=static_cast<std::int16_t>(std::lround(std::clamp(sample,-32700.0f,32700.0f)));
                }
            }
            double finalEnergy=0;for(unsigned i=0;i<160;++i)finalEnergy+=double(pcm[block*160+i])*pcm[block*160+i];
            vad_.store(probability,std::memory_order_relaxed);inputRms_.store(float(std::sqrt(energy/480)),std::memory_order_relaxed);
            retainedRms_.store(float(std::sqrt(retained/480)),std::memory_order_relaxed);gateGain_.store(gate_.gain(),std::memory_order_relaxed);
            noiseFloor_.store(gate_.noiseFloor(),std::memory_order_relaxed);outputRms_.store(float(std::sqrt(finalEnergy/160)),std::memory_order_relaxed);
            gateOpen_.store(gateOpen,std::memory_order_relaxed);background_.store(gate_.background(),std::memory_order_relaxed);
            gateTarget_.store(gate_.target(),std::memory_order_relaxed);dryMix_.store(1-(wet_+(idleWet_-wet_)*(1-gate_.gain())),std::memory_order_relaxed);
            dry_=input_;
        }return true;
    }
};
}
