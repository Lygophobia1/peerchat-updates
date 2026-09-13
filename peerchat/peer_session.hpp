#pragma once

#include "session_transport_types.hpp"

#include <map>
#include <optional>
#include <utility>

namespace peerchat::session {

// Initial logical session state. It is scoped by the owning profile manager and
// a verified remote device, never by an IP address or social conversation ID.
class PeerSession {
public:
    PeerSession(PeerSessionIdentity identity, SessionEpoch epoch)
        : identity_(std::move(identity)), epoch_(epoch) {}

    const PeerSessionIdentity& identity() const { return identity_; }
    SessionEpoch epoch() const { return epoch_; }
    std::uint64_t revision() const { return revision_; }
    std::optional<TransportInstanceId> activeBinding(ChannelKind channel) const {
        auto found = activeBindings_.find(channel);
        return found == activeBindings_.end()
            ? std::optional<TransportInstanceId>{} : found->second;
    }

    // Compatibility accessor retained for the Stage 2 focused tests.
    std::optional<TransportInstanceId> observedBinding(ChannelKind channel) const {
        return activeBinding(channel);
    }

    void observeLegacyActive(TransportInstanceId instance) {
        activeBindings_[ChannelKind::Control] = instance;
        activeBindings_[ChannelKind::ReliableMessages] = instance;
        ++revision_;
    }

    void observeLegacyClosed(TransportInstanceId instance) {
        bool changed = false;
        for (auto it = activeBindings_.begin(); it != activeBindings_.end();) {
            if (it->second == instance) { it = activeBindings_.erase(it); changed = true; }
            else ++it;
        }
        if (changed) ++revision_;
    }

private:
    PeerSessionIdentity identity_;
    SessionEpoch epoch_;
    std::uint64_t revision_ = 0;
    // Stage 3 makes the owning SessionTransportManager authoritative for these
    // bindings. Delivery eligibility deliberately remains outside PeerSession.
    std::map<ChannelKind, TransportInstanceId> activeBindings_;
};

} // namespace peerchat::session
