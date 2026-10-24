#pragma once

#include "peer_session.hpp"
#include "direct_handoff.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace peerchat::session {

// The profile-scoped Stage 3 continuity owner. Node still owns the concrete
// sockets and executes these decisions on its existing worker, but it no longer
// decides winners, retry timing, cancellation eligibility, or stale-token
// validity. Stage 4 delivery/ACK state is a separate profile-scoped owner and
// consumes authenticated transport-instance identities without living here.
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
          retryCapMilliseconds_(retryCapMilliseconds), handoff_(localDeviceId_) {}

    // Server relay is the privacy-preserving default for reliable control.
    // DirectOnly is deliberately opt-in for the retained diagnostic path.
    void setDirectOnly(bool value) { directOnly_ = value; }

    // Observations are not demand. Platform adapters enable this policy for
    // server-assisted profiles; explicit direct/manual transports are unchanged.
    void setDemandDriven(bool value) { demandDriven_ = value; }
    void demandLiveRoute(const std::string& peer,std::uint64_t untilMilliseconds,std::uint64_t now=0) {
        auto& until=liveDemand_[peer];const bool newlyDemanded=until<=now;until=std::max(until,untilMilliseconds);
        auto plan=plans_.find(peer);
        if(newlyDemanded&&plan!=plans_.end()&&plan->second.relayAvailable&&!plan->second.suspended&&!hasActiveWinner(peer))plan->second.relayDueMilliseconds=now;
    }
    // Explicit authenticated incoming work may expedite a disconnected route.
    // Ordinary presence/polling retains backoff; repeated hints are rate bounded.
    void expediteLiveRoute(const std::string& peer,std::uint64_t until,std::uint64_t now) {
        demandLiveRoute(peer,until,now);
        auto found=plans_.find(peer);
        if(found==plans_.end()||!found->second.relayAvailable||found->second.suspended||hasRepresentation(peer))return;
        auto& plan=found->second;
        if(now<plan.expediteAllowedAt)return;
        plan.expediteAllowedAt=now+1000;
        plan.relayDueMilliseconds=now;
    }
    bool liveRouteDemanded(const std::string& peer,std::uint64_t now) const {
        auto found=liveDemand_.find(peer);
        return !demandDriven_||directOnly_||(found!=liveDemand_.end()&&found->second>now);
    }
    // Archive jobs are bounded demand on this SAME retry/winner owner. They
    // never borrow a friend's continuity or make a same-account device trusted.
    bool demandArchiveRoute(const ReliableAuthorization& authorization,std::uint64_t until,
                            std::uint64_t network,std::uint64_t now){
        const auto& a=authorization.archive;
        if(!authorization.valid()||authorization.scope!=ReliableAuthorizationScope::ArchiveControl||until<=now||until-now>60000||
           network<networkRevision_||!((a.source==localDeviceId_&&a.reader==authorization.expectedRemoteDeviceId)||
           (a.reader==localDeviceId_&&a.source==authorization.expectedRemoteDeviceId)))return false;
        const auto key=authorization.continuityKey();auto found=plans_.find(key);
        if(found==plans_.end()){
            size_t scoped=0;for(const auto& entry:plans_)if(entry.second.authorization.scope==ReliableAuthorizationScope::ArchiveControl)++scoped;
            if(scoped>=8)return false;
        }else{
            const auto& previous=found->second.authorization.archive;
            if(previous.sourceGeneration!=a.sourceGeneration||previous.readerGeneration!=a.readerGeneration||
               previous.sourceGrantId!=a.sourceGrantId||previous.readerGrantId!=a.readerGrantId)return false;
        }
        auto& plan=plans_[key];const bool changed=plan.networkRevision!=network||!plan.relayAvailable;
        plan.authorization=authorization;plan.networkRevision=network;plan.relayAvailable=true;plan.suspended=false;
        if(changed)plan.relayDueMilliseconds=now;
        demandLiveRoute(key,until,now);return true;
    }
    // Additive demand adapter for existing authenticated group-control scopes.
    // The application verifies membership; this owner retains retry/winner policy.
    bool demandGroupRoute(const ReliableAuthorization& authorization,std::uint64_t until,
                          std::uint64_t network,std::uint64_t now){
        if(!authorization.valid()||authorization.scope!=ReliableAuthorizationScope::GroupControl||until<=now||
           until-now>60000||network<networkRevision_||authorization.expectedRemoteDeviceId==localDeviceId_)return false;
        auto key=authorization.continuityKey();auto found=plans_.find(key);
        if(found==plans_.end()){
            size_t count=0;for(const auto& p:plans_)if(p.second.authorization.scope==ReliableAuthorizationScope::GroupControl)++count;
            // Match the bounded authenticated input (128 groups x256 devices).
            // Physical/pending attempts remain capped by the platform executor;
            // offline early peers must not exclude later eligible memberships.
            if(count>=128u*256u)return false;
        }else if(found->second.authorization.rosterGeneration!=authorization.rosterGeneration)return false;
        auto& plan=plans_[key];bool changed=plan.networkRevision!=network||!plan.relayAvailable;
        const auto previousRetry=plan.relayDueMilliseconds;
        const bool retainBackoff=!changed&&plan.relayFailures&&previousRetry!=never();
        plan.authorization=authorization;plan.networkRevision=network;plan.relayAvailable=true;plan.suspended=false;
        if(changed){plan.relayFailures=0;plan.relayDelayMilliseconds=0;plan.relayDueMilliseconds=now;}
        demandLiveRoute(key,until,now);
        // Polling renews the same short group demand. Expiry during a busy
        // native turn must not turn that renewal into a new failed-scope retry.
        // Network changes and completed/revoked plans establish fresh demand.
        if(retainBackoff)plan.relayDueMilliseconds=previousRetry;
        return true;
    }
    std::vector<TransportInstanceId> finishArchiveRoute(const ReliableAuthorization& authorization){
        std::vector<TransportInstanceId> retired;
        if(authorization.scope!=ReliableAuthorizationScope::ArchiveControl||!authorization.valid())return retired;
        const auto key=authorization.continuityKey();
        const auto sameGeneration=[&](const ReliableAuthorization& value){
            const auto& a=authorization.archive;const auto& b=value.archive;
            return value.scope==ReliableAuthorizationScope::ArchiveControl&&value.continuityKey()==key&&
                a.sourceGeneration==b.sourceGeneration&&a.readerGeneration==b.readerGeneration&&
                a.sourceGrantId==b.sourceGrantId&&a.readerGrantId==b.readerGrantId;
        };
        auto plan=plans_.find(key);if(plan!=plans_.end()&&!sameGeneration(plan->second.authorization))return retired;
        for(const auto& item:transports_)if(!item.second.closed&&item.second.continuityKey()==key&&!sameGeneration(item.second.authorization))return retired;
        for(auto& item:transports_)if(!item.second.closed&&item.second.continuityKey()==key){
            item.second.cancelled=true;retired.push_back(item.second.instance);closeTransport(epoch_,item.second.instance,"archive work finished or cancelled",false);
        }
        removePeer(key);return retired;
    }
    std::vector<TransportInstanceId> finishGroupRoute(const ReliableAuthorization& authorization){
        std::vector<TransportInstanceId> retired;
        if(!authorization.valid()||authorization.scope!=ReliableAuthorizationScope::GroupControl)return retired;
        const auto key=authorization.continuityKey();auto plan=plans_.find(key);
        if(plan!=plans_.end()&&(plan->second.authorization.scope!=ReliableAuthorizationScope::GroupControl||
            plan->second.authorization.rosterGeneration!=authorization.rosterGeneration))return retired;
        for(const auto& row:transports_)if(!row.second.closed&&row.second.continuityKey()==key&&
            (row.second.authorization.scope!=ReliableAuthorizationScope::GroupControl||row.second.authorization.rosterGeneration!=authorization.rosterGeneration))return retired;
        for(auto& row:transports_)if(!row.second.closed&&row.second.continuityKey()==key){row.second.cancelled=true;retired.push_back(row.second.instance);closeTransport(epoch_,row.second.instance,"group key control complete",false);}
        removePeer(key);return retired;
    }
    std::vector<TransportInstanceId> retireIdleRoutes(std::uint64_t now) {
        std::vector<TransportInstanceId> result;
        for(auto& item:transports_) {
            auto& record=item.second;
            if(record.closed||record.cancelled||record.authorization.scope==ReliableAuthorizationScope::GroupControl||
               record.route.kind!=TransportKind::Relay||p2p_.count(record.continuityKey())||liveRouteDemanded(record.continuityKey(),now))continue;
            record.cancelled=true;result.push_back(record.instance);
            closeTransport(epoch_,record.instance,"no live route demand",false);
        }
        return result;
    }

    bool registerAttempt(SessionEpoch epoch, TransportInstanceId instance,
                         std::string expectedRemoteDeviceId,
                         RouteObservation route, std::uint64_t nowMilliseconds) {
        if (epoch != epoch_ || !instance.value) return false;
        auto found = transports_.find(instance.value);
        if (found != transports_.end()) {
            return found->second.authorization.scope == ReliableAuthorizationScope::Friend &&
                   found->second.expectedRemoteDeviceId == expectedRemoteDeviceId;
        }
        TransportRecord record;
        record.handoffCandidate=canPrepareHandoff(expectedRemoteDeviceId,route);
        record.instance = instance;
        record.expectedRemoteDeviceId = std::move(expectedRemoteDeviceId);
        record.authorization.expectedRemoteDeviceId = record.expectedRemoteDeviceId;
        record.route = std::move(route);
        record.state = TransportState::Connecting;
        record.startedMilliseconds = record.lastProgressMilliseconds = nowMilliseconds;
        transports_.emplace(instance.value, std::move(record));
        const auto& expected = transports_.at(instance.value).expectedRemoteDeviceId;
        if (!expected.empty()) touch(expected);
        return true;
    }

    bool registerGroupControlAttempt(SessionEpoch epoch, TransportInstanceId instance,
                                     ReliableAuthorization authorization,
                                     RouteObservation route, std::uint64_t nowMilliseconds) {
        return authorization.scope==ReliableAuthorizationScope::GroupControl&&
            registerScopedAttempt(epoch,instance,std::move(authorization),std::move(route),nowMilliseconds);
    }
    bool registerArchiveControlAttempt(SessionEpoch epoch,TransportInstanceId instance,
                                      ReliableAuthorization authorization,RouteObservation route,std::uint64_t nowMilliseconds){
        const auto& scope=authorization.archive;
        if(authorization.scope!=ReliableAuthorizationScope::ArchiveControl||
            !((scope.source==localDeviceId_&&scope.reader==authorization.expectedRemoteDeviceId)||
              (scope.reader==localDeviceId_&&scope.source==authorization.expectedRemoteDeviceId)))return false;
        return registerScopedAttempt(epoch,instance,std::move(authorization),std::move(route),nowMilliseconds);
    }
    // The physical endpoint adopts the scheduler's existing pending instance;
    // it does not register a duplicate, reset backoff or replace a winner.
    // Incoming role is learned at pairing; every already-known route/scope
    // field must match. RelayRequests::attach remains the one-shot handoff.
    bool recognizeArchiveControlAttempt(SessionEpoch epoch,TransportInstanceId instance,
                                       const ReliableAuthorization& authorization,const RouteObservation& route)const{
        if(epoch!=epoch_||!authorization.valid()||!authorization.permitsArchiveControl()||route.networkRevision<networkRevision_)return false;
        auto found=transports_.find(instance.value);if(found==transports_.end())return false;
        const auto& record=found->second;const auto& old=record.authorization;const auto& a=old.archive;const auto& b=authorization.archive;
        return !record.closed&&!record.cancelled&&record.state==TransportState::Connecting&&!record.locallyTrusted&&!record.remoteAccepted&&
            old.scope==authorization.scope&&old.groupId==authorization.groupId&&old.rosterGeneration==authorization.rosterGeneration&&
            old.expectedRemoteDeviceId==authorization.expectedRemoteDeviceId&&a.account==b.account&&a.issuer==b.issuer&&a.source==b.source&&a.reader==b.reader&&
            a.sourceGeneration==b.sourceGeneration&&a.readerGeneration==b.readerGeneration&&a.sourceGrantId==b.sourceGrantId&&a.readerGrantId==b.readerGrantId&&
            record.route.revision.value==route.revision.value&&record.route.networkRevision==route.networkRevision&&record.route.endpointUtf8==route.endpointUtf8&&
            record.route.kind==route.kind&&record.route.routeClass==route.routeClass&&record.route.sourceUtf8==route.sourceUtf8&&record.route.routeUtf8==route.routeUtf8;
    }
    bool recognizeGroupControlAttempt(SessionEpoch epoch,TransportInstanceId instance,
                                     const ReliableAuthorization& authorization,const RouteObservation& route)const{
        if(epoch!=epoch_||!authorization.valid()||authorization.scope!=ReliableAuthorizationScope::GroupControl||route.networkRevision<networkRevision_)return false;
        auto found=transports_.find(instance.value);if(found==transports_.end())return false;
        const auto& record=found->second;const auto& old=record.authorization;
        return !record.closed&&!record.cancelled&&record.state==TransportState::Connecting&&!record.locallyTrusted&&!record.remoteAccepted&&
            old.scope==authorization.scope&&old.groupId==authorization.groupId&&old.rosterGeneration==authorization.rosterGeneration&&
            old.expectedRemoteDeviceId==authorization.expectedRemoteDeviceId&&record.route.revision.value==route.revision.value&&
            record.route.networkRevision==route.networkRevision&&record.route.endpointUtf8==route.endpointUtf8&&record.route.kind==route.kind&&
            record.route.routeClass==route.routeClass&&record.route.sourceUtf8==route.sourceUtf8&&record.route.routeUtf8==route.routeUtf8;
    }
