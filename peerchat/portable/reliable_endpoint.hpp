#pragma once
#include "../shared_peer_runtime.hpp"
#include "../session_transport_manager.hpp"
#include "../direct_transport_adapter.hpp"
#include <deque>

namespace peerchat::portable {
// Platform execution adapter for a messaging-only relay. One shared protocol
// runtime, existing profile-scoped owners, no socket/thread/retry owner here.
class ReliableEndpoint {
public:
    enum class EventKind { TrustRequired, Ready, Message, Delivered, Closed };
    struct Event { EventKind kind;std::string peer,id;std::optional<StoredMessage> message; };
private:
    const Profile& local_;
    Storage& store_;
    session::SessionTransportManager& sessions_;
    delivery::DeliveryManager& deliveries_;
    const reliable::AuthenticatedFriendSnapshot& friends_;
    session::SessionEpoch epoch_;
    session::TransportInstanceId id_;
    std::unique_ptr<TlsSession> tls_;
    reliable::SharedPeerRuntime runtime_;
    session::DirectTransportAdapter observation_;
    Blob output_;
    std::deque<Event> events_;
    bool closed_=false;
    std::vector<session::TransportInstanceId> retired_;
    static constexpr size_t MaxOutput=2*1024*1024;
    std::map<std::string,Contact> contacts()const{
        std::map<std::string,Contact> result;
        for(auto& contact:store_.contacts())result.emplace(contact.peerId,std::move(contact));
        return result;
    }
    void event(Event value){
        if(events_.size()>=256)throw std::runtime_error("Reliable event consumer is stalled");
        events_.push_back(std::move(value));
    }
    void collect(){
        auto bytes=tls_->takeOutput();
        protocol::check(bytes.size()<=MaxOutput&&output_.size()<=MaxOutput-bytes.size());
        output_.insert(output_.end(),bytes.begin(),bytes.end());
    }
    void packet(protocol::Writer& value){
        auto bytes=value.frame();tls_->send(bytes);
        sessions_.observeTransport(observation_.observedSend(bytes.size()),now_);collect();
    }
    std::uint64_t now_=0;
    std::uint64_t began_=0;
    void activate(){
        if(!runtime_.approved||!runtime_.remoteApproved||sessions_.active(id_))return;
        auto result=sessions_.arbitrateWinner(epoch_,id_,now_);
        protocol::check(result.activated());
        protocol::check(deliveries_.authorizeTransport(id_,runtime_.peerId));
        retired_.insert(retired_.end(),result.retire.begin(),result.retire.end());
        event({EventKind::Ready,runtime_.peerId,{},std::nullopt});pump();
    }
    void approve(){
        protocol::check(!closed_&&runtime_.hello&&!runtime_.identityMismatch&&friends_.contains(runtime_.peerId));
        auto known=contacts();auto found=known.find(runtime_.peerId);
        protocol::check(found==known.end()||found->second.publicKey.empty()||found->second.publicKey==tls_->remotePublicKey());
        reliable::persistAuthenticatedContact(store_,runtime_,found==known.end()?nullptr:&found->second,tls_->remotePublicKey());
        runtime_.locallyTrusted();
        sessions_.observeTransport(observation_.locallyTrusted(),now_);
        protocol::check(sessions_.markLocallyTrusted(epoch_,id_));
        protocol::Writer accepted(protocol::Accepted);packet(accepted);activate();
    }
    void dispatch(const Blob& bytes){
        sessions_.observeTransport(observation_.observedReceive(bytes.size()),now_);
        protocol::Reader reader(bytes);
        auto kind=runtime_.enter(reader,true,tls_->remotePublicKey(),local_.peerId);
        if(kind==reliable::SharedPeerRuntime::FrameKind::Hello){
            protocol::check(!runtime_.identityMismatch&&friends_.contains(runtime_.peerId));
            protocol::check(sessions_.observeTransport(observation_.identityPresented(runtime_.peerId,runtime_.remoteCapabilities),now_));
            auto known=contacts();auto found=known.find(runtime_.peerId);
            if(runtime_.verifiedContactMatches(found==known.end()?nullptr:&found->second,tls_->remotePublicKey()))approve();
            else event({EventKind::TrustRequired,runtime_.peerId,{},std::nullopt});
            return;
        }
        if(kind==reliable::SharedPeerRuntime::FrameKind::Accepted){
            protocol::check(sessions_.markRemoteAccepted(epoch_,id_));activate();return;
        }
        runtime_.requireOnline(sessions_.active(id_));
        protocol::check(friends_.contains(runtime_.peerId));
        if(reader.type()==protocol::Text){
            auto result=reliable::receiveText(reader,runtime_.logicalOrdering,runtime_.peerId,local_,store_,deliveries_);
            if(result.message){
                protocol::Writer ack(protocol::Ack);ack.string(result.messageId);packet(ack);
                if(result.inserted)event({EventKind::Message,runtime_.peerId,result.messageId,std::move(result.message)});
            }
        }else if(reader.type()==protocol::Ack){
            auto id=reliable::receiveAck(reader);
            if(deliveries_.acknowledgeMessage(runtime_.peerId,id_,id))event({EventKind::Delivered,runtime_.peerId,id,std::nullopt});
            pump();
        }else throw std::runtime_error("Unsupported operation on messaging-only endpoint");
    }
public:
    ReliableEndpoint(const Profile& local,Storage& store,session::SessionTransportManager& sessions,
            delivery::DeliveryManager& deliveries,const reliable::AuthenticatedFriendSnapshot& friends,
            session::SessionEpoch epoch,session::TransportInstanceId id,std::unique_ptr<TlsSession> tls,
            const std::string& expected,session::RouteObservation route,std::uint64_t now)
        :local_(local),store_(store),sessions_(sessions),deliveries_(deliveries),friends_(friends),
         epoch_(epoch),id_(id),tls_(std::move(tls)),observation_(epoch,id,route,expected),now_(now),began_(now){
        protocol::check(tls_&&protocol::id(expected)&&expected!=local.peerId&&friends.contains(expected)&&
            route.kind==session::TransportKind::Relay&&route.routeClass==session::RouteClass::ServerRelay);
        runtime_.expected=expected;
        protocol::check(sessions_.registerAttempt(epoch,id,expected,route,now));
    }
    ~ReliableEndpoint(){close(false);}
    bool ready()const{return !closed_&&sessions_.active(id_);}
    bool closed()const noexcept{return closed_;}
    bool authenticationExpired(std::uint64_t now)const noexcept{return now>=began_&&now-began_>=20000;}
    const std::string& expected()const noexcept{return runtime_.expected;}
    void receive(const Blob& bytes,std::uint64_t now){
        protocol::check(!closed_&&friends_.contains(runtime_.expected)&&sessions_.attachablePeer(epoch_,id_)==std::optional<std::string>{runtime_.expected});now_=now;
        try{
            sessions_.observeProgress(epoch_,id_,now);
            bool live=runtime_.receive(*tls_,bytes,local_,protocol::CapabilityMutualTls|protocol::CapabilityLogicalOrdering,0,
                [&]{return !closed_;},[&]{collect();},[&](protocol::Writer& value){packet(value);},[&](const Blob& frame){dispatch(frame);});
            if(!live)close(true);
        }catch(...){close(false);throw;}
    }
    bool initialPin(std::uint64_t generation,std::uint64_t now){
        now_=now;auto key=tls_->ready()?tls_->remotePublicKey():Blob{};
        auto pins=contacts();
        auto decision=friends_.initialPin(&runtime_,!closed_,runtime_.expected,generation,tls_->ready()?&key:nullptr,pins);
        if(decision!=reliable::InitialPinDecision::Allowed)return false;
        approve();return true;
    }
    void pump(){
        if(!ready()||!friends_.contains(runtime_.expected))return;
        for(const auto& message:deliveries_.pendingMessages(runtime_.peerId,100)){
            if(deliveries_.wasSubmittedMessage(runtime_.peerId,message.id,id_))continue;
            if(deliveries_.inFlightCount(id_)>=16||output_.size()>MaxOutput/2)break;
            auto value=reliable::textPacket(message,runtime_.logicalOrdering);packet(value);
            protocol::check(deliveries_.submittedMessage(runtime_.peerId,message.id,id_));
        }
    }
    Blob takeOutput(){
        auto count=std::min<size_t>(65536,output_.size());
        Blob bytes(output_.begin(),output_.begin()+count);
        output_.erase(output_.begin(),output_.begin()+count);
        if(!closed_)pump();
        return bytes;
    }
    std::optional<Event> poll(){if(events_.empty())return {};auto value=std::move(events_.front());events_.pop_front();return value;}
    std::vector<session::TransportInstanceId> takeRetired(){auto result=std::move(retired_);retired_.clear();return result;}
    void close(bool recoverable)noexcept{
        if(closed_)return;
        closed_=true;runtime_.stop();
        try{deliveries_.retireTransport(id_);}catch(...){}
        try{sessions_.closeTransport(epoch_,id_,"platform relay closed",recoverable);}catch(...){}
        try{tls_->shutdown();}catch(...){}
        output_.clear();
        try{event({EventKind::Closed,runtime_.expected,{},std::nullopt});}catch(...){}
    }
};
}
