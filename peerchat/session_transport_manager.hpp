#pragma once

#include "peer_session.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace peerchat::session {

// The profile-scoped Stage 3 continuity owner. Node still owns the concrete
// sockets and executes these decisions on its existing worker, but it no longer
// decides winners, retry timing, cancellation eligibility, or stale-token
// validity. Delivery/ACK state remains on legacy connections until Stage 4.
class SessionTransportManager {
public:
    SessionTransportManager(std::string localProfileScope,
                            std::string localDeviceId,
                            SessionEpoch epoch,
                            std::uint32_t retryBaseMilliseconds = 5000,
                            std::uint32_t retryCapMilliseconds = 300000)
        : localProfileScope_(std::move(localProfileScope)),
          localDeviceId_(std::move(localDeviceId)), epoch_(epoch),
          retryBaseMilliseconds_(retryBaseMilliseconds),
          retryCapMilliseconds_(retryCapMilliseconds) {}

    bool registerAttempt(SessionEpoch epoch, TransportInstanceId instance,
                         std::string expectedRemoteDeviceId,
                         RouteObservation route, std::uint64_t nowMilliseconds) {
        if (epoch != epoch_ || !instance.value) return false;
        auto found = transports_.find(instance.value);
        if (found != transports_.end()) {
            return found->second.expectedRemoteDeviceId == expectedRemoteDeviceId;
        }
        TransportRecord record;
        record.instance = instance;
        record.expectedRemoteDeviceId = std::move(expectedRemoteDeviceId);
        record.route = std::move(route);
        record.state = TransportState::Connecting;
        record.startedMilliseconds = record.lastProgressMilliseconds = nowMilliseconds;
        transports_.emplace(instance.value, std::move(record));
        const auto& expected = transports_.at(instance.value).expectedRemoteDeviceId;
        if (!expected.empty()) touch(expected);
        return true;
    }

    bool observeTransport(const TransportEvent& event, std::uint64_t nowMilliseconds) {
        if (event.sessionEpoch() != epoch_ || !event.transportInstance().value) return false;
        auto found = transports_.find(event.transportInstance().value);
        if (found == transports_.end()) {
            if (!registerAttempt(event.sessionEpoch(), event.transportInstance(),
                                 event.expectedRemoteDeviceId(), event.route(), nowMilliseconds)) return false;
            found = transports_.find(event.transportInstance().value);
        }
        auto& record = found->second;
        if (record.closed) return false;
        record.route = event.route();
        record.state = event.state();
        if (!event.expectedRemoteDeviceId().empty()) record.expectedRemoteDeviceId = event.expectedRemoteDeviceId();
        if (!event.presentedRemoteDeviceId().empty()) record.presentedRemoteDeviceId = event.presentedRemoteDeviceId();
        record.capabilities = event.capabilities();
        if (event.kind() == TransportEventKind::ReceiveAvailable ||
            event.kind() == TransportEventKind::SendAccepted)
            record.lastProgressMilliseconds = nowMilliseconds;
        const auto peer = record.continuityKey();
        if (!peer.empty()) touch(peer);
        return true;
    }

    // Compatibility name retained for the Stage 2 focused test surface.
    bool observeLegacyDecision(const TransportEvent& event) {
        bool accepted = observeTransport(event, 0);
        if (!accepted) return false;
        if (event.state() == TransportState::Active) {
            auto found = transports_.find(event.transportInstance().value);
            if (found != transports_.end()) {
                found->second.locallyTrusted = true;
                found->second.remoteAccepted = true;
                arbitrateWinner(event.sessionEpoch(), event.transportInstance(), 0);
            }
        } else if (event.kind() == TransportEventKind::Closed) {
            closeTransport(event.sessionEpoch(), event.transportInstance(), event.reasonUtf8(),
                           event.state() == TransportState::Failed);
        }
        return true;
    }

    bool markRemoteAccepted(SessionEpoch epoch, TransportInstanceId instance) {
        auto found = current(epoch, instance);
        if (!found) return false;
        (*found)->remoteAccepted = true;
        const auto peer = (*found)->continuityKey();
        if (!peer.empty()) touch(peer);
        return true;
    }

