#pragma once

#define RTC_STATIC
#include <rtc/rtc.h>
#include "windows_media/peerchat_screen_media.h"
#include "media_ice_configuration.hpp"
#include "screen_share_manager.hpp"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace peerchat {

// Owns one H.264 WebRTC media generation. Logical share state, peer trust,
// signaling authorization and recovery deadlines belong to ScreenShareManager.
class ScreenShareMedia {
public:
    enum class Route : std::uint32_t { Unknown, DirectHostIce, NatTraversedIce, TurnRelay };
    enum class EventKind { Description, Candidate, Connected, Disconnected, Failed };
    // This is deliberately about the media-engine failure boundary, not a
    // recovery decision.  ScreenShareManager/Node decide whether a failure is
    // eligible for a new media generation.
    enum class FailureDomain { None, LocalCapture, LocalEncoder, IceTurnTransport,
                               DtlsSecurity, Rtp, RemoteDecoder, Presentation };
    struct Event { EventKind kind; std::string value, detail; FailureDomain failure = FailureDomain::None; };
    struct Options {
        bool sender = false;
        std::uint32_t sourceKind = 0;
        std::uint64_t sourceHandle = 0;
        std::uint64_t presentationWindow = 0;
        std::uint32_t width = 0, height = 0, fps = 60, bitrate = 20000000;
        bool captureCursor = true;
        screen::Backend backend = screen::Backend::NativeWindows;
        MediaIceConfiguration ice;
    };
    struct Statistics {
        std::uint64_t encodedSubmitted = 0, rtpReceived = 0;
        std::uint64_t rawRtpCallbacks = 0, rtpParseFailures = 0, remoteTrackCallbacks = 0;
        std::uint64_t accessUnitsDelivered = 0, lostPackets = 0;
        std::uint64_t rtpSequenceGap = 0, rtpOutOfOrder = 0, rtpDuplicate = 0;
        std::uint64_t rtpTimestampChangeBeforeMarker = 0, rtpIncompleteFuA = 0;
        std::uint64_t rtpAbandonedAccessUnit = 0, rtpStaleGenerationPacket = 0;
        std::uint64_t decoded = 0, presented = 0;
        std::uint64_t captured = 0, encoderSubmitted = 0, encoded = 0;
        std::uint64_t droppedBeforeEncode = 0, droppedEncodedQueue = 0, droppedDecodeQueue = 0;
        std::uint64_t senderSps = 0, senderPps = 0, senderIdr = 0, senderNonIdr = 0;
        std::uint64_t receiverSps = 0, receiverPps = 0, receiverIdr = 0, receiverNonIdr = 0;
        std::uint64_t keyframeRequests = 0;
        std::uint64_t captureCallbacks = 0, lastCaptureCallbackAgeMs = 0;
        std::uint32_t captureCallbackStage = 0, encoderWorkerStage = 0;
        std::uint64_t captureCallbackStageElapsedMs = 0, encoderWorkerStageElapsedMs = 0;
        std::uint32_t pendingCaptureFrame = 0, encodedQueueDepth = 0, encoderInputReady = 0;
        std::uint32_t encoderStartupStage = PC_SCREEN_START_NONE, encoderStartupFailureOperation = 0;
        std::int32_t encoderStartupFailureHresult = 0;
        bool encoderFailed = false;
        std::uint32_t encoderFailedOperation = 0;
        std::int32_t encoderLastHresult = 0, encoderProcessInputHresult = 0, encoderProcessOutputHresult = 0;
        std::uint64_t encoderProcessInputFailures = 0, encoderProcessOutputFailures = 0;
        std::uint64_t encoderStreamChanges = 0, encoderOutputTypeRenegotiations = 0;
        std::int32_t encoderOutputTypeRenegotiationHresult = 0;
        std::uint32_t encoderOutputTypeWidth = 0, encoderOutputTypeHeight = 0, encoderOutputTypeFps = 0;
        bool encoderCbrSupported = false, encoderCbrApplied = false;
        bool encoderLowLatencySupported = false, encoderLowLatencyApplied = false;
        bool encoderForceKeyframeSupported = false, encoderForceKeyframeApplied = false;
        std::wstring encoderName, adapterName, encoderOutputTypeName;
        std::uint64_t adapterDriverVersion = 0;
        std::uint32_t decoderQueueDepth = 0, decoderWorkerStage = 0;
        std::uint64_t decoderWorkerStageElapsedMs = 0;
        std::uint32_t largestDecoderAccessUnitBytes = 0;
        std::int32_t decoderLastProcessInput = 0, decoderLastProcessOutput = 0;
        std::uint64_t decoderProcessInputFailures = 0, decoderProcessOutputFailures = 0;
        std::uint64_t decoderStreamChanges = 0, decoderNeedMoreInput = 0, decoderOutputSuccesses = 0;
        std::uint64_t encodedBytesSubmitted = 0, mediaBytesReceived = 0;
        double captureFps = 0, encodeFps = 0, transmittedFps = 0;
        double receivedFps = 0, renderedFps = 0, outgoingMbps = 0, incomingMbps = 0;
        double averageDecodeMs = 0;
        bool ratesAvailable = false, decodeLatencyAvailable = false;
        std::uint32_t width = 0, height = 0;
        bool connected = false, failed = false;
        bool trackOpen = false;
    };

    explicit ScreenShareMedia(Options options) : options_(options) { rtcInitLogger(RTC_LOG_NONE, nullptr); }
    ~ScreenShareMedia() { stop(); }
    ScreenShareMedia(const ScreenShareMedia&) = delete;
    ScreenShareMedia& operator=(const ScreenShareMedia&) = delete;
    static bool available() {
        Api api;
        if (!api.open()) return false;
        pc_screen_capabilities capability{};
        capability.structure_size = sizeof(capability);
        const bool okay = api.queryCapabilities(&capability) != 0 &&
            capability.capture_supported != 0 && capability.d3d11_supported != 0 &&
            ((capability.hardware_encoder_codecs | capability.software_encoder_codecs) & PC_SCREEN_H264) != 0 &&
            (capability.decoder_codecs & PC_SCREEN_H264) != 0;
        api.close();
        return okay;
    }
    static bool supportsBackend(screen::Backend backend, bool sender) {
        Api api;
        if (!api.open()) return false;
        pc_screen_capabilities capability{};
        capability.structure_size = sizeof(capability);
        const bool queried = api.queryCapabilities(&capability) != 0;
        api.close();
        if (!queried || !capability.d3d11_supported || !(capability.decoder_codecs & PC_SCREEN_H264)) return false;
        if (!sender) return true;
        return capability.capture_supported &&
            ((backend == screen::Backend::WebRtcCompatibility ?
                capability.software_encoder_codecs : capability.hardware_encoder_codecs) & PC_SCREEN_H264) != 0;
    }

    bool startOffer(std::string& error) {
        stop();
        if (!create(error)) return false;
        if (rtcSetLocalDescription(pc_, "offer") < 0) { error = "Could not create secure video offer"; stop(); return false; }
        return true;
    }
    bool startAnswer(const std::string& offer, std::string& error) {
        stop();
        if (!create(error)) return false;
        if (rtcSetRemoteDescription(pc_, offer.c_str(), "offer") < 0 ||
            rtcSetLocalDescription(pc_, "answer") < 0) {
            error = "Could not accept secure video offer"; stop(); return false;
        }
        return true;
    }
    bool setAnswer(const std::string& answer) {
        return pc_ >= 0 && rtcSetRemoteDescription(pc_, answer.c_str(), "answer") >= 0;
    }
    bool addCandidate(const std::string& candidate, const std::string& mid) {
        if (options_.ice.policy == MediaIceConfiguration::Policy::RelayOnly &&
            !MediaIceConfiguration::relayCandidate(candidate)) return false;
        return pc_ >= 0 && rtcAddRemoteCandidate(pc_, candidate.c_str(), mid.c_str()) >= 0;
    }

