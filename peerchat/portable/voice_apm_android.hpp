#pragma once
#include "../voice_processing_presets.hpp"
#include "../rnnoise_capture.hpp"
#include <atomic>
#include <cstdint>
#include <string>
namespace peerchat {
// RNNoise handles capture suppression. AudioRecord retains device AEC/AGC.
class VoiceApmCapture {
public:
    bool open(bool ns=true,bool agc=true,NoiseSuppressionStrength noise=NoiseSuppressionStrength::Medium,
              AutomaticGainStrength gain=AutomaticGainStrength::Balanced,bool=false,bool automaticNoiseControl=true,unsigned=1){
        close();ns_=ns;agc_=agc;noise_=noise;gain_=gain;bypassed_=0;processed_=0;
        automatic_=automaticNoiseControl&&androidAutomaticNoiseControl.load();
        active_=!automatic_||rnnoise_.open(androidAutomaticNoiseControlLevel.load());return active_;
    }
    void close(){rnnoise_.close();active_=false;}
    bool processFrame(std::int16_t* pcm){if(resetPending_.exchange(false))rnnoise_.reset();if(!pcm||!active_||(automatic_&&!rnnoise_.processFrame(pcm))){++bypassed_;return false;}++processed_;return true;}
    bool processRender20ms(const std::int16_t*,std::size_t,unsigned){return false;}
    void requestReset(){resetPending_=true;}
    void bypassForTest(){active_=false;}
    NoiseTrace noiseTrace()const{return rnnoise_.trace();}
    std::wstring noiseControlDiagnostics()const{return automatic_?rnnoise_.diagnostics():L"\nautomatic_noise_control_active=false";}
    bool active()const{return active_;}
    bool echoCancellation()const{return false;}
    bool referenceActive()const{return false;}
    unsigned failure()const{return active_?0:1;}
    unsigned delayMs()const{return 0;}
    std::uint64_t renderCalls()const{return 0;}
    std::uint64_t referenceUnderruns()const{return 0;}
    std::uint64_t resets()const{return 0;}
    std::uint64_t captureCalls()const{return processed_*2;}
    std::uint64_t processedFrames()const{return processed_;}
    std::uint64_t bypassedFrames()const{return bypassed_;}
    std::uint64_t measuredFrames()const{return 0;}
    float preRms()const{return 0;}
    float prePeak()const{return 0;}
    float postRms()const{return 0;}
    float postPeak()const{return 0;}
    bool noiseSuppression()const{return ns_;}
    bool automaticGain()const{return agc_;}
    NoiseSuppressionStrength noiseStrength()const{return noise_;}
    AutomaticGainStrength gainStrength()const{return gain_;}
    std::wstring performanceText()const{return std::wstring(L"\nautomatic_noise_control=")+(automatic_?L"RNNoise + adaptive soft gate; 11.292 ms signal delay":L"off; original device processing")+L"\nWebRTC APM unavailable; Android device AEC/AGC reported separately"+noiseControlDiagnostics();}
private:
    bool automatic_=true;
    std::atomic<bool> resetPending_{false};
    RnNoiseCapture rnnoise_;
    std::atomic<bool> active_{false};
    std::atomic<std::uint64_t> processed_{0};
    bool ns_=true,agc_=true;
    NoiseSuppressionStrength noise_=NoiseSuppressionStrength::Medium;
    AutomaticGainStrength gain_=AutomaticGainStrength::Balanced;
    std::atomic<std::uint64_t> bypassed_{0};
};
}
