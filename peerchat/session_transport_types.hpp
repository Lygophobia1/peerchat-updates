#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

enum class TransportKind : std::uint8_t { Direct, Relay };
enum class RouteClass : std::uint8_t { Unknown, DirectLan, DirectInternet, ServerRelay };

// Authorization is independent of the physical relay route. A group-only
// session never inherits the permissions of a friend conversation with the
// same device, or vice versa.
enum class ReliableAuthorizationScope : std::uint8_t { Friend, GroupControl, ArchiveControl };
struct ArchiveControlScope {
    std::uint64_t account=0,sourceGeneration=0,readerGeneration=0;
    std::string issuer,source,reader,sourceGrantId,readerGrantId;
    bool empty()const{return !account&&!sourceGeneration&&!readerGeneration&&issuer.empty()&&source.empty()&&reader.empty()&&sourceGrantId.empty()&&readerGrantId.empty();}
};
struct ReliableAuthorization {
    ReliableAuthorizationScope scope = ReliableAuthorizationScope::Friend;
    std::string groupId;
    std::uint64_t rosterGeneration = 0;
    std::string expectedRemoteDeviceId;
    ArchiveControlScope archive{};

    static bool hexId(const std::string& value, std::size_t length) {
        if (value.size() != length) return false;
        for (char c : value)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }
    bool valid() const {
        if (!hexId(expectedRemoteDeviceId, 64)) return false;
        if(scope==ReliableAuthorizationScope::Friend)return groupId.empty()&&rosterGeneration==0&&archive.empty();
        if(scope==ReliableAuthorizationScope::GroupControl)return hexId(groupId,32)&&rosterGeneration>0&&archive.empty();
        return scope==ReliableAuthorizationScope::ArchiveControl&&groupId.empty()&&!rosterGeneration&&
            archive.account>0&&archive.sourceGeneration>0&&archive.readerGeneration>0&&
            hexId(archive.issuer,64)&&hexId(archive.source,64)&&hexId(archive.reader,64)&&archive.source!=archive.reader&&
            hexId(archive.sourceGrantId,32)&&hexId(archive.readerGrantId,32)&&
            (expectedRemoteDeviceId==archive.source||expectedRemoteDeviceId==archive.reader);
    }
    std::string continuityKey() const {
        if(scope==ReliableAuthorizationScope::Friend)return expectedRemoteDeviceId;
        if(scope==ReliableAuthorizationScope::GroupControl)return "group-control:"+groupId+":"+expectedRemoteDeviceId;
        if(scope==ReliableAuthorizationScope::ArchiveControl)return "archive-control:"+std::to_string(archive.account)+":"+archive.issuer+":"+archive.source+":"+archive.reader;
        return {};
    }
    bool permitsDirectMessage() const { return scope == ReliableAuthorizationScope::Friend; }
    bool permitsGroupKeyControl() const { return scope == ReliableAuthorizationScope::GroupControl; }
    bool permitsArchiveControl() const { return scope == ReliableAuthorizationScope::ArchiveControl; }
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
    // The local network observation under which this route was created. A
    // delayed invalidation may retire only instances from an older observation.
    std::uint64_t networkRevision = 0;
    TransportKind kind = TransportKind::Direct;
    RouteClass routeClass = RouteClass::Unknown;
};

enum class P2pAvailability : std::uint8_t { Unavailable, Testing, Available, Promoted };

struct RetryTarget {
    std::string remoteDeviceId;
    RouteObservation route;
};

struct RetrySchedule {
    bool scheduled = false;
    bool suspended = false;
    std::uint32_t failureCount = 0;
    std::uint32_t delayMilliseconds = 0;
    std::uint64_t dueMilliseconds = 0;
    RetryTarget target;
};

enum class WinnerDisposition : std::uint8_t {
    Activated,
    ReplacedExisting,
    AwaitingHandoff,
    VerifiedStandby,
    RejectedDuplicate,
    RejectedStaleToken,
    RejectedNotReady
};

struct WinnerDecision {
    WinnerDisposition disposition = WinnerDisposition::RejectedNotReady;
    std::string remoteDeviceId;
    TransportInstanceId winner;
    std::vector<TransportInstanceId> retire;
    std::string reasonUtf8;

    bool activated() const {
        return disposition == WinnerDisposition::Activated ||
               disposition == WinnerDisposition::ReplacedExisting;
    }
};

struct CancellationDecision {
    bool accepted = false;
    bool staleObservation = false;
    bool retainedActiveWinner = false;
    bool activeWinnerCancelled = false;
    std::vector<TransportInstanceId> cancel;
    std::vector<TransportInstanceId> updateRoute;
    std::string reasonUtf8;
};

struct CloseDecision {
    bool accepted = false;
    bool wasActiveWinner = false;
    bool hasActiveWinner = false;
    bool hasPendingAttempt = false;
    std::string remoteDeviceId;
};

// Complete value snapshot for one verified/pending remote device. This is the
// only state WPF needs in order to project connectivity; attempt failures are
// retained separately and can never replace an active binding in the snapshot.
struct SessionSnapshot {
    SessionEpoch epoch;
    std::uint64_t revision = 0;
    std::uint64_t networkRevision = 0;
    std::string remoteDeviceId;
    bool connected = false;
    bool pending = false;
    bool retryScheduled = false;
    bool retrySuspended = false;
    TransportInstanceId activeTransport;
    RouteObservation activeRoute;
    TransportInstanceId latestFailedTransport;
    RouteObservation latestFailedRoute;
    std::string latestFailureUtf8;
    std::uint32_t retryDelayMilliseconds = 0;
    P2pAvailability p2pAvailability = P2pAvailability::Unavailable;
    TransportInstanceId p2pStandbyTransport;
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
