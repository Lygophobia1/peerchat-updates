#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace peerchat::session {

// These values are deliberately platform-neutral and never cross the current
// peer wire protocol. They identify local runtime ownership, not social accounts.
struct SessionEpoch {
    std::uint64_t value = 0;
    friend bool operator==(SessionEpoch left, SessionEpoch right) { return left.value == right.value; }
    friend bool operator!=(SessionEpoch left, SessionEpoch right) { return !(left == right); }
};

struct TransportInstanceId {
    std::uint64_t value = 0;
    friend bool operator==(TransportInstanceId left, TransportInstanceId right) { return left.value == right.value; }
    friend bool operator!=(TransportInstanceId left, TransportInstanceId right) { return !(left == right); }
};

struct RouteRevision {
    std::uint64_t value = 0;
};

struct PeerSessionIdentity {
    // localProfileScope is an opaque local-only profile identifier. It is not
    // an account ID, Peer ID, endpoint, credential, or wire value.
    std::string localProfileScope;
    std::string localDeviceId;
    std::string verifiedRemoteDeviceId;
};

enum class ChannelKind : std::uint8_t {
    Control,
    ReliableMessages,
    RealtimeMedia
};

struct ChannelRequirements {
    bool reliable = true;
    bool ordered = true;
    bool lowLatency = false;
    std::size_t maximumPayloadBytes = 0;
};

enum class TransportState : std::uint8_t {
    Created,
    Connecting,
    IdentityPresented,
    LocallyTrusted,
    Active,
    Closing,
    Closed,
    Failed
};

enum class SendAcceptance : std::uint8_t {
    AcceptedForTransport,
    RejectedNotActive,
    RejectedPayloadTooLarge,
    RejectedClosing
};

struct TransportSendResult {
    SendAcceptance acceptance = SendAcceptance::RejectedNotActive;
    std::size_t payloadBytes = 0;

    bool accepted() const { return acceptance == SendAcceptance::AcceptedForTransport; }
};

struct RouteObservation {
    RouteRevision revision;
    std::string endpointUtf8;
    std::string sourceUtf8;
    std::string routeUtf8;
    bool incoming = false;
};

enum class TransportEventKind : std::uint8_t {
    StateChanged,
    ReceiveAvailable,
    SendAccepted,
    Closed
};

// A value event is copied into the profile-scoped owner. It contains no socket,
// borrowed buffer, callback, secret, message contents, or platform handle.
class TransportEvent {
public:
    TransportEvent(TransportEventKind kind, SessionEpoch sessionEpoch,
                   TransportInstanceId transportInstance, RouteObservation route,
                   TransportState state, std::string expectedRemoteDeviceId,
                   std::string presentedRemoteDeviceId, std::uint64_t capabilities,
                   std::size_t payloadBytes = 0, std::string reasonUtf8 = {})
        : kind_(kind), sessionEpoch_(sessionEpoch), transportInstance_(transportInstance),
          route_(std::move(route)), state_(state),
          expectedRemoteDeviceId_(std::move(expectedRemoteDeviceId)),
          presentedRemoteDeviceId_(std::move(presentedRemoteDeviceId)),
          capabilities_(capabilities), payloadBytes_(payloadBytes),
          reasonUtf8_(std::move(reasonUtf8)) {}

    TransportEventKind kind() const { return kind_; }
    SessionEpoch sessionEpoch() const { return sessionEpoch_; }
    TransportInstanceId transportInstance() const { return transportInstance_; }
    const RouteObservation& route() const { return route_; }
    TransportState state() const { return state_; }
    const std::string& expectedRemoteDeviceId() const { return expectedRemoteDeviceId_; }
    const std::string& presentedRemoteDeviceId() const { return presentedRemoteDeviceId_; }
    std::uint64_t capabilities() const { return capabilities_; }
    std::size_t payloadBytes() const { return payloadBytes_; }
    const std::string& reasonUtf8() const { return reasonUtf8_; }

private:
    TransportEventKind kind_;
    SessionEpoch sessionEpoch_;
    TransportInstanceId transportInstance_;
    RouteObservation route_;
    TransportState state_;
    std::string expectedRemoteDeviceId_;
    std::string presentedRemoteDeviceId_;
    std::uint64_t capabilities_;
    std::size_t payloadBytes_;
    std::string reasonUtf8_;
};

} // namespace peerchat::session
