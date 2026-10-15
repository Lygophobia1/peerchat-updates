#pragma once
#include "../voice_processing_presets.hpp"
#include <atomic>
#include <cstdint>
#include <string>
namespace peerchat {
// Android AudioRecord owns optional session effects. This reports WebRTC APM
// unavailable honestly and preserves PCM: it does not imitate APM or its presets.
class VoiceApmCapture {
public:
    bool open(bool ns=true,bool agc=true,NoiseSuppressionStrength noise=NoiseSuppressionStrength::Medium,
              AutomaticGainStrength gain=AutomaticGainStrength::Balanced,bool=false){
        ns_=ns;agc_=agc;noise_=noise;gain_=gain;bypassed_=0;return false;
    }
    void close(){}
    bool processFrame(std::int16_t*){++bypassed_;return false;}
    bool processRender20ms(const std::int16_t*,std::size_t,unsigned){return false;}
    void requestReset(){}
    void bypassForTest(){}
    bool active()const{return false;}
    bool echoCancellation()const{return false;}
    bool referenceActive()const{return false;}
    unsigned failure()const{return 1;}
    unsigned delayMs()const{return 0;}
    std::uint64_t renderCalls()const{return 0;}
    std::uint64_t referenceUnderruns()const{return 0;}
    std::uint64_t resets()const{return 0;}
    std::uint64_t captureCalls()const{return 0;}
    std::uint64_t processedFrames()const{return 0;}
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
    std::wstring performanceText()const{return L"\napm_backend=unavailable; Android AudioRecord session effects are reported separately";}
private:
    bool ns_=true,agc_=true;
    NoiseSuppressionStrength noise_=NoiseSuppressionStrength::Medium;
    AutomaticGainStrength gain_=AutomaticGainStrength::Balanced;
    std::atomic<std::uint64_t> bypassed_{0};
};
}
