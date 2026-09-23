#pragma once

#define RTC_STATIC
#include <rtc/rtc.h>
#include "windows_media/peerchat_screen_media.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iomanip>
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
    struct Event { EventKind kind; std::string value, detail; };
    struct Options {
        bool sender = false;
        std::uint32_t sourceKind = 0;
        std::uint64_t sourceHandle = 0;
        std::uint64_t presentationWindow = 0;
        std::uint32_t width = 0, height = 0, fps = 60, bitrate = 20000000;
        bool captureCursor = true;
    };
    struct Statistics {
        std::uint64_t encodedSubmitted = 0, rtpReceived = 0;
        std::uint64_t accessUnitsDelivered = 0, lostPackets = 0;
        std::uint64_t decoded = 0, presented = 0;
        std::uint64_t captured = 0, encoderSubmitted = 0, encoded = 0;
        std::uint64_t droppedBeforeEncode = 0, droppedEncodedQueue = 0, droppedDecodeQueue = 0;
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
            (capability.hardware_encoder_codecs & PC_SCREEN_H264) != 0 &&
            (capability.decoder_codecs & PC_SCREEN_H264) != 0;
        api.close();
        return okay;
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
                    push(EventKind::Failed, "Windows screen encoder failed");
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
        return result;
    }

    Statistics statistics() const {
        Statistics value;
        value.encodedSubmitted = encodedSubmitted_;
        value.rtpReceived = rtpReceived_;
        value.accessUnitsDelivered = accessUnitsDelivered_;
        value.lostPackets = lostPackets_;
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
                if (status.decode_operations) {
                    value.decodeLatencyAvailable = true;
                    value.averageDecodeMs = double(status.decode_latency_microseconds_total) /
                        double(status.decode_operations) / 1000.0;
                }
            }
        }
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
        char local[512]{}, remote[512]{};
        std::wstring pair = L"unavailable";
        if (pc_ >= 0 && rtcGetSelectedCandidatePair(pc_, local, sizeof(local),
                remote, sizeof(remote)) >= 0)
            pair = std::wstring(local, local + std::strlen(local)) + L" -> " +
                   std::wstring(remote, remote + std::strlen(remote));
        std::wostringstream out;
        out << std::fixed << std::setprecision(2)
            << L"media_connected=" << (data.connected ? L"true" : L"false")
            << L"\ntrack_open=" << (data.trackOpen ? L"true" : L"false")
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
            << L"\ndropped_decode_queue=" << data.droppedDecodeQueue
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
            << L"\nencoder=" << (options_.sender ? L"Windows Media Foundation hardware H.264 MFT" : L"Unavailable (receiver-only)")
            << L"\ndecoder=" << (!options_.sender ? L"Windows Media Foundation D3D11-aware MFT (hardware acceleration not confirmed)" : L"Unavailable (sender-only)")
            << L"\nrtp_transport_packets_sent=not exposed by pinned C API"
            << L"\nremote_rtp_callback_packets=" << data.rtpReceived
            << L"\ncomplete_access_units_delivered=" << data.accessUnitsDelivered
            << L"\nrtp_reassembly_discontinuities=" << data.lostPackets
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

    void stop() {
        stopping_ = true;
        trackOpen_ = false;
        if (pc_ >= 0) {
            rtcSetStateChangeCallback(pc_, nullptr);
            rtcSetTrackCallback(pc_, nullptr);
            rtcSetLocalDescriptionCallback(pc_, nullptr);
            rtcSetLocalCandidateCallback(pc_, nullptr);
            rtcSetUserPointer(pc_, nullptr);
        }
        for (int track : {track_, receiveTrack_}) if (track >= 0) {
            rtcSetMessageCallback(track, nullptr);
            rtcSetOpenCallback(track, nullptr);
            rtcSetClosedCallback(track, nullptr);
            rtcSetUserPointer(track, nullptr);
        }
        if (encoder_) { api_.encoderClose(encoder_); encoder_ = nullptr; }
        if (decoder_) { api_.decoderClose(decoder_); decoder_ = nullptr; }
        if (receiveTrack_ >= 0 && receiveTrack_ != track_) rtcDeleteTrack(receiveTrack_);
        if (track_ >= 0) rtcDeleteTrack(track_);
        if (pc_ >= 0) { rtcClosePeerConnection(pc_); rtcDeletePeerConnection(pc_); }
        pc_ = track_ = receiveTrack_ = -1;
        api_.close();
        {
            std::lock_guard lock(mutex_);
            events_.clear();
            rtpFrame_.clear();
            rtpActive_ = false;
        }
        connected_ = false;
    }