    std::vector<Event> poll() {
        std::vector<Event> result;
        {
            std::lock_guard lock(mutex_);
            while (!events_.empty()) {
                result.push_back(std::move(events_.front()));
                events_.pop_front();
            }
        }
        if (options_.sender && encoder_ && trackOpen_ && !stopping_) {
            if (encodedScratch_.empty()) encodedScratch_.resize(1024 * 1024);
            for (unsigned count = 0; count < 4; ++count) {
                pc_screen_encoded_frame info{};
                info.structure_size = sizeof(info);
                int popped = api_.encoderPop(encoder_, encodedScratch_.data(),
                    static_cast<std::uint32_t>(encodedScratch_.size()), &info);
                if (popped == -1 && info.length <= 8 * 1024 * 1024) {
                    encodedScratch_.resize(info.length);
                    popped = api_.encoderPop(encoder_, encodedScratch_.data(),
                        static_cast<std::uint32_t>(encodedScratch_.size()), &info);
                }
                if (popped == 0) break;
                if (popped < 0 || info.length == 0 || info.length > encodedScratch_.size()) {
                    failed_ = true;
                    if (!localFailureReported_) {
                        localFailureReported_ = true;
                        push(EventKind::Failed, "Windows screen encoder failed", {}, encoderFailureDomain());
                    }
                    break;
                }
                if (rtcSendMessage(track_, reinterpret_cast<const char*>(encodedScratch_.data()),
                        static_cast<int>(info.length)) < 0) break;
                ++encodedSubmitted_;
                encodedBytesSubmitted_ += info.length;
                encodedWidth_ = info.width;
                encodedHeight_ = info.height;
                std::uint32_t timestamp = 0;
                if (rtcGetCurrentTrackTimestamp(track_, &timestamp) >= 0)
                    rtcSetTrackRtpTimestamp(track_, timestamp +
                        static_cast<std::uint32_t>(90000u / options_.fps));
            }
        }
        if (!options_.sender && decoder_ && !localFailureReported_) {
            pc_screen_decoder_status status{};
            status.structure_size = sizeof(status);
            if (api_.decoderStatus(decoder_, &status) && status.failed) {
                failed_ = true;
                localFailureReported_ = true;
                const auto domain = status.worker_stage >= PC_SCREEN_DECODER_PRESENT_SETUP
                    ? FailureDomain::Presentation : FailureDomain::RemoteDecoder;
                push(EventKind::Failed, domain == FailureDomain::Presentation
                    ? "Windows screen presentation failed" : "Windows H.264 screen decoder failed", {}, domain);
            }
        }
        if (options_.sender && encoder_ && !localFailureReported_ &&
            std::chrono::steady_clock::now() >= startupDeadline_) {
            const auto data = statistics();
            if (!data.captured && !data.encoderFailed) {
                failed_ = true;
                localFailureReported_ = true;
                startupWatchdogFailed_ = true;
                push(EventKind::Failed, "Screen capture startup timed out before the first frame callback", {},
                    FailureDomain::LocalCapture);
            }
        }
        return result;
    }

    Statistics statistics() const {
        Statistics value;
        value.encodedSubmitted = encodedSubmitted_;
        value.rtpReceived = rtpReceived_;
        value.rawRtpCallbacks = rawRtpCallbacks_;
        value.rtpParseFailures = rtpParseFailures_;
        value.remoteTrackCallbacks = remoteTrackCallbacks_;
        value.accessUnitsDelivered = accessUnitsDelivered_;
        value.lostPackets = lostPackets_;
        value.rtpSequenceGap = rtpSequenceGap_;
        value.rtpOutOfOrder = rtpOutOfOrder_;
        value.rtpDuplicate = rtpDuplicate_;
        value.rtpTimestampChangeBeforeMarker = rtpTimestampChangeBeforeMarker_;
        value.rtpIncompleteFuA = rtpIncompleteFuA_;
        value.rtpAbandonedAccessUnit = rtpAbandonedAccessUnit_;
        value.rtpStaleGenerationPacket = rtpStaleGenerationPacket_;
        value.encodedBytesSubmitted = encodedBytesSubmitted_;
        value.mediaBytesReceived = mediaBytesReceived_;
        value.connected = connected_;
        value.failed = failed_;
        value.trackOpen = trackOpen_;
        value.width = encodedWidth_;
        value.height = encodedHeight_;
        if (encoder_) {
            pc_screen_encoder_status status{};
            status.structure_size = sizeof(status);
            if (api_.encoderStatus(encoder_, &status)) {
                value.captured = status.captured_frames;
                value.encoderSubmitted = status.submitted_to_encoder;
                value.encoded = status.encoded_frames;
                value.droppedBeforeEncode = status.dropped_before_encode;
                value.droppedEncodedQueue = status.dropped_encoded_queue;
                value.captureCallbacks = status.capture_callback_count;
                value.lastCaptureCallbackAgeMs = status.last_capture_callback_age_ms;
                value.captureCallbackStage = status.capture_callback_stage;
                value.captureCallbackStageElapsedMs = status.capture_callback_stage_elapsed_ms;
                value.encoderWorkerStage = status.encoder_worker_stage;
                value.encoderWorkerStageElapsedMs = status.encoder_worker_stage_elapsed_ms;
                value.pendingCaptureFrame = status.pending_capture_frame;
                value.encodedQueueDepth = status.encoded_queue_depth;
                value.encoderInputReady = status.encoder_input_ready;
                value.encoderStartupStage = status.startup_stage;
                value.encoderStartupFailureOperation = status.startup_failure_operation;
                value.encoderStartupFailureHresult = status.startup_failure_hresult;
                value.encoderFailed = status.failed != 0;
                value.encoderFailedOperation = status.failed_operation;
                value.encoderLastHresult = status.last_hresult;
                value.encoderProcessInputHresult = status.process_input_hresult;
                value.encoderProcessOutputHresult = status.process_output_hresult;
                value.encoderProcessInputFailures = status.process_input_failures;
                value.encoderProcessOutputFailures = status.process_output_failures;
                value.encoderStreamChanges = status.stream_change_count;
                value.encoderOutputTypeRenegotiations = status.output_type_renegotiation_count;
                value.encoderOutputTypeRenegotiationHresult = status.output_type_renegotiation_hresult;
                value.encoderOutputTypeWidth = status.output_type_width;
                value.encoderOutputTypeHeight = status.output_type_height;
                value.encoderOutputTypeFps = status.output_type_frames_per_second;
                value.encoderCbrSupported = status.cbr_control_supported != 0;
                value.encoderCbrApplied = status.cbr_control_applied != 0;
                value.encoderLowLatencySupported = status.low_latency_control_supported != 0;
                value.encoderLowLatencyApplied = status.low_latency_control_applied != 0;
                value.encoderForceKeyframeSupported = status.force_keyframe_control_supported != 0;
                value.encoderForceKeyframeApplied = status.force_keyframe_control_applied != 0;
                value.encoderName = status.encoder_name;
                value.adapterName = status.adapter_name;
                value.encoderOutputTypeName = status.output_type_name;
                value.adapterDriverVersion = status.driver_version;
                value.keyframeRequests = status.keyframe_requests;
                value.senderSps = status.sps_nals;value.senderPps = status.pps_nals;
                value.senderIdr = status.idr_nals;value.senderNonIdr = status.non_idr_nals;
                value.failed = value.failed || value.encoderFailed;
            }
        }
        if (decoder_) {
            pc_screen_decoder_status status{};
            status.structure_size = sizeof(status);
            if (api_.decoderStatus(decoder_, &status)) {
                value.decoded = status.decoded_frames;
                value.presented = status.presented_frames;
                value.failed = value.failed || status.failed != 0;
                value.width = status.decoded_width;
                value.height = status.decoded_height;
                value.droppedDecodeQueue = status.dropped_decode_queue;
                value.decoderQueueDepth = status.queue_depth;
                value.decoderWorkerStage = status.worker_stage;
                value.decoderWorkerStageElapsedMs = status.worker_stage_elapsed_ms;
                value.largestDecoderAccessUnitBytes = status.largest_access_unit_bytes;
                value.decoderLastProcessInput = status.last_process_input_hresult;
                value.decoderLastProcessOutput = status.last_process_output_hresult;
                value.decoderProcessInputFailures = status.process_input_failures;
                value.decoderProcessOutputFailures = status.process_output_failures;
                value.decoderStreamChanges = status.stream_change_count;
                value.decoderNeedMoreInput = status.need_more_input_count;
                value.decoderOutputSuccesses = status.output_success_count;
                if (status.decode_operations) {
                    value.decodeLatencyAvailable = true;
                    value.averageDecodeMs = double(status.decode_latency_microseconds_total) /
                        double(status.decode_operations) / 1000.0;
                }
            }
        }
        value.receiverSps=receiverSps_;value.receiverPps=receiverPps_;value.receiverIdr=receiverIdr_;value.receiverNonIdr=receiverNonIdr_;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard sampleLock(sampleMutex_);
        if (samples_.empty() || now - samples_.back().time >= std::chrono::milliseconds(500)) {
            samples_.push_back({now, value.captured, value.encoded, value.encodedSubmitted,
                value.accessUnitsDelivered, value.presented, value.encodedBytesSubmitted,
                value.mediaBytesReceived});
            while (samples_.size() > 2 && now - samples_.front().time > std::chrono::seconds(2))
                samples_.pop_front();
        }
        if (samples_.size() >= 2) {
            const auto& first = samples_.front();
            const auto& last = samples_.back();
            const double seconds = std::chrono::duration<double>(last.time - first.time).count();
            if (seconds >= 0.5) {
                value.ratesAvailable = true;
                value.captureFps = double(last.captured - first.captured) / seconds;
                value.encodeFps = double(last.encoded - first.encoded) / seconds;
                value.transmittedFps = double(last.submitted - first.submitted) / seconds;
                value.receivedFps = double(last.received - first.received) / seconds;
                value.renderedFps = double(last.presented - first.presented) / seconds;
                value.outgoingMbps = double(last.sentBytes - first.sentBytes) * 8.0 / seconds / 1000000.0;
                value.incomingMbps = double(last.receivedBytes - first.receivedBytes) * 8.0 / seconds / 1000000.0;
            }
        }
        return value;
    }

