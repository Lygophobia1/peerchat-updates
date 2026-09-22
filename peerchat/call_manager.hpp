#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace peerchat::calls {

struct CallId {
    std::string value;
    bool operator==(const CallId& other) const noexcept { return value == other.value; }
};

enum class CallType { Voice };
enum class Direction { Outgoing, Incoming };
enum class State { Idle, Outgoing, Incoming, Negotiating, ConnectingMedia, Connected, Ending, Ended, Failed };
enum class ParticipantState { Invited, Ringing, Negotiating, Connected, Left, Failed };
enum class MediaState { Idle, Negotiating, Connected, Interrupted, Recovering, Failed, Closed };
enum class MediaRoute { Unknown, DirectLanIce, NatTraversedIce, TurnRelay };

struct Participant {
    std::string peerId;
    ParticipantState state = ParticipantState::Invited;
    std::string remoteDescription;
    std::vector<std::pair<std::string, std::string>> candidates;
};

struct CallSession {
    CallId id;
    CallType type = CallType::Voice;
    Direction direction = Direction::Outgoing;
    State state = State::Idle;
    std::uint64_t lifetime = 0;
    std::uint64_t deadlineMilliseconds = 0;
    std::map<std::string, Participant> participants;
    bool muted = false;
    bool deafened = false;
    MediaState mediaState = MediaState::Idle;
    std::uint64_t mediaGeneration = 0;
    std::uint64_t mediaRecoveryDeadlineMilliseconds = 0;
    std::uint64_t nextMediaAttemptMilliseconds = 0;
    unsigned mediaRecoveryAttempts = 0;
    bool mediaRecoveryCapable = false;
    bool controlAvailable = true;
    std::uint64_t controlGraceDeadlineMilliseconds = 0;
    MediaRoute mediaRoute = MediaRoute::Unknown;
};

enum class IncomingDisposition { Started, Duplicate, ReplaceOutgoing, Busy, Replay };

// Profile-scoped authoritative call lifetime. Stage 6 permits one active 1:1
// call, but the owned model is participant-shaped so adding group call routing
// later does not require replacing call identity or lifecycle ownership.
class CallManager {
    std::optional<CallSession> active_;
    std::deque<std::string> finished_;
    std::uint64_t nextLifetime_ = 1;

