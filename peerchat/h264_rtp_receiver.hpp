#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace peerchat {
// A complete encoded AU discarded before RTP has no observable sequence gap.
// Keep that reference break local and resume transmission at a fresh bootstrap.
class H264SenderContinuity {
public:
    bool allow(std::uint64_t ordinal, bool idr, bool configOnly = false) {
        if (last_ && ordinal != last_ + 1) waiting_ = true;
        last_ = ordinal;
        if (waiting_ && !idr && !configOnly) return false;
        if (idr) waiting_ = false;
        return true;
    }
    void sendFailed() { waiting_ = true; }
    bool waiting() const { return waiting_; }
private:
    std::uint64_t last_ = 0;
    bool waiting_ = true;
};
// One media generation; the caller serializes receive/tick/status access.
// No sockets, decoder, scheduler, or logical share ownership here.
class H264RtpReceiver {
public:
    struct Packet {
        std::uint16_t sequence = 0;
        std::uint32_t timestamp = 0;
        bool marker = false;
        std::vector<std::uint8_t> payload;
        std::uint64_t arrivalMs = 0;
        bool reordered = false;
    };
    struct AccessUnit {
        std::vector<std::uint8_t> bytes, types;
        std::uint32_t timestamp = 0;
        std::uint16_t first = 0, last = 0;
        bool recovery = false;
    };
    struct Stats {
        std::uint64_t outOfOrder = 0, reorderedRecovered = 0, sequenceLoss = 0;
        std::uint64_t duplicate = 0, late = 0, incompleteFu = 0, incompleteAu = 0;
        std::uint64_t prevented = 0, timestampBeforeMarker = 0, idr = 0;
        std::uint64_t recoveryAccepted = 0, decoderQueueRefused = 0;
        std::uint64_t sps = 0, pps = 0, nonIdr = 0;
    };
    using Submit = std::function<bool(const AccessUnit&)>;
    static constexpr unsigned WindowPackets = 32;
    static constexpr std::uint64_t WindowMs = 20;
    const Stats& stats() const { return stats_; }
    bool needsKeyframe() const { return waitingIdr_; }
    void reset() { *this = H264RtpReceiver(); }
    void receive(Packet packet, std::uint64_t now, const Submit& submit) {
        packet.arrivalMs = now;
        if (!initialized_) { initialized_ = true; expected_ = packet.sequence; }
        const auto delta = distance(packet.sequence, expected_);
        if (delta < 0) { ++stats_.late; return; }
        packet.reordered = delta > 0;
        const auto inserted = pending_.emplace(packet.sequence, std::move(packet)).second;
        if (!inserted) { ++stats_.duplicate; return; }
        if (delta > 0) ++stats_.outOfOrder;
        drain(submit);
        expire(now, submit);
    }
    void tick(std::uint64_t now, const Submit& submit) { expire(now, submit); }
private:
    static int distance(std::uint16_t a, std::uint16_t b) {
        const auto delta = static_cast<std::uint16_t>(a - b);
        return delta < 32768 ? int(delta) : int(delta) - 65536;
    }
    void clearAu() { au_ = {}; active_ = fragmented_ = false; }
    void damage() {
        waitingIdr_ = true;
        if (active_) {
            ++stats_.incompleteAu; ++stats_.prevented;
            if (fragmented_) ++stats_.incompleteFu;
            abandoned_ = au_.timestamp; hasAbandoned_ = true;
        }
        clearAu();
    }
    bool append(const std::uint8_t* bytes, std::size_t size) {
        if (!size || au_.bytes.size() + size + 4 > 8 * 1024 * 1024) return false;
        const auto type = bytes[0] & 31;
        if ((bytes[0] & 128) || type == 0 || type > 23) return false;
        au_.types.push_back(static_cast<std::uint8_t>(type));
        au_.bytes.insert(au_.bytes.end(), {0,0,0,1});
        au_.bytes.insert(au_.bytes.end(), bytes, bytes + size);
        return true;
    }
    void complete(const Submit& submit) {
        if (fragmented_ || au_.bytes.empty()) { damage(); return; }
        bool idr = false, delta = false;
        std::vector<std::uint8_t> sps, pps;
        std::size_t offset = 0;
        // All NALs were reconstructed with four-byte Annex B separators.
        for (auto type : au_.types) {
            std::size_t end = offset + 4;
            while (end + 4 <= au_.bytes.size() &&
                   !(au_.bytes[end] == 0 && au_.bytes[end+1] == 0 &&
                     au_.bytes[end+2] == 0 && au_.bytes[end+3] == 1)) ++end;
            if (end + 4 > au_.bytes.size()) end = au_.bytes.size();
            if ((type == 7 || type == 8) && end - offset > 65536) { damage(); return; }
            if (type == 7) { ++stats_.sps; sps.assign(au_.bytes.begin()+offset, au_.bytes.begin()+end); }
            if (type == 8) { ++stats_.pps; pps.assign(au_.bytes.begin()+offset, au_.bytes.begin()+end); }
            if (type == 5) idr = true;
            if (type == 1) { delta = true; ++stats_.nonIdr; }
            offset = end;
        }
        if (idr) ++stats_.idr;
        // Mixed IDR/non-IDR pictures must never become one decoder sample.
        if (idr && delta) { damage(); return; }
        if (!sps.empty()) sps_ = std::move(sps);
        if (!pps.empty()) pps_ = std::move(pps);
        if (waitingIdr_) {
            if (!idr || sps_.empty() || pps_.empty()) {
                if (idr || delta) ++stats_.prevented;
                clearAu(); return;
            }
            au_.recovery = true;
            std::vector<std::uint8_t> bootstrap;
            if (std::find(au_.types.begin(), au_.types.end(), 7) == au_.types.end())
                bootstrap.insert(bootstrap.end(), sps_.begin(), sps_.end());
            if (std::find(au_.types.begin(), au_.types.end(), 8) == au_.types.end())
                bootstrap.insert(bootstrap.end(), pps_.begin(), pps_.end());
            if (bootstrap.size() + au_.bytes.size() > 8 * 1024 * 1024) { damage(); return; }
            bootstrap.insert(bootstrap.end(), au_.bytes.begin(), au_.bytes.end());
            au_.bytes = std::move(bootstrap);
        }
        if (submit(au_)) {
            if (au_.recovery) { waitingIdr_ = false; ++stats_.recoveryAccepted; }
        } else {
            // Refusing a delta AU in the bounded decoder queue also breaks references.
            ++stats_.decoderQueueRefused; ++stats_.prevented; waitingIdr_ = true;
        }
        clearAu();
    }
    void process(const Packet& packet, const Submit& submit) {
        if (haveTimestamp_ && packet.timestamp != lastTimestamp_ &&
            static_cast<std::uint32_t>(packet.timestamp - lastTimestamp_) >= 0x80000000u) {
            ++stats_.late; return;
        }
        lastTimestamp_ = packet.timestamp; haveTimestamp_ = true;
        if (hasAbandoned_ && packet.timestamp == abandoned_) return;
        if (active_ && packet.timestamp != au_.timestamp) {
            ++stats_.timestampBeforeMarker; damage();
        }
        if (!active_) {
            active_ = true; au_.timestamp = packet.timestamp; au_.first = packet.sequence;
        }
        au_.last = packet.sequence;
        const auto& p = packet.payload;
        bool valid = !p.empty() && !(p[0] & 128);
        const auto type = p.empty() ? 0 : p[0] & 31;
        if (valid && type >= 1 && type <= 23) {
            valid = !fragmented_ && append(p.data(), p.size());
        } else if (valid && type == 24) {
            valid = !fragmented_;
            std::size_t cursor = 1;
            while (valid && cursor + 2 <= p.size()) {
                const std::size_t size = (std::size_t(p[cursor]) << 8) | p[cursor+1];
                cursor += 2;
                valid = size && size <= p.size()-cursor && append(p.data()+cursor, size);
                if (valid) cursor += size;
            }
            valid = valid && cursor > 1 && cursor == p.size();
        } else if (valid && type == 28 && p.size() >= 3) {
            const bool start = (p[1] & 128) != 0, end = (p[1] & 64) != 0;
            const auto header = static_cast<std::uint8_t>((p[0] & 224) | (p[1] & 31));
            valid = !(p[0] & 128) && !(p[1] & 32) && !(start && end);
            if (start) {
                valid = valid && !fragmented_ && append(&header, 1);
                fragmented_ = valid; fragmentHeader_ = header;
            } else valid = valid && fragmented_ && header == fragmentHeader_;
            if (valid && au_.bytes.size() + p.size()-2 <= 8 * 1024 * 1024)
                au_.bytes.insert(au_.bytes.end(), p.begin()+2, p.end());
            else valid = false;
            if (valid && end) fragmented_ = false;
            if (!valid && !fragmented_) ++stats_.incompleteFu;
        } else valid = false;
        if (!valid) { damage(); return; }
        if (packet.marker) complete(submit);
    }
    void drain(const Submit& submit) {
        for (;;) {
            const auto next = pending_.find(expected_);
            if (next == pending_.end()) break;
            Packet packet = std::move(next->second); pending_.erase(next);
            if (packet.reordered) ++stats_.reorderedRecovered;
            process(packet, submit); ++expected_;
        }
    }
    void expire(std::uint64_t now, const Submit& submit) {
        while (!pending_.empty()) {
            auto nearest = pending_.begin();
            std::uint64_t oldest = now;
            int furthest = 0;
            for (auto it = pending_.begin(); it != pending_.end(); ++it) {
                if (distance(it->first, expected_) < distance(nearest->first, expected_)) nearest = it;
                oldest = std::min(oldest, it->second.arrivalMs);
                furthest = std::max(furthest, distance(it->first, expected_));
            }
            if (furthest < int(WindowPackets) && now - oldest < WindowMs) break;
            stats_.sequenceLoss += static_cast<unsigned>(distance(nearest->first, expected_));
            const bool hadAu = active_;
            const auto damagedTimestamp = au_.timestamp;
            damage();
            // The missing span may include the next timestamp's prefix too.
            // Without an AU-start marker in RTP, never assume its tail is complete.
            if (!hadAu || nearest->second.timestamp != damagedTimestamp) {
                abandoned_ = nearest->second.timestamp; hasAbandoned_ = true;
                ++stats_.incompleteAu; ++stats_.prevented;
            }
            expected_ = nearest->first;
            drain(submit);
        }
    }
    Stats stats_;
    std::map<std::uint16_t, Packet> pending_;
    AccessUnit au_;
    std::vector<std::uint8_t> sps_, pps_;
    std::uint16_t expected_ = 0;
    std::uint32_t abandoned_ = 0;
    std::uint32_t lastTimestamp_ = 0;
    std::uint8_t fragmentHeader_ = 0;
    bool initialized_ = false, active_ = false, fragmented_ = false;
    bool hasAbandoned_ = false, waitingIdr_ = true;
    bool haveTimestamp_ = false;
};
}
