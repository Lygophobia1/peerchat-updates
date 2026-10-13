#pragma once
#include "../shared_peer_runtime.hpp"
#include "../session_transport_manager.hpp"
#include "../direct_transport_adapter.hpp"
#include "../archive_session.hpp"
#include "../archive_history.hpp"
#include <deque>

namespace peerchat::portable {
// Platform execution adapter for a messaging-only relay. One shared protocol
// runtime, existing profile-scoped owners, no socket/thread/retry owner here.
class ReliableEndpoint {
public:
    enum class EventKind { TrustRequired, Ready, Message, Delivered, Closed };
    struct Event { EventKind kind;std::string peer,id;std::optional<StoredMessage> message; };
    struct ArchiveAdmission {
        archive::Authorization* policy=nullptr; // Existing profile owner outlives endpoint.
        session::ReliableAuthorization scope;
        // Called on the existing worker; the profile/controller owner outlives
        // this endpoint. Empty response is permitted for a consumed page.
        std::function<Blob(const Blob&)> control{};
        bool attachments=false;
    };
    // Platform execution seam into the ONE profile TransferManager. No sockets,
    // retry policy, transfer journal or key ownership lives in this endpoint.
    using TransferReceiver=std::function<void(const std::string&,const Blob&,std::uint64_t)>;
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
    std::optional<ArchiveAdmission> archive_;
    TransferReceiver transfer_;
    bool archiveBound_=false,archiveBindingSent_=false;
    bool authorized()const{
        return archive_?archive_->policy&&archive::currentSessionAuthorization(*archive_->policy,archive_->scope):
            friends_.contains(runtime_.expected);
    }
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
        if(!archive_)protocol::check(deliveries_.authorizeTransport(id_,runtime_.peerId));
        retired_.insert(retired_.end(),result.retire.begin(),result.retire.end());
        if(archive_){
            protocol::check(authorized());
            if(!archiveBindingSent_){archiveBindingSent_=true;auto bind=archive::sessionBinding(archive_->scope);packet(bind);}
            return;
        }
        event({EventKind::Ready,runtime_.peerId,{},std::nullopt});pump();
    }
    void approve(){
        protocol::check(!closed_&&runtime_.hello&&!runtime_.identityMismatch&&authorized());
        if(archive_){
            auto key=tls_->remotePublicKey();
            protocol::check(archive::currentSessionAuthorization(*archive_->policy,archive_->scope,&key));
        }else{
            auto known=contacts();auto found=known.find(runtime_.peerId);
            protocol::check(found==known.end()||found->second.publicKey.empty()||found->second.publicKey==tls_->remotePublicKey());
            reliable::persistAuthenticatedContact(store_,runtime_,found==known.end()?nullptr:&found->second,tls_->remotePublicKey());
        }
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
            protocol::check(!runtime_.identityMismatch&&authorized());
            protocol::check(sessions_.observeTransport(observation_.identityPresented(runtime_.peerId,runtime_.remoteCapabilities),now_));
            if(archive_){
                protocol::check((runtime_.remoteCapabilities&protocol::CapabilityArchiveControl)!=0);approve();return;
            }
            auto known=contacts();auto found=known.find(runtime_.peerId);
            if(runtime_.verifiedContactMatches(found==known.end()?nullptr:&found->second,tls_->remotePublicKey()))approve();
            else event({EventKind::TrustRequired,runtime_.peerId,{},std::nullopt});
            return;
        }
        if(kind==reliable::SharedPeerRuntime::FrameKind::Accepted){
            protocol::check(sessions_.markRemoteAccepted(epoch_,id_));activate();return;
        }
        runtime_.requireOnline(sessions_.active(id_));
        if(archive_){
            protocol::check(authorized());
            if(!archiveBound_){
                archive::verifySessionBinding(reader,archive_->scope);
                archiveBound_=true;event({EventKind::Ready,runtime_.peerId,{},std::nullopt});return;
            }
            protocol::check(bool(archive_->control));archive::validateHistoryControl(bytes);
            auto response=archive_->control(bytes);protocol::check(authorized());
            if(!response.empty())sendArchiveControl(response);
            return;
        }
        protocol::check(friends_.contains(runtime_.peerId));
        if(reader.type()==protocol::FileTransfer){
            protocol::check(bool(transfer_)&&(runtime_.remoteCapabilities&protocol::CapabilityFileTransfer)!=0);
            transfer_(runtime_.peerId,bytes,now_);
        }else if(reader.type()==protocol::Text){
            auto result=reliable::receiveText(reader,runtime_.logicalOrdering,runtime_.peerId,local_,store_,deliveries_,friends_.localAccount());
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
            const std::string& expected,session::RouteObservation route,std::uint64_t now,
            std::optional<ArchiveAdmission> archiveAdmission={},TransferReceiver transferReceiver={})
        :local_(local),store_(store),sessions_(sessions),deliveries_(deliveries),friends_(friends),
         epoch_(epoch),id_(id),tls_(std::move(tls)),observation_(epoch,id,route,expected),
         archive_(std::move(archiveAdmission)),transfer_(std::move(transferReceiver)),now_(now),began_(now){
        runtime_.expected=expected;
        protocol::check(tls_&&protocol::id(expected)&&expected!=local.peerId&&authorized()&&
            route.kind==session::TransportKind::Relay&&route.routeClass==session::RouteClass::ServerRelay);
        if(archive_){
            protocol::check(archive_->policy->localDevice()==local.peerId&&archive_->scope.expectedRemoteDeviceId==expected);
            protocol::check(sessions_.recognizeArchiveControlAttempt(epoch,id,archive_->scope,route)||
                sessions_.registerArchiveControlAttempt(epoch,id,archive_->scope,route,now));
        }else protocol::check(sessions_.registerAttempt(epoch,id,expected,route,now));
    }
    ~ReliableEndpoint(){close(false);}
    bool ready()const{return !closed_&&sessions_.active(id_)&&(!archive_||archiveBound_)&&authorized();}
    bool closed()const noexcept{return closed_;}
    void sendArchiveControl(const Blob& bytes){
        protocol::check(archive_&&ready());archive::validateHistoryControl(bytes);
        protocol::Writer value(protocol::ArchiveControl);value.data=bytes;packet(value);
    }
    bool authenticationExpired(std::uint64_t now)const noexcept{return now>=began_&&now-began_>=20000;}
    const std::string& expected()const noexcept{return runtime_.expected;}
    const session::ReliableAuthorization* archiveScope()const noexcept{return archive_?&archive_->scope:nullptr;}
    void receive(const Blob& bytes,std::uint64_t now){
        protocol::check(!closed_);now_=now;
        try{
            const auto continuity=archive_?archive_->scope.continuityKey():runtime_.expected;
            protocol::check(authorized()&&sessions_.attachablePeer(epoch_,id_)==std::optional<std::string>{continuity});
            sessions_.observeProgress(epoch_,id_,now);
            const auto capabilities=protocol::CapabilityMutualTls|protocol::CapabilityLogicalOrdering|
                (archive_?protocol::CapabilityArchiveControl:0)|((archive_?archive_->attachments:bool(transfer_))?protocol::CapabilityFileTransfer:0);
            bool live=runtime_.receive(*tls_,bytes,local_,capabilities,0,
                [&]{return !closed_;},[&]{collect();},[&](protocol::Writer& value){packet(value);},[&](const Blob& frame){dispatch(frame);});
            if(!live)close(true);
        }catch(...){close(false);throw;}
    }
    bool initialPin(std::uint64_t generation,std::uint64_t now){
        if(archive_)return false;
        now_=now;auto key=tls_->ready()?tls_->remotePublicKey():Blob{};
        auto pins=contacts();
        auto decision=friends_.initialPin(&runtime_,!closed_,runtime_.expected,generation,tls_->ready()?&key:nullptr,pins);
        if(decision!=reliable::InitialPinDecision::Allowed)return false;
        approve();return true;
    }
    void pump(){
        if(archive_||!ready()||!friends_.contains(runtime_.expected))return;
        for(const auto& message:deliveries_.pendingMessages(runtime_.peerId,100)){
            if(deliveries_.wasSubmittedMessage(runtime_.peerId,message.id,id_))continue;
            if(deliveries_.inFlightCount(id_)>=16||output_.size()>MaxOutput/2)break;
            auto value=reliable::textPacket(message,runtime_.logicalOrdering);packet(value);
            protocol::check(deliveries_.submittedMessage(runtime_.peerId,message.id,id_));
        }
    }
    bool sendTransfer(const Blob& bytes){
        if(archive_||!transfer_||!ready()||!friends_.contains(runtime_.expected)||
           !(runtime_.remoteCapabilities&protocol::CapabilityFileTransfer)||output_.size()>MaxOutput/2)return false;
        protocol::check(bytes.size()<=65536);protocol::Reader reader(bytes);protocol::check(reader.type()==protocol::FileTransfer);
        protocol::Writer frame(protocol::FileTransfer);frame.data=bytes;packet(frame);return true;
    }
    Blob takeOutput(){
        if(archive_&&!authorized()){close(false);return {};}
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