    void remember(const std::string& id) {
        if (id.empty()) return;
        finished_.push_back(id);
        while (finished_.size() > 64) finished_.pop_front();
    }

public:
    const CallSession* active() const noexcept { return active_ ? &*active_ : nullptr; }
    CallSession* active() noexcept { return active_ ? &*active_ : nullptr; }
    State state() const noexcept { return active_ ? active_->state : State::Idle; }
    bool idle() const noexcept { return !active_; }
    bool finished(const std::string& id) const noexcept {
        for (const auto& value : finished_) if (value == id) return true;
        return false;
    }
    std::string peer() const {
        return !active_ || active_->participants.empty() ? std::string{} : active_->participants.begin()->first;
    }
    std::uint64_t lifetime() const noexcept { return active_ ? active_->lifetime : 0; }
    bool matches(std::uint64_t lifetime, const std::string& id, const std::string& peerId) const noexcept {
        return active_ && active_->lifetime == lifetime && active_->id.value == id && active_->participants.count(peerId) != 0;
    }
    bool matchesMedia(std::uint64_t lifetime, const std::string& id, const std::string& peerId, std::uint64_t generation) const noexcept {
        return matches(lifetime,id,peerId) && active_->mediaGeneration == generation;
    }
    bool beginOutgoing(CallId id, CallType type, const std::vector<std::string>& peers, std::uint64_t deadline) {
        if (active_ || id.value.empty() || peers.empty()) return false;
        CallSession session; session.id = std::move(id); session.type = type; session.direction = Direction::Outgoing;
        session.state = State::Outgoing; session.lifetime = nextLifetime_++; session.deadlineMilliseconds = deadline;
        for (const auto& peerId : peers) if (!peerId.empty()) { Participant participant;participant.peerId=peerId;participant.state=ParticipantState::Ringing;session.participants.emplace(peerId,std::move(participant)); }
        if (session.participants.empty()) return false;
        active_ = std::move(session); return true;
    }
    IncomingDisposition beginIncoming(CallId id, CallType type, const std::string& peerId,
                                      const std::string& description, const std::string& localPeerId,
                                      std::uint64_t deadline) {
        if (finished(id.value)) return IncomingDisposition::Replay;
        if (active_ && active_->id == id && active_->participants.count(peerId)) return IncomingDisposition::Duplicate;
        bool replace = active_ && active_->direction == Direction::Outgoing && peer() == peerId && localPeerId > peerId;
        if (active_ && !replace) return IncomingDisposition::Busy;
        if (replace) finish(State::Ended);
        CallSession session; session.id = std::move(id); session.type = type; session.direction = Direction::Incoming;
        session.state = State::Incoming; session.lifetime = nextLifetime_++; session.deadlineMilliseconds = deadline;
        Participant participant;participant.peerId=peerId;participant.state=ParticipantState::Ringing;participant.remoteDescription = description;
        session.participants.emplace(peerId, std::move(participant)); active_ = std::move(session);
        return replace ? IncomingDisposition::ReplaceOutgoing : IncomingDisposition::Started;
    }
    bool transition(const std::string& id, State state, std::uint64_t deadline = 0) {
        if (!active_ || active_->id.value != id) return false;
        active_->state = state; active_->deadlineMilliseconds = deadline;
        auto participantState = state == State::Connected ? ParticipantState::Connected :
            (state == State::Failed ? ParticipantState::Failed : ParticipantState::Negotiating);
        for (auto& item : active_->participants) item.second.state = participantState;
        return true;
    }
    bool setMuted(bool value) noexcept { if (!active_) return false; active_->muted = value; return true; }
    bool setDeafened(bool value) noexcept { if (!active_) return false; active_->deafened = value; return true; }
    bool setMediaRecoveryCapable(const std::string& peerId,bool value) noexcept { if(!active_||!active_->participants.count(peerId))return false;active_->mediaRecoveryCapable=value;return true; }
    bool beginInitialMedia(const std::string& id) noexcept {if(!active_||active_->id.value!=id)return false;active_->mediaGeneration=1;active_->mediaState=MediaState::Negotiating;active_->mediaRoute=MediaRoute::Unknown;return true;}
    bool interruptMedia(std::uint64_t now,std::uint64_t deadline) noexcept {if(!active_||!active_->mediaRecoveryCapable)return false;bool alreadyRecovering=active_->mediaState==MediaState::Interrupted||active_->mediaState==MediaState::Recovering;active_->mediaState=MediaState::Interrupted;active_->mediaRoute=MediaRoute::Unknown;if(!alreadyRecovering||!active_->mediaRecoveryDeadlineMilliseconds){active_->mediaRecoveryDeadlineMilliseconds=deadline;active_->mediaRecoveryAttempts=0;}active_->nextMediaAttemptMilliseconds=now;return true;}
    std::uint64_t beginMediaRecovery(std::uint64_t now,std::uint64_t retryDelay) noexcept {if(!active_||!active_->mediaRecoveryCapable||now<active_->nextMediaAttemptMilliseconds||now>=active_->mediaRecoveryDeadlineMilliseconds)return 0;++active_->mediaGeneration;active_->mediaState=MediaState::Recovering;++active_->mediaRecoveryAttempts;active_->nextMediaAttemptMilliseconds=now+retryDelay;return active_->mediaGeneration;}
    bool acceptMediaGeneration(std::uint64_t generation,std::uint64_t deadline) noexcept {if(!active_||!active_->mediaRecoveryCapable||generation<=active_->mediaGeneration)return false;active_->mediaGeneration=generation;active_->mediaState=MediaState::Recovering;active_->mediaRoute=MediaRoute::Unknown;if(!active_->mediaRecoveryDeadlineMilliseconds||deadline<active_->mediaRecoveryDeadlineMilliseconds)active_->mediaRecoveryDeadlineMilliseconds=deadline;active_->nextMediaAttemptMilliseconds=0;return true;}
    bool transitionMedia(std::uint64_t generation,MediaState state,MediaRoute route=MediaRoute::Unknown) noexcept {if(!active_||active_->mediaGeneration!=generation)return false;active_->mediaState=state;if(route!=MediaRoute::Unknown)active_->mediaRoute=route;if(state==MediaState::Connected){active_->mediaRecoveryDeadlineMilliseconds=0;active_->nextMediaAttemptMilliseconds=0;active_->mediaRecoveryAttempts=0;}return true;}
    bool mediaRetryDue(std::uint64_t now)const noexcept{return active_&&active_->mediaRecoveryCapable&&(active_->mediaState==MediaState::Interrupted||active_->mediaState==MediaState::Recovering)&&active_->mediaRecoveryDeadlineMilliseconds&&now>=active_->nextMediaAttemptMilliseconds&&now<active_->mediaRecoveryDeadlineMilliseconds;}
    bool mediaTimedOut(std::uint64_t now)const noexcept{return active_&&active_->mediaRecoveryDeadlineMilliseconds&&now>=active_->mediaRecoveryDeadlineMilliseconds&&active_->mediaState!=MediaState::Connected;}
    void deferMediaRetry(std::uint64_t now,std::uint64_t delay)noexcept{if(active_)active_->nextMediaAttemptMilliseconds=now+delay;}
    void observeControl(bool available,std::uint64_t deadline=0)noexcept{if(!active_)return;active_->controlAvailable=available;active_->controlGraceDeadlineMilliseconds=available?0:deadline;}
    bool controlAndMediaTimedOut(std::uint64_t now)const noexcept{return active_&&!active_->controlAvailable&&active_->controlGraceDeadlineMilliseconds&&now>=active_->controlGraceDeadlineMilliseconds&&active_->mediaState!=MediaState::Connected;}
    bool timedOut(std::uint64_t now) const noexcept { return active_ && active_->deadlineMilliseconds && now >= active_->deadlineMilliseconds; }
    std::optional<CallSession> finish(State terminal) {
        if (!active_) return {};
        active_->state = terminal; for (auto& item : active_->participants) item.second.state = terminal == State::Failed ? ParticipantState::Failed : ParticipantState::Left;
        auto result = std::move(active_); remember(result->id.value); active_.reset(); return result;
    }
    void reset() { if (active_) finish(State::Ended); finished_.clear(); }
};

} // namespace peerchat::calls
