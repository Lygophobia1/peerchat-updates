#pragma once
#include "audio_receive/peerchat_audio_receive.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <windows.h>

namespace peerchat {
// Reusable receive/mix owner, independent of room semantics and route topology.
// Caller binds each opaque source key to an authenticated account/device lifetime.
// It is not an SSRC/account lookup and does not own a device, timer or transport.
class VoiceReceiveEngine {
public:
    VoiceReceiveEngine() = default;
    ~VoiceReceiveEngine();
    VoiceReceiveEngine(const VoiceReceiveEngine&) = delete;
    VoiceReceiveEngine& operator=(const VoiceReceiveEngine&) = delete;
    bool open(const std::wstring& absoluteDllPath);
    void close();
    bool authorize(const std::string& sourceKey, std::uint64_t generation);
    void retire(const std::string& sourceKey);
    int insertRtp(const std::string& sourceKey, std::uint64_t generation,
                  const unsigned char* packet, std::size_t count);
    // Only the output device worker calls this. Two NetEq/mixer pulls, no PCM queue.
    // Failure/busy yields silence for the affected block, never a blocking retry.
    bool pull20ms(std::int16_t* pcm, std::size_t samples, bool deafened);
    struct Snapshot {
        unsigned authorizedSources=0, activeSources=0;
        std::uint64_t acceptedPackets=0,rejectedPackets=0,mixedBlocks=0,underrunBlocks=0;
        int lastError=0;
        std::map<std::string,pc_rx_source_stats> sources;
    };
    Snapshot snapshot();
private:
    struct Source {std::uint64_t generation=0;pc_rx_source handle=0;std::uint32_t ssrc=0;};
    using Create=decltype(&pc_rx_create);using Destroy=decltype(&pc_rx_destroy);
    using Add=decltype(&pc_rx_source_add);using Remove=decltype(&pc_rx_source_remove);
    using Insert=decltype(&pc_rx_insert);using Pull=decltype(&pc_rx_pull_10ms);using Stats=decltype(&pc_rx_get_source_stats);
    HMODULE module_=nullptr;pc_rx_engine engine_=0;
    Create create_=nullptr;Destroy destroy_=nullptr;Add add_=nullptr;Remove remove_=nullptr;
    Insert insert_=nullptr;Pull pull_=nullptr;Stats stats_=nullptr;
    std::mutex mutex_;std::map<std::string,Source> sources_;Snapshot counters_;
    std::atomic<std::uint64_t> busyPullBlocks_{0};
    void closeLocked();
};
}
