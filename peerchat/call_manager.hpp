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
    bool timedOut(std::uint64_t now) const noexcept { return active_ && active_->deadlineMilliseconds && now >= active_->deadlineMilliseconds; }
    std::optional<CallSession> finish(State terminal) {
        if (!active_) return {};
        active_->state = terminal; for (auto& item : active_->participants) item.second.state = terminal == State::Failed ? ParticipantState::Failed : ParticipantState::Left;
        auto result = std::move(active_); remember(result->id.value); active_.reset(); return result;
    }
    void reset() { if (active_) finish(State::Ended); finished_.clear(); }
};

} // namespace peerchat::calls
