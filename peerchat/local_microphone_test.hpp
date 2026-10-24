#pragma once
#include "audio.hpp"
#include "voice_apm_capture.hpp"
#include <array>
#include <vector>
#include <mutex>
#include <cmath>
#include <algorithm>
namespace peerchat {
// Explicit local UI operation only. No Node, network, files or message events.
class LocalMicrophoneTest {
    using Frame=std::array<std::int16_t,320>;
    static constexpr std::size_t Limit=400; // eight seconds, two bounded streams
    std::unique_ptr<Audio> audio_;VoiceApmCapture apm_;std::mutex mutex_;
    std::vector<Frame> raw_,processed_;std::size_t count_=0,cursor_=0;
    bool recording_=false,processing_=false;int playback_=0;std::wstring error_;
    double rawSquares_=0,processedSquares_=0;std::uint64_t rawFullScale_=0,processedFullScale_=0;
    void capture(const std::vector<unsigned char>& bytes){
        if(bytes.size()!=640)return;
        {std::lock_guard<std::mutex> lock(mutex_);if(!recording_)return;}
        Frame raw{},processed{};
        for(std::size_t i=0;i<320;++i)raw[i]=static_cast<std::int16_t>(bytes[2*i]|(unsigned(bytes[2*i+1])<<8));
        processed=raw;if(processing_&&!apm_.processFrame(processed.data())){std::lock_guard<std::mutex> lock(mutex_);error_=L"Processing unavailable; processed preview is raw bypass.";}
        bool finished=false;{
            std::lock_guard<std::mutex> lock(mutex_);if(!recording_||count_>=Limit)return;
            raw_[count_]=raw;processed_[count_]=processed;++count_;
            for(std::size_t i=0;i<320;++i){rawSquares_+=double(raw[i])*raw[i];processedSquares_+=double(processed[i])*processed[i];if(raw[i]==32767||raw[i]==-32768)++rawFullScale_;if(processed[i]==32767||processed[i]==-32768)++processedFullScale_;}
            if(count_==Limit){recording_=false;finished=true;}
        }
        // Audio explicitly supports capture-callback mute/generation retirement.
        if(finished){std::wstring ignored;audio_->setMuted(true,ignored);}
    }
    bool pull(std::int16_t* pcm,std::size_t n){std::lock_guard<std::mutex> lock(mutex_);if(n!=320)return false;if(playback_==0||cursor_>=count_){playback_=0;return false;}const auto& frame=playback_==1?raw_[cursor_]:processed_[cursor_];std::copy(frame.begin(),frame.end(),pcm);++cursor_;return true;}
public:
    struct State {bool recording=false,available=false,processing=false;int frames=0,played=0,playing=0;float rawRms=0,processedRms=0;std::uint64_t rawFullScale=0,processedFullScale=0;std::wstring error;};
    ~LocalMicrophoneTest(){clear();}
    bool start(int input,int output,int volume,bool enabled,bool ns,bool agc,unsigned noise,unsigned gain,bool automatic=true,unsigned level=1){
        clear();
        if(volume<0||volume>200||noise>2||gain>2){std::lock_guard<std::mutex> lock(mutex_);error_=L"Invalid local microphone settings.";return false;}
        raw_.resize(Limit);processed_.resize(Limit);processing_=enabled;
        if(enabled&&!apm_.open(ns,agc,static_cast<NoiseSuppressionStrength>(noise),static_cast<AutomaticGainStrength>(gain),false,automatic,level)){clear();std::lock_guard<std::mutex> lock(mutex_);error_=L"Existing audio processor could not initialize.";return false;}
        audio_=std::make_unique<Audio>();audio_->setDevices(input,output);audio_->setVolumes(volume/100.0f,0.5f);
        audio_->setCaptureHandler([this](const auto& frame){capture(frame);});audio_->setPlayoutHandler([this](auto* pcm,std::size_t n){return pull(pcm,n);});
        recording_=true;std::wstring error;
        if(!audio_->start(error)||!audio_->telemetry().inputAvailable){clear();std::lock_guard<std::mutex> lock(mutex_);error_=error.empty()?L"No microphone is available.":error;return false;}return true;
    }
    void stopRecording(){ {std::lock_guard<std::mutex> lock(mutex_);recording_=false;playback_=0;}if(audio_){std::wstring ignored;audio_->setMuted(true,ignored);}}
    bool listen(int mode){if(mode!=1&&mode!=2)return false;stopRecording();std::lock_guard<std::mutex> lock(mutex_);if(!audio_||count_==0)return false;cursor_=0;playback_=mode;return true;}
    void clear(){
        {std::lock_guard<std::mutex> lock(mutex_);recording_=false;playback_=0;}
        if(audio_){audio_->stop();audio_.reset();}apm_.close();
        std::lock_guard<std::mutex> lock(mutex_);if(!raw_.empty())SecureZeroMemory(raw_.data(),raw_.size()*sizeof(Frame));if(!processed_.empty())SecureZeroMemory(processed_.data(),processed_.size()*sizeof(Frame));
        std::vector<Frame>().swap(raw_);std::vector<Frame>().swap(processed_);count_=cursor_=0;rawSquares_=processedSquares_=0;rawFullScale_=processedFullScale_=0;processing_=false;error_.clear();
    }
    State state(){std::lock_guard<std::mutex> lock(mutex_);State s;s.recording=recording_;s.available=count_>0;s.processing=processing_;s.frames=static_cast<int>(count_);s.played=static_cast<int>(cursor_);s.playing=playback_;s.error=error_;s.rawFullScale=rawFullScale_;s.processedFullScale=processedFullScale_;if(count_){s.rawRms=static_cast<float>(std::sqrt(rawSquares_/(count_*320))/32768);s.processedRms=static_cast<float>(std::sqrt(processedSquares_/(count_*320))/32768);}return s;}
#ifdef PEERCHAT_TEST_MICROPHONE
    void testCapture(const std::vector<unsigned char>& frame){capture(frame);}
    bool testPull(std::int16_t* pcm){return pull(pcm,320);}
#endif
};
}
