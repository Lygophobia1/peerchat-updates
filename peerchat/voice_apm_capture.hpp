#pragma once

#if defined(__ANDROID__)
#include "portable/voice_apm_android.hpp"
#else

#include "audio_processing/peerchat_audio_processing.h"
#include "rnnoise_capture.hpp"
#include "voice_processing_presets.hpp"
#include "media_performance.hpp"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <utility>
#include <chrono>

namespace peerchat {

// One instance per VoiceMedia generation. Only the capture worker calls processFrame;
// all buffers are fixed-size and owned here or by the caller.
class VoiceApmCapture {
public:
    VoiceApmCapture() = default;
    VoiceApmCapture(const VoiceApmCapture&) = delete;
    VoiceApmCapture& operator=(const VoiceApmCapture&) = delete;
    ~VoiceApmCapture() { close(); }

    bool open(bool noiseSuppression = true, bool automaticGain = true,
              NoiseSuppressionStrength noiseStrength = NoiseSuppressionStrength::Medium,
              AutomaticGainStrength gainStrength = AutomaticGainStrength::Balanced,
              bool echoCancellation = false, bool automaticNoiseControl = true, unsigned automaticNoiseControlLevel = 1) {
        close();
        performance_.reset();
        captureCalls_ = 0;
        processedFrames_ = 0;
        bypassedFrames_ = 0;
        failure_ = 0;
        measuredFrames_ = 0;
        preRms_ = prePeak_ = postRms_ = postPeak_ = 0.0f;
        noiseSuppression_ = noiseSuppression;
        automaticGain_ = automaticGain;
        noiseStrength_ = noiseStrength;
        gainStrength_ = gainStrength;
        echoCancellation_=echoCancellation;renderCalls_=0;referenceUnderruns_=0;resets_=0;
        resetPending_=false;lastRenderMs_=0;delayMs_=0;
        module_ = LoadLibraryExW(L"PeerChat.AudioProcessing.dll", nullptr,
                                 LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module_) { failure_ = 1; return false; }
        create_ = loadSymbol<Create>("pc_apm_create");
        destroy_ = loadSymbol<Destroy>("pc_apm_destroy");
        process_ = loadSymbol<Process>("pc_apm_process_capture_10ms");
        stats_ = loadSymbol<Stats>("pc_apm_get_stats");
        render_=loadSymbol<Process>("pc_apm_process_render_10ms");
        delay_=loadSymbol<Delay>("pc_apm_set_stream_delay_ms");
        reset_=loadSymbol<Reset>("pc_apm_reset");
        if (!create_ || !destroy_ || !process_ || !stats_ || !render_ || !delay_ || !reset_) { failure_ = 2; close(); return false; }
        pc_apm_config config{};
        config.struct_size = sizeof(config);
        config.sample_rate_hz = PC_APM_SAMPLE_RATE_HZ;
        config.channels = PC_APM_CHANNELS;
        automaticNoiseControl_=automaticNoiseControl;
        // If the optional RNNoise module is unavailable, retain the working
        // WebRTC path and expose the fallback in diagnostics.
        rnnoiseFallback_=automaticNoiseControl_ && !rnnoise_.open(automaticNoiseControlLevel);
        automaticNoiseControl_=automaticNoiseControl_&&!rnnoiseFallback_;
        config.noise_suppression_enabled = !automaticNoiseControl_ && noiseSuppression ? 1u : 0u;
        config.automatic_gain_enabled = automaticGain ? 1u : 0u;
        config.echo_cancellation_enabled = echoCancellation ? 1u : 0u;
        config.noise_suppression_strength = static_cast<std::uint32_t>(noiseStrength);
        config.automatic_gain_strength = static_cast<std::uint32_t>(gainStrength);
        instance_ = create_(&config);
        if (!instance_) { failure_ = 3; close(); return false; }
        pc_apm_stats result{};
        result.struct_size = sizeof(result);
        if (stats_(instance_, &result) != PC_APM_OK ||
            result.sample_rate_hz != 16000 || result.channels != 1 ||
            result.noise_suppression_enabled != config.noise_suppression_enabled ||
            result.automatic_gain_enabled != config.automatic_gain_enabled ||
            result.noise_suppression_strength != config.noise_suppression_strength ||
            result.automatic_gain_strength != config.automatic_gain_strength ||
            result.echo_cancellation_enabled != config.echo_cancellation_enabled) {
            failure_ = 4;
            close();
            return false;
        }
        active_ = true;
        failure_ = 0;
        return true;
    }

