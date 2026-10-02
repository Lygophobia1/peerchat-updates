#pragma once
#include "voice_apm_capture.hpp"
#include "voice_media.hpp"
#include <opus/opus.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace peerchat {
// One capture-side DSP/encoder per local media session. The device owner delivers
// an already-volume-adjusted 20 ms frame here, once, independent of route count.
// Transport replicates the returned encoded packet; no destination owns DSP.
class LocalVoiceSource {
public:
    struct Packet {std::array<unsigned char,4000> bytes{};int size=0;std::uint32_t timestamp=0;};
    ~LocalVoiceSource(){close();}
    LocalVoiceSource()=default;
    LocalVoiceSource(const LocalVoiceSource&)=delete;
    LocalVoiceSource& operator=(const LocalVoiceSource&)=delete;
    bool open(VoiceMedia::ProcessingOptions options){
        close();options_=options;int error=OPUS_OK;
        encoder_=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&error);
        if(!encoder_||error!=OPUS_OK){close();return false;}
        if(opus_encoder_ctl(encoder_,OPUS_SET_BITRATE(24000))!=OPUS_OK||
            opus_encoder_ctl(encoder_,OPUS_SET_COMPLEXITY(5))!=OPUS_OK||
            opus_encoder_ctl(encoder_,OPUS_SET_INBAND_FEC(1))!=OPUS_OK){close();return false;}
        if(options_.enabled)apm_.open(options_.noiseSuppression,options_.automaticGain,options_.noiseSuppressionStrength,options_.automaticGainStrength);
        muted_=false;frames_=0;encoded_=0;timestamp_=0;return true;
    }
    // Lifecycle is serialized by the session/device owner. Only the capture
    // worker processes; mute is atomic and checked before any encoded fanout.
    bool process20ms(const unsigned char* pcm,std::size_t bytes,Packet& packet){
        packet.size=0;
        if(muted_||!encoder_||!pcm||bytes!=640)return false;
        ++frames_;std::array<std::int16_t,320> samples{};
        for(unsigned i=0;i<320;++i)samples[i]=static_cast<std::int16_t>(pcm[2*i]|(unsigned(pcm[2*i+1])<<8));
        if(options_.enabled)apm_.processFrame(samples.data()); // failure preserves raw input
        packet.timestamp=timestamp_;timestamp_+=960;
        int size=opus_encode(encoder_,samples.data(),320,packet.bytes.data(),packet.bytes.size());
        if(size<=0||muted_)return false;
        packet.size=size;++encoded_;return true;
    }
    void setMuted(bool value){muted_=value;}
    void close(){if(encoder_)opus_encoder_destroy(encoder_);encoder_=nullptr;apm_.close();}
    std::uint64_t captureFrames()const{return frames_;}
    std::uint64_t encodedPackets()const{return encoded_;}
    const VoiceApmCapture& processing()const{return apm_;}
private:
    OpusEncoder* encoder_=nullptr;VoiceApmCapture apm_;VoiceMedia::ProcessingOptions options_;
    std::atomic<bool> muted_{false};std::atomic<std::uint64_t> frames_{0},encoded_{0};
    std::uint32_t timestamp_=0;
};
}