    bool markLocallyTrusted(SessionEpoch epoch, TransportInstanceId instance) {
        auto found = current(epoch, instance);
        if (!found) return false;
        (*found)->locallyTrusted = true;
        (*found)->state = TransportState::LocallyTrusted;
        const auto peer = (*found)->remoteDeviceId();
        if (!peer.empty()) touch(peer);
        return true;
    }

    void observeProgress(SessionEpoch epoch, TransportInstanceId instance,
                         std::uint64_t nowMilliseconds) {
        auto found = current(epoch, instance);
        if (found) (*found)->lastProgressMilliseconds = nowMilliseconds;
    }

    WinnerDecision arbitrateWinner(SessionEpoch epoch, TransportInstanceId instance,
                                   std::uint64_t nowMilliseconds) {
        WinnerDecision decision;
        decision.winner = instance;
        auto candidateFound = current(epoch, instance);
        if (!candidateFound) {
            decision.disposition = WinnerDisposition::RejectedStaleToken;
            decision.reasonUtf8 = "stale transport lifetime";
            return decision;
        }
        auto& candidate = **candidateFound;
        const auto peer = candidate.remoteDeviceId();
        decision.remoteDeviceId = peer;
        if (peer.empty() || !candidate.locallyTrusted || !candidate.remoteAccepted) {
            decision.disposition = WinnerDisposition::RejectedNotReady;
            decision.reasonUtf8 = "transport is not mutually authenticated and trusted";
            return decision;
        }
        auto& continuity = continuityFor(peer);
        if (continuity.activeTransport.value == instance.value) {
            decision.disposition = WinnerDisposition::Activated;
            return decision;
        }
        if (continuity.activeTransport.value) {
            auto existing = transports_.find(continuity.activeTransport.value);
            if (existing != transports_.end() && !existing->second.closed) {
                const bool existingStale = nowMilliseconds >= existing->second.lastProgressMilliseconds &&
                    nowMilliseconds - existing->second.lastProgressMilliseconds > 2000;
                const bool candidateLosesDirection = candidate.route.incoming == (localDeviceId_ < peer);
                if (!existingStale && candidateLosesDirection) {
                    candidate.state = TransportState::Closing;
                    candidate.cancelled = true;
                    decision.disposition = WinnerDisposition::RejectedDuplicate;
                    decision.reasonUtf8 = "duplicate connection loser (deterministic direction)";
                    touch(peer);
                    return decision;
                }
                existing->second.state = TransportState::Closing;
                existing->second.cancelled = true;
                decision.retire.push_back(existing->second.instance);
                decision.disposition = WinnerDisposition::ReplacedExisting;
                decision.reasonUtf8 = existingStale
                    ? "duplicate connection superseded stale authenticated session"
                    : "duplicate connection loser (deterministic direction)";
            }
        }
        if (decision.disposition != WinnerDisposition::ReplacedExisting)
            decision.disposition = WinnerDisposition::Activated;
        candidate.state = TransportState::Active;
        continuity.activeTransport = instance;
        continuity.sessionEstablished = true;
        continuity.session.observeLegacyActive(instance);
        retries_.erase(peer);
        if (!candidate.expectedRemoteDeviceId.empty()) retries_.erase(candidate.expectedRemoteDeviceId);
        touch(peer);
        return decision;
    }

    CloseDecision closeTransport(SessionEpoch epoch, TransportInstanceId instance,
                                 std::string reasonUtf8, bool failed) {
        CloseDecision decision;
        auto found = current(epoch, instance);
        if (!found) return decision;
        auto& record = **found;
        decision.accepted = true;
        decision.remoteDeviceId = record.continuityKey();
        record.closed = true;
        record.state = failed ? TransportState::Failed : TransportState::Closed;
        const auto peer = decision.remoteDeviceId;
        if (!peer.empty()) {
            auto continuity = continuities_.find(peer);
            if (continuity != continuities_.end()) {
                if (continuity->second.activeTransport.value == instance.value) {
                    decision.wasActiveWinner = true;
                    continuity->second.activeTransport = {};
                    continuity->second.session.observeLegacyClosed(instance);
                }
                if (failed) {
                    continuity->second.latestFailedTransport = instance;
                    continuity->second.latestFailedRoute = record.route;
                    continuity->second.latestFailureUtf8 = std::move(reasonUtf8);
                }
                decision.hasActiveWinner = continuity->second.activeTransport.value != 0;
                decision.hasPendingAttempt = hasPendingAttempt(peer, instance);
                touch(peer);
            }
        }
        return decision;
    }

