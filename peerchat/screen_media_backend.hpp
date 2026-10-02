#pragma once

#include "windows_media/peerchat_screen_media.h"
#include "h264_rtp_receiver.hpp"
#include <windows.h>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <deque>
#include <sstream>
#include <string>
#include <vector>

namespace peerchat {

// Loader policy is unchanged: only the adjacent, verified adapter is accepted.
// These mechanics have no ICE, peer identity, room or reconnect ownership.
struct ScreenMediaApi {
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


// One source owns one WGC/encoder lifetime. The borrowed AU remains valid until
// the next pop/close, and may be submitted synchronously to multiple routes.
template<class Adapter = ScreenMediaApi>
class BasicScreenCaptureEncodeBackend {
public:
    ~BasicScreenCaptureEncodeBackend() { close(); }
    BasicScreenCaptureEncodeBackend() = default;
    BasicScreenCaptureEncodeBackend(const BasicScreenCaptureEncodeBackend&) = delete;
    BasicScreenCaptureEncodeBackend& operator=(const BasicScreenCaptureEncodeBackend&) = delete;
    bool open(const pc_screen_encoder_options& options) {
        close();
        if (!api_.open()) return false;
        encoder_ = api_.encoderOpen(&options);
        return encoder_ != nullptr;
    }
    explicit operator bool() const { return encoder_ != nullptr; }
    int pop(pc_screen_encoded_frame& info) {
        if (!encoder_) return -2;
        if (scratch_.empty()) scratch_.resize(1024 * 1024);
        int result = api_.encoderPop(encoder_, scratch_.data(),
            static_cast<std::uint32_t>(scratch_.size()), &info);
        if (result == -1 && info.length <= 8 * 1024 * 1024) {
            scratch_.resize(info.length);
            result = api_.encoderPop(encoder_, scratch_.data(),
                static_cast<std::uint32_t>(scratch_.size()), &info);
        }
        if (result > 0 && (!info.length || info.length > scratch_.size())) return -2;
        return result;
    }
    const std::uint8_t* data() const { return scratch_.data(); }
    bool status(pc_screen_encoder_status& value) const {
        return encoder_ && api_.encoderStatus(encoder_, &value) != 0;
    }
    bool requestKeyframe() { return encoder_ && api_.encoderRequestKeyframe(encoder_) != 0; }
    void close() {
        if (encoder_) api_.encoderClose(encoder_);
        encoder_ = nullptr;
        api_.close();
    }
private:
    Adapter api_;
    pc_screen_encoder* encoder_ = nullptr;
    std::vector<std::uint8_t> scratch_;
};

// Reuses the accepted reassembly/recovery algorithm and native decoder without
// a transport dependency. The route adapter must serialize receive/tick/reset,
// drain callbacks before close, and issue any requested keyframe on its route.
template<class Adapter = ScreenMediaApi>
class BasicScreenReceiveDecodeBackend {
public:
    BasicScreenReceiveDecodeBackend() = default;
    ~BasicScreenReceiveDecodeBackend() { close(); }
    BasicScreenReceiveDecodeBackend(const BasicScreenReceiveDecodeBackend&) = delete;
    BasicScreenReceiveDecodeBackend& operator=(const BasicScreenReceiveDecodeBackend&) = delete;
    bool open(const pc_screen_decoder_options& options) {
        close();
        reset();
        if (!api_.open()) return false;
        decoder_ = api_.decoderOpen(&options);
        return decoder_ != nullptr;
    }
    explicit operator bool() const { return decoder_ != nullptr; }
    bool status(pc_screen_decoder_status& value) const {
        return decoder_ && api_.decoderStatus(decoder_, &value) != 0;
    }
    void close() {
        if (decoder_) api_.decoderClose(decoder_);
        decoder_ = nullptr;
        api_.close();
        // Final counters survive close for retained teardown diagnostics.
    }
    void reset() {
        rtpReceiver_.reset();
        accessUnitsDelivered_ = recoveryTimestamp_ = recoveryCompleted_ = 0;
        recoveryOutputPending_ = false;
        firstAccessUnitNals_.clear();
    }
    void receive(H264RtpReceiver::Packet packet, std::uint64_t now) {
        rtpReceiver_.receive(std::move(packet), now,
            [this](const H264RtpReceiver::AccessUnit& au) { return submitAccessUnit(au); });
    }
    void tick(std::uint64_t now) {
        rtpReceiver_.tick(now,
            [this](const H264RtpReceiver::AccessUnit& au) { return submitAccessUnit(au); });
    }
    static bool parseRtp(const char* packet, int size, const std::uint8_t*& payload,
        size_t& length, std::uint16_t& sequence, std::uint32_t& timestamp, bool& marker) {
        if (!packet || size < 12) return false;
        auto* bytes = reinterpret_cast<const std::uint8_t*>(packet);
        if ((bytes[0] >> 6) != 2 || (bytes[1] & 127) != 102) return false;
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

    // Internal counters are read only under the route's existing callback lock.
    H264RtpReceiver rtpReceiver_;
    std::atomic<std::uint64_t> accessUnitsDelivered_{0};
    std::uint64_t recoveryTimestamp_ = 0, recoveryCompleted_ = 0;
    bool recoveryOutputPending_ = false;
    std::deque<std::string> firstAccessUnitNals_;
private:
    bool submitAccessUnit(const H264RtpReceiver::AccessUnit& au) {
        if (firstAccessUnitNals_.size() < 32) {
            std::ostringstream details;
            details << "timestamp=" << au.timestamp << ";first_sequence=" << au.first
                << ";last_sequence=" << au.last << ";marker=1;nal_types=";
            for (auto type : au.types) details << unsigned(type) << ',';
            details << ";bytes=" << au.bytes.size() << ";single_timestamp=true"
                << ";recovery=" << (au.recovery ? "true" : "false");
            firstAccessUnitNals_.push_back(details.str());
        }
        const bool accepted = api_.decoderPush(decoder_, au.bytes.data(),
            static_cast<std::uint32_t>(au.bytes.size()),
            static_cast<std::uint64_t>(au.timestamp) * 10000000ull / 90000ull) == 1;
        if (accepted) {
            ++accessUnitsDelivered_;
            if (au.recovery) {
                recoveryOutputPending_ = true;
                recoveryTimestamp_ = static_cast<std::uint64_t>(au.timestamp) * 10000000ull / 90000ull;
            }
        }
        return accepted;
    }
    Adapter api_;
    pc_screen_decoder* decoder_ = nullptr;
};

using ScreenCaptureEncodeBackend = BasicScreenCaptureEncodeBackend<>;
using ScreenReceiveDecodeBackend = BasicScreenReceiveDecodeBackend<>;

} // namespace peerchat