    std::wstring diagnostics() const {
        auto data = statistics();
        std::wstring firstAccessUnits = L"none";
        {
            std::lock_guard lock(mutex_);
            if (!firstAccessUnitNals_.empty()) {
                firstAccessUnits.clear();
                for (size_t i = 0; i < firstAccessUnitNals_.size(); ++i) {
                    if (i) firstAccessUnits += L" | ";
                    const auto& item = firstAccessUnitNals_[i];
                    firstAccessUnits.append(item.begin(), item.end());
                }
            }
        }
        char local[512]{}, remote[512]{};
        std::wstring pair = L"unavailable";
        if (pc_ >= 0 && rtcGetSelectedCandidatePair(pc_, local, sizeof(local),
                remote, sizeof(remote)) >= 0)
            pair = std::wstring(local, local + std::strlen(local)) + L" -> " +
                   std::wstring(remote, remote + std::strlen(remote));
        rtcDirection direction = RTC_DIRECTION_UNKNOWN;
        if (track_ >= 0) rtcGetTrackDirection(track_, &direction);
        char trackMid[64]{}, remoteMid[64]{};
        if (track_ >= 0) rtcGetTrackMid(track_, trackMid, sizeof(trackMid));
        if (receiveTrack_ >= 0) rtcGetTrackMid(receiveTrack_, remoteMid, sizeof(remoteMid));
        std::wostringstream out;
        out << std::fixed << std::setprecision(2)
            << L"media_connected=" << (data.connected ? L"true" : L"false")
            << L"\ntrack_open=" << (data.trackOpen ? L"true" : L"false")
            << L"\nlocal_track_direction=" <<
                (direction == RTC_DIRECTION_SENDONLY ? L"sendonly" :
                 direction == RTC_DIRECTION_RECVONLY ? L"recvonly" :
                 direction == RTC_DIRECTION_SENDRECV ? L"sendrecv" :
                 direction == RTC_DIRECTION_INACTIVE ? L"inactive" : L"unknown")
            << L"\nlocal_track_mid=" << (trackMid[0] ? trackMid : "unavailable")
            << L"\nremote_track_mid=" << (remoteMid[0] ? remoteMid : "unavailable")
            << L"\nreceiver_callback_owner=" << (options_.sender ? L"sender" :
                receiveTrack_ >= 0 ? L"remote track" : L"precreated recvonly track")
            << L"\nconfigured_h264_payload_type=102"
            << L"\ncodec=H.264\nsecurity=DTLS-SRTP"
            << L"\nselected_ice_pair=" << pair
            << L"\nrequested_fps=" << (options_.sender ? std::to_wstring(options_.fps) : L"Unavailable (sender-only)")
            << L"\nrequested_bitrate_bits_per_second=" << (options_.sender ? std::to_wstring(options_.bitrate) : L"Unavailable (sender-only)")
            << L"\ndecoder_configured_fps=" << (!options_.sender ? std::to_wstring(options_.fps) : L"Unavailable (sender-only)")
            << L"\nmedia_rtt_ms=Unavailable (pinned libdatachannel C API)"
            << L"\nmedia_packet_loss_percent=Unavailable (pinned libdatachannel C API)"
            << L"\nmedia_jitter_ms=Unavailable (pinned libdatachannel C API)"
            << L"\ntransport_bytes_sent=Unavailable (pinned libdatachannel C API)"
            << L"\ntransport_bytes_received=Unavailable (pinned libdatachannel C API)"
            << L"\nnegotiated_codec=" << (data.trackOpen ? L"H.264" : L"Unavailable (track not open)")
            << L"\nnegotiated_fps=Unavailable (not exposed by pinned C API)"
            << L"\neffective_frame_width=" << data.width
            << L"\neffective_frame_height=" << data.height
            << L"\ncapture_cursor=" << (options_.captureCursor ? L"true" : L"false")
            << L"\nencoded_submit_api_accepted=" << data.encodedSubmitted
            << L"\nencoded_bytes_submit_api_accepted=" << data.encodedBytesSubmitted
            << L"\nmedia_rtp_bytes_received=" << data.mediaBytesReceived
            << L"\ncaptured_frames=" << data.captured
            << L"\nencoder_input_frames=" << data.encoderSubmitted
            << L"\nencoded_frames=" << data.encoded
            << L"\ndropped_before_encode=" << data.droppedBeforeEncode
            << L"\ndropped_encoded_queue=" << data.droppedEncodedQueue
            << L"\ncapture_callback_count=" << data.captureCallbacks
            << L"\nlast_capture_callback_age_ms=" << (data.captureCallbacks ?
                std::to_wstring(data.lastCaptureCallbackAgeMs) : L"Unavailable (no callback yet)")
            << L"\ncapture_callback_stage=" << data.captureCallbackStage
            << L"\ncapture_callback_stage_name=" <<
                (data.captureCallbackStage == PC_SCREEN_CAPTURE_NEXT ? L"acquiring WGC frame" :
                 data.captureCallbackStage == PC_SCREEN_CAPTURE_COPY ? L"D3D11 CopyResource" :
                 data.captureCallbackStage == PC_SCREEN_CAPTURE_QUEUE ? L"queueing capture frame" :
                 data.captureCallbackStage == PC_SCREEN_CAPTURE_CREATE_TEXTURE ? L"creating D3D11 capture texture" :
                 data.captureCallbackStage == PC_SCREEN_CAPTURE_GPU_LOCK ? L"waiting for GPU lock before copy" :
                 L"idle")
            << L"\ncapture_callback_stage_elapsed_ms=" << data.captureCallbackStageElapsedMs
            << L"\nencoder_worker_stage=" << data.encoderWorkerStage
            << L"\nencoder_worker_stage_name=" <<
                (data.encoderWorkerStage == PC_SCREEN_ENCODER_INITIALIZING ? L"initializing" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_WAITING ? L"waiting for input or MFT event" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_CONVERT ? L"D3D11 video conversion" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_INPUT ? L"Media Foundation ProcessInput" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_OUTPUT ? L"Media Foundation ProcessOutput" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_STOPPING ? L"stopping" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_STOPPED ? L"stopped" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_CREATE_SURFACES ? L"creating D3D11 video surfaces" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_GPU_LOCK ? L"waiting for GPU lock before conversion" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_VIDEO_BLT ? L"D3D11 VideoProcessorBlt" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_CREATE_SAMPLE ? L"creating Media Foundation input sample" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_CREATE_COPY_TEXTURE ? L"creating D3D11 capture copy" :
                 data.encoderWorkerStage == PC_SCREEN_ENCODER_COPY_CAPTURE ? L"D3D11 CopyResource on encoder worker" :
                 L"unavailable")
            << L"\nencoder_worker_stage_elapsed_ms=" << data.encoderWorkerStageElapsedMs
            << L"\npending_capture_frame=" << data.pendingCaptureFrame
            << L"\nencoded_queue_depth=" << data.encodedQueueDepth
            << L"\nencoder_input_ready=" << data.encoderInputReady
            << L"\nscreen_start_stage=" << data.encoderStartupStage
            << L"\nscreen_start_stage_name=" << startupStageName(data.encoderStartupStage)
            << L"\nscreen_start_command_accepted=true"
            << L"\nscreen_start_video_track_created=" << (track_ >= 0 ? L"true" : L"false")
            << L"\nscreen_start_video_track_open=" << (data.trackOpen ? L"true" : L"false")
            << L"\nscreen_start_failure_domain=" << (startupWatchdogFailed_ ? L"local_capture" : startupFailureDomain(data.encoderStartupFailureOperation))
            << L"\nscreen_start_failure_hresult=0x" << std::hex << static_cast<std::uint32_t>(data.encoderStartupFailureHresult) << std::dec
            << L"\nscreen_start_failure_reason=" << (startupWatchdogFailed_ ? L"capture startup watchdog expired before first callback" : startupFailureReason(data.encoderStartupFailureOperation))
            << L"\nencoder_failed=" << (data.encoderFailed ? L"true" : L"false")
            << L"\nencoder_failed_operation=" << data.encoderFailedOperation
            << L"\nencoder_last_hresult=0x" << std::hex << static_cast<std::uint32_t>(data.encoderLastHresult)
            << L"\nencoder_process_input_hresult=0x" << static_cast<std::uint32_t>(data.encoderProcessInputHresult)
            << L"\nencoder_process_output_hresult=0x" << static_cast<std::uint32_t>(data.encoderProcessOutputHresult) << std::dec
            << L"\nencoder_process_input_failures=" << data.encoderProcessInputFailures
            << L"\nencoder_process_output_failures=" << data.encoderProcessOutputFailures
            << L"\nencoder_stream_change_count=" << data.encoderStreamChanges
            << L"\nencoder_output_type_renegotiation_count=" << data.encoderOutputTypeRenegotiations
            << L"\nencoder_output_type_renegotiation_hresult=0x" << std::hex << static_cast<std::uint32_t>(data.encoderOutputTypeRenegotiationHresult) << std::dec
            << L"\nencoder_output_type=" << (data.encoderOutputTypeName.empty() ? L"Unavailable" : data.encoderOutputTypeName)
            << L"\nencoder_output_type_width=" << data.encoderOutputTypeWidth
            << L"\nencoder_output_type_height=" << data.encoderOutputTypeHeight
            << L"\nencoder_output_type_fps=" << data.encoderOutputTypeFps
            << L"\nencoder_mft=" << (data.encoderName.empty() ? L"Unavailable" : data.encoderName)
            << L"\nencoder_adapter=" << (data.adapterName.empty() ? L"Unavailable" : data.adapterName)
            << L"\nencoder_driver_version=" << data.adapterDriverVersion
            << L"\nencoder_cbr_control=" << (data.encoderCbrSupported ? (data.encoderCbrApplied ? L"applied" : L"unsupported/rejected") : L"unavailable")
            << L"\nencoder_low_latency_control=" << (data.encoderLowLatencySupported ? (data.encoderLowLatencyApplied ? L"applied" : L"unsupported/rejected") : L"unavailable")
            << L"\nencoder_force_keyframe_control=" << (data.encoderForceKeyframeSupported ? (data.encoderForceKeyframeApplied ? L"applied" : L"unsupported/rejected") : L"unavailable")
            << L"\nh264_sender_sps=" << data.senderSps << L"\nh264_sender_pps=" << data.senderPps
            << L"\nh264_sender_idr=" << data.senderIdr << L"\nh264_sender_non_idr=" << data.senderNonIdr
            << L"\nh264_receiver_sps=" << data.receiverSps << L"\nh264_receiver_pps=" << data.receiverPps
            << L"\nh264_receiver_idr=" << data.receiverIdr << L"\nh264_receiver_non_idr=" << data.receiverNonIdr
            << L"\nh264_first_access_units=" << firstAccessUnits
            << L"\nkeyframe_requests=" << data.keyframeRequests
            << L"\ndropped_decode_queue=" << data.droppedDecodeQueue
            << L"\ndecoder_queue_depth=" << data.decoderQueueDepth
            << L"\ndecoder_worker_stage=" << data.decoderWorkerStage
            << L"\ndecoder_worker_stage_elapsed_ms=" << data.decoderWorkerStageElapsedMs
            << L"\nlargest_decoder_access_unit_bytes=" << data.largestDecoderAccessUnitBytes
            << L"\ndecoder_process_input_hresult=0x" << std::hex << static_cast<std::uint32_t>(data.decoderLastProcessInput)
            << L"\ndecoder_process_output_hresult=0x" << static_cast<std::uint32_t>(data.decoderLastProcessOutput) << std::dec
            << L"\ndecoder_process_input_failures=" << data.decoderProcessInputFailures
            << L"\ndecoder_process_output_failures=" << data.decoderProcessOutputFailures
            << L"\ndecoder_stream_change_count=" << data.decoderStreamChanges
            << L"\ndecoder_need_more_input_count=" << data.decoderNeedMoreInput
            << L"\ndecoder_output_success_count=" << data.decoderOutputSuccesses
            << L"\ndropped_decode_queue_semantics=complete access units refused by full 8-frame decoder queue"
            << L"\ncomplete_access_units_delivered_semantics=complete access units accepted into decoder queue"
            << L"\ntransport_dropped_frames=Unavailable (pinned libdatachannel C API)"
            << L"\npresentation_dropped_frames=Unavailable (terminal presentation failure only)"
            << L"\ncapture_fps=" << (data.ratesAvailable && options_.sender ? std::to_wstring(data.captureFps) : L"Unavailable")
            << L"\nencode_fps=" << (data.ratesAvailable && options_.sender ? std::to_wstring(data.encodeFps) : L"Unavailable")
            << L"\ntransmitted_fps_send_api=" << (data.ratesAvailable && options_.sender ? std::to_wstring(data.transmittedFps) : L"Unavailable")
            << L"\nreceived_fps_access_units=" << (data.ratesAvailable && !options_.sender ? std::to_wstring(data.receivedFps) : L"Unavailable")
            << L"\nrendered_fps=" << (data.ratesAvailable && !options_.sender ? std::to_wstring(data.renderedFps) : L"Unavailable")
            << L"\neffective_encoded_mbps_send_api=" << (data.ratesAvailable && options_.sender ? std::to_wstring(data.outgoingMbps) : L"Unavailable")
            << L"\neffective_received_mbps_rtp_callback=" << (data.ratesAvailable && !options_.sender ? std::to_wstring(data.incomingMbps) : L"Unavailable")
            << L"\ndecode_latency_average_ms=" << (data.decodeLatencyAvailable ? std::to_wstring(data.averageDecodeMs) : L"Unavailable")
            << L"\nencode_latency_ms=Unavailable"
            << L"\nmedia_stack=libdatachannel WebRTC-compatible ICE / DTLS-SRTP / H.264 RTP"
            << L"\nencoder=" << (options_.sender ?
                (options_.backend == screen::Backend::WebRtcCompatibility ?
                    L"Windows Media Foundation software H.264 MFT" :
                    L"Windows Media Foundation hardware H.264 MFT") : L"Unavailable (receiver-only)")
            << L"\ndecoder=" << (!options_.sender ? L"Windows Media Foundation D3D11-aware MFT (hardware acceleration not confirmed)" : L"Unavailable (sender-only)")
            << L"\nrtp_transport_packets_sent=not exposed by pinned C API"
            << L"\nremote_track_callback_count=" << data.remoteTrackCallbacks
            << L"\nremote_rtp_callback_packets=" << data.rawRtpCallbacks
            << L"\nvalid_rtp_packets_received=" << data.rtpReceived
            << L"\nrtp_parse_failures=" << data.rtpParseFailures
            << L"\nrtp_packetized=" << L"Unavailable (pinned C API)"
            << L"\nrtp_handed_to_transport=" << L"Unavailable (pinned C API)"
            << L"\ncomplete_access_units_delivered=" << data.accessUnitsDelivered
            << L"\nrtp_reassembly_discontinuities=" << data.lostPackets
            << L"\nrtp_sequence_gap=" << data.rtpSequenceGap
            << L"\nrtp_out_of_order=" << data.rtpOutOfOrder
            << L"\nrtp_duplicate=" << data.rtpDuplicate
            << L"\nrtp_timestamp_change_before_marker=" << data.rtpTimestampChangeBeforeMarker
            << L"\nrtp_incomplete_fu_a=" << data.rtpIncompleteFuA
            << L"\nrtp_abandoned_access_unit=" << data.rtpAbandonedAccessUnit
            << L"\nrtp_stale_generation_packet=" << data.rtpStaleGenerationPacket
            << L"\ndecoded_frames=" << data.decoded
            << L"\npresented_frames=" << data.presented
            << L"\nmedia_failed=" << (data.failed ? L"true" : L"false");
        return out.str();
    }
    Route route() const {
        if (pc_ < 0 || !connected_) return Route::Unknown;
        char local[512]{}, remote[512]{};
        if (rtcGetSelectedCandidatePair(pc_, local, sizeof(local),remote,sizeof(remote)) < 0)
            return Route::Unknown;
        const std::string left(local), right(remote);
        if (left.find(" typ relay") != std::string::npos ||
            right.find(" typ relay") != std::string::npos) return Route::TurnRelay;
        if (left.find(" typ srflx") != std::string::npos ||
            right.find(" typ srflx") != std::string::npos ||
            left.find(" typ prflx") != std::string::npos ||
            right.find(" typ prflx") != std::string::npos) return Route::NatTraversedIce;
        if (left.find(" typ host") != std::string::npos &&
            right.find(" typ host") != std::string::npos) return Route::DirectHostIce;
        return Route::Unknown;
    }