    // On failure the caller's original 320 samples remain untouched.
    bool processFrame(std::int16_t* pcm) {
        MediaPerformance::Span timed(performance_,MediaPerformance::ApmCapture,20000);
        if (!pcm) return false;
        if (!active_) { ++bypassedFrames_; return false; }
        if(!applyReset()){++bypassedFrames_;return false;}
        if(echoCancellation_){
            if(!referenceActive())++referenceUnderruns_;
            const auto result=delay_(instance_,delayMs_.load());
            if(result!=PC_APM_OK){fail(result);++bypassedFrames_;return false;}
        }
        const auto before = measure(pcm);
        for (unsigned block = 0; block != 2; ++block) {
            ++captureCalls_;
            const auto result = process_(instance_, pcm + block * 160,
                                         processed_.data() + block * 160, 160);
            if (result != PC_APM_OK) {
                failure_ = static_cast<unsigned>(result) + 100;
                active_ = false;
                ++bypassedFrames_;
                return false;
            }
        }
        if(automaticNoiseControl_ && !rnnoise_.processFrame(processed_.data())){failure_=6;active_=false;++bypassedFrames_;return false;}
        const auto after = measure(processed_.data());
        const auto smooth = [this](std::atomic<float>& destination, float sample) {
            destination = measuredFrames_ == 0 ? sample : destination.load() * 0.9f + sample * 0.1f;
        };
        smooth(preRms_, before.first); smooth(prePeak_, before.second);
        smooth(postRms_, after.first); smooth(postPeak_, after.second);
        ++measuredFrames_;
        for (unsigned sample = 0; sample != 320; ++sample) pcm[sample] = processed_[sample];
        ++processedFrames_;
        return true;
    }

    void bypassForTest() { active_ = false; failure_ = 199; }
    // Capture/render are serialized by the Audio device worker, not by a new
    // thread/queue. Reference data is the final remote-only speaker mix.
    bool processRender20ms(const std::int16_t* pcm,std::size_t samples,unsigned delayMs){
        MediaPerformance::Span timed(performance_,MediaPerformance::ApmRender,20000);
        if(!echoCancellation_||!active_||!pcm||samples!=320)return false;
        if(!applyReset())return false;
        delayMs_=delayMs>2000?2000:delayMs;
        for(unsigned block=0;block<2;++block){
            auto result=render_(instance_,pcm+block*160,renderOutput_.data(),160);
            if(result!=PC_APM_OK){fail(result);return false;}
            ++renderCalls_;
        }
        lastRenderMs_=nowMs();return true;
    }
    void requestReset(){resetPending_=true;}
    bool echoCancellation()const{return echoCancellation_;}
    bool referenceActive()const{auto last=lastRenderMs_.load();return active_&&last&&nowMs()-last<200;}
    std::uint64_t renderCalls()const{return renderCalls_;}
    std::uint64_t referenceUnderruns()const{return referenceUnderruns_;}
    std::uint64_t resets()const{return resets_;}
    std::wstring performanceText()const{return performance_.text()+L"\nautomatic_noise_control="+(automaticNoiseControl_?L"RNNoise + adaptive soft gate":L"off; original WebRTC processing")+L"\nrnnoise_fallback="+(rnnoiseFallback_?L"true":L"false")+L"\nrnnoise_added_signal_delay_ms=11.292 when enabled"+noiseControlDiagnostics();}
    unsigned delayMs()const{return delayMs_;}
    NoiseTrace noiseTrace()const{return rnnoise_.trace();}
    std::wstring noiseControlDiagnostics()const{return automaticNoiseControl_?rnnoise_.diagnostics():L"\nautomatic_noise_control_active=false";}
    bool active() const { return active_; }
    unsigned failure() const { return failure_; }
    std::uint64_t captureCalls() const { return captureCalls_; }
    std::uint64_t processedFrames() const { return processedFrames_; }
    std::uint64_t bypassedFrames() const { return bypassedFrames_; }
    std::uint64_t measuredFrames() const { return measuredFrames_; }
    float preRms() const { return preRms_; }
    float prePeak() const { return prePeak_; }
    float postRms() const { return postRms_; }
    float postPeak() const { return postPeak_; }
    bool noiseSuppression() const { return noiseSuppression_; }
    bool automaticGain() const { return automaticGain_; }
    NoiseSuppressionStrength noiseStrength() const { return noiseStrength_; }
    AutomaticGainStrength gainStrength() const { return gainStrength_; }

