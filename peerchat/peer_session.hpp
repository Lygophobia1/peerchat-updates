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
    std::optional<TransportInstanceId> observedBinding(ChannelKind channel) const {
        auto found = observedBindings_.find(channel);
        return found == observedBindings_.end()
            ? std::optional<TransportInstanceId>{} : found->second;
    }

    void observeLegacyActive(TransportInstanceId instance) {
        observedBindings_[ChannelKind::Control] = instance;
        observedBindings_[ChannelKind::ReliableMessages] = instance;
        ++revision_;
    }

    void observeLegacyClosed(TransportInstanceId instance) {
        bool changed = false;
        for (auto it = observedBindings_.begin(); it != observedBindings_.end();) {
            if (it->second == instance) { it = observedBindings_.erase(it); changed = true; }
            else ++it;
        }
        if (changed) ++revision_;
    }

private:
    PeerSessionIdentity identity_;
    SessionEpoch epoch_;
    std::uint64_t revision_ = 0;
    // Projection only in Stage 2. Node remains the active/winner authority.
    std::map<ChannelKind, TransportInstanceId> observedBindings_;
};

} // namespace peerchat::session