    CancellationDecision prepareImmediateRetry(SessionEpoch epoch, const std::string& peer,
                                               const RouteObservation& replacement, bool manual,
                                               std::uint64_t nowMilliseconds) {
        CancellationDecision decision;
        if (epoch != epoch_) { decision.reasonUtf8 = "stale session lifetime"; return decision; }
        decision.accepted = true;
        for (auto& item : transports_) {
            auto& record = item.second;
            if (record.closed || !record.matches(peer)) continue;
            if (isActive(record.instance)) { decision.retainedActiveWinner = true; continue; }
            if (!manual && record.route.endpointUtf8 == replacement.endpointUtf8) {
                record.route = replacement;
                decision.updateRoute.push_back(record.instance);
                continue;
            }
            record.cancelled = true;
            record.state = TransportState::Closing;
            decision.cancel.push_back(record.instance);
        }
        requestImmediateRetry(peer, replacement, nowMilliseconds);
        touch(peer);
        return decision;
    }

    CancellationDecision cancelRoutes(SessionEpoch epoch, const std::string& peer,
                                      const std::string& endpointUtf8, bool allRoutes,
                                      std::uint64_t expectedNetworkRevision) {
        CancellationDecision decision;
        if (epoch != epoch_) { decision.reasonUtf8 = "stale session lifetime"; return decision; }
        if (expectedNetworkRevision && expectedNetworkRevision < networkRevision_) {
            decision.staleObservation = true;
            decision.reasonUtf8 = "stale network observation ignored";
            return decision;
        }
        decision.accepted = true;
        for (auto& item : transports_) {
            auto& record = item.second;
            if (record.closed || !record.matches(peer)) continue;
            const bool matches = allRoutes
                ? (!expectedNetworkRevision || record.route.networkRevision < expectedNetworkRevision)
                : record.route.endpointUtf8 == endpointUtf8;
            if (!matches) continue;
            if (!allRoutes && isActive(record.instance)) {
                decision.retainedActiveWinner = true;
                continue;
            }
            if (isActive(record.instance)) decision.activeWinnerCancelled = true;
            record.cancelled = true;
            record.state = TransportState::Closing;
            decision.cancel.push_back(record.instance);
        }
        auto retry = retries_.find(peer);
        if (retry != retries_.end()) {
            const bool matches = allRoutes
                ? (!expectedNetworkRevision || retry->second.target.route.networkRevision < expectedNetworkRevision)
                : retry->second.target.route.endpointUtf8 == endpointUtf8;
            if (matches) retries_.erase(retry);
        }
        touch(peer);
        return decision;
    }

    bool observeNetwork(SessionEpoch epoch, std::uint64_t revision) {
        if (epoch != epoch_ || revision < networkRevision_) return false;
        if (revision == networkRevision_) return true;
        networkRevision_ = revision;
        for (auto& item : continuities_) ++item.second.revision;
        return true;
    }

    void seedRetry(const std::string& peer, RouteObservation route, std::uint64_t dueMilliseconds) {
        if (peer.empty()) return;
        auto& retry = retries_[peer];
        retry.target = {peer, std::move(route)};
        retry.dueMilliseconds = dueMilliseconds;
        retry.suspended = false;
        touch(peer);
    }

    void requestImmediateRetry(const std::string& peer, RouteObservation route,
                               std::uint64_t nowMilliseconds) {
        if (peer.empty()) return;
        auto& retry = retries_[peer];
        retry.target = {peer, std::move(route)};
        retry.suspended = false;
        retry.dueMilliseconds = hasActiveWinner(peer) ? never() : nowMilliseconds;
        touch(peer);
    }