    void close() {
        rnnoise_.close();
        if (instance_ && destroy_) destroy_(instance_);
        instance_ = nullptr;
        if (module_) FreeLibrary(module_);
        module_ = nullptr;
        create_ = nullptr; destroy_ = nullptr; process_ = nullptr; stats_ = nullptr;
        render_=nullptr;delay_=nullptr;reset_=nullptr;
        active_ = false;
        echoCancellation_=false;
    }

private:
    bool automaticNoiseControl_=true,rnnoiseFallback_=false;
    RnNoiseCapture rnnoise_;
    MediaPerformance performance_;
    static std::int64_t nowMs(){return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();}
    void fail(pc_apm_result error){failure_=100+static_cast<unsigned>(error);active_=false;}
    bool applyReset(){
        if(!resetPending_.exchange(false))return true;
        auto result=reset_(instance_);if(result!=PC_APM_OK){fail(result);return false;}
        rnnoise_.reset();if(echoCancellation_)++resets_;lastRenderMs_=0;return true;
    }
    static std::pair<float,float> measure(const std::int16_t* pcm) {
        std::uint64_t squares = 0;
        unsigned peak = 0;
        for (unsigned i = 0; i != 320; ++i) {
            const auto sample = static_cast<int>(pcm[i]);
            const auto magnitude = static_cast<unsigned>(sample < 0 ? -sample : sample);
            squares += static_cast<std::uint64_t>(magnitude) * magnitude;
            if (magnitude > peak) peak = magnitude;
        }
        return {static_cast<float>(std::sqrt(static_cast<double>(squares) / 320.0) / 32768.0),
                static_cast<float>(peak / 32768.0)};
    }
    using Create = pc_apm* (*)(const pc_apm_config*);
    using Destroy = void (*)(pc_apm*);
    using Process = pc_apm_result (*)(pc_apm*, const std::int16_t*, std::int16_t*, std::uint32_t);
    using Stats = pc_apm_result (*)(const pc_apm*, pc_apm_stats*);
    using Delay = pc_apm_result (*)(pc_apm*,std::uint32_t);
    using Reset = pc_apm_result (*)(pc_apm*);
    template<class Function> Function loadSymbol(const char* name) const {
        static_assert(sizeof(Function) == sizeof(FARPROC), "Windows function pointer size mismatch");
        const auto address = GetProcAddress(module_, name);
        Function result{};
        std::memcpy(&result, &address, sizeof(result));
        return result;
    }
    HMODULE module_ = nullptr;
    pc_apm* instance_ = nullptr;
    Create create_ = nullptr;
    Destroy destroy_ = nullptr;
    Process process_ = nullptr;
    Stats stats_ = nullptr;
    Process render_=nullptr;Delay delay_=nullptr;Reset reset_=nullptr;
    std::array<std::int16_t,160> renderOutput_{};
    bool echoCancellation_=false;
    std::atomic<bool> resetPending_{false};
    std::atomic<std::int64_t> lastRenderMs_{0};
    std::atomic<unsigned> delayMs_{0};
    std::atomic<std::uint64_t> renderCalls_{0},referenceUnderruns_{0},resets_{0};
    std::array<std::int16_t, 320> processed_{};
    std::atomic<bool> active_{false};
    std::atomic<unsigned> failure_{0};
    std::atomic<std::uint64_t> captureCalls_{0}, processedFrames_{0}, bypassedFrames_{0};
    std::atomic<std::uint64_t> measuredFrames_{0};
    std::atomic<float> preRms_{0.0f}, prePeak_{0.0f}, postRms_{0.0f}, postPeak_{0.0f};
    bool noiseSuppression_ = true, automaticGain_ = true;
    NoiseSuppressionStrength noiseStrength_ = NoiseSuppressionStrength::Medium;
    AutomaticGainStrength gainStrength_ = AutomaticGainStrength::Balanced;
};

} // namespace peerchat
#endif
