#pragma once
#include "archive_sync.hpp"
#include "session_transport_manager.hpp"

namespace peerchat::archive {
// Profile-worker application execution. No thread/socket/reconnect policy:
// demand, retry and winner selection remain SessionTransportManager operations.
class Execution {
    Storage& storage_;const Profile& local_;Authorization& policy_;
    delivery::DeliveryManager& deliveries_;session::SessionTransportManager& sessions_;
    std::unique_ptr<SyncReader> reader_;
    std::optional<session::ReliableAuthorization> scope_;
    std::map<std::string,std::pair<session::ReliableAuthorization,std::uint64_t>> serving_;
    bool running_=false;std::string state_="Not started";
    std::string intentContext_,intentIssuer_,intentSource_;Blob savedIntent_;bool restore_=false;
    std::uint64_t pendingAt_=0;
    DeviceAccounts directory()const{return [this](const std::string& device){return storage_.historicalDeviceAccount(device);};}
    void intent(const std::string& issuer,const std::string& source,bool active){
        protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveIntent/v1");w.number(policy_.account());w.string(local_.peerId);w.string(issuer);w.string(source);w.number(active);
        storage_.saveArchiveSyncState(intentContext_,savedIntent_,w.data);savedIntent_=std::move(w.data);intentIssuer_=issuer;intentSource_=source;restore_=false;
    }
public:
    Execution(Storage& storage,const Profile& local,Authorization& policy,delivery::DeliveryManager& deliveries,
        session::SessionTransportManager& sessions):storage_(storage),local_(local),policy_(policy),deliveries_(deliveries),sessions_(sessions){
        protocol::Writer key(LocalFormat);key.string("PeerChat/ArchiveIntentContext/v1");key.number(policy_.account());key.string(local_.peerId);
        static const char hex[]="0123456789abcdef";for(auto b:digestDevicePayload(key.data)){intentContext_+=hex[b>>4];intentContext_+=hex[b&15];}
        savedIntent_=storage_.archiveSyncState(intentContext_);
        if(!savedIntent_.empty()){
            protocol::Reader r(savedIntent_);require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchiveIntent/v1"&&r.number()==policy_.account()&&r.string()==local_.peerId);
            intentIssuer_=r.string();intentSource_=r.string();auto active=r.number();r.end();
            require(protocol::id(intentIssuer_)&&protocol::id(intentSource_)&&intentSource_!=local_.peerId&&active<=1);restore_=active!=0;
            state_=restore_?"Waiting for archive authorization":"Paused or previously completed";
        }
    }
    const std::optional<session::ReliableAuthorization>& scope()const{return scope_;}
    bool running()const{return running_;}
    const std::string& state()const{return state_;}
    // A corrupt checkpoint/page stops only archive work, never the profile's
    // ordinary messaging worker. The protected record is not overwritten.
    void failed(){restore_=false;running_=false;if(reader_)reader_->cancel();state_="Archive synchronization failed closed";}
    SyncReader::Snapshot snapshot()const{return reader_?reader_->snapshot():SyncReader::Snapshot{0,0,false,false,false};}
    std::vector<session::TransportInstanceId> stop(){
        if(!intentIssuer_.empty())intent(intentIssuer_,intentSource_,false);
        running_=false;state_="Paused";if(reader_)reader_->cancel();
        return scope_?sessions_.finishArchiveRoute(*scope_):std::vector<session::TransportInstanceId>{};
    }
    void start(const std::string& issuer,const std::string& source,bool newScan){
        require(!running_&&source!=local_.peerId);
        auto scope=sessionAuthorization(policy_,issuer,source,local_.peerId);
        auto reader=std::make_unique<SyncReader>(storage_,local_,policy_,deliveries_,scope,directory());
        if(newScan&&reader->snapshot().complete)reader->rescan();
        intent(issuer,source,!reader->snapshot().complete);
        scope_=std::move(scope);reader_=std::move(reader);running_=!reader_->snapshot().complete;
        state_=running_?"Connecting to archive":"History synchronized";
    }
    void incoming(const session::ReliableAuthorization& scope,std::uint64_t now,std::uint64_t network){
        require(scope.archive.source==local_.peerId&&currentSessionAuthorization(policy_,scope));
        hold(scope,now,network);
    }
    // Existing bounded demand owner, shared by history pages and authorized
    // TransferManager work; neither adapter owns a reconnect loop.
    void hold(const session::ReliableAuthorization& scope,std::uint64_t now,std::uint64_t network){
        require(currentSessionAuthorization(policy_,scope));
        auto key=scope.continuityKey();require(serving_.count(key)||serving_.size()<8);
        require(now<=UINT64_MAX-30000&&sessions_.demandArchiveRoute(scope,now+30000,network,now));
        serving_[key]={scope,now+30000};
    }
    std::vector<session::TransportInstanceId> tick(std::uint64_t now,std::uint64_t network){
        try{return tickCurrent(now,network);}catch(const std::exception&){
            failed();return scope_?sessions_.finishArchiveRoute(*scope_):std::vector<session::TransportInstanceId>{};
        }
    }
    std::vector<session::TransportInstanceId> tickCurrent(std::uint64_t now,std::uint64_t network){
        std::vector<session::TransportInstanceId> close;
        auto retire=[&](const auto& scope){auto ids=sessions_.finishArchiveRoute(scope);close.insert(close.end(),ids.begin(),ids.end());};
        if(restore_){
            // Only absence of current authorization is retryable. Corrupt
            // durable state must not be mislabeled as an offline source.
            bool allowed=false;try{(void)sessionAuthorization(policy_,intentIssuer_,intentSource_,local_.peerId);allowed=true;}catch(const std::exception&){}
            if(!allowed){state_=policy_.historySyncEnabled()?"Archive authorization unavailable":"History synchronization is off";return close;}
            start(intentIssuer_,intentSource_,false);
        }
        for(auto it=serving_.begin();it!=serving_.end();){
            if(now>=it->second.second||!currentSessionAuthorization(policy_,it->second.first)){retire(it->second.first);it=serving_.erase(it);}
            else{sessions_.demandArchiveRoute(it->second.first,it->second.second,network,now);++it;}
        }
        if(!running_||!scope_){if(scope_&&!serving_.count(scope_->continuityKey()))retire(*scope_);return close;}
        if(!currentSessionAuthorization(policy_,*scope_)){
            reader_->disconnected();state_=policy_.historySyncEnabled()?"Archive authorization unavailable":"History synchronization is off";
            retire(*scope_);return close;
        }
        if(reader_->snapshot().complete){intent(scope_->archive.issuer,scope_->archive.source,false);running_=false;state_="History synchronized";if(!serving_.count(scope_->continuityKey()))retire(*scope_);return close;}
        if(reader_->snapshot().pending&&now>=pendingAt_&&now-pendingAt_>=15000){
            auto route=sessions_.snapshot(scope_->continuityKey());
            if(route&&route->activeTransport.value){auto id=route->activeTransport;sessions_.closeTransport(route->epoch,id,"Archive page response deadline",true);close.push_back(id);
                sessions_.scheduleRelayRetry(scope_->continuityKey(),now,static_cast<std::uint32_t>(now));}
            reader_->disconnected();state_="Archive source response timed out";
        }
        require(now<=UINT64_MAX-30000);
        if(sessions_.demandArchiveRoute(*scope_,now+30000,network,now)){
            auto route=sessions_.snapshot(scope_->continuityKey());
            state_=route&&route->connected?"Synchronizing history":"Connecting to archive";
        }else state_="Archive route unavailable";
        return close;
    }
    Blob request(const session::ReliableAuthorization& scope,std::uint64_t now){
        require(currentSessionAuthorization(policy_,scope));
        if(!running_||!scope_||sessionBinding(*scope_).data!=sessionBinding(scope).data||reader_->snapshot().pending)return {};
        auto bytes=reader_->nextRequest();pendingAt_=now;return bytes;
    }
    Blob control(const session::ReliableAuthorization& scope,const Blob& bytes,std::uint64_t utc,std::uint64_t now){
        require(currentSessionAuthorization(policy_,scope));
        if(scope.archive.source==local_.peerId){
            auto found=serving_.find(scope.continuityKey());require(found!=serving_.end()&&now<found->second.second&&
                sessionBinding(found->second.first).data==sessionBinding(scope).data);
            auto response=serveSyncControl(storage_,local_,policy_,scope,bytes,directory(),utc);
            require(now<=UINT64_MAX-30000);found->second.second=now+30000;return response;
        }
        require(running_&&scope_&&sessionBinding(*scope_).data==sessionBinding(scope).data);
        try{reader_->accept(bytes);return {};}
        catch(...){intent(scope_->archive.issuer,scope_->archive.source,false);running_=false;reader_->cancel();state_="Archive data rejected; synchronization stopped";throw;}
    }
    void disconnected(const session::ReliableAuthorization& scope){
        if(scope_&&sessionBinding(*scope_).data==sessionBinding(scope).data&&reader_)reader_->disconnected();
    }
};
}
