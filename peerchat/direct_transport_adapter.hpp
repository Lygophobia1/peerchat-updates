#pragma once

#include "session_transport_types.hpp"

#include <utility>

namespace peerchat::session {

// Observes one existing Node::Connection. It owns no socket, TLS object, timer,
// retry, winner decision, delivery state, or callback. Stage 2 uses it only to
// describe decisions already made by the legacy authoritative implementation.
class DirectTransportAdapter {
public:
    DirectTransportAdapter(SessionEpoch epoch, TransportInstanceId instance,
                           RouteObservation route, std::string expectedRemoteDeviceId,
                           std::size_t maximumPayloadBytes = 70000)
        : epoch_(epoch), instance_(instance), route_(std::move(route)),
          expectedRemoteDeviceId_(std::move(expectedRemoteDeviceId)),
          maximumPayloadBytes_(maximumPayloadBytes) {}

    TransportEvent connecting() {
        state_ = TransportState::Connecting;
        return event(TransportEventKind::StateChanged);
    }

    TransportEvent identityPresented(std::string presentedRemoteDeviceId,
                                     std::uint64_t capabilities) {
        presentedRemoteDeviceId_ = std::move(presentedRemoteDeviceId);
        capabilities_ = capabilities;
        state_ = TransportState::IdentityPresented;
        return event(TransportEventKind::StateChanged);
    }

    void updateRoute(RouteObservation route) { route_ = std::move(route); }

    TransportEvent locallyTrusted() {
        state_ = TransportState::LocallyTrusted;
        return event(TransportEventKind::StateChanged);
    }

    TransportEvent activated() {
        state_ = TransportState::Active;
        return event(TransportEventKind::StateChanged);
    }

    TransportSendResult sendAccepted(std::size_t payloadBytes) const {
        TransportSendResult result;
        result.payloadBytes = payloadBytes;
        if (payloadBytes > maximumPayloadBytes_) {
            result.acceptance = SendAcceptance::RejectedPayloadTooLarge;
        } else if (state_ == TransportState::Closing || state_ == TransportState::Closed ||
                   state_ == TransportState::Failed) {
            result.acceptance = SendAcceptance::RejectedClosing;
        } else {
            result.acceptance = state_ == TransportState::Active
                ? SendAcceptance::AcceptedForTransport
                : SendAcceptance::RejectedNotActive;
        }
        return result;
    }

    TransportEvent closing(std::string reasonUtf8) {
        state_ = TransportState::Closing;
        return event(TransportEventKind::StateChanged, 0, std::move(reasonUtf8));
    }

    TransportEvent observedSend(std::size_t payloadBytes) const {
        return event(TransportEventKind::SendAccepted, payloadBytes);
    }

    TransportEvent observedReceive(std::size_t payloadBytes) const {
        return event(TransportEventKind::ReceiveAvailable, payloadBytes);
    }

    TransportEvent closed(std::string reasonUtf8, bool failed) {
        state_ = failed ? TransportState::Failed : TransportState::Closed;
        return event(TransportEventKind::Closed, 0, std::move(reasonUtf8));
    }

    TransportState state() const { return state_; }
    SessionEpoch sessionEpoch() const { return epoch_; }
    TransportInstanceId instanceId() const { return instance_; }
    RouteRevision routeRevision() const { return route_.revision; }

private:
    TransportEvent event(TransportEventKind kind, std::size_t payloadBytes = 0,
                         std::string reasonUtf8 = {}) const {
        return {kind, epoch_, instance_, route_, state_, expectedRemoteDeviceId_,
                presentedRemoteDeviceId_, capabilities_, payloadBytes,
                std::move(reasonUtf8)};
    }

    SessionEpoch epoch_;
    TransportInstanceId instance_;
    RouteObservation route_;
    std::string expectedRemoteDeviceId_;
    std::string presentedRemoteDeviceId_;
    std::uint64_t capabilities_ = 0;
    std::size_t maximumPayloadBytes_ = 0;
    TransportState state_ = TransportState::Created;
};

} // namespace peerchat::session