private:
    struct Api {
        HMODULE module = nullptr;
        decltype(&pc_screen_query_capabilities) queryCapabilities = nullptr;
        decltype(&pc_screen_encoder_open) encoderOpen = nullptr;
        decltype(&pc_screen_encoder_pop) encoderPop = nullptr;
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
            encoderOpen = nullptr; encoderPop = nullptr; encoderStatus = nullptr; encoderClose = nullptr;
            decoderOpen = nullptr; decoderPush = nullptr;
            decoderStatus = nullptr; decoderClose = nullptr;
        }
    };

    static void description(int, const char* sdp, const char* type, void* context) {
        if (context && sdp && type) static_cast<ScreenShareMedia*>(context)->push(
            EventKind::Description, sdp, type);
    }
    static void candidate(int, const char* value, const char* mid, void* context) {
        if (context && value) static_cast<ScreenShareMedia*>(context)->push(
            EventKind::Candidate, value, mid ? mid : "0");
    }
    static void state(int, rtcState value, void* context) {
        if (!context) return;
        auto& self = *static_cast<ScreenShareMedia*>(context);
        if (value == RTC_CONNECTED) {
            self.connected_ = true;
            self.push(EventKind::Connected);
        } else if (value == RTC_FAILED) {
            self.failed_ = true;
            self.push(EventKind::Failed, "Secure screen media failed");
        } else if (value == RTC_DISCONNECTED || value == RTC_CLOSED) {
            self.connected_ = false;
            self.push(EventKind::Disconnected);
        }
    }
    static void opened(int, void* context) {
        if (context) static_cast<ScreenShareMedia*>(context)->trackOpen_ = true;
    }
    static void closed(int, void* context) {
        if (context) static_cast<ScreenShareMedia*>(context)->trackOpen_ = false;
    }
    static void remoteTrack(int, int track, void* context) {
        if (!context || track < 0) return;
        auto& self = *static_cast<ScreenShareMedia*>(context);
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
        rtpFrame_.insert(rtpFrame_.end(), {0, 0, 0, 1});
        rtpFrame_.insert(rtpFrame_.end(), data, data + length);
        return true;
    }

    void received(const char* packet, int size) {
        if (stopping_ || options_.sender || !decoder_) return;
        const std::uint8_t* payload = nullptr;
        size_t length = 0;
        std::uint16_t sequence = 0;
        std::uint32_t timestamp = 0;
        bool marker = false;
        if (!parseRtp(packet, size, payload, length, sequence, timestamp, marker)) return;
        ++rtpReceived_;
        mediaBytesReceived_ += static_cast<std::uint64_t>(size);
        std::lock_guard lock(mutex_);
        if (rtpActive_ && (timestamp != rtpTimestamp_ || sequence !=
                static_cast<std::uint16_t>(lastSequence_ + 1))) {
            ++lostPackets_;
            rtpFrame_.clear();
            rtpFragmented_ = false;
        }
        rtpActive_ = true;
        rtpTimestamp_ = timestamp;
        lastSequence_ = sequence;
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
            rtpFrame_.clear();
            rtpFragmented_ = false;
        }
        if (marker) {
            if (!rtpFrame_.empty() && !rtpFragmented_ &&
                api_.decoderPush(decoder_, rtpFrame_.data(),
                    static_cast<std::uint32_t>(rtpFrame_.size()),
                    static_cast<std::uint64_t>(timestamp) * 10000000ull / 90000ull) == 1)
                ++accessUnitsDelivered_;
            rtpFrame_.clear();
            rtpFragmented_ = false;
            rtpActive_ = false;
        }
    }
    static void message(int, const char* packet, int size, void* context) {
        if (context) static_cast<ScreenShareMedia*>(context)->received(packet, size);
    }

    void push(EventKind kind, std::string value = {}, std::string detail = {}) {
        if (stopping_) return;
        std::lock_guard lock(mutex_);
        if (events_.size() == 128) events_.pop_front();
        events_.push_back({kind, std::move(value), std::move(detail)});
    }

    bool create(std::string& error) {
        stopping_ = false;
        connected_ = failed_ = trackOpen_ = false;
        encodedSubmitted_ = rtpReceived_ = accessUnitsDelivered_ = lostPackets_ = 0;
        encodedBytesSubmitted_ = mediaBytesReceived_ = 0;
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
            encoder_ = api_.encoderOpen(&capture);
            if (!encoder_) { error = "Windows screen capture or H.264 hardware encoding failed"; stop(); return false; }
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
        rtcConfiguration config{};
        const char* servers[] = {"stun:stun.l.google.com:19302"};
        config.iceServers = servers;
        config.iceServersCount = 1;
        config.certificateType = RTC_CERTIFICATE_ECDSA;
        config.iceTransportPolicy = RTC_TRANSPORT_POLICY_ALL;
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
    std::deque<Event> events_;
    std::vector<std::uint8_t> rtpFrame_;
    std::vector<std::uint8_t> encodedScratch_;
    std::uint16_t lastSequence_ = 0;
    std::uint32_t rtpTimestamp_ = 0;
    bool rtpActive_ = false, rtpFragmented_ = false;
    std::atomic<bool> stopping_{true}, trackOpen_{false}, connected_{false}, failed_{false};
    std::atomic<std::uint64_t> encodedSubmitted_{0}, rtpReceived_{0};
    std::atomic<std::uint32_t> encodedWidth_{0}, encodedHeight_{0};
    std::atomic<std::uint64_t> accessUnitsDelivered_{0}, lostPackets_{0};
    std::atomic<std::uint64_t> encodedBytesSubmitted_{0}, mediaBytesReceived_{0};
    struct RateSample {
        std::chrono::steady_clock::time_point time;
        std::uint64_t captured, encoded, submitted, received, presented, sentBytes, receivedBytes;
    };
    mutable std::mutex sampleMutex_;
    mutable std::deque<RateSample> samples_;
};
}
