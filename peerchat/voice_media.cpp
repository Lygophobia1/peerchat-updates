#define RTC_STATIC
#include <rtc/rtc.h>
#include <opus/opus.h>
#include "voice_media.hpp"
#include "audio.hpp"
#include "voice_apm_capture.hpp"
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
std::atomic<std::uint64_t> NextMediaSessionId{1};
const char* pcStateName(rtcState state) {
    switch(state){case RTC_NEW:return "new";case RTC_CONNECTING:return "connecting";case RTC_CONNECTED:return "connected";case RTC_DISCONNECTED:return "disconnected";case RTC_FAILED:return "failed";case RTC_CLOSED:return "closed";}return "unknown";
}
const char* iceStateName(rtcIceState state) {
    switch(state){case RTC_ICE_NEW:return "new";case RTC_ICE_CHECKING:return "checking";case RTC_ICE_CONNECTED:return "connected";case RTC_ICE_COMPLETED:return "completed";case RTC_ICE_FAILED:return "failed";case RTC_ICE_DISCONNECTED:return "disconnected";case RTC_ICE_CLOSED:return "closed";}return "unknown";
}
const char* gatheringStateName(rtcGatheringState state) {
    switch(state){case RTC_GATHERING_NEW:return "new";case RTC_GATHERING_INPROGRESS:return "in-progress";case RTC_GATHERING_COMPLETE:return "complete";}return "unknown";
}
const char* signalingStateName(rtcSignalingState state) {
    switch(state){case RTC_SIGNALING_STABLE:return "stable";case RTC_SIGNALING_HAVE_LOCAL_OFFER:return "have-local-offer";case RTC_SIGNALING_HAVE_REMOTE_OFFER:return "have-remote-offer";case RTC_SIGNALING_HAVE_LOCAL_PRANSWER:return "have-local-pranswer";case RTC_SIGNALING_HAVE_REMOTE_PRANSWER:return "have-remote-pranswer";}return "unknown";
}
std::string candidateSummary(const std::string& candidate) {
    std::istringstream input(candidate);std::vector<std::string> parts;std::string part;while(input>>part)parts.push_back(part);
    std::string type="unknown",addressClass="unknown";for(size_t i=0;i+1<parts.size();++i)if(parts[i]=="typ")type=parts[i+1];
    if(parts.size()>4){const auto& address=parts[4];if(address.find(':')!=std::string::npos)addressClass="ipv6";else if(address.rfind("10.",0)==0||address.rfind("192.168.",0)==0||address.rfind("172.",0)==0)addressClass="private-ipv4";else addressClass="public-ipv4";}
    return "type="+type+" address_class="+addressClass;
}
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
    std::mutex captureMutex;
    std::deque<Event> events;
    std::unique_ptr<Audio> audio;
    VoiceApmCapture apm;
    ProcessingOptions processing;
    std::atomic<std::uint64_t> unprocessedFrames{0};
    std::atomic<bool> apmWarningSent{false};
#ifdef PEERCHAT_TEST_VOICE_FRAMES
    std::atomic<bool> failApmNextFrame{false};
