#pragma once
#include "session_transport_types.hpp"
#include <map>
#include <string>
#include <vector>

namespace peerchat::session {
// Private state machine composed by SessionTransportManager; it never owns a
// socket, scheduler or authoritative channel binding. All inputs are authenticated
// by the manager. Local instance IDs are never compared with the remote PC's IDs.
enum class HandoffMessage : std::uint8_t { Hello=0, Prepare=1, Ready=2, Commit=3, Promoted=4, Confirm=5 };
struct HandoffEnvelope {
    HandoffMessage message = HandoffMessage::Hello;
    std::string transaction, previous;
};
struct HandoffEffect {
    enum class Kind { Send, Promote, Retire, Restore };
    Kind kind;
    TransportInstanceId instance, previous;
    HandoffEnvelope envelope;
    std::string reason;
};
class DirectHandoff {
public:
    static constexpr std::uint64_t PrepareTimeout=10000, RetryInterval=500, DrainTimeout=3000;
    explicit DirectHandoff(std::string local): local_(std::move(local)) {}
    void attach(TransportInstanceId id, const std::string& peer, std::string nonce,
                std::uint64_t now) { links_.emplace(id.value, Link{peer,std::move(nonce),{},now}); }
    bool ready(TransportInstanceId id) const {
        auto it=links_.find(id.value); return it!=links_.end()&&!it->second.remote.empty();
    }
    bool busy(const std::string& peer) const { return pending_.count(peer)!=0; }
    bool draining(TransportInstanceId id) const {
        for(const auto& item:pending_) if(item.second.promoted&&item.second.old.value==id.value) return true;
        return false;
    }
    std::vector<HandoffEffect> hello(TransportInstanceId id) const {
        auto it=links_.find(id.value); if(it==links_.end()) return {};
        return {send(id,{HandoffMessage::Hello,it->second.local,{}})};
    }
    std::vector<HandoffEffect> receive(TransportInstanceId id, TransportInstanceId active,
                                     const HandoffEnvelope& envelope, std::uint64_t now) {
        auto link=links_.find(id.value); if(link==links_.end()) return {};
        auto& l=link->second;
        if(envelope.message==HandoffMessage::Hello) {
            if(!hex(envelope.transaction,32)||!envelope.previous.empty()||
               (!l.remote.empty()&&l.remote!=envelope.transaction)) return {retire(id,"invalid handoff connection token")};
            l.remote=envelope.transaction;
            return consider(id,active,now);
        }
        if(!ready(id)||!hex(envelope.transaction,64)||!hex(envelope.previous,64)||
           envelope.transaction!=binding(id)) return {}; // stale/cross-instance replay
        auto found=pending_.find(l.peer);
        if(envelope.message==HandoffMessage::Prepare) {
            // The lower verified Peer ID is the sole proposal coordinator, even
            // when the other PC opened the candidate. Simultaneous routes thus
            // become one serialized proposal, never two independent promotions.
            if(local_<l.peer) return {};
            if(found==pending_.end()) {
                if(id.value==active.value||!ready(active)||envelope.previous!=binding(active)) return {};
                Transaction t; t.candidate=id;t.old=active;t.id=envelope.transaction;t.anchor=envelope.previous;
                t.deadline=now+PrepareTimeout;t.nextRetry=now+RetryInterval;t.last=HandoffMessage::Ready;
                found=pending_.emplace(l.peer,std::move(t)).first;
            }
            auto& t=found->second;
            if(!matches(t,id,envelope)) return {retire(id,"competing handoff candidate")};
            return {send(id,{t.promoted?HandoffMessage::Promoted:HandoffMessage::Ready,t.id,t.anchor})};
        }
        if(found==pending_.end()||!matches(found->second,id,envelope)) return {};
        auto& t=found->second;
        if(envelope.message==HandoffMessage::Ready&&local_<l.peer&&!t.promoted) {
            t.last=HandoffMessage::Commit;t.nextRetry=now+RetryInterval;
            return {send(id,{t.last,t.id,t.anchor})};
        }
        if(envelope.message==HandoffMessage::Commit&&local_>l.peer) {
            std::vector<HandoffEffect> result;
            if(!t.promoted) { t.promoted=true; result.push_back({HandoffEffect::Kind::Promote,id,t.old,{},"handoff commit authenticated"}); }
            t.last=HandoffMessage::Promoted;t.nextRetry=now+RetryInterval;
            result.push_back(send(id,{t.last,t.id,t.anchor}));return result;
        }
        if(envelope.message==HandoffMessage::Promoted&&local_<l.peer&&t.last==HandoffMessage::Commit) {
            t.promoted=true;t.confirmed=true;t.deadline=now+DrainTimeout;t.nextRetry=now+RetryInterval;t.last=HandoffMessage::Confirm;
            return {{HandoffEffect::Kind::Promote,id,t.old,{},"handoff promotion acknowledged"},send(id,{t.last,t.id,t.anchor})};
        }
        if(envelope.message==HandoffMessage::Promoted&&local_<l.peer&&t.promoted)
            return {send(id,{HandoffMessage::Confirm,t.id,t.anchor})};
        if(envelope.message==HandoffMessage::Confirm&&local_>l.peer&&t.promoted) {
            if(!t.confirmed) {t.confirmed=true;t.deadline=now+DrainTimeout;}
        }
        return {};
    }
    std::vector<HandoffEffect> consider(TransportInstanceId id, TransportInstanceId active,std::uint64_t now) {
        auto it=links_.find(id.value);
        if(it==links_.end()||!ready(id)||!ready(active)||id.value==active.value||local_>it->second.peer) return {};
        auto found=pending_.find(it->second.peer);
        if(found!=pending_.end()) return found->second.candidate.value==id.value
            ? std::vector<HandoffEffect>{} : std::vector<HandoffEffect>{retire(id,"simultaneous candidate deferred to coordinator")};
        Transaction t;t.candidate=id;t.old=active;t.id=binding(id);t.anchor=binding(active);
        t.deadline=now+PrepareTimeout;t.nextRetry=now+RetryInterval;t.last=HandoffMessage::Prepare;
        pending_.emplace(it->second.peer,t);
        return {send(id,{t.last,t.id,t.anchor})};
    }
    std::vector<HandoffEffect> tick(std::uint64_t now) {
        std::vector<HandoffEffect> result;
        for(auto it=pending_.begin();it!=pending_.end();) {
            auto& t=it->second;
            if(now>=t.deadline) {
                if(t.confirmed) result.push_back(retire(t.old,"handoff bounded drain completed"));
                else {if(t.promoted)result.push_back({HandoffEffect::Kind::Restore,t.old,t.candidate,{},"handoff confirmation timeout; restore previous route"});result.push_back(retire(t.candidate,"handoff preparation timed out"));}
                it=pending_.erase(it);continue;
            }
            if(now>=t.nextRetry) {result.push_back(send(t.candidate,{t.last,t.id,t.anchor}));t.nextRetry=now+RetryInterval;}
            ++it;
        }
        return result;
    }
    std::vector<HandoffEffect> closed(TransportInstanceId id) {
        std::vector<HandoffEffect> result;
        for(auto it=pending_.begin();it!=pending_.end();) {
            auto& t=it->second;
            if(t.candidate.value==id.value) {
                if(t.promoted&&links_.count(t.old.value))result.push_back({HandoffEffect::Kind::Restore,t.old,id,{},"promoted route failed; restore retained route"});
                it=pending_.erase(it);
            } else ++it;
        }
        links_.erase(id.value);return result;
    }
    std::string binding(TransportInstanceId id) const {
        auto it=links_.find(id.value);if(it==links_.end()||it->second.remote.empty())return {};
        return local_<it->second.peer?it->second.local+it->second.remote:it->second.remote+it->second.local;
    }
private:
    struct Link { std::string peer,local,remote;std::uint64_t began; };
    struct Transaction {
        TransportInstanceId candidate,old;std::string id,anchor;
        std::uint64_t deadline=0,nextRetry=0;bool promoted=false,confirmed=false;
        HandoffMessage last=HandoffMessage::Prepare;
    };
    static bool hex(const std::string& s,std::size_t length) {if(s.size()!=length)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
    static bool matches(const Transaction& t,TransportInstanceId id,const HandoffEnvelope& e) {return t.candidate.value==id.value&&t.id==e.transaction&&t.anchor==e.previous;}
    static HandoffEffect send(TransportInstanceId id,HandoffEnvelope envelope) {return {HandoffEffect::Kind::Send,id,{},std::move(envelope),{}};}
    static HandoffEffect retire(TransportInstanceId id,std::string reason) {return {HandoffEffect::Kind::Retire,id,{},{},std::move(reason)};}
    std::string local_;
    std::map<std::uint64_t,Link> links_;
    std::map<std::string,Transaction> pending_;
};
} // namespace peerchat::session
