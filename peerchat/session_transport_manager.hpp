#pragma once

#include "peer_session.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>

namespace peerchat::session {

// Profile-scoped Stage 2 projection. Node remains the sole production owner of
// retry, invalidation, duplicate arbitration, trust, delivery, and voice. This
// manager has no clock, worker, socket, timer, close action, or send path.
class SessionTransportManager {
public:
    SessionTransportManager(std::string localProfileScope,
                            std::string localDeviceId,
                            SessionEpoch epoch)
        : localProfileScope_(std::move(localProfileScope)),
          localDeviceId_(std::move(localDeviceId)), epoch_(epoch) {}

    bool observeLegacyDecision(const TransportEvent& event) {
        if (event.sessionEpoch() != epoch_) return false;
        const auto& remote = event.presentedRemoteDeviceId().empty()
            ? event.expectedRemoteDeviceId() : event.presentedRemoteDeviceId();
        if (remote.empty()) return true;

        auto found = sessions_.find(remote);
        if (found == sessions_.end()) {
            // A logical PeerSession starts only after the legacy authority has
            // authenticated, trusted, arbitrated, and activated the device.
            if (event.state() != TransportState::Active) return true;
            PeerSessionIdentity identity{localProfileScope_, localDeviceId_, remote};
            found = sessions_.emplace(remote, PeerSession(std::move(identity), epoch_)).first;
        }
        if (event.state() == TransportState::Active) {
            found->second.observeLegacyActive(event.transportInstance());
        } else if (event.kind() == TransportEventKind::Closed) {
            found->second.observeLegacyClosed(event.transportInstance());
        }
        return true;
    }

    const PeerSession* find(const std::string& verifiedRemoteDeviceId) const {
        auto found = sessions_.find(verifiedRemoteDeviceId);
        return found == sessions_.end() ? nullptr : &found->second;
    }

    SessionEpoch epoch() const { return epoch_; }

private:
    std::string localProfileScope_;
    std::string localDeviceId_;
    SessionEpoch epoch_;
    std::map<std::string, PeerSession> sessions_;
};

} // namespace peerchat::session