#endif
    OpusEncoder* encoder = nullptr;
    OpusDecoder* decoder = nullptr;
    int pc = -1, sendTrack = -1, receiveTrack = -1;
    std::atomic<bool> stopping{false}, deafened{false}, trackOpen{false};
    std::atomic<std::uint64_t> capturedFrames{0}, encodedFrames{0}, submittedFrames{0};
    std::atomic<std::uint64_t> peerMediaPackets{0},peerMediaBytes{0},trackCallbackPackets{0},receivedPackets{0},decodedFrames{0},playbackFrames{0};
    std::uint64_t mediaSessionId=0,networkGeneration=0,localCandidatesGenerated=0,remoteCandidatesReceived=0;
    std::string networkFingerprint,mediaState="created",iceState="new",gatheringState="new",signalingState="stable",latestAudioError="none",latestMediaError="none";
    std::deque<std::string> localCandidateSummaries,remoteCandidateSummaries;
    int inputDevice=-1,outputDevice=-1;
    float inputGain=1.0f,outputGain=1.0f;
    MediaIceConfiguration ice;

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
        if (ptr && cand) {auto& self=*static_cast<Impl*>(ptr);if(self.ice.policy==MediaIceConfiguration::Policy::RelayOnly&&!MediaIceConfiguration::relayCandidate(cand))return;{std::lock_guard<std::mutex> lock(self.mutex);++self.localCandidatesGenerated;if(self.localCandidateSummaries.size()>=16)self.localCandidateSummaries.pop_front();self.localCandidateSummaries.push_back(candidateSummary(cand));}self.push(EventKind::Candidate, cand, mid ? mid : "0");}
    }
    static void opened(int, void* ptr) { if (ptr) static_cast<Impl*>(ptr)->trackOpen=true; }
    static void closed(int, void* ptr) { if (ptr) static_cast<Impl*>(ptr)->trackOpen=false; }

    static void state(int, rtcState state, void* ptr) {
        if (!ptr) return;
        auto& self = *static_cast<Impl*>(ptr);
        {std::lock_guard<std::mutex> lock(self.mutex);self.mediaState=pcStateName(state);}
        if (state == RTC_CONNECTED) {
            { std::lock_guard<std::mutex> lock(self.mutex); self.mediaState="connected"; }
            // Device lifecycle is processed by the owning node, never a library callback.
            self.push(EventKind::Connected);
        } else if (state == RTC_DISCONNECTED || state == RTC_CLOSED) {
            { std::lock_guard<std::mutex> lock(self.mutex); self.mediaState="disconnected"; }
            self.push(EventKind::Disconnected);
        } else if (state == RTC_FAILED) {
            { std::lock_guard<std::mutex> lock(self.mutex); self.mediaState="failed"; self.latestMediaError="ICE/DTLS-SRTP media connection failed"; }
            self.push(EventKind::Failed, "ICE/DTLS-SRTP media connection failed");
        }
    }
    static void iceStateChanged(int, rtcIceState state, void* ptr){if(ptr){auto& self=*static_cast<Impl*>(ptr);std::lock_guard<std::mutex> lock(self.mutex);self.iceState=iceStateName(state);}}
    static void gathering(int, rtcGatheringState state, void* ptr){if(ptr){auto& self=*static_cast<Impl*>(ptr);std::lock_guard<std::mutex> lock(self.mutex);self.gatheringState=gatheringStateName(state);}}
    static void signaling(int, rtcSignalingState state, void* ptr){if(ptr){auto& self=*static_cast<Impl*>(ptr);std::lock_guard<std::mutex> lock(self.mutex);self.signalingState=signalingStateName(state);}}
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
        ++self.trackCallbackPackets;
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
        std::lock_guard<std::mutex> captureLock(captureMutex);
        if (stopping || sendTrack < 0 || !encoder || bytes.size() != 640) return;
        ++capturedFrames;
        opus_int16 pcm[320];
        for (int i = 0; i < 320; ++i) pcm[i] = static_cast<opus_int16>(bytes[2*i] | (bytes[2*i+1] << 8));
#ifdef PEERCHAT_TEST_VOICE_FRAMES
        if (failApmNextFrame.exchange(false)) apm.bypassForTest();
