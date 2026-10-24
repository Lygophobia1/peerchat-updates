#pragma once
#include "../session_transport_manager.hpp"
#include "../reliable_peer_protocol.hpp"

namespace peerchat::portable {
// Execution records, not a scheduler: only SessionTransportManager decides when
// to request/retry a route. Caller serializes access with the profile gate.
class RelayRequests {
public:
    struct Request {
        session::TransportInstanceId id;
        std::string peer;
        session::RouteObservation route;
        std::uint64_t began=0;
        session::ReliableAuthorization authorization{};
    };
private:
    session::SessionTransportManager& owner_;
    const reliable::AuthenticatedFriendSnapshot& friends_;
    session::SessionEpoch epoch_;
    std::uint64_t& next_;
    std::uint64_t network_=0;
    std::map<std::uint64_t,Request> pending_;
    std::map<std::string,bool> presence_;
    std::function<bool(const session::ReliableAuthorization&)> archiveCurrent_;
    std::function<bool(const session::ReliableAuthorization&)> groupCurrent_;
    bool allowed(const Request& request)const{return request.authorization.permitsArchiveControl()?
        archiveCurrent_&&archiveCurrent_(request.authorization):request.authorization.scope==session::ReliableAuthorizationScope::GroupControl?
        groupCurrent_&&groupCurrent_(request.authorization):friends_.contains(request.peer);}
    static std::string continuity(const Request& request){return request.authorization.scope!=session::ReliableAuthorizationScope::Friend?request.authorization.continuityKey():request.peer;}
public:
    RelayRequests(session::SessionTransportManager& owner,const reliable::AuthenticatedFriendSnapshot& friends,
                  session::SessionEpoch epoch,std::uint64_t& next,std::function<bool(const session::ReliableAuthorization&)> archiveCurrent={},
                  std::function<bool(const session::ReliableAuthorization&)> groupCurrent={})
        :owner_(owner),friends_(friends),epoch_(epoch),next_(next),archiveCurrent_(std::move(archiveCurrent)),groupCurrent_(std::move(groupCurrent)){owner_.setDemandDriven(true);}
    std::uint64_t network()const noexcept{return network_;}
    bool authenticatedNetwork(std::uint64_t generation,std::uint64_t network){
        if(!friends_.localAccount()||generation!=friends_.generation()||!network||network<network_||!owner_.observeNetwork(epoch_,network))return false;
        network_=network;return true;
    }
    bool presence(const std::string& peer,bool online,std::uint64_t generation,std::uint64_t network,std::uint64_t now){
        if(!friends_.contains(peer)||generation!=friends_.generation()||!network||network<network_)return false;
        const bool changedNetwork=network!=network_;
        if(!owner_.observeNetwork(epoch_,network))return false;
        network_=network;
        auto previous=presence_.find(peer);
        // Repeated identical presence must not overwrite the owner's backoff.
        if(!changedNetwork&&previous!=presence_.end()&&previous->second==online)return true;
        presence_[peer]=online;
        return owner_.replaceCandidates(epoch_,peer,{},online,network,now);
    }
    void revoke(){
        for(auto it=presence_.begin();it!=presence_.end();){
            if(friends_.contains(it->first)){++it;continue;}
            owner_.removePeer(it->first);it=presence_.erase(it);
        }
        for(auto it=pending_.begin();it!=pending_.end();){
            if(allowed(it->second)){++it;continue;}
            owner_.closeTransport(epoch_,it->second.id,"authenticated friendship revoked",false);
            it=pending_.erase(it);
        }
    }
    void failed(const Request& request,std::uint64_t now,std::uint32_t random,bool retry){
        owner_.closeTransport(epoch_,request.id,"platform relay failed",retry);
        auto key=continuity(request);
        // A retired attempt on an old network is not a failure of the newly
        // authenticated group route. Friend/archive retry behavior is unchanged.
        const bool oldGroupNetwork=request.authorization.scope==session::ReliableAuthorizationScope::GroupControl&&request.route.networkRevision!=network_;
        if(retry&&!oldGroupNetwork&&allowed(request)&&!owner_.hasActiveWinner(key))owner_.scheduleRelayRetry(key,now,random);
    }
    std::vector<Request> take(std::uint64_t now,std::uint32_t random,std::size_t capacity){
        revoke();
        for(auto it=pending_.begin();it!=pending_.end();){
            // Same bounded peer-authentication timeout as the Windows worker.
            if(owner_.attachablePeer(epoch_,it->second.id)&&now-it->second.began<20000){++it;continue;}
            auto request=it->second;it=pending_.erase(it);failed(request,now,random,true);
        }
        std::vector<Request> result;
        if(pending_.size()>=capacity)return result;
        for(const auto& due:owner_.takeDueRelayAttempts(now)){
            const bool archive=due.authorization.permitsArchiveControl();
            const bool group=due.authorization.scope==session::ReliableAuthorizationScope::GroupControl;
            if((archive?(!archiveCurrent_||!archiveCurrent_(due.authorization)):group?(!groupCurrent_||!groupCurrent_(due.authorization)):!friends_.contains(due.remoteDeviceId))||due.networkRevision!=network_)continue;
            if(pending_.size()>=capacity||next_==std::numeric_limits<std::uint64_t>::max()){
                owner_.scheduleRelayRetry((archive||group)?due.authorization.continuityKey():due.remoteDeviceId,now,random);continue;
            }
            auto id=session::TransportInstanceId{next_++};
            session::RouteObservation route{{id.value},"relay:"+due.remoteDeviceId,"PeerChat server relay","opaque relay",false,
                due.networkRevision,session::TransportKind::Relay,session::RouteClass::ServerRelay};
            if(group){if(!owner_.registerGroupControlAttempt(epoch_,id,due.authorization,route,now))continue;}
            else if(archive){if(!owner_.registerArchiveControlAttempt(epoch_,id,due.authorization,route,now))continue;}
            else if(!owner_.registerAttempt(epoch_,id,due.remoteDeviceId,route,now))continue;
            Request request{id,due.remoteDeviceId,std::move(route),now,due.authorization};
            pending_.emplace(id.value,request);result.push_back(std::move(request));
        }
        return result;
    }
    // Physical adapters use this readout to retire sockets whose native request
    // was revoked while rendezvous was still waiting. It grants no TLS authority.
    bool pending(session::TransportInstanceId id)const {
        auto found=pending_.find(id.value);
        return found!=pending_.end()&&allowed(found->second)&&found->second.route.networkRevision==network_&&
            owner_.attachablePeer(epoch_,id)==std::optional<std::string>{continuity(found->second)};
    }
    std::optional<Request> attach(session::TransportInstanceId id,const std::string& expected){
        auto found=pending_.find(id.value);
        if(found==pending_.end()||found->second.peer!=expected||!allowed(found->second)||
           found->second.route.networkRevision!=network_||owner_.attachablePeer(epoch_,id)!=std::optional<std::string>{continuity(found->second)})return {};
        auto result=found->second;pending_.erase(found);return result;
    }
    bool close(session::TransportInstanceId id,std::uint64_t now,std::uint32_t random,bool retry){
        auto found=pending_.find(id.value);if(found==pending_.end())return false;
        auto request=found->second;pending_.erase(found);failed(request,now,random,retry);return true;
    }
};
}