    RetrySchedule scheduleRetry(const std::string& peer, RouteObservation route,
                                std::uint64_t nowMilliseconds, std::uint32_t randomValue,
                                bool suspend = false) {
        RetrySchedule result;
        if (peer.empty() || route.endpointUtf8.empty() || hasActiveWinner(peer)) return result;
        auto& retry = retries_[peer];
        retry.target = {peer, std::move(route)};
        if (suspend) {
            retry.suspended = true;
            retry.dueMilliseconds = never();
        } else {
            retry.suspended = false;
            retry.failureCount = std::min<std::uint32_t>(retry.failureCount + 1, 31);
            retry.delayMilliseconds = backoffDelay(retry.failureCount, randomValue);
            retry.dueMilliseconds = nowMilliseconds + retry.delayMilliseconds;
        }
        result = retry.schedule();
        touch(peer);
        return result;
    }

    void suspendRetry(const std::string& peer) {
        auto& retry = retries_[peer];
        retry.target.remoteDeviceId = peer;
        retry.suspended = true;
        retry.dueMilliseconds = never();
        touch(peer);
    }

    std::vector<RetryTarget> takeDueRetries(std::uint64_t nowMilliseconds) {
        std::vector<RetryTarget> due;
        for (auto& item : retries_) {
            auto& retry = item.second;
            if (!retry.suspended && retry.dueMilliseconds <= nowMilliseconds &&
                !hasActiveWinner(item.first) && !hasPendingAttempt(item.first, {})) {
                retry.dueMilliseconds = never();
                due.push_back(retry.target);
                touch(item.first);
            }
        }
        return due;
    }

    bool hasRepresentation(const std::string& peer) const {
        return hasActiveWinner(peer) || hasPendingAttempt(peer, {});
    }

    bool hasActiveWinner(const std::string& peer) const {
        auto found = continuities_.find(peer);
        return found != continuities_.end() && found->second.activeTransport.value != 0;
    }

    void removePeer(const std::string& peer) {
        retries_.erase(peer);
        auto continuity = continuities_.find(peer);
        if (continuity != continuities_.end() && !continuity->second.activeTransport.value)
            continuities_.erase(continuity);
    }

    std::optional<SessionSnapshot> snapshot(const std::string& peer) const {
        auto found = continuities_.find(peer);
        if (found == continuities_.end()) return {};
        SessionSnapshot result;
        result.epoch = epoch_;
        result.revision = found->second.revision;
        result.networkRevision = networkRevision_;
        result.remoteDeviceId = peer;
        result.activeTransport = found->second.activeTransport;
        result.connected = result.activeTransport.value != 0;
        if (result.connected) {
            auto active = transports_.find(result.activeTransport.value);
            if (active != transports_.end()) result.activeRoute = active->second.route;
        }
        result.pending = hasPendingAttempt(peer, {});
        result.latestFailedTransport = found->second.latestFailedTransport;
        result.latestFailedRoute = found->second.latestFailedRoute;
        result.latestFailureUtf8 = found->second.latestFailureUtf8;
        auto retry = retries_.find(peer);
        if (retry != retries_.end()) {
            result.retryScheduled = !retry->second.suspended && retry->second.dueMilliseconds != never();
            result.retrySuspended = retry->second.suspended;
            result.retryDelayMilliseconds = retry->second.delayMilliseconds;
        }
        return result;
    }

    const PeerSession* find(const std::string& verifiedRemoteDeviceId) const {
        auto found = continuities_.find(verifiedRemoteDeviceId);
        return found == continuities_.end() || !found->second.sessionEstablished
            ? nullptr : &found->second.session;
    }

    SessionEpoch epoch() const { return epoch_; }
    std::uint64_t networkRevision() const { return networkRevision_; }

private:
    static constexpr std::uint64_t never() { return std::numeric_limits<std::uint64_t>::max(); }