#endif
        if (processing.enabled) {
            if (!apm.processFrame(pcm) && !apmWarningSent.exchange(true))
                push(EventKind::AudioProcessingError, "Microphone processing unavailable; raw PCM bypass active");
        } else ++unprocessedFrames;
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
        deafened=false;trackOpen=false;
        capturedFrames=0;encodedFrames=0;submittedFrames=0;
        peerMediaPackets=0;peerMediaBytes=0;trackCallbackPackets=0;receivedPackets=0;decodedFrames=0;playbackFrames=0;
        {std::lock_guard<std::mutex> lock(mutex);mediaSessionId=NextMediaSessionId++;localCandidatesGenerated=remoteCandidatesReceived=0;localCandidateSummaries.clear();remoteCandidateSummaries.clear();mediaState="created";iceState="new";gatheringState="new";signalingState="stable";latestAudioError=latestMediaError="none";}
        int opusError = OPUS_OK;
        encoder = opus_encoder_create(16000, 1, OPUS_APPLICATION_VOIP, &opusError);
        decoder = opus_decoder_create(16000, 1, &opusError);
        if (!encoder || !decoder || opusError != OPUS_OK) { error = L"Could not initialize the Opus voice codec."; return false; }
        opus_encoder_ctl(encoder, OPUS_SET_BITRATE(24000));
        opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
        opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(1));
        apmWarningSent = false;
        unprocessedFrames = 0;
        if (processing.enabled && !apm.open(processing.noiseSuppression, processing.automaticGain) && !apmWarningSent.exchange(true))
            push(EventKind::AudioProcessingError, "Microphone processing unavailable; raw PCM bypass active");
        audio = std::make_unique<Audio>();
        audio->setDevices(inputDevice,outputDevice);
        audio->setVolumes(inputGain,outputGain);
        audio->setCaptureHandler([this](const std::vector<unsigned char>& frame){ captured(frame); });
        const auto now=static_cast<std::int64_t>(std::time(nullptr));
        if(!ice.valid(now) && ice.policy!=MediaIceConfiguration::Policy::DirectTestOnly){error=L"TURN media credentials are unavailable or expiring. Messaging remains connected.";return false;}
        rtcConfiguration config{};
        std::string turnUri;
        const char* iceServers[1]{};
        if(ice.policy==MediaIceConfiguration::Policy::RelayOnly){turnUri=ice.turnUdpUri();iceServers[0]=turnUri.c_str();config.iceServers=iceServers;config.iceServersCount=1;}
        config.certificateType = RTC_CERTIFICATE_ECDSA;
        config.iceTransportPolicy = ice.policy==MediaIceConfiguration::Policy::RelayOnly?RTC_TRANSPORT_POLICY_RELAY:RTC_TRANSPORT_POLICY_ALL;
        config.disableAutoNegotiation = true;
        config.forceMediaTransport = true;
        pc = rtcCreatePeerConnection(&config);
        if (pc < 0) { error = L"Could not create the secure WebRTC media connection."; return false; }
        rtcSetUserPointer(pc, this);
        rtcSetLocalDescriptionCallback(pc, &Impl::description);
        rtcSetLocalCandidateCallback(pc, &Impl::candidate);
        rtcSetStateChangeCallback(pc, &Impl::state);
        rtcSetIceStateChangeCallback(pc, &Impl::iceStateChanged);
        rtcSetGatheringStateChangeCallback(pc, &Impl::gathering);
        rtcSetSignalingStateChangeCallback(pc, &Impl::signaling);
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
        // C API tracks have an independent registry and must be deleted explicitly.
        // Detach callbacks before releasing audio/codecs or their owning instance.
        if(pc>=0){rtcSetStateChangeCallback(pc,nullptr);rtcSetIceStateChangeCallback(pc,nullptr);rtcSetGatheringStateChangeCallback(pc,nullptr);rtcSetSignalingStateChangeCallback(pc,nullptr);rtcSetTrackCallback(pc,nullptr);rtcSetLocalDescriptionCallback(pc,nullptr);rtcSetLocalCandidateCallback(pc,nullptr);rtcSetMediaInterceptorCallback(pc,nullptr);rtcSetUserPointer(pc,nullptr);}
        for(int track : {sendTrack,receiveTrack}) if(track>=0){rtcSetMessageCallback(track,nullptr);rtcSetOpenCallback(track,nullptr);rtcSetClosedCallback(track,nullptr);rtcSetUserPointer(track,nullptr);}
        if (audio) audio->stop();
        apm.close();
        if(receiveTrack>=0&&receiveTrack!=sendTrack)rtcDeleteTrack(receiveTrack);
        if(sendTrack>=0)rtcDeleteTrack(sendTrack);
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

