#define RTC_STATIC
#include <rtc/rtc.h>
#include <opus/opus.h>
#include "voice_media.hpp"
#include "audio.hpp"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>

namespace peerchat {
namespace {
bool rtpPayload(const char* packet, int size, const unsigned char*& payload, int& payloadSize) {
    if (!packet || size < 12) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(packet);
    if ((bytes[0] >> 6) != 2) return false;
    int offset = 12 + 4 * (bytes[0] & 0x0f);
    if (offset > size) return false;
    if (bytes[0] & 0x10) {
        if (offset + 4 > size) return false;
        int words = (bytes[offset + 2] << 8) | bytes[offset + 3];
        offset += 4 + words * 4;
        if (offset > size) return false;
    }
    int end = size;
    if (bytes[0] & 0x20) {
        int padding = bytes[size - 1];
        if (!padding || padding > end - offset) return false;
        end -= padding;
    }
    payload = bytes + offset;
    payloadSize = end - offset;
    return payloadSize > 0;
}
}

struct VoiceMedia::Impl {
    std::mutex mutex;
    std::deque<Event> events;
    std::unique_ptr<Audio> audio;
    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;
    int pc = -1, sendTrack = -1, receiveTrack = -1;
    std::atomic<bool> stopping{false}, deafened{false};
    int inputDevice=-1,outputDevice=-1;

    void push(EventKind kind, std::string value = {}, std::string detail = {}) {
        if (stopping) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (events.size() >= 128) events.pop_front();
        events.push_back({kind, std::move(value), std::move(detail)});
    }
    static void description(int, const char* sdp, const char* type, void* ptr) {
        if (ptr && sdp && type) static_cast<Impl*>(ptr)->push(EventKind::Description, sdp, type);
    }
    static void candidate(int, const char* cand, const char* mid, void* ptr) {
        if (ptr && cand) static_cast<Impl*>(ptr)->push(EventKind::Candidate, cand, mid ? mid : "0");
    }
    static void state(int, rtcState state, void* ptr) {
        if (!ptr) return;
        auto& self = *static_cast<Impl*>(ptr);
        if (state == RTC_CONNECTED) {
            std::wstring error;
            if (!self.audio->start(error) || !self.audio->setMuted(false, error))
                self.push(EventKind::AudioError, std::string(error.begin(), error.end()));
            self.push(EventKind::Connected);
        } else if (state == RTC_DISCONNECTED || state == RTC_CLOSED) self.push(EventKind::Disconnected);
        else if (state == RTC_FAILED) self.push(EventKind::Failed, "ICE/DTLS-SRTP media connection failed");
    }
    static void track(int, int tr, void* ptr) {
        if (!ptr || tr < 0) return;
        auto& self = *static_cast<Impl*>(ptr);
        self.receiveTrack = tr;
        rtcSetUserPointer(tr, ptr);
        rtcSetMessageCallback(tr, &Impl::message);
        rtcChainRtcpReceivingSession(tr);
    }
    static void message(int, const char* packet, int size, void* ptr) {
        if (!ptr || size <= 0) return;
        auto& self = *static_cast<Impl*>(ptr);
        if (self.deafened || !self.decoder) return;
        const unsigned char* payload = nullptr; int payloadSize = 0;
        if (!rtpPayload(packet, size, payload, payloadSize)) return;
        std::vector<opus_int16> pcm(960);
        int samples = opus_decode(self.decoder, payload, payloadSize, pcm.data(), static_cast<int>(pcm.size()), 0);
        if (samples != 320) return;
        std::vector<unsigned char> bytes(static_cast<size_t>(samples) * 2);
        for (int i = 0; i < samples; ++i) {
            auto bits = static_cast<std::uint16_t>(pcm[i]);
            bytes[2 * i] = static_cast<unsigned char>(bits);
            bytes[2 * i + 1] = static_cast<unsigned char>(bits >> 8);
        }
        self.audio->play(0, bytes);
    }
    void captured(const std::vector<unsigned char>& bytes) {
        if (stopping || sendTrack < 0 || !encoder || bytes.size() != 640) return;
        opus_int16 pcm[320];
        for (int i = 0; i < 320; ++i) pcm[i] = static_cast<opus_int16>(bytes[2*i] | (bytes[2*i+1] << 8));
        unsigned char encoded[4000];
        int size = opus_encode(encoder, pcm, 320, encoded, sizeof(encoded));
        if (size > 0) rtcSendMessage(sendTrack, reinterpret_cast<const char*>(encoded), size);
    }
    bool create(std::wstring& error) {
        stopping = false;
        int opusError = OPUS_OK;
        encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &opusError);
        decoder = opus_decoder_create(16000, 1, &opusError);
        if (!encoder || !decoder || opusError != OPUS_OK) { error = L"Could not initialize the Opus voice codec."; return false; }
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
        opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
        audio = std::make_unique<Audio>();
        audio->setDevices(inputDevice,outputDevice);
        audio->setCaptureHandler([this](const std::vector<unsigned char>& frame){ captured(frame); });
        rtcConfiguration config{};
        config.certificateType = RTC_CERTIFICATE_ECDSA;
        config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;
        config.disableAutoNegotiation = true;
        config.forceMediaTransport = true;
        pc = rtcCreatePeerConnection(&config);
        if (pc < 0) { error = L"Could not create the secure WebRTC media connection."; return false; }
        rtcSetUserPointer(pc, this);
        rtcSetLocalDescriptionCallback(pc, &Impl::description);
        rtcSetLocalCandidateCallback(pc, &Impl::candidate);
        rtcSetStateChangeCallback(pc, &Impl::state);
        rtcSetTrackCallback(pc, &Impl::track);
        std::random_device random;
        rtcTrackInit track{}; track.direction=RTC_DIRECTION_SENDRECV;track.codec=RTC_CODEC_OPUS;track.payloadType=111;
        track.ssrc=(static_cast<std::uint32_t>(random())<<16)^random();track.mid="0";track.name="PeerChat voice";track.msid="peerchat";track.trackId="voice";
        sendTrack = rtcAddTrackEx(pc, &track);
        if (sendTrack < 0) { error = L"Could not create the Opus media track."; return false; }
        rtcPacketizerInit packetizer{};packetizer.ssrc=track.ssrc;packetizer.cname="peerchat";packetizer.payloadType=111;packetizer.clockRate=48000;
        if (rtcSetOpusPacketizer(sendTrack, &packetizer) < 0 || rtcChainRtcpSrReporter(sendTrack) < 0) { error=L"Could not configure Opus RTP.";return false; }
        return true;
    }
    void close() {
        stopping = true;
        if (audio) audio->stop();
        if (pc >= 0) { rtcSetUserPointer(pc, nullptr); rtcClosePeerConnection(pc); rtcDeletePeerConnection(pc); pc=-1; }
        sendTrack=receiveTrack=-1;
        if (encoder) opus_encoder_destroy(encoder);
        encoder=nullptr;
        if (decoder) opus_decoder_destroy(decoder);
        decoder=nullptr;
        audio.reset();
        std::lock_guard<std::mutex> lock(mutex); events.clear();
    }
};

