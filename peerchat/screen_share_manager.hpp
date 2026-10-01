#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace peerchat::screen {
enum class State : std::uint32_t { Idle, Offering, Incoming, Connecting, Connected, Recovering };
enum class Backend : std::uint32_t { NativeWindows, WebRtcCompatibility };
// A compatibility selection is a requirement for this endpoint, not a
// preference that may override the other endpoint's requirement.
inline Backend negotiateBackend(Backend local, Backend remote) {
    return local == Backend::WebRtcCompatibility || remote == Backend::WebRtcCompatibility
        ? Backend::WebRtcCompatibility : Backend::NativeWindows;
}

// Profile-scoped logical share owner. A media engine is disposable and must
// never determine whether the authenticated share itself still exists.
class ScreenShareManager {
public:
    struct Share {
        std::string id, peer, remoteOffer;
        std::uint64_t lifetime = 0, generation = 1, deadline = 0;
        State state = State::Idle;
        bool sender = false, controlAvailable = true;
        Backend backend = Backend::NativeWindows;
        std::vector<std::pair<std::string, std::string>> candidates;
        Backend localRequirement = Backend::NativeWindows;
        Backend peerRequirement = Backend::NativeWindows;
        bool backendNegotiated = false;
    };
    const Share* active() const { return share_.state == State::Idle ? nullptr : &share_; }
    Share* active() { return share_.state == State::Idle ? nullptr : &share_; }
    bool start(const std::string& id, const std::string& peer, bool sender,
               State state, std::uint64_t deadline, Backend backend = Backend::NativeWindows) {
        if (active() || id.empty() || peer.empty() ||
            (state != State::Offering && state != State::Incoming)) return false;
        share_ = {id, peer, {}, ++lastLifetime_, 1, deadline, state, sender, true, backend, {}};
        share_.localRequirement = backend;
        return true;
    }
    bool matches(const std::string& id, const std::string& peer) const {
        return active() && share_.id == id && share_.peer == peer;
    }
    bool matchesMedia(std::uint64_t lifetime, const std::string& id,
                      const std::string& peer, std::uint64_t generation) const {
        return matches(id, peer) && share_.lifetime == lifetime &&
               share_.generation == generation;
    }
    bool transition(State state, std::uint64_t deadline = 0) {
        if (!active()) return false;
        share_.state = state;
        share_.deadline = deadline;
        return true;
    }
    bool acceptGeneration(std::uint64_t generation, std::uint64_t deadline) {
        // A reliable control binding can disappear after the sender queues a
        // restart offer. Skip a bounded number of unseen generations while
        // still rejecting replays and implausible jumps for this ShareId.
        if (!active() || generation <= share_.generation ||
            generation > share_.generation + 8) return false;
        share_.generation = generation;
        share_.state = State::Recovering;
        share_.deadline = deadline;
        share_.candidates.clear();
        return true;
    }
    std::uint64_t restart(std::uint64_t deadline) {
        if (!active()) return 0;
        ++share_.generation;
        share_.state = State::Recovering;
        share_.deadline = deadline;
        share_.candidates.clear();
        return share_.generation;
    }
    bool queueCandidate(std::string value, std::string mid) {
        if (!active() || share_.candidates.size() >= 64) return false;
        share_.candidates.emplace_back(std::move(value), std::move(mid));
        return true;
    }
    bool expired(std::uint64_t now) const {
        return active() && share_.deadline && now >= share_.deadline;
    }
    void clear() { share_ = {}; }
private:
    Share share_;
    std::uint64_t lastLifetime_ = 0;
};
}