    struct TransportRecord {
        TransportInstanceId instance;
        RouteObservation route;
        TransportState state = TransportState::Created;
        std::string expectedRemoteDeviceId;
        std::string presentedRemoteDeviceId;
        std::uint64_t capabilities = 0;
        std::uint64_t startedMilliseconds = 0;
        std::uint64_t lastProgressMilliseconds = 0;
        bool locallyTrusted = false;
        bool remoteAccepted = false;
        bool cancelled = false;
        bool closed = false;
        std::string remoteDeviceId() const { return presentedRemoteDeviceId.empty() ? expectedRemoteDeviceId : presentedRemoteDeviceId; }
        std::string continuityKey() const { return !locallyTrusted&&!expectedRemoteDeviceId.empty() ? expectedRemoteDeviceId : remoteDeviceId(); }
        bool matches(const std::string& peer) const { return !peer.empty() && (expectedRemoteDeviceId == peer || presentedRemoteDeviceId == peer); }
    };

    struct ContinuityState {
        explicit ContinuityState(PeerSession value) : session(std::move(value)) {}
        PeerSession session;
        bool sessionEstablished = false;
        TransportInstanceId activeTransport;
        TransportInstanceId latestFailedTransport;
        RouteObservation latestFailedRoute;
        std::string latestFailureUtf8;
        std::uint64_t revision = 0;
    };

    struct RetryState {
        RetryTarget target;
        std::uint64_t dueMilliseconds = never();
        std::uint32_t failureCount = 0;
        std::uint32_t delayMilliseconds = 0;
        bool suspended = false;
        RetrySchedule schedule() const {
            RetrySchedule result;
            result.scheduled = !suspended && dueMilliseconds != never();
            result.suspended = suspended;
            result.failureCount = failureCount;
            result.delayMilliseconds = delayMilliseconds;
            result.dueMilliseconds = dueMilliseconds;
            result.target = target;
            return result;
        }
    };

    std::optional<TransportRecord*> current(SessionEpoch epoch, TransportInstanceId instance) {
        if (epoch != epoch_) return {};
        auto found = transports_.find(instance.value);
        if (found == transports_.end() || found->second.closed) return {};
        return &found->second;
    }

    ContinuityState& continuityFor(const std::string& peer) {
        auto found = continuities_.find(peer);
        if (found == continuities_.end()) {
            PeerSessionIdentity identity{localProfileScope_, localDeviceId_, peer};
            found = continuities_.emplace(peer, ContinuityState(PeerSession(std::move(identity), epoch_))).first;
        }
        return found->second;
    }

    void touch(const std::string& peer) { if (!peer.empty()) ++continuityFor(peer).revision; }

    bool isActive(TransportInstanceId instance) const {
        for (const auto& item : continuities_)
            if (item.second.activeTransport.value == instance.value) return true;
        return false;
    }

    bool hasPendingAttempt(const std::string& peer, TransportInstanceId except) const {
        for (const auto& item : transports_) {
            const auto& record = item.second;
            if (record.instance.value == except.value || record.closed || record.cancelled) continue;
            if (record.matches(peer) && !isActive(record.instance)) return true;
        }
        return false;
    }

    std::uint32_t backoffDelay(std::uint32_t failures, std::uint32_t randomValue) const {
        std::uint64_t base = retryBaseMilliseconds_;
        for (std::uint32_t index = 1; index < failures && base < retryCapMilliseconds_; ++index)
            base = std::min<std::uint64_t>(base * 2, retryCapMilliseconds_);
        const std::uint64_t varied = base * (80 + (randomValue % 41)) / 100;
        return static_cast<std::uint32_t>(std::min<std::uint64_t>(varied, retryCapMilliseconds_));
    }

    std::string localProfileScope_;
    std::string localDeviceId_;
    SessionEpoch epoch_;
    std::uint32_t retryBaseMilliseconds_;
    std::uint32_t retryCapMilliseconds_;
    std::uint64_t networkRevision_ = 0;
    std::map<std::uint64_t, TransportRecord> transports_;
    std::map<std::string, ContinuityState> continuities_;
    std::map<std::string, RetryState> retries_;
};

} // namespace peerchat::session
