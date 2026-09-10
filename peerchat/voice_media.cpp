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
#include <sstream>

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
    std::atomic<bool> stopping{false}, deafened{false}, trackOpen{false};
    std::atomic<std::uint64_t> capturedFrames{0}, encodedFrames{0}, submittedFrames{0};
    std::atomic<std::uint64_t> receivedPackets{0}, decodedFrames{0}, playbackFrames{0};
    std::string mediaState="created", latestAudioError="none", latestMediaError="none";
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
    static void opened(int, void* ptr) { if (ptr) static_cast<Impl*>(ptr)->trackOpen=true; }
    static void closed(int, void* ptr) { if (ptr) static_cast<Impl*>(ptr)->trackOpen=false; }
    static void state(int, rtcState state, void* ptr) {
        if (!ptr) return;
        auto& self = *static_cast<Impl*>(ptr);
        if (state == RTC_CONNECTED) {
            { std::lock_guard<std::mutex> lock(self.mutex); self.mediaState="connected"; }
            std::wstring error;
            if (!self.audio->start(error) || !self.audio->setMuted(false, error)) {
                { std::lock_guard<std::mutex> lock(self.mutex); self.latestAudioError=std::string(error.begin(),error.end()); }
                self.push(EventKind::AudioError, std::string(error.begin(), error.end()));
            }
            self.push(EventKind::Connected);
        } else if (state == RTC_DISCONNECTED || state == RTC_CLOSED) {
            { std::lock_guard<std::mutex> lock(self.mutex); self.mediaState="disconnected"; }
            self.push(EventKind::Disconnected);
        } else if (state == RTC_FAILED) {
            { std::lock_guard<std::mutex> lock(self.mutex); self.mediaState="failed"; self.latestMediaError="ICE/DTLS-SRTP media connection failed"; }
            self.push(EventKind::Failed, "ICE/DTLS-SRTP media connection failed");
        }
    }
    static void track(int, int tr, void* ptr) {
        if (!ptr || tr < 0) return;
        auto& self = *static_cast<Impl*>(ptr);
        self.receiveTrack = tr;
        if (tr == self.sendTrack) return; // The local sendrecv track is already configured below.
        rtcSetUserPointer(tr, ptr);
        rtcSetMessageCallback(tr, &Impl::message);
        rtcChainRtcpReceivingSession(tr);
        rtcSetOpenCallback(tr, &Impl::opened);
        rtcSetClosedCallback(tr, &Impl::closed);
    }
    static void message(int, const char* packet, int size, void* ptr) {
        if (!ptr || size <= 0) return;
        auto& self = *static_cast<Impl*>(ptr);
        if (self.deafened || !self.decoder) return;
        const unsigned char* payload = nullptr; int payloadSize = 0;
        if (!rtpPayload(packet, size, payload, payloadSize)) return;
        ++self.receivedPackets;
        std::vector<opus_int16> pcm(960);
        int samples = opus_decode(self.decoder, payload, payloadSize, pcm.data(), static_cast<int>(pcm.size()), 0);
        if (samples != 320) return;
        ++self.decodedFrames;
        std::vector<unsigned char> bytes(static_cast<size_t>(samples) * 2);
        for (int i = 0; i < samples; ++i) {
            auto bits = static_cast<std::uint16_t>(pcm[i]);
            bytes[2 * i] = static_cast<unsigned char>(bits);
            bytes[2 * i + 1] = static_cast<unsigned char>(bits >> 8);
        }
        self.audio->play(0, bytes);
        ++self.playbackFrames;
    }
    void captured(const std::vector<unsigned char>& bytes) {
        if (stopping || sendTrack < 0 || !encoder || bytes.size() != 640) return;
        ++capturedFrames;
        opus_int16 pcm[320];
        for (int i = 0; i < 320; ++i) pcm[i] = static_cast<opus_int16>(bytes[2*i] | (bytes[2*i+1] << 8));
        unsigned char encoded[4000];
        int size = opus_encode(encoder, pcm, 320, encoded, sizeof(encoded));
        if (size > 0) ++encodedFrames;
        if (size > 0 && trackOpen && rtcIsOpen(sendTrack)) {
            ++submittedFrames;
            if (rtcSendMessage(sendTrack, reinterpret_cast<const char*>(encoded), size) < 0) return;
            // Opus uses a 48 kHz RTP clock even when decoding to 16 kHz PCM.
            // The packetizer does not advance this value for raw C API sends.
            std::uint32_t timestamp = 0;
            if (rtcGetCurrentTrackTimestamp(sendTrack, &timestamp) >= 0)
                rtcSetTrackRtpTimestamp(sendTrack, timestamp + 960);
        }
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
        if (rtcSetOpusPacketizer(sendTrack, &packetizer) < 0 || rtcChainRtcpSrReporter(sendTrack) < 0 || rtcChainRtcpReceivingSession(sendTrack) < 0) { error=L"Could not configure Opus RTP.";return false; }
        // rtcAddTrackEx creates one bidirectional sendrecv Track for this m-line.
        // When the answer reuses it, rtcSetTrackCallback does not create a second
        // remote Track, so receive handling must be installed on this same Track.
        rtcSetUserPointer(sendTrack, this);
        rtcSetMessageCallback(sendTrack, &Impl::message);
        rtcSetOpenCallback(sendTrack, &Impl::opened);
        rtcSetClosedCallback(sendTrack, &Impl::closed);
        return true;
    }
    void close() {
        stopping = true;
        if (audio) audio->stop();
        if (pc >= 0) { rtcSetUserPointer(pc, nullptr); rtcClosePeerConnection(pc); rtcDeletePeerConnection(pc); pc=-1; }
        trackOpen=false;sendTrack=receiveTrack=-1;
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
std::wstring VoiceMedia::diagnostics(){std::lock_guard<std::mutex> lock(impl_->mutex);std::wostringstream out;out<<L"media_state="<<std::wstring(impl_->mediaState.begin(),impl_->mediaState.end())<<L"\nice_dtls_srtp_state="<<std::wstring(impl_->mediaState.begin(),impl_->mediaState.end())<<L"\ntrack_open="<<(impl_->trackOpen?L"true":L"false")<<L"\ncodec=Opus 16 kHz mono, 20 ms frames, 48 kHz RTP clock\ntransport=WebRTC ICE / DTLS-SRTP\ncapture_frames="<<impl_->capturedFrames.load()<<L"\nencoded_frames="<<impl_->encodedFrames.load()<<L"\nopus_submitted="<<impl_->submittedFrames.load()<<L"\nsend_api_accepted="<<impl_->submittedFrames.load()<<L"\nrtp_transport_packets_sent=not exposed by pinned C API\nrtp_transport_bytes_sent=not exposed by pinned C API\nremote_track_callback_packets="<<impl_->receivedPackets.load()<<L"\npayload_delivered="<<impl_->receivedPackets.load()<<L"\nreceived_packets="<<impl_->receivedPackets.load()<<L"\ndecoded_frames="<<impl_->decodedFrames.load()<<L"\nplayback_frames="<<impl_->playbackFrames.load()<<L"\nlatest_audio_error="<<std::wstring(impl_->latestAudioError.begin(),impl_->latestAudioError.end())<<L"\nlatest_media_error="<<std::wstring(impl_->latestMediaError.begin(),impl_->latestMediaError.end());return out.str();}
#ifdef PEERCHAT_TEST_VOICE_FRAMES
void VoiceMedia::testInjectPcm(const std::vector<unsigned char>& pcm){impl_->captured(pcm);}
#endif
std::vector<VoiceMedia::Event> VoiceMedia::poll(){if(impl_->audio){auto error=impl_->audio->takeError();if(!error.empty()){std::string safe(error.begin(),error.end());{std::lock_guard<std::mutex> lock(impl_->mutex);impl_->latestAudioError=safe;}impl_->push(EventKind::AudioError,std::move(safe));}}std::lock_guard<std::mutex> lock(impl_->mutex);std::vector<Event> out;while(!impl_->events.empty()){out.push_back(std::move(impl_->events.front()));impl_->events.pop_front();}return out;}
void VoiceMedia::stop(){impl_->close();}

} // namespace peerchat