VoiceMedia::VoiceMedia(int inputDevice,int outputDevice):impl_(std::make_unique<Impl>()) { impl_->inputDevice=inputDevice;impl_->outputDevice=outputDevice;rtcInitLogger(RTC_LOG_NONE, nullptr); }
VoiceMedia::~VoiceMedia(){ stop(); }
bool VoiceMedia::startOffer(std::wstring& error){stop();if(!impl_->create(error))return false;return rtcSetLocalDescription(impl_->pc,"offer")>=0;}
bool VoiceMedia::startAnswer(const std::string& offer,std::wstring& error){stop();if(!impl_->create(error))return false;if(rtcSetRemoteDescription(impl_->pc,offer.c_str(),"offer")<0||rtcSetLocalDescription(impl_->pc,"answer")<0){error=L"The remote voice offer was invalid.";return false;}return true;}
bool VoiceMedia::setAnswer(const std::string& answer,std::wstring& error){if(impl_->pc<0||rtcSetRemoteDescription(impl_->pc,answer.c_str(),"answer")<0){error=L"The remote voice answer was invalid.";return false;}return true;}
bool VoiceMedia::addCandidate(const std::string& candidate,const std::string& mid,std::wstring& error){if(impl_->pc<0||rtcAddRemoteCandidate(impl_->pc,candidate.c_str(),mid.c_str())<0){error=L"The remote ICE candidate was invalid.";return false;}return true;}
bool VoiceMedia::setMuted(bool muted,std::wstring& error){return impl_->audio&&impl_->audio->setMuted(muted,error);}
void VoiceMedia::setDeafened(bool value){impl_->deafened=value;}
std::vector<VoiceMedia::Event> VoiceMedia::poll(){std::lock_guard<std::mutex> lock(impl_->mutex);std::vector<Event> out;while(!impl_->events.empty()){out.push_back(std::move(impl_->events.front()));impl_->events.pop_front();}return out;}
void VoiceMedia::stop(){impl_->close();}

} // namespace peerchat