    std::wstring startupDiagnostics() const {
        const auto data = statistics();
        std::wostringstream out;
        out << L"screen_start_stage=" << data.encoderStartupStage
            << L"\nscreen_start_stage_name=" << startupStageName(data.encoderStartupStage)
            << L"\nscreen_start_failure_domain=" << (startupWatchdogFailed_ ? L"local_capture" : startupFailureDomain(data.encoderStartupFailureOperation))
            << L"\nscreen_start_failure_hresult=0x" << std::hex
            << static_cast<std::uint32_t>(data.encoderStartupFailureHresult) << std::dec
            << L"\nscreen_start_failure_reason=" << (startupWatchdogFailed_ ? L"capture startup watchdog expired before first callback" : startupFailureReason(data.encoderStartupFailureOperation));
        return out.str();
    }

    void stop(const std::function<void(std::uint32_t)>& progress = {}) {
        auto stage = [&](std::uint32_t value) { if (progress) progress(value); };
        stage(1); // Retire media callbacks before any owned object is destroyed.
        stopping_ = true;
        trackOpen_ = false;
        connected_ = false;
        stage(2); // Detach peer-connection callbacks.
        if (pc_ >= 0) {
            rtcSetStateChangeCallback(pc_, nullptr);
            rtcSetTrackCallback(pc_, nullptr);
            rtcSetLocalDescriptionCallback(pc_, nullptr);
            rtcSetLocalCandidateCallback(pc_, nullptr);
            rtcSetUserPointer(pc_, nullptr);
        }
        stage(3); // Detach track callbacks.
        int retiredReceiveTrack = -1;
        {
            std::lock_guard callbacks(callbackRegistrationMutex_);
            retiredReceiveTrack = receiveTrack_;
            for (int track : {track_, retiredReceiveTrack}) if (track >= 0) {
                rtcSetMessageCallback(track, nullptr);
                rtcSetOpenCallback(track, nullptr);
                rtcSetClosedCallback(track, nullptr);
                rtcSetUserPointer(track, nullptr);
            }
        }
        // An RTP callback that entered before detachment must finish before its
        // decoder and callback-owned buffers can be destroyed.
        stage(4); // Drain an already-entered RTP callback.
        { std::lock_guard callbackDrain(mutex_); }
        stage(5); // Wait for native capture/encoder teardown.
        if (encoder_) { api_.encoderClose(encoder_); encoder_ = nullptr; }
        stage(6); // Wait for native decoder/presentation teardown.
        if (decoder_) { api_.decoderClose(decoder_); decoder_ = nullptr; }
        stage(7); // Delete detached tracks.
        if (retiredReceiveTrack >= 0 && retiredReceiveTrack != track_) rtcDeleteTrack(retiredReceiveTrack);
        if (track_ >= 0) rtcDeleteTrack(track_);
        stage(8); // Close and delete the ICE/DTLS-SRTP peer connection.
        if (pc_ >= 0) { rtcClosePeerConnection(pc_); rtcDeletePeerConnection(pc_); }
        pc_ = track_ = receiveTrack_ = -1;
        stage(9); // Unload the Windows media adapter after all workers exit.
        api_.close();
        {
            std::lock_guard lock(mutex_);
            events_.clear();
            rtpFrame_.clear();
            rtpActive_ = false;
        }
        connected_ = false;
        stage(10);
    }

private:
    struct Api {
        HMODULE module = nullptr;
        decltype(&pc_screen_query_capabilities) queryCapabilities = nullptr;
        decltype(&pc_screen_encoder_open) encoderOpen = nullptr;
        decltype(&pc_screen_encoder_pop) encoderPop = nullptr;
        decltype(&pc_screen_encoder_request_keyframe) encoderRequestKeyframe = nullptr;
        decltype(&pc_screen_encoder_status_get) encoderStatus = nullptr;
        decltype(&pc_screen_encoder_close) encoderClose = nullptr;
        decltype(&pc_screen_decoder_open) decoderOpen = nullptr;
        decltype(&pc_screen_decoder_push) decoderPush = nullptr;
        decltype(&pc_screen_decoder_status_get) decoderStatus = nullptr;
        decltype(&pc_screen_decoder_close) decoderClose = nullptr;
        bool open() {
            wchar_t path[32768]{};
            const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
            if (!length || length >= 32768) return false;
            std::wstring file(path, length);
            const auto slash = file.find_last_of(L"\\/");
            if (slash == std::wstring::npos) return false;
            file.resize(slash + 1);
            file += L"PeerChat.ScreenMedia.dll";
            module = LoadLibraryExW(file.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!module) return false;
            wchar_t actual[32768]{};
            const DWORD actualLength = GetModuleFileNameW(module, actual, 32768);
            if (!actualLength || actualLength >= 32768 || _wcsicmp(actual, file.c_str()) != 0) {
                close();
                return false;
            }
#define PC_SCREEN_IMPORT(field, name) do { \
            auto address = GetProcAddress(module, name); \
            static_assert(sizeof(field) == sizeof(address), "Function pointer size mismatch"); \
            if (!address) { close(); return false; } \
            std::memcpy(&field, &address, sizeof(field)); \
        } while (false);
            PC_SCREEN_IMPORT(queryCapabilities, "pc_screen_query_capabilities")
            PC_SCREEN_IMPORT(encoderOpen, "pc_screen_encoder_open")
            PC_SCREEN_IMPORT(encoderPop, "pc_screen_encoder_pop")
            PC_SCREEN_IMPORT(encoderRequestKeyframe, "pc_screen_encoder_request_keyframe")
            PC_SCREEN_IMPORT(encoderStatus, "pc_screen_encoder_status_get")
            PC_SCREEN_IMPORT(encoderClose, "pc_screen_encoder_close")
            PC_SCREEN_IMPORT(decoderOpen, "pc_screen_decoder_open")
            PC_SCREEN_IMPORT(decoderPush, "pc_screen_decoder_push")
            PC_SCREEN_IMPORT(decoderStatus, "pc_screen_decoder_status_get")
            PC_SCREEN_IMPORT(decoderClose, "pc_screen_decoder_close")
#undef PC_SCREEN_IMPORT
            return true;
        }
        void close() {
            if (module) FreeLibrary(module);
            module = nullptr;
            queryCapabilities = nullptr;
            encoderOpen = nullptr; encoderPop = nullptr; encoderRequestKeyframe = nullptr;
            encoderStatus = nullptr; encoderClose = nullptr;
            decoderOpen = nullptr; decoderPush = nullptr;
            decoderStatus = nullptr; decoderClose = nullptr;
        }
    };

    static void description(int, const char* sdp, const char* type, void* context) {
        if (context && sdp && type) static_cast<ScreenShareMedia*>(context)->push(
            EventKind::Description, sdp, type);
    }
    static void candidate(int, const char* value, const char* mid, void* context) {
        if (!context || !value) return;
        auto& self = *static_cast<ScreenShareMedia*>(context);
        if (self.options_.ice.policy == MediaIceConfiguration::Policy::RelayOnly &&
            !MediaIceConfiguration::relayCandidate(value)) return;
        self.push(EventKind::Candidate, value, mid ? mid : "0");
    }
    static void state(int, rtcState value, void* context) {
        if (!context) return;
        auto& self = *static_cast<ScreenShareMedia*>(context);
        if (self.stopping_) return;
        if (value == RTC_CONNECTED) {
            self.connected_ = true;
            self.push(EventKind::Connected);
        } else if (value == RTC_FAILED) {
            self.failed_ = true;
            self.push(EventKind::Failed, "ICE/TURN/DTLS screen media transport failed", {},
                FailureDomain::IceTurnTransport);
        } else if (value == RTC_DISCONNECTED || value == RTC_CLOSED) {
            self.connected_ = false;
            self.push(EventKind::Disconnected);
        }
    }
    static void opened(int, void* context) {
        if (!context) return;
        auto& self = *static_cast<ScreenShareMedia*>(context);
        if (self.stopping_) return;
        self.trackOpen_ = true;
        // Capture starts before SDP/ICE completes. An independent bootstrap is
        // required when this particular sender track becomes usable.
        if (self.options_.sender) self.requestEncoderKeyframe();
    }
    static void closed(int, void* context) {
        if (context) static_cast<ScreenShareMedia*>(context)->trackOpen_ = false;
    }
    static void remoteTrack(int, int track, void* context) {
        if (!context || track < 0) return;
        auto& self = *static_cast<ScreenShareMedia*>(context);
        std::lock_guard callbacks(self.callbackRegistrationMutex_);
        if(self.stopping_ || (self.receiveTrack_>=0 && self.receiveTrack_!=track))return;
        ++self.remoteTrackCallbacks_;
        self.receiveTrack_ = track;
        if (track == self.track_) return;
        rtcSetUserPointer(track, context);
        rtcSetMessageCallback(track, &ScreenShareMedia::message);
        rtcChainRtcpReceivingSession(track);
        rtcSetOpenCallback(track, &ScreenShareMedia::opened);
        rtcSetClosedCallback(track, &ScreenShareMedia::closed);
    }

    static bool parseRtp(const char* packet, int size, const std::uint8_t*& payload,
        size_t& length, std::uint16_t& sequence, std::uint32_t& timestamp, bool& marker) {
        if (!packet || size < 12) return false;
        auto* bytes = reinterpret_cast<const std::uint8_t*>(packet);
        if ((bytes[0] >> 6) != 2) return false;
        size_t offset = 12 + 4 * (bytes[0] & 15);
        if (offset > static_cast<size_t>(size)) return false;
        if (bytes[0] & 16) {
            if (offset + 4 > static_cast<size_t>(size)) return false;
            offset += 4 + 4 * (static_cast<size_t>(bytes[offset + 2]) * 256 + bytes[offset + 3]);
            if (offset > static_cast<size_t>(size)) return false;
        }
        size_t end = static_cast<size_t>(size);
        if (bytes[0] & 32) {
            const auto padding = bytes[end - 1];
            if (!padding || padding > end - offset) return false;
            end -= padding;
        }
        if (end <= offset) return false;
        payload = bytes + offset;
        length = end - offset;
        marker = (bytes[1] & 128) != 0;
        sequence = static_cast<std::uint16_t>((bytes[2] << 8) | bytes[3]);
        timestamp = (std::uint32_t(bytes[4]) << 24) | (std::uint32_t(bytes[5]) << 16) |
            (std::uint32_t(bytes[6]) << 8) | bytes[7];
        return true;
    }

    bool appendNal(const std::uint8_t* data, size_t length) {
        if (!length || rtpFrame_.size() + length + 4 > 8 * 1024 * 1024) return false;
        const auto type = data[0] & 31;
        currentAuNalTypes_.push_back(type);
        switch (type) {
        case 7: ++receiverSps_; ++currentAuSps_; break;
        case 8: ++receiverPps_; ++currentAuPps_; break;
        case 5: ++receiverIdr_; ++currentAuIdr_; break;
        case 1: ++receiverNonIdr_; ++currentAuNonIdr_; break;
        default: break;
        }
        rtpFrame_.insert(rtpFrame_.end(), {0, 0, 0, 1});
        rtpFrame_.insert(rtpFrame_.end(), data, data + length);
        return true;
    }

    struct ReceivedRtpPacket {
        std::uint16_t sequence = 0;
        std::uint32_t timestamp = 0;
        bool marker = false;
        std::vector<std::uint8_t> payload;
    };

    static std::int16_t sequenceDistance(std::uint16_t value, std::uint16_t expected) {
        return static_cast<std::int16_t>(value - expected);
    }

    void resetCurrentAccessUnit(bool abandoned) {
        if (abandoned && !rtpFrame_.empty()) {
            ++lostPackets_;
            ++rtpAbandonedAccessUnit_;
            if (rtpFragmented_) ++rtpIncompleteFuA_;
        }
        rtpFrame_.clear();
        rtpFragmented_ = false;
        rtpActive_ = false;
        currentAuSps_ = currentAuPps_ = currentAuIdr_ = currentAuNonIdr_ = 0;
        currentAuNalTypes_.clear();
        currentAuFirstSequence_ = currentAuLastSequence_ = 0;
    }

    void completeCurrentAccessUnit(const ReceivedRtpPacket& packet) {
        if (!rtpFrame_.empty() && firstAccessUnitNals_.size() < 32) {
            std::ostringstream types;
            for (size_t index = 0; index < currentAuNalTypes_.size(); ++index) {
                if (index) types << ',';
                types << currentAuNalTypes_[index];
            }
            const auto vcl = std::count_if(currentAuNalTypes_.begin(), currentAuNalTypes_.end(),
                [](std::uint8_t type) { return type == 1 || type == 5; });
            firstAccessUnitNals_.push_back("timestamp=" + std::to_string(rtpTimestamp_) +
                ";first_sequence=" + std::to_string(currentAuFirstSequence_) +
                ";last_sequence=" + std::to_string(currentAuLastSequence_) +
                ";marker=" + (packet.marker ? "1" : "0") +
                ";nal_types=" + types.str() + ";bytes=" + std::to_string(rtpFrame_.size()) +
                ";vcl=" + std::to_string(vcl) + ";idr_and_non_idr=" +
                ((currentAuIdr_ && currentAuNonIdr_) ? "true" : "false") +
                ";single_timestamp=true");
        }
        if (!rtpFrame_.empty() && !rtpFragmented_ &&
            api_.decoderPush(decoder_, rtpFrame_.data(),
                static_cast<std::uint32_t>(rtpFrame_.size()),
                static_cast<std::uint64_t>(rtpTimestamp_) * 10000000ull / 90000ull) == 1)
            ++accessUnitsDelivered_;
        resetCurrentAccessUnit(false);
    }

    void processReceivedPacket(const ReceivedRtpPacket& packet) {
        if (!rtpActive_) {
            rtpActive_ = true;
            rtpTimestamp_ = packet.timestamp;
            currentAuFirstSequence_ = packet.sequence;
        } else if (packet.timestamp != rtpTimestamp_) {
            // A timestamp change before the previous marker is a real AU boundary
            // failure, never an invitation to combine pictures in one decoder sample.
            ++rtpTimestampChangeBeforeMarker_;
            resetCurrentAccessUnit(true);
            rtpActive_ = true;
            rtpTimestamp_ = packet.timestamp;
            currentAuFirstSequence_ = packet.sequence;
        }
        currentAuLastSequence_ = packet.sequence;
        const auto* payload = packet.payload.data();
        const auto length = packet.payload.size();
        const auto nalType = payload[0] & 31;
        bool valid = true;
        if (nalType >= 1 && nalType <= 23) {
            valid = appendNal(payload, length);
        } else if (nalType == 24) {
            size_t cursor = 1;
            while (valid && cursor + 2 <= length) {
                const size_t nalSize = (size_t(payload[cursor]) << 8) | payload[cursor + 1];
                cursor += 2;
                valid = nalSize > 0 && cursor + nalSize <= length &&
                    appendNal(payload + cursor, nalSize);
                cursor += nalSize;
            }
            valid = valid && cursor == length;
        } else if (nalType == 28 && length >= 3) {
            const bool start = (payload[1] & 128) != 0;
            const bool end = (payload[1] & 64) != 0;
            if (start) {
                const std::uint8_t header = (payload[0] & 224) | (payload[1] & 31);
                valid = appendNal(&header, 1);
                rtpFragmented_ = valid;
            } else valid = rtpFragmented_;
            if (valid && rtpFrame_.size() + length - 2 <= 8 * 1024 * 1024)
                rtpFrame_.insert(rtpFrame_.end(), payload + 2, payload + length);
            else valid = false;
            if (end) rtpFragmented_ = false;
        } else valid = false;
        if (!valid) {
            resetCurrentAccessUnit(true);
            return;
        }
        if (packet.marker) completeCurrentAccessUnit(packet);
    }

    void drainReceivedPackets() {
        while (sequenceInitialized_) {
            auto next = pendingRtpPackets_.find(expectedSequence_);
            if (next == pendingRtpPackets_.end()) break;
            ReceivedRtpPacket packet = std::move(next->second);
            pendingRtpPackets_.erase(next);
            processReceivedPacket(packet);
            ++expectedSequence_;
        }
    }

    void queueReceivedPacket(ReceivedRtpPacket packet) {
        if (!sequenceInitialized_) {
            sequenceInitialized_ = true;
            expectedSequence_ = packet.sequence;
        }
        const auto distance = sequenceDistance(packet.sequence, expectedSequence_);
        if (distance < 0) {
            // A packet that has already passed the bounded reorder window is stale;
            // retain it as evidence without damaging the current access unit.
            ++rtpOutOfOrder_;
            return;
        }
        if (distance > kRtpReorderWindow) {
            // The missing span exceeded the bounded window. Drop only the incomplete
            // current AU, then resume at this newer packet.
            rtpSequenceGap_ += static_cast<std::uint16_t>(distance);
            resetCurrentAccessUnit(true);
            pendingRtpPackets_.clear();
            expectedSequence_ = packet.sequence;
        } else if (distance > 0) {
            auto [_, inserted] = pendingRtpPackets_.emplace(packet.sequence, std::move(packet));
            if (!inserted) ++rtpDuplicate_;
            else ++rtpOutOfOrder_;
            if (!inserted || pendingRtpPackets_.size() < kRtpReorderWindow) return;
            // Do not wait without bound for one missing RTP packet. This bounded
            // skip is the point at which a gap becomes permanent.
            const auto first = pendingRtpPackets_.begin()->first;
            rtpSequenceGap_ += static_cast<std::uint16_t>(sequenceDistance(first, expectedSequence_));
            resetCurrentAccessUnit(true);
            expectedSequence_ = first;
            drainReceivedPackets();
            return;
        }
        pendingRtpPackets_.emplace(packet.sequence, std::move(packet));
        drainReceivedPackets();
    }

    void received(const char* packet, int size) {
        ++rawRtpCallbacks_;
        if (stopping_) { ++rtpStaleGenerationPacket_; return; }
        if (options_.sender || !decoder_) return;
        const std::uint8_t* payload = nullptr;
        size_t length = 0;
        std::uint16_t sequence = 0;
        std::uint32_t timestamp = 0;
        bool marker = false;
        if (!parseRtp(packet, size, payload, length, sequence, timestamp, marker)) {
            ++rtpParseFailures_;return;
        }
        ++rtpReceived_;
        mediaBytesReceived_ += static_cast<std::uint64_t>(size);
        std::lock_guard lock(mutex_);
        if (stopping_ || !decoder_) { ++rtpStaleGenerationPacket_; return; }
        ReceivedRtpPacket copied;
        copied.sequence = sequence;
        copied.timestamp = timestamp;
        copied.marker = marker;
        copied.payload.assign(payload, payload + length);
        queueReceivedPacket(std::move(copied));
    }
    static void message(int, const char* packet, int size, void* context) {
        if (context) static_cast<ScreenShareMedia*>(context)->received(packet, size);
    }

    FailureDomain encoderFailureDomain() const {
        if (!encoder_ || !api_.encoderStatus) return FailureDomain::LocalEncoder;
        pc_screen_encoder_status status{};
        status.structure_size = sizeof(status);
        if (!api_.encoderStatus(encoder_, &status)) return FailureDomain::LocalEncoder;
        return status.failed_operation == PC_SCREEN_ENCODER_FAILURE_WGC ||
               status.failed_operation == PC_SCREEN_ENCODER_FAILURE_CAPTURE
            ? FailureDomain::LocalCapture : FailureDomain::LocalEncoder;
    }

    static const wchar_t* startupStageName(std::uint32_t stage) {
        switch(stage) {
        case PC_SCREEN_START_SOURCE_VALIDATED:return L"source validated";
        case PC_SCREEN_START_D3D_DEVICE_CREATED:return L"D3D11 device created";
        case PC_SCREEN_START_D3D_BRIDGE_CREATED:return L"Direct3D device bridge created";
        case PC_SCREEN_START_WGC_ITEM_ACQUIRED:return L"WGC item acquired";
        case PC_SCREEN_START_ENCODER_ENUMERATION:return L"encoder enumeration started";
        case PC_SCREEN_START_ENCODER_CREATED:return L"encoder created";
        case PC_SCREEN_START_FRAME_POOL_CREATED:return L"capture frame pool created";
        case PC_SCREEN_START_CAPTURE_SESSION_CREATED:return L"capture session created";
        case PC_SCREEN_START_CALLBACK_REGISTERED:return L"frame callback registered";
        case PC_SCREEN_START_CAPTURE_STARTED:return L"capture started; awaiting first callback";
        case PC_SCREEN_START_FIRST_CAPTURED:return L"first captured frame";
        case PC_SCREEN_START_FIRST_ENCODER_INPUT:return L"first encoder input";
        default:return L"command accepted";
        }
    }
    static const wchar_t* startupFailureDomain(std::uint32_t operation) {
        return operation == PC_SCREEN_ENCODER_FAILURE_WGC || operation == PC_SCREEN_ENCODER_FAILURE_CAPTURE
            ? L"local_capture" : operation ? L"local_encoder" : L"none";
    }
    static const wchar_t* startupFailureReason(std::uint32_t operation) {
        switch(operation) {
        case PC_SCREEN_ENCODER_FAILURE_WGC:return L"Windows Graphics Capture initialization failed";
        case PC_SCREEN_ENCODER_FAILURE_D3D11:return L"D3D11 device initialization failed";
        case PC_SCREEN_ENCODER_FAILURE_ENUMERATE_MFT:return L"H.264 encoder enumeration failed";
        case PC_SCREEN_ENCODER_FAILURE_ACTIVATE_MFT:return L"H.264 encoder activation failed";
        case PC_SCREEN_ENCODER_FAILURE_CAPTURE:return L"screen capture callback failed";
        case PC_SCREEN_ENCODER_FAILURE_MEDIA_TYPE:return L"H.264 encoder media type configuration failed";
        case PC_SCREEN_ENCODER_FAILURE_PROCESS_INPUT:return L"H.264 encoder ProcessInput failed";
        case PC_SCREEN_ENCODER_FAILURE_PROCESS_OUTPUT:return L"H.264 encoder ProcessOutput failed";
        case PC_SCREEN_ENCODER_FAILURE_EXCEPTION:return L"screen-media initialization exception";
        default:return L"none";
        }
    }

    void push(EventKind kind, std::string value = {}, std::string detail = {},
              FailureDomain failure = FailureDomain::None) {
        if (stopping_) return;
        std::lock_guard lock(mutex_);
        if (stopping_) return;
        if (events_.size() == 128) events_.pop_front();
        events_.push_back({kind, std::move(value), std::move(detail), failure});
    }

    void requestEncoderKeyframe() {
        if (!options_.sender || !encoder_ || stopping_) return;
        if (api_.encoderRequestKeyframe(encoder_)) ++senderKeyframeRequests_;
    }


    bool create(std::string& error) {
        stopping_ = false;
        connected_ = failed_ = trackOpen_ = false;
        localFailureReported_ = false;
        startupWatchdogFailed_ = false;
        startupDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        encodedSubmitted_ = rtpReceived_ = accessUnitsDelivered_ = lostPackets_ = 0;
        rawRtpCallbacks_ = rtpParseFailures_ = remoteTrackCallbacks_ = 0;
        rtpSequenceGap_ = rtpOutOfOrder_ = rtpDuplicate_ = 0;
        rtpTimestampChangeBeforeMarker_ = rtpIncompleteFuA_ = rtpAbandonedAccessUnit_ = 0;
        rtpStaleGenerationPacket_ = 0;
        encodedBytesSubmitted_ = mediaBytesReceived_ = 0;
        receiverSps_ = receiverPps_ = receiverIdr_ = receiverNonIdr_ = 0;
        senderKeyframeRequests_ = 0;
        {
            std::lock_guard lock(mutex_);
            rtpFrame_.clear();
            rtpActive_ = rtpFragmented_ = false;
            rtpTimestamp_ = 0;
            expectedSequence_ = 0;
            sequenceInitialized_ = false;
            pendingRtpPackets_.clear();
            currentAuSps_ = currentAuPps_ = currentAuIdr_ = currentAuNonIdr_ = 0;
            currentAuNalTypes_.clear();
            currentAuFirstSequence_ = currentAuLastSequence_ = 0;
            firstAccessUnitNals_.clear();
        }
        { std::lock_guard sampleLock(sampleMutex_); samples_.clear(); }
        encodedWidth_ = encodedHeight_ = 0;
        if (!api_.open()) { error = "Windows screen media adapter is unavailable"; return false; }
        if (options_.sender) {
            pc_screen_encoder_options capture{};
            capture.structure_size = sizeof(capture);
            capture.source_kind = options_.sourceKind;
            capture.source_handle = options_.sourceHandle;
            capture.frames_per_second = options_.fps;
            capture.bitrate_bits_per_second = options_.bitrate;
            capture.capture_cursor = options_.captureCursor ? 1u : 0u;
            capture.backend = options_.backend == screen::Backend::WebRtcCompatibility ?
                PC_SCREEN_BACKEND_WEBRTC_COMPATIBILITY : PC_SCREEN_BACKEND_NATIVE;
            encoder_ = api_.encoderOpen(&capture);
            if (!encoder_) { error = options_.backend == screen::Backend::WebRtcCompatibility ?
                "Windows screen capture or software H.264 encoding failed" :
                "Windows screen capture or H.264 hardware encoding failed"; stop(); return false; }
        } else {
            pc_screen_decoder_options viewer{};
            viewer.structure_size = sizeof(viewer);
            viewer.presentation_window = options_.presentationWindow;
            viewer.width = options_.width;
            viewer.height = options_.height;
            viewer.frames_per_second = options_.fps;
            decoder_ = api_.decoderOpen(&viewer);
            if (!decoder_) { error = "Windows H.264 screen decoder or viewer failed"; stop(); return false; }
        }
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        if (!options_.ice.valid(now) && options_.ice.policy != MediaIceConfiguration::Policy::DirectTestOnly) {
            error = "TURN media credentials are unavailable or expiring. Messaging remains connected.";
            stop();
            return false;
        }
        rtcConfiguration config{};
        std::string turnUri;
        const char* servers[1]{};
        if (options_.ice.policy == MediaIceConfiguration::Policy::RelayOnly) {
            turnUri = options_.ice.turnUdpUri();
            servers[0] = turnUri.c_str();
            config.iceServers = servers;
            config.iceServersCount = 1;
        }
        config.certificateType = RTC_CERTIFICATE_ECDSA;
        config.iceTransportPolicy = options_.ice.policy == MediaIceConfiguration::Policy::RelayOnly ? RTC_TRANSPORT_POLICY_RELAY : RTC_TRANSPORT_POLICY_ALL;
        config.disableAutoNegotiation = true;
        config.forceMediaTransport = true;
        pc_ = rtcCreatePeerConnection(&config);
        if (pc_ < 0) { error = "Could not create secure screen media connection"; stop(); return false; }
        rtcSetUserPointer(pc_, this);
        rtcSetLocalDescriptionCallback(pc_, &ScreenShareMedia::description);
        rtcSetLocalCandidateCallback(pc_, &ScreenShareMedia::candidate);
        rtcSetStateChangeCallback(pc_, &ScreenShareMedia::state);
        rtcSetTrackCallback(pc_, &ScreenShareMedia::remoteTrack);
        std::random_device random;
        rtcTrackInit track{};
        track.direction = options_.sender ? RTC_DIRECTION_SENDONLY : RTC_DIRECTION_RECVONLY;
        track.codec = RTC_CODEC_H264;
        track.payloadType = 102;
        track.ssrc = (static_cast<std::uint32_t>(random()) << 16) ^ random();
        track.mid = "0";
        track.name = "PeerChat screen";
        track.msid = "peerchat-screen";
        track.trackId = "screen";
        track.profile = "profile-level-id=42e01f;packetization-mode=1";
        track_ = rtcAddTrackEx(pc_, &track);
        if (track_ < 0) { error = "Could not create H.264 media track"; stop(); return false; }
        if (options_.sender) {
            rtcPacketizerInit packetizer{};
            packetizer.ssrc = track.ssrc;
            packetizer.cname = "peerchat-screen";
            packetizer.payloadType = 102;
            packetizer.clockRate = 90000;
            packetizer.maxFragmentSize = 1200;
            packetizer.nalSeparator = RTC_NAL_SEPARATOR_START_SEQUENCE;
            if (rtcSetH264Packetizer(track_, &packetizer) < 0 ||
                rtcChainRtcpSrReporter(track_) < 0) {
                error = "Could not configure H.264 RTP packetization";
                stop();
                return false;
            }
        } else rtcChainRtcpReceivingSession(track_);
        rtcSetUserPointer(track_, this);
        rtcSetMessageCallback(track_, &ScreenShareMedia::message);
        rtcSetOpenCallback(track_, &ScreenShareMedia::opened);
        rtcSetClosedCallback(track_, &ScreenShareMedia::closed);
        return true;
    }

    Options options_;
    Api api_;
    pc_screen_encoder* encoder_ = nullptr;
    pc_screen_decoder* decoder_ = nullptr;
    int pc_ = -1, track_ = -1, receiveTrack_ = -1;
    mutable std::mutex mutex_;
    std::mutex callbackRegistrationMutex_;
    std::deque<Event> events_;
    std::vector<std::uint8_t> rtpFrame_;
    std::vector<std::uint8_t> encodedScratch_;
    static constexpr std::int16_t kRtpReorderWindow = 32;
    std::map<std::uint16_t, ReceivedRtpPacket> pendingRtpPackets_;
    std::uint16_t expectedSequence_ = 0;
    std::uint32_t rtpTimestamp_ = 0;
    bool sequenceInitialized_ = false, rtpActive_ = false, rtpFragmented_ = false;
    std::uint64_t currentAuSps_ = 0, currentAuPps_ = 0, currentAuIdr_ = 0, currentAuNonIdr_ = 0;
    std::vector<std::uint8_t> currentAuNalTypes_;
    std::uint16_t currentAuFirstSequence_ = 0, currentAuLastSequence_ = 0;
    std::deque<std::string> firstAccessUnitNals_;
    std::atomic<bool> stopping_{true}, trackOpen_{false}, connected_{false}, failed_{false};
    bool localFailureReported_ = false;
    std::atomic<bool> startupWatchdogFailed_{false};
    std::chrono::steady_clock::time_point startupDeadline_{};
    std::atomic<std::uint64_t> encodedSubmitted_{0}, rtpReceived_{0};
    std::atomic<std::uint64_t> rawRtpCallbacks_{0}, rtpParseFailures_{0}, remoteTrackCallbacks_{0};
    std::atomic<std::uint32_t> encodedWidth_{0}, encodedHeight_{0};
    std::atomic<std::uint64_t> accessUnitsDelivered_{0}, lostPackets_{0};
    std::atomic<std::uint64_t> rtpSequenceGap_{0}, rtpOutOfOrder_{0}, rtpDuplicate_{0};
    std::atomic<std::uint64_t> rtpTimestampChangeBeforeMarker_{0}, rtpIncompleteFuA_{0};
    std::atomic<std::uint64_t> rtpAbandonedAccessUnit_{0}, rtpStaleGenerationPacket_{0};
    std::atomic<std::uint64_t> encodedBytesSubmitted_{0}, mediaBytesReceived_{0};
    std::atomic<std::uint64_t> receiverSps_{0}, receiverPps_{0}, receiverIdr_{0}, receiverNonIdr_{0};
    std::atomic<std::uint64_t> senderKeyframeRequests_{0};
    struct RateSample {
        std::chrono::steady_clock::time_point time;
        std::uint64_t captured, encoded, submitted, received, presented, sentBytes, receivedBytes;
    };
    mutable std::mutex sampleMutex_;
    mutable std::deque<RateSample> samples_;
};
}