VoiceMedia::VoiceMedia(MediaIceConfiguration ice,int inputDevice,int outputDevice,std::uint64_t networkGeneration,std::string networkFingerprint):VoiceMedia(std::move(ice),inputDevice,outputDevice,networkGeneration,std::move(networkFingerprint),ProcessingOptions{}) {}
VoiceMedia::VoiceMedia(MediaIceConfiguration ice,int inputDevice,int outputDevice,std::uint64_t networkGeneration,std::string networkFingerprint,ProcessingOptions processing):impl_(std::make_unique<Impl>()) { impl_->ice=std::move(ice);impl_->inputDevice=inputDevice;impl_->outputDevice=outputDevice;impl_->networkGeneration=networkGeneration;impl_->networkFingerprint=std::move(networkFingerprint);impl_->processing=processing;rtcInitLogger(RTC_LOG_NONE, nullptr); }
VoiceMedia::~VoiceMedia(){ stop(); }
bool VoiceMedia::startOffer(std::wstring& error){stop();if(!impl_->create(error))return false;return rtcSetLocalDescription(impl_->pc,"offer")>=0;}
bool VoiceMedia::startAnswer(const std::string& offer,std::wstring& error){stop();if(!impl_->create(error))return false;if(rtcSetRemoteDescription(impl_->pc,offer.c_str(),"offer")<0||rtcSetLocalDescription(impl_->pc,"answer")<0){error=L"The remote voice offer was invalid.";return false;}return true;}
bool VoiceMedia::setAnswer(const std::string& answer,std::wstring& error){if(impl_->pc<0||rtcSetRemoteDescription(impl_->pc,answer.c_str(),"answer")<0){error=L"The remote voice answer was invalid.";return false;}return true;}
bool VoiceMedia::addCandidate(const std::string& candidate,const std::string& mid,std::wstring& error){if(impl_->ice.policy==MediaIceConfiguration::Policy::RelayOnly&&!MediaIceConfiguration::relayCandidate(candidate)){error=L"Direct ICE candidates are disabled by the TURN relay-only media policy.";return false;}if(impl_->pc<0||rtcAddRemoteCandidate(impl_->pc,candidate.c_str(),mid.c_str())<0){error=L"The remote ICE candidate was invalid.";return false;}{std::lock_guard<std::mutex> lock(impl_->mutex);++impl_->remoteCandidatesReceived;if(impl_->remoteCandidateSummaries.size()>=16)impl_->remoteCandidateSummaries.pop_front();impl_->remoteCandidateSummaries.push_back(candidateSummary(candidate));}return true;}
bool VoiceMedia::setMuted(bool muted,std::wstring& error){return impl_->audio&&impl_->audio->setMuted(muted,error);}
void VoiceMedia::setDeafened(bool value){impl_->deafened=value;}
void VoiceMedia::setVolumes(float inputGain,float outputGain){impl_->inputGain=inputGain;impl_->outputGain=outputGain;if(impl_->audio)impl_->audio->setVolumes(inputGain,outputGain);}
MediaTelemetry VoiceMedia::telemetry(){
    auto result=impl_->audio?impl_->audio->telemetry():MediaTelemetry{};
    if(impl_->deafened)result.outputLevel=0.0f;
    if(impl_->stopping)return {};
    return result;
}
std::wstring VoiceMedia::diagnostics(){std::lock_guard<std::mutex> lock(impl_->mutex);char local[512]{},remote[512]{};std::wstring pair=L"unavailable";if(impl_->pc>=0&&rtcGetSelectedCandidatePair(impl_->pc,local,sizeof(local),remote,sizeof(remote))>=0)pair=std::wstring(local,local+std::strlen(local))+L" -> "+std::wstring(remote,remote+std::strlen(remote));auto summaries=[&](const std::deque<std::string>& values){std::wstring result;if(values.empty())return std::wstring(L"none");for(const auto& value:values){if(!result.empty())result+=L" | ";result+=std::wstring(value.begin(),value.end());}return result;};std::wostringstream out;out<<L"media_session_id="<<impl_->mediaSessionId<<L"\nfresh_peer_connection=true\nnetwork_generation_at_media_creation="<<impl_->networkGeneration<<L"\nnetwork_fingerprint_at_media_creation="<<std::wstring(impl_->networkFingerprint.begin(),impl_->networkFingerprint.end())<<L"\nmedia_ice_generation="<<impl_->ice.generation<<L"\nmedia_ice_policy="<<(impl_->ice.policy==MediaIceConfiguration::Policy::RelayOnly?L"relay-only":L"test-direct")<<L"\nmedia_state="<<std::wstring(impl_->mediaState.begin(),impl_->mediaState.end())<<L"\nice_dtls_srtp_state="<<std::wstring(impl_->mediaState.begin(),impl_->mediaState.end())<<L"\nice_connection_state="<<std::wstring(impl_->iceState.begin(),impl_->iceState.end())<<L"\nice_gathering_state="<<std::wstring(impl_->gatheringState.begin(),impl_->gatheringState.end())<<L"\nsignaling_state="<<std::wstring(impl_->signalingState.begin(),impl_->signalingState.end())<<L"\ntrack_open="<<(impl_->trackOpen?L"true":L"false")<<L"\nselected_ice_pair="<<pair<<L"\nlocal_ice_candidates_generated="<<impl_->localCandidatesGenerated<<L"\nlocal_ice_candidate_summaries="<<summaries(impl_->localCandidateSummaries)<<L"\nremote_ice_candidates_added="<<impl_->remoteCandidatesReceived<<L"\nremote_ice_candidate_summaries="<<summaries(impl_->remoteCandidateSummaries)<<L"\nice_servers="<<(impl_->ice.policy==MediaIceConfiguration::Policy::RelayOnly?L"TURN configured; transport=udp; relay-only":L"test direct ICE")<<L"\ncodec=Opus 16 kHz mono, 20 ms frames, 48 kHz RTP clock\ntransport=WebRTC ICE / DTLS-SRTP\ncapture_frames="<<impl_->capturedFrames.load()<<L"\nencoded_frames="<<impl_->encodedFrames.load()<<L"\nopus_submitted="<<impl_->submittedFrames.load()<<L"\nsend_api_accepted="<<impl_->submittedFrames.load()<<L"\nrtp_transport_packets_sent=not exposed by pinned C API\nrtp_transport_bytes_sent=not exposed by pinned C API\npeer_connection_incoming_media_packets=not exposed safely by pinned C API\npeer_connection_incoming_media_bytes=not exposed safely by pinned C API\nremote_track_callback_packets="<<impl_->trackCallbackPackets.load()<<L"\npayload_delivered="<<impl_->receivedPackets.load()<<L"\nreceived_packets="<<impl_->receivedPackets.load()<<L"\ndecoded_frames="<<impl_->decodedFrames.load()<<L"\nplayback_frames="<<impl_->playbackFrames.load()<<L"\nlatest_audio_error="<<std::wstring(impl_->latestAudioError.begin(),impl_->latestAudioError.end())<<L"\nlatest_media_error="<<std::wstring(impl_->latestMediaError.begin(),impl_->latestMediaError.end())<<L"\napm_enabled="<<(impl_->processing.enabled?L"true":L"false")<<L"\nnoise_suppression="<<(impl_->processing.enabled&&impl_->processing.noiseSuppression?L"true":L"false")<<L"\nautomatic_gain_control="<<(impl_->processing.enabled&&impl_->processing.automaticGain?L"true":L"false")<<L"\necho_cancellation=false\napm_capture_active="<<(impl_->apm.active()?L"true":L"false")<<L"\napm_sample_rate_hz=16000\napm_channels=1\napm_capture_10ms_calls="<<impl_->apm.captureCalls()<<L"\napm_processed_20ms_frames="<<impl_->apm.processedFrames()<<L"\napm_raw_bypass_frames="<<impl_->apm.bypassedFrames()+impl_->unprocessedFrames.load()<<L"\napm_level_frames="<<impl_->apm.measuredFrames()<<L"\napm_pre_rms="<<(impl_->apm.measuredFrames()?std::to_wstring(impl_->apm.preRms()):L"Unavailable")<<L"\napm_pre_peak="<<(impl_->apm.measuredFrames()?std::to_wstring(impl_->apm.prePeak()):L"Unavailable")<<L"\napm_post_rms="<<(impl_->apm.measuredFrames()?std::to_wstring(impl_->apm.postRms()):L"Unavailable")<<L"\napm_post_peak="<<(impl_->apm.measuredFrames()?std::to_wstring(impl_->apm.postPeak()):L"Unavailable")<<L"\napm_failure_code="<<impl_->apm.failure();if(impl_->audio)out<<L"\n"<<impl_->audio->diagnostics();return out.str();}
VoiceMedia::Route VoiceMedia::route(){
    std::lock_guard<std::mutex> lock(impl_->mutex);
    char local[512]{},remote[512]{};
    if(impl_->pc<0||rtcGetSelectedCandidatePair(impl_->pc,local,sizeof(local),remote,sizeof(remote))<0)return Route::Unknown;
    std::string pair=std::string(local)+" "+remote;
    if(pair.find(" typ relay")!=std::string::npos)return Route::TurnRelay;
    if(pair.find(" typ srflx")!=std::string::npos||pair.find(" typ prflx")!=std::string::npos)return Route::NatTraversedIce;
    return pair.find(" typ host")!=std::string::npos?Route::DirectLanIce:Route::Unknown;
}
#ifdef PEERCHAT_TEST_VOICE_FRAMES
void VoiceMedia::testInjectPcm(const std::vector<unsigned char>& pcm){impl_->captured(pcm);}
void VoiceMedia::testFailApmNextFrame(){impl_->failApmNextFrame=true;}
#endif
std::vector<VoiceMedia::Event> VoiceMedia::poll(){
    std::vector<Event> out;
    {std::lock_guard<std::mutex> lock(impl_->mutex);while(!impl_->events.empty()){out.push_back(std::move(impl_->events.front()));impl_->events.pop_front();}}
    std::wstring error;
    for(const auto& event:out)if(event.kind==EventKind::Connected&&impl_->audio)impl_->audio->start(error);
    if(impl_->audio){auto workerError=impl_->audio->takeError();if(!workerError.empty())error=std::move(workerError);}
    if(!error.empty()){
        std::string safe(error.begin(),error.end());
        {std::lock_guard<std::mutex> lock(impl_->mutex);impl_->latestAudioError=safe;}
        out.push_back({EventKind::AudioError,std::move(safe),{}});
    }
    return out;
}
void VoiceMedia::stop(){impl_->close();}

} // namespace peerchat