private:
    // The same lifetime/winner machinery owns both restricted purposes.
    bool registerScopedAttempt(SessionEpoch epoch,TransportInstanceId instance,
                               ReliableAuthorization authorization,RouteObservation route,std::uint64_t nowMilliseconds){
        if (epoch != epoch_ || !instance.value || !authorization.valid() ||
            authorization.scope == ReliableAuthorizationScope::Friend ||
            route.kind != TransportKind::Relay || route.routeClass != RouteClass::ServerRelay ||
            transports_.count(instance.value)) return false;
        const auto remote = authorization.expectedRemoteDeviceId;
        TransportRecord record;
        record.instance = instance;
        record.route = std::move(route);
        record.expectedRemoteDeviceId = remote;
        record.authorization = std::move(authorization);
        record.state = TransportState::Connecting;
        record.startedMilliseconds = record.lastProgressMilliseconds = nowMilliseconds;
        const auto key = record.continuityKey();
        transports_.emplace(instance.value, std::move(record));
        continuityFor(key, remote);
        touch(key);
        return true;
    }
public:
    std::optional<ReliableAuthorization> authorization(SessionEpoch epoch,
                                                        TransportInstanceId instance) const {
        if (epoch != epoch_) return {};
        auto found = transports_.find(instance.value);
        if (found == transports_.end() || found->second.closed || found->second.cancelled) return {};
        return found->second.authorization;
    }

    // The authenticated roster provider calls this on every epoch change or
    // revocation. The caller closes the returned physical relay instances.
    std::vector<TransportInstanceId> invalidateGroupControl(SessionEpoch epoch,
                                                              const std::string& groupId,
                                                              std::uint64_t currentGeneration) {
        std::vector<TransportInstanceId> closed;
        if (epoch != epoch_ || !ReliableAuthorization::hexId(groupId, 32)) return closed;
        std::vector<std::string> removedPlans;
        for(const auto& p:plans_)if(p.second.authorization.scope==ReliableAuthorizationScope::GroupControl&&
            p.second.authorization.groupId==groupId&&(!currentGeneration||p.second.authorization.rosterGeneration!=currentGeneration))removedPlans.push_back(p.first);
        for(const auto& key:removedPlans)removePeer(key);
        for (auto& item : transports_) {
            auto& record = item.second;
            if (record.closed || record.authorization.scope != ReliableAuthorizationScope::GroupControl ||
                record.authorization.groupId != groupId ||
                (currentGeneration && record.authorization.rosterGeneration == currentGeneration)) continue;
            record.cancelled = true;
            closed.push_back(record.instance);
            closeTransport(epoch, record.instance, "group-control roster authorization superseded", false);
        }
        return closed;
    }

    std::vector<TransportInstanceId> invalidateArchiveControl(SessionEpoch epoch,const std::string& issuer,
                                                              const std::string& device,std::uint64_t currentGeneration){
        std::vector<TransportInstanceId> closed;
        if(epoch!=epoch_||!ReliableAuthorization::hexId(issuer,64)||!ReliableAuthorization::hexId(device,64))return closed;
        for(auto& item:transports_){auto& record=item.second;const auto& scope=record.authorization.archive;
            if(record.closed||record.cancelled||record.authorization.scope!=ReliableAuthorizationScope::ArchiveControl||scope.issuer!=issuer)continue;
            const bool source=scope.source==device,reader=scope.reader==device;
            if(!source&&!reader&&!(device==issuer&&!currentGeneration))continue;
            if(currentGeneration&&((source&&scope.sourceGeneration==currentGeneration)||(reader&&scope.readerGeneration==currentGeneration)))continue;
            record.cancelled=true;closed.push_back(record.instance);
            closeTransport(epoch,record.instance,"archive authorization superseded or revoked",false);
        }
        std::vector<std::string> invalidPlans;
        for(const auto& item:plans_){const auto& authorization=item.second.authorization;const auto& scope=authorization.archive;
            if(authorization.scope!=ReliableAuthorizationScope::ArchiveControl||scope.issuer!=issuer)continue;
            const bool source=scope.source==device,reader=scope.reader==device;
            if(!source&&!reader&&!(device==issuer&&!currentGeneration))continue;
            if(currentGeneration&&((source&&scope.sourceGeneration==currentGeneration)||(reader&&scope.readerGeneration==currentGeneration)))continue;
            invalidPlans.push_back(item.first);
        }
        for(const auto& key:invalidPlans)removePeer(key);
        return closed;
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
        if (record.closed || record.cancelled) return false;
        if (record.authorization.scope != ReliableAuthorizationScope::Friend &&
            ((!event.expectedRemoteDeviceId().empty() &&
              event.expectedRemoteDeviceId() != record.authorization.expectedRemoteDeviceId) ||
             event.route().kind != TransportKind::Relay ||
             event.route().routeClass != RouteClass::ServerRelay ||
             (!event.presentedRemoteDeviceId().empty() &&
              event.presentedRemoteDeviceId() != record.authorization.expectedRemoteDeviceId)))
            return false;
        record.route = event.route();
        record.state = event.state();
        if (!event.expectedRemoteDeviceId().empty()) record.expectedRemoteDeviceId = event.expectedRemoteDeviceId();
        if (!event.presentedRemoteDeviceId().empty()) record.presentedRemoteDeviceId = event.presentedRemoteDeviceId();
        record.capabilities = event.capabilities();
        if((record.capabilities&DirectHandoffCapability)&&(record.capabilities&HandoffCandidateIntent))record.handoffCandidate=true;
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
        const auto peer = (*found)->continuityKey();
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
        if (candidate.authorization.scope != ReliableAuthorizationScope::Friend) {
            if (candidate.route.kind != TransportKind::Relay ||
                candidate.route.routeClass != RouteClass::ServerRelay ||
                candidate.expectedRemoteDeviceId != candidate.presentedRemoteDeviceId ||
                candidate.expectedRemoteDeviceId != candidate.authorization.expectedRemoteDeviceId) {
                decision.disposition = WinnerDisposition::RejectedNotReady;
                decision.reasonUtf8 = "group-control identity or route is not authorized";
                return decision;
            }
            const auto key = candidate.continuityKey();
            auto& scoped = continuityFor(key, peer);
            if (scoped.activeTransport.value && scoped.activeTransport.value != instance.value) {
                auto previous = transports_.find(scoped.activeTransport.value);
                if (previous != transports_.end() && !previous->second.closed) {
                    previous->second.cancelled = true;
                    previous->second.state = TransportState::Closing;
                    decision.retire.push_back(previous->second.instance);
                    decision.disposition = WinnerDisposition::ReplacedExisting;
                    decision.reasonUtf8 = "group-control relay replaced prior scoped session";
                }
            }
            if (decision.disposition != WinnerDisposition::ReplacedExisting)
                decision.disposition = WinnerDisposition::Activated;
            candidate.state = TransportState::Active;
            scoped.activeTransport = instance;
            scoped.sessionEstablished = true;
            scoped.session.observeLegacyActive(instance);
            auto plan=plans_.find(key);if(plan!=plans_.end()){plan->second.relayFailures=0;plan->second.relayDelayMilliseconds=0;}
            touch(key);
            return decision;
        }
        auto& continuity = continuityFor(peer);
        // A consent-scoped direct route is deliberately not a delivery winner.
        // It may authenticate beside relay, but only an explicit later command
        // can start the existing make-before-break handoff.
        if (candidate.route.kind == TransportKind::Direct) {
            auto authorization = p2p_.find(peer);
            if (authorization != p2p_.end() && authorization->second.availability == P2pAvailability::Testing &&
                authorization->second.route.endpointUtf8 == candidate.route.endpointUtf8 &&
                authorization->second.networkRevision == candidate.route.networkRevision &&
                authorization->second.expiryMilliseconds > nowMilliseconds && continuity.activeTransport.value) {
                auto active = transports_.find(continuity.activeTransport.value);
                if (active != transports_.end() && !active->second.closed &&
                    active->second.route.kind == TransportKind::Relay) {
                    candidate.standby = true;
                    candidate.state = TransportState::LocallyTrusted;
                    authorization->second.standby = instance;
                    authorization->second.availability = P2pAvailability::Available;
                    // Arm both peers for the proven handoff protocol, but do
                    // not emit Hello until PromoteVerifiedP2P is requested.
                    handoff_.attach(instance, peer, handoffNonce(instance), nowMilliseconds);
                    touch(peer);
                    decision.disposition = WinnerDisposition::VerifiedStandby;
                    decision.reasonUtf8 = "verified direct P2P standby available; relay remains active";
                    return decision;
                }
            }
        }
        if(candidate.handoffCandidate&&!continuity.activeTransport.value){
            decision.disposition=WinnerDisposition::RejectedNotReady;
            decision.reasonUtf8="handoff has no retained original binding";return decision;
        }
        if (continuity.activeTransport.value == instance.value) {
            decision.disposition = WinnerDisposition::Activated;
            return decision;
        }
        if (continuity.activeTransport.value) {
            auto existing = transports_.find(continuity.activeTransport.value);
            if (existing != transports_.end() && !existing->second.closed) {
                // A delayed relay completion can never replace a healthy direct
                // winner. Direct candidates may replace relay only through the
                // authenticated Stage 5 make-before-break exchange below.
                if(existing->second.route.kind==TransportKind::Direct&&candidate.route.kind==TransportKind::Relay){
                    candidate.state=TransportState::Closing;candidate.cancelled=true;
                    decision.disposition=WinnerDisposition::RejectedDuplicate;
                    decision.reasonUtf8="relay candidate superseded by healthy direct winner";
                    touch(peer);return decision;
                }
                if (candidate.handoffCandidate&&(candidate.capabilities & DirectHandoffCapability) &&
                    (existing->second.capabilities & DirectHandoffCapability) && handoff_.ready(existing->second.instance)) {
                    const auto required=existing->second.capabilities&~HandoffCandidateIntent;
                    if((candidate.capabilities&required)!=required){
                        decision.disposition=WinnerDisposition::RejectedNotReady;
                        decision.reasonUtf8="candidate lacks active channel capabilities";return decision;
                    }
                    decision.disposition = WinnerDisposition::AwaitingHandoff;
                    decision.reasonUtf8 = "authenticated candidate awaiting coordinated handoff";
                    return decision;
                }
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
        if(candidate.route.kind==TransportKind::Direct){
            retries_.erase(peer);
            if (!candidate.expectedRemoteDeviceId.empty()) retries_.erase(candidate.expectedRemoteDeviceId);
        }
        auto plan=plans_.find(peer);
        if(plan!=plans_.end()){
            if(candidate.route.kind==TransportKind::Relay){
                plan->second.relayFailures=0;plan->second.relayDelayMilliseconds=0;
                for(auto& route:plan->second.direct)if(route.dueMilliseconds==never())route.dueMilliseconds=nowMilliseconds+retryBaseMilliseconds_;
            }else plan->second.relayDueMilliseconds=never();
        }
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
        auto continuity = !peer.empty() ? continuities_.find(peer) : continuities_.end();
        if (continuity != continuities_.end() && continuity->second.activeTransport.value == instance.value) {
            auto scoped=p2p_.find(peer);
            if(scoped!=p2p_.end()&&scoped->second.availability==P2pAvailability::Promoted&&scoped->second.standby.value==instance.value&&scoped->second.relay.value){
                auto relay=transports_.find(scoped->second.relay.value);
                if(relay!=transports_.end()&&!relay->second.closed&&trusted(relay->second)){
                    continuity->second.activeTransport=relay->second.instance;continuity->second.session.observeLegacyActive(relay->second.instance);
                    relay->second.state=TransportState::Active;scoped->second.standby={};scoped->second.availability=P2pAvailability::Unavailable;
                    pendingHandoffEffects_.push_back({HandoffEffect::Kind::Restore,relay->second.instance,instance,{},"direct P2P failed; restored retained relay"});
                }
            }
        }
        if (!peer.empty()) {
            auto scoped=p2p_.find(peer);
            if(scoped!=p2p_.end()&&scoped->second.standby.value==instance.value) {
                scoped->second.standby={};
                if(scoped->second.availability!=P2pAvailability::Promoted) scoped->second.availability=P2pAvailability::Unavailable;
            }
        }
        auto recovery = applyHandoff(handoff_.closed(instance));
        pendingHandoffEffects_.insert(pendingHandoffEffects_.end(),recovery.begin(),recovery.end());
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
            if(handoff_.draining(record.instance)||handoff_.busy(peer))continue;
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
            if (isActive(record.instance)&&(!allRoutes||
                ((record.capabilities&DirectHandoffCapability)&&handoff_.ready(record.instance)))) {
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
        auto plan=plans_.find(peer);
        if(plan!=plans_.end()){plan->second.suspended=false;touch(peer);return;}
        auto& retry = retries_[peer];
        retry.target = {peer, std::move(route)};
        retry.suspended = false;
        auto activeKind=activeTransportKind(peer);
        retry.dueMilliseconds = activeKind&&*activeKind==TransportKind::Direct ? never() : nowMilliseconds;
        touch(peer);
    }

    RetrySchedule scheduleRetry(const std::string& peer, RouteObservation route,
                                std::uint64_t nowMilliseconds, std::uint32_t randomValue,
                                bool suspend = false) {
        RetrySchedule result;
        if (peer.empty() || route.endpointUtf8.empty()) return result;
        auto plan=plans_.find(peer);
        if(plan!=plans_.end()&&route.kind==TransportKind::Direct){
            if(suspend){plan->second.suspended=true;for(auto& candidate:plan->second.direct)candidate.dueMilliseconds=never();plan->second.relayDueMilliseconds=never();result.suspended=true;result.target={peer,route};touch(peer);return result;}
            CandidateRoute* failed=nullptr;
            for(auto& candidate:plan->second.direct)if(candidate.route.endpointUtf8==route.endpointUtf8){failed=&candidate;break;}
            if(failed){
                failed->failures=std::min<std::uint32_t>(failed->failures+1,31);
                failed->delayMilliseconds=backoffDelay(failed->failures,randomValue);
                failed->dueMilliseconds=nowMilliseconds+failed->delayMilliseconds;
                for(auto& candidate:plan->second.direct)if(candidate.priority>failed->priority&&candidate.failures==0){candidate.dueMilliseconds=nowMilliseconds;break;}
                result.scheduled=true;result.failureCount=failed->failures;result.delayMilliseconds=failed->delayMilliseconds;
                result.dueMilliseconds=failed->dueMilliseconds;result.target={peer,failed->route};touch(peer);return result;
            }
        }
        auto activeKind=activeTransportKind(peer);
        if(activeKind&&*activeKind==TransportKind::Direct)return result;
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
        auto plan=plans_.find(peer);if(plan!=plans_.end()){plan->second.suspended=true;for(auto& candidate:plan->second.direct)candidate.dueMilliseconds=never();plan->second.relayDueMilliseconds=never();touch(peer);return;}
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
        for(auto& item:plans_){
            const auto activeKind=activeTransportKind(item.first);
            if(item.second.suspended||item.second.networkRevision<networkRevision_)continue;
            if(!directOnly_) {
                auto authorization=p2p_.find(item.first);
                if(authorization==p2p_.end()||authorization->second.availability!=P2pAvailability::Testing) continue;
                if(authorization->second.expiryMilliseconds<=nowMilliseconds){authorization->second.availability=P2pAvailability::Unavailable;item.second.direct.clear();touch(item.first);continue;}
            }
            if(activeKind&&*activeKind==TransportKind::Direct)continue;
            if(hasPendingAttempt(item.first,{}))continue;
            CandidateRoute* selected=nullptr;
            for(auto& candidate:item.second.direct)
                if(candidate.dueMilliseconds<=nowMilliseconds&&(!selected||candidate.priority<selected->priority))selected=&candidate;
            if(selected){selected->dueMilliseconds=never();due.push_back({item.first,selected->route});touch(item.first);}
        }
        return due;
    }

    // The only normal-profile direct entry point. Ordinary discovery/saved
    // endpoints lack the scoped token and cannot construct this operation.
    bool beginScopedP2pProbe(SessionEpoch epoch,const std::string& peer,const std::string& authorizationId,
                             std::uint64_t authorizationExpiryMilliseconds,RouteObservation route,std::uint64_t nowMilliseconds) {
        if(epoch!=epoch_||peer.empty()||route.kind!=TransportKind::Direct||route.endpointUtf8.empty()||
           authorizationId.size()!=43||authorizationExpiryMilliseconds<=nowMilliseconds||
           route.networkRevision!=networkRevision_||!hasActiveRelay(peer)) return false;
        if(auto existing=p2p_.find(peer);existing!=p2p_.end()&&existing->second.availability!=P2pAvailability::Unavailable)return false;
        p2p_[peer]={authorizationId,authorizationExpiryMilliseconds,route.networkRevision,P2pAvailability::Testing,{}, {},route};
        auto& plan=plans_[peer];plan.direct.clear();plan.direct.push_back({std::move(route),0,0,0,nowMilliseconds});plan.networkRevision=networkRevision_;
        touch(peer);return true;
    }

    std::vector<HandoffEffect> promoteVerifiedP2P(SessionEpoch epoch,const std::string& peer,
                                                   const std::string& authorizationId,std::uint64_t nowMilliseconds) {
        if(epoch!=epoch_)return {};
        auto state=p2p_.find(peer);
        if(state==p2p_.end()||state->second.availability!=P2pAvailability::Available||state->second.id!=authorizationId||
           state->second.expiryMilliseconds<=nowMilliseconds||state->second.networkRevision!=networkRevision_||
           !state->second.standby.value||!hasActiveRelay(peer))return {};
        auto standby=transports_.find(state->second.standby.value);
        if(standby==transports_.end()||!trusted(standby->second)||!standby->second.standby)return {};
        state->second.availability=P2pAvailability::Promoted;state->second.relay=continuityFor(peer).activeTransport;
        standby->second.handoffAttached=true;standby->second.handoffBegan=nowMilliseconds;touch(peer);
        return handoff_.hello(standby->second.instance);
    }

    std::vector<TransportInstanceId> cancelScopedP2p(SessionEpoch epoch,const std::string& peer) {
        if(epoch!=epoch_)return {};
        std::vector<TransportInstanceId> result;auto state=p2p_.find(peer);
        if(state==p2p_.end())return result;
        for(auto& item:transports_)if(item.second.matches(peer)&&!item.second.closed&&item.second.route.endpointUtf8==state->second.route.endpointUtf8){item.second.cancelled=true;item.second.state=TransportState::Closing;result.push_back(item.second.instance);}
        p2p_.erase(state);auto plan=plans_.find(peer);if(plan!=plans_.end())plan->second.direct.clear();touch(peer);return result;
    }

    // Candidate providers replace observations; they never choose a winner or
    // start a transport. Stale network generations are rejected here.
    bool replaceCandidates(SessionEpoch epoch,const std::string& peer,
                           std::vector<RouteObservation> direct,bool relayAvailable,
                           std::uint64_t networkRevision,std::uint64_t nowMilliseconds,bool forceImmediate=false) {
        if(epoch!=epoch_||peer.empty()||networkRevision<networkRevision_)return false;
        auto& plan=plans_[peer];
        const auto previousRevision=plan.networkRevision,previousRelayDue=plan.relayDueMilliseconds;
        const bool previousRelayAvailable=plan.relayAvailable;const bool hadDirect=!plan.direct.empty();
        std::map<std::string,CandidateRoute> previous;
        for(auto& item:plan.direct)previous.emplace(item.route.endpointUtf8,item);
        bool changed=forceImmediate||previousRevision!=networkRevision||previous.size()!=direct.size();
        if(!changed)for(const auto& route:direct)if(!previous.count(route.endpointUtf8)){changed=true;break;}
        auto legacyRetry=retries_.find(peer);if(legacyRetry!=retries_.end()){if(legacyRetry->second.suspended)plan.suspended=true;retries_.erase(legacyRetry);}
        if(changed)plan.suspended=false;
        plan.direct.clear();unsigned priority=0;
        for(auto& route:direct){
            if(route.endpointUtf8.empty()||route.kind!=TransportKind::Direct||route.networkRevision!=networkRevision)continue;
            CandidateRoute candidate;candidate.route=std::move(route);candidate.priority=priority++;candidate.dueMilliseconds=never();
            auto old=previous.find(candidate.route.endpointUtf8);
            if(old!=previous.end()){candidate.failures=old->second.failures;candidate.delayMilliseconds=old->second.delayMilliseconds;candidate.dueMilliseconds=old->second.dueMilliseconds;}
            plan.direct.push_back(std::move(candidate));
        }
        plan.networkRevision=networkRevision;plan.relayAvailable=relayAvailable;
        plan.beganMilliseconds=nowMilliseconds;
        const auto activeKind=activeTransportKind(peer);
        if(!activeKind||*activeKind==TransportKind::Relay){
            if(directOnly_&&!plan.suspended&&!plan.direct.empty()&&(!hadDirect||changed))plan.direct.front().dueMilliseconds=nowMilliseconds;
            // Normal profiles are server-first: a discovery endpoint can never
            // start direct TCP; relay is requested immediately.
            plan.relayDueMilliseconds=!plan.suspended&&relayAvailable?(directOnly_?(previousRelayAvailable&&!changed?previousRelayDue:nowMilliseconds+DirectFallbackWindowMilliseconds):nowMilliseconds):never();
        }else plan.relayDueMilliseconds=never();
        touch(peer);return true;
    }

    struct RelayAttempt { std::string remoteDeviceId;std::uint64_t networkRevision=0;ReliableAuthorization authorization{}; };
    std::vector<RelayAttempt> takeDueRelayAttempts(std::uint64_t nowMilliseconds) {
        std::vector<RelayAttempt> result;
        for(auto& item:plans_){auto& plan=item.second;
            if(!liveRouteDemanded(item.first,nowMilliseconds))continue;
            auto activeKind=activeTransportKind(item.first);
            if(plan.suspended||!plan.relayAvailable||plan.networkRevision<networkRevision_||
               (activeKind&&*activeKind==TransportKind::Direct)||
               plan.relayDueMilliseconds>nowMilliseconds)continue;
            bool relayPending=false;
            for(const auto& transport:transports_)if(!transport.second.closed&&!transport.second.cancelled&&
                transport.second.matches(item.first)&&transport.second.route.kind==TransportKind::Relay){relayPending=true;break;}
            if(relayPending)continue;
            plan.relayDueMilliseconds=never();
            result.push_back({plan.authorization.scope!=ReliableAuthorizationScope::Friend?plan.authorization.expectedRemoteDeviceId:item.first,plan.networkRevision,plan.authorization});touch(item.first);
        }
        return result;
    }

    std::optional<std::string> attachablePeer(SessionEpoch epoch,TransportInstanceId instance) const {
        if(epoch!=epoch_)return {};
        auto found=transports_.find(instance.value);
        if(found==transports_.end()||found->second.closed||found->second.cancelled||
           found->second.route.networkRevision<networkRevision_)return {};
        auto peer=found->second.continuityKey();
        return peer.empty()?std::optional<std::string>{}:std::optional<std::string>{peer};
    }

    RetrySchedule scheduleRelayRetry(const std::string& peer,std::uint64_t nowMilliseconds,
                                     std::uint32_t randomValue) {
        RetrySchedule result;auto found=plans_.find(peer);if(found==plans_.end()||found->second.suspended||!found->second.relayAvailable)return result;
        auto& plan=found->second;plan.relayFailures=std::min<std::uint32_t>(plan.relayFailures+1,31);
        plan.relayDelayMilliseconds=backoffDelay(plan.relayFailures,randomValue);
        plan.relayDueMilliseconds=nowMilliseconds+plan.relayDelayMilliseconds;
        result.scheduled=true;result.failureCount=plan.relayFailures;result.delayMilliseconds=plan.relayDelayMilliseconds;
        result.dueMilliseconds=plan.relayDueMilliseconds;result.target={peer,{}};touch(peer);return result;
    }

    bool hasRepresentation(const std::string& peer) const {
        return hasActiveWinner(peer) || hasPendingAttempt(peer, {});
    }

    bool hasActiveWinner(const std::string& peer) const {
        auto found = continuities_.find(peer);
        return found != continuities_.end() && found->second.activeTransport.value != 0;
    }

    void removePeer(const std::string& peer) {
        liveDemand_.erase(peer);
        retries_.erase(peer);
        plans_.erase(peer);
        p2p_.erase(peer);
        for(auto& item:transports_)if(item.second.matches(peer))item.second.locallyTrusted=false;
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
        auto plan=plans_.find(peer);
        if(plan!=plans_.end()){
            result.retrySuspended=result.retrySuspended||plan->second.suspended;
            if(!plan->second.suspended){
                if(plan->second.relayDueMilliseconds!=never()){result.retryScheduled=true;result.retryDelayMilliseconds=std::max(result.retryDelayMilliseconds,plan->second.relayDelayMilliseconds);}
                for(const auto& candidate:plan->second.direct)if(candidate.dueMilliseconds!=never()){result.retryScheduled=true;result.retryDelayMilliseconds=std::max(result.retryDelayMilliseconds,candidate.delayMilliseconds);}
            }
        }
        auto p2p=p2p_.find(peer);
        if(p2p!=p2p_.end()) { result.p2pAvailability=p2p->second.availability;result.p2pStandbyTransport=p2p->second.standby; }
        return result;
    }

    const PeerSession* find(const std::string& verifiedRemoteDeviceId) const {
        auto found = continuities_.find(verifiedRemoteDeviceId);
        return found == continuities_.end() || !found->second.sessionEstablished
            ? nullptr : &found->second.session;
    }

    SessionEpoch epoch() const { return epoch_; }
    std::uint64_t networkRevision() const { return networkRevision_; }

    static constexpr std::uint64_t DirectHandoffCapability=16;
    static constexpr std::uint64_t HandoffCandidateIntent=32;
    bool handoffCandidate(TransportInstanceId id) const {
        auto found=transports_.find(id.value);return found!=transports_.end()&&found->second.handoffCandidate;
    }
    bool active(TransportInstanceId id) const { return isActive(id); }
    bool draining(TransportInstanceId id) const { return handoff_.draining(id); }
    bool canPrepareHandoff(const std::string& peer,const RouteObservation& route) const {
        auto state=snapshot(peer);
        if(!state||!state->connected||route.networkRevision<networkRevision_||state->activeRoute.endpointUtf8==route.endpointUtf8||
           handoff_.busy(peer)||hasPendingAttempt(peer,{}))return false;
        auto found=transports_.find(state->activeTransport.value);
        return found!=transports_.end()&&(found->second.capabilities&DirectHandoffCapability)&&handoff_.ready(found->second.instance)&&
            !(found->second.route.kind==TransportKind::Direct&&route.kind==TransportKind::Relay);
    }
    std::vector<HandoffEffect> attachHandoff(SessionEpoch epoch,TransportInstanceId id,
                                            const std::string& nonce,std::uint64_t now) {
        auto found=current(epoch,id);if(!found||!trusted(**found)||!((*found)->capabilities&DirectHandoffCapability))return {};
        auto& record=**found;record.handoffAttached=true;record.handoffBegan=now;
        handoff_.attach(id,record.remoteDeviceId(),nonce,now);return handoff_.hello(id);
    }
    std::vector<HandoffEffect> receiveHandoff(SessionEpoch epoch,TransportInstanceId id,
                                             const HandoffEnvelope& envelope,std::uint64_t now) {
        auto found=current(epoch,id);if(!found||!trusted(**found)||(!(*found)->handoffAttached&&!(*found)->standby))return {};
        const bool standbyHello=(*found)->standby&&!(*found)->handoffAttached&&envelope.message==HandoffMessage::Hello;
        if(!isActive(id)&&!handoff_.draining(id)&&(*found)->route.networkRevision<networkRevision_)
            return applyHandoff({{HandoffEffect::Kind::Retire,id,{},{},"candidate network generation superseded"}});
        auto state=snapshot((*found)->remoteDeviceId());if(!state)return {};
        auto effects=applyHandoff(handoff_.receive(id,state->activeTransport,envelope,now));
        if(standbyHello){(*found)->handoffAttached=true;auto hello=handoff_.hello(id);effects.insert(effects.begin(),hello.begin(),hello.end());}
        return effects;
    }
    std::vector<HandoffEffect> handoffTick(SessionEpoch epoch,std::uint64_t now) {
        if(epoch!=epoch_)return {};
        auto result=std::move(pendingHandoffEffects_);pendingHandoffEffects_.clear();
        auto timed=applyHandoff(handoff_.tick(now));result.insert(result.end(),timed.begin(),timed.end());
        for(auto& item:transports_) {
            auto& record=item.second;if(record.closed||record.cancelled||!record.handoffAttached||!trusted(record)||handoff_.draining(record.instance))continue;
            bool retainedRelay=false;for(const auto& scoped:p2p_)if(scoped.second.availability==P2pAvailability::Promoted&&scoped.second.relay.value==record.instance.value){retainedRelay=true;break;}
            if(retainedRelay)continue;
            if(!isActive(record.instance)&&record.route.networkRevision<networkRevision_){
                auto expired=applyHandoff({{HandoffEffect::Kind::Retire,record.instance,{},{},"candidate network generation superseded"}});
                result.insert(result.end(),expired.begin(),expired.end());continue;
            }
            if(!isActive(record.instance)&&!handoff_.draining(record.instance)&&now-record.handoffBegan>=DirectHandoff::PrepareTimeout) {
                result.push_back({HandoffEffect::Kind::Retire,record.instance,{},{},"unselected handoff candidate expired"});continue;
            }
            auto state=snapshot(record.remoteDeviceId());if(!state)continue;
            auto effects=applyHandoff(handoff_.consider(record.instance,state->activeTransport,now));
            result.insert(result.end(),effects.begin(),effects.end());
        }
        return result;
    }

private:
    static constexpr std::uint64_t never() { return std::numeric_limits<std::uint64_t>::max(); }
    static std::string handoffNonce(TransportInstanceId id) {
        std::ostringstream value;value<<std::hex<<std::setfill('0')<<std::setw(16)<<id.value<<std::setw(16)<<(id.value^0x9e3779b97f4a7c15ULL);return value.str();
    }
#ifdef PEERCHAT_TEST_SHORT_RECONNECT_BACKOFF
    static constexpr std::uint64_t DirectFallbackWindowMilliseconds=300;
#else
    static constexpr std::uint64_t DirectFallbackWindowMilliseconds=8000;
#endif

    struct CandidateRoute {RouteObservation route;unsigned priority=0;std::uint32_t failures=0,delayMilliseconds=0;std::uint64_t dueMilliseconds=never();};
    struct CandidatePlan {
        ReliableAuthorization authorization{};
        std::vector<CandidateRoute> direct;bool relayAvailable=false,suspended=false;std::uint64_t networkRevision=0,beganMilliseconds=0;
        std::uint64_t relayDueMilliseconds=never(),expediteAllowedAt=0;std::uint32_t relayFailures=0,relayDelayMilliseconds=0;
    };

    struct TransportRecord {
        TransportInstanceId instance;
        RouteObservation route;
        TransportState state = TransportState::Created;
        ReliableAuthorization authorization;
        std::string expectedRemoteDeviceId;
        std::string presentedRemoteDeviceId;
        std::uint64_t capabilities = 0;
        std::uint64_t startedMilliseconds = 0;
        std::uint64_t lastProgressMilliseconds = 0;
        bool locallyTrusted = false;
        bool remoteAccepted = false;
        bool cancelled = false;
        bool closed = false;
        bool handoffAttached = false;
        bool handoffCandidate = false;
        bool standby = false;
        std::uint64_t handoffBegan = 0;
        std::string remoteDeviceId() const { return presentedRemoteDeviceId.empty() ? expectedRemoteDeviceId : presentedRemoteDeviceId; }
        std::string continuityKey() const { return authorization.scope != ReliableAuthorizationScope::Friend
            ? authorization.continuityKey()
            : (!locallyTrusted&&!expectedRemoteDeviceId.empty() ? expectedRemoteDeviceId : remoteDeviceId()); }
        bool matches(const std::string& peer) const {
            if(peer.empty())return false;
            if(authorization.scope!=ReliableAuthorizationScope::Friend)return continuityKey()==peer;
            return expectedRemoteDeviceId==peer||presentedRemoteDeviceId==peer||continuityKey()==peer;
        }
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

    struct ScopedP2pState {
        std::string id;std::uint64_t expiryMilliseconds=0,networkRevision=0;
        P2pAvailability availability=P2pAvailability::Unavailable;
        TransportInstanceId standby,relay;
        RouteObservation route;
    };

    std::optional<TransportRecord*> current(SessionEpoch epoch, TransportInstanceId instance) {
        if (epoch != epoch_) return {};
        auto found = transports_.find(instance.value);
        if (found == transports_.end() || found->second.closed) return {};
        return &found->second;
    }

    ContinuityState& continuityFor(const std::string& key,const std::string& remoteDeviceId) {
        auto found = continuities_.find(key);
        if (found == continuities_.end()) {
            PeerSessionIdentity identity{localProfileScope_, localDeviceId_, remoteDeviceId};
            found = continuities_.emplace(key, ContinuityState(PeerSession(std::move(identity), epoch_))).first;
        }
        return found->second;
    }
    ContinuityState& continuityFor(const std::string& peer) { return continuityFor(peer, peer); }

    void touch(const std::string& peer) { if (!peer.empty()) ++continuityFor(peer).revision; }

    bool isActive(TransportInstanceId instance) const {
        for (const auto& item : continuities_)
            if (item.second.activeTransport.value == instance.value) return true;
        return false;
    }

    std::optional<TransportKind> activeTransportKind(const std::string& peer) const {
        auto continuity=continuities_.find(peer);if(continuity==continuities_.end()||!continuity->second.activeTransport.value)return {};
        auto transport=transports_.find(continuity->second.activeTransport.value);
        return transport==transports_.end()?std::optional<TransportKind>{}:transport->second.route.kind;
    }

    bool hasActiveRelay(const std::string& peer) const {
        auto continuity=continuities_.find(peer);if(continuity==continuities_.end()||!continuity->second.activeTransport.value)return false;
        auto transport=transports_.find(continuity->second.activeTransport.value);
        return transport!=transports_.end()&&!transport->second.closed&&transport->second.route.kind==TransportKind::Relay;
    }

    bool hasPendingAttempt(const std::string& peer, TransportInstanceId except) const {
        for (const auto& item : transports_) {
            const auto& record = item.second;
            if (record.instance.value == except.value || record.closed || record.cancelled) continue;
            if (record.matches(peer) && !record.standby&&!isActive(record.instance)&&!handoff_.draining(record.instance)) return true;
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
    std::map<std::string, CandidatePlan> plans_;
    bool demandDriven_=false;
    std::map<std::string,std::uint64_t> liveDemand_;
    std::map<std::string, ScopedP2pState> p2p_;
    DirectHandoff handoff_;
    bool directOnly_ = false;
    std::vector<HandoffEffect> pendingHandoffEffects_;

    static bool trusted(const TransportRecord& r) {
        return !r.closed&&!r.cancelled&&r.locallyTrusted&&r.remoteAccepted&&!r.presentedRemoteDeviceId.empty()&&
            (r.expectedRemoteDeviceId.empty()||r.expectedRemoteDeviceId==r.presentedRemoteDeviceId);
    }
    std::vector<HandoffEffect> applyHandoff(std::vector<HandoffEffect> effects) {
        std::vector<HandoffEffect> applied;
        for(auto& effect:effects) {
            auto found=current(epoch_,effect.instance);
            if(!found)continue;
            auto& record=**found;
            if(effect.kind==HandoffEffect::Kind::Retire){
                bool retainRelay=false;for(const auto& scoped:p2p_)if(scoped.second.availability==P2pAvailability::Promoted&&scoped.second.relay.value==effect.instance.value){retainRelay=true;break;}
                if(retainRelay)continue;
                record.cancelled=true;record.state=TransportState::Closing;
            }
            else if(!trusted(record))continue;
            if(effect.kind==HandoffEffect::Kind::Promote||effect.kind==HandoffEffect::Kind::Restore) {
                if(!trusted(record))continue;
                auto& continuity=continuityFor(record.remoteDeviceId());
                if(continuity.activeTransport.value!=effect.previous.value)continue;
                continuity.activeTransport=effect.instance;continuity.session.observeLegacyActive(effect.instance);
                if(effect.kind==HandoffEffect::Kind::Promote){auto scoped=p2p_.find(record.remoteDeviceId());if(scoped!=p2p_.end()&&scoped->second.standby.value==effect.instance.value){scoped->second.availability=P2pAvailability::Promoted;scoped->second.relay=effect.previous;}}
                record.state=TransportState::Active;retries_.erase(record.remoteDeviceId());touch(record.remoteDeviceId());
            }
            applied.push_back(std::move(effect));
        }
        return applied;
    }
};

} // namespace peerchat::session
