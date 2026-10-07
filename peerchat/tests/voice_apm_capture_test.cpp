#include "voice_apm_capture.hpp"
#include <array>
#include <cstdint>
#include <iostream>

int main() {
    for(unsigned generation=0;generation<8;++generation){
        peerchat::VoiceApmCapture apm;
        const bool aec=generation%2==0;
        if(!apm.open(true,true,peerchat::NoiseSuppressionStrength::Medium,
            peerchat::AutomaticGainStrength::Balanced,aec)||apm.echoCancellation()!=aec)return 10;
        std::array<std::int16_t,320> reference{},capture{};
        for(unsigned frame=0;frame<150;++frame){
            for(unsigned i=0;i<320;++i){reference[i]=static_cast<std::int16_t>((i+frame)%40*100-2000);capture[i]=reference[i]/3;}
            if(apm.processRender20ms(reference.data(),320,80)!=aec||!apm.processFrame(capture.data()))return 11;
        }
        if(apm.captureCalls()!=300||apm.renderCalls()!=(aec?300u:0u)||apm.referenceUnderruns()!=0)return 12;
        apm.requestReset();apm.processRender20ms(reference.data(),320,80);
        if(!apm.processFrame(capture.data())||apm.resets()!=(aec?1u:0u))return 13;
        if(apm.processRender20ms(reference.data(),319,80))return 14;
        apm.bypassForTest();auto raw=capture;
        if(apm.processRender20ms(reference.data(),320,80)||apm.processFrame(capture.data())||capture!=raw)return 15;
    }
    for (int generation = 0; generation != 12; ++generation) {
        peerchat::VoiceApmCapture apm;
        const bool ns = (generation & 1) != 0;
        const bool agc = (generation & 2) != 0;
        const auto nsStrength=static_cast<peerchat::NoiseSuppressionStrength>(generation%3);
        const auto gainStrength=static_cast<peerchat::AutomaticGainStrength>((generation/3)%3);
        if (!apm.open(ns, agc, nsStrength, gainStrength) || !apm.active() || apm.noiseSuppression() != ns || apm.automaticGain() != agc || apm.noiseStrength()!=nsStrength || apm.gainStrength()!=gainStrength) return 1;
        std::array<std::int16_t, 320> frame{};
        for (unsigned i = 0; i != frame.size(); ++i) frame[i] = static_cast<std::int16_t>(i * 13);
        const auto original = frame;
        if (!apm.processFrame(frame.data())) return 2;
        if (apm.captureCalls() != 2 || apm.processedFrames() != 1 || apm.bypassedFrames() != 0 || apm.measuredFrames() != 1 || apm.prePeak() <= 0.0f || apm.postPeak() < 0.0f) return 3;
        apm.bypassForTest();
        frame = original;
        if (apm.processFrame(frame.data()) || frame != original || apm.bypassedFrames() != 1 || apm.failure() != 199) return 4;
    }
    std::cout << "PASS: per-generation APM creation, two 10 ms capture blocks per 20 ms frame, and raw-PCM bypass.\n";
}
