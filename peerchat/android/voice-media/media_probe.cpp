#ifndef RTC_STATIC
#define RTC_STATIC
#endif
#include <rtc/rtc.h>
#include <opus/opus.h>
#include "audio.hpp"
#include "portable/android_media_capabilities.hpp"
#include "voice_media.hpp"
#include <array>
// Device-free link/runtime probe: no ICE gathering, sockets or microphone.
int main() {
    int error=0;
    auto* encoder=opus_encoder_create(16000,1,OPUS_APPLICATION_VOIP,&error);
    if(!encoder||error!=OPUS_OK)return 1;
    std::array<opus_int16,320> pcm{};std::array<unsigned char,4000> encoded{};
    int size=opus_encode(encoder,pcm.data(),320,encoded.data(),encoded.size());
    opus_encoder_destroy(encoder);
    if(size<=0)return 2;
    // These references force actual RTC media/DTLS-SRTP implementation linkage.
    volatile auto pc=&rtcCreatePeerConnection;
    volatile auto packetizer=&rtcSetOpusPacketizer;
    if(!pc||!packetizer)return 3;
    peerchat::Audio audio;std::wstring message;
    if(audio.start(message)||message.empty())return 4; // unprepared always denied
    if(peerchat::androidVoiceAvailable())return 5; // this proof excludes NetEq
    peerchat::VoiceMedia media(peerchat::MediaIceConfiguration{});
    if(media.startOffer(message))return 6; // no receive authorization or device access
    return 0;
}
