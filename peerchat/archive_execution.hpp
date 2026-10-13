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
    std::uint64_t pendingAt_=0,nextAutomaticAt_=0;
    std::uint64_t pendingAttempt_=0;
    bool completedIntent_=false;
    std::string lastIncoming_="none";
    std::string rejectedStage_="none";
    const char* sourceRejectedStage_="none";
    std::vector<std::string> transportEvidence_;
    std::uint64_t received_=0,lastSuccessful_=0,scanGeneration_=0;
    std::string presentationContext_;Blob savedPresentation_;
    DeviceAccounts directory()const{return [this](const std::string& device){return storage_.historicalDeviceAccount(device);};}
    void intent(const std::string& issuer,const std::string& source,unsigned disposition){
        require(disposition<=2); // 0 explicit pause/failure, 1 running, 2 completed.
        protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveIntent/v2");w.number(policy_.account());w.string(local_.peerId);w.string(issuer);w.string(source);w.number(disposition);
        storage_.saveArchiveSyncState(intentContext_,savedIntent_,w.data);savedIntent_=std::move(w.data);intentIssuer_=issuer;intentSource_=source;restore_=false;completedIntent_=disposition==2;
    }
public:
    Execution(Storage& storage,const Profile& local,Authorization& policy,delivery::DeliveryManager& deliveries,
        session::SessionTransportManager& sessions):storage_(storage),local_(local),policy_(policy),deliveries_(deliveries),sessions_(sessions){
        protocol::Writer key(LocalFormat);key.string("PeerChat/ArchiveIntentContext/v1");key.number(policy_.account());key.string(local_.peerId);
        static const char hex[]="0123456789abcdef";for(auto b:digestDevicePayload(key.data)){intentContext_+=hex[b>>4];intentContext_+=hex[b&15];}
        savedIntent_=storage_.archiveSyncState(intentContext_);
        key.string("PeerChat/ArchivePresentation/v1");
        for(auto b:digestDevicePayload(key.data)){presentationContext_+=hex[b>>4];presentationContext_+=hex[b&15];}
        savedPresentation_=storage_.archiveSyncState(presentationContext_);
        if(!savedPresentation_.empty()){
            protocol::Reader r(savedPresentation_);require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchivePresentation/v1"&&r.number()==policy_.account()&&r.string()==local_.peerId);
            lastSuccessful_=r.number();r.end();require(lastSuccessful_<=INT64_MAX);
        }
        if(!savedIntent_.empty()){
            protocol::Reader r(savedIntent_);require(r.type()==LocalFormat);auto version=r.string();require((version=="PeerChat/ArchiveIntent/v1"||version=="PeerChat/ArchiveIntent/v2")&&r.number()==policy_.account()&&r.string()==local_.peerId);
            intentIssuer_=r.string();intentSource_=r.string();auto active=r.number();r.end();
            require(protocol::id(intentIssuer_)&&protocol::id(intentSource_)&&intentSource_!=local_.peerId&&active<=(version=="PeerChat/ArchiveIntent/v1"?1u:2u));restore_=active==1;completedIntent_=active==2;
            // Old false intents cannot distinguish Pause from completion. Never
            // silently undo an explicit legacy pause; Sync now upgrades safely.
            state_=restore_?"Waiting for archive authorization":completedIntent_?"History synchronized":"Paused or legacy completed; Sync now resumes";
        }
    }
    const std::optional<session::ReliableAuthorization>& scope()const{return scope_;}
    bool running()const{return running_;}
    std::uint64_t received()const{return received_;}
    std::uint64_t lastSuccessful()const{return lastSuccessful_;}
    std::uint64_t scanGeneration()const{return scanGeneration_;}
    std::uint64_t historyRevision()const{return static_cast<std::uint64_t>(storage_.archiveHistoryWatermark());}
    // Read-only application diagnostics; holding a route does not imply pages
    // have transferred. No fabricated provider counters or percentage.
    std::vector<std::string> providingDevices(std::uint64_t now)const{
        std::vector<std::string> result;
        for(const auto& entry:serving_)if(now<entry.second.second&&entry.second.first.archive.source==local_.peerId&&currentSessionAuthorization(policy_,entry.second.first)){
            auto route=sessions_.snapshot(entry.first);if(route&&route->connected)result.push_back(entry.second.first.archive.reader);
        }
        return result;
    }
    // Initial participation only. A durable pause/completion is never silently
    // replaced, and this predicate cannot enroll a device or issue a grant.
    bool needsInitialSync()const{return savedIntent_.empty()&&!reader_&&!running_&&policy_.historySyncEnabled();}
    bool needsAutomaticSync(std::uint64_t now)const{return policy_.historySyncEnabled()&&(needsInitialSync()||(completedIntent_&&!running_&&now>=nextAutomaticAt_));}
    const std::string& lastAutomaticSource()const{return intentSource_;}
    // Only an explicitly active restart intent may change authorization scope.
    // Never reinterpret a legacy paused intent, or mutate trust/grants/cursors.
    std::string blockedRestoreSource()const{
        if(!restore_||!policy_.historySyncEnabled())return {};
        try{(void)sessionAuthorization(policy_,intentIssuer_,intentSource_,local_.peerId);return {};}catch(const std::exception&){return intentSource_;}
    }
    void resumeAuthorized(const std::string& issuer,const std::string& source){
        require(restore_&&source==intentSource_&&!running_);
        // start validates the complete new scope and opens its own durable
        // cursor. The previous issuer's cursor remains untouched; imports dedupe.
        start(issuer,source,false);
    }
    void startAutomatic(const std::string& issuer,const std::string& source,std::uint64_t now){
        require(needsAutomaticSync(now)&&now<=UINT64_MAX-60000);start(issuer,source,true);nextAutomaticAt_=now+60000;
    }
    const std::string& state()const{return state_;}
    // Bounded, volatile, read-only evidence. Callers supply constant stages,
    // never exception messages, TLS tokens, payloads or storage contents.
    void recordTransport(std::uint64_t attempt,const session::ReliableAuthorization& scope,const char* stage)noexcept{
        try{
            if(!scope.valid()||!scope.permitsArchiveControl()||scope.archive.account!=policy_.account())return;
            const std::string label=stage?stage:"unknown";
            if(label.empty()||label.size()>64||label.find_first_not_of("abcdefghijklmnopqrstuvwxyz_")!=std::string::npos)return;
            std::ostringstream line;line<<"archive_transport attempt="<<attempt<<" role="<<(scope.archive.source==local_.peerId?"source":"reader")
                <<" remote="<<scope.expectedRemoteDeviceId<<" stage="<<label;
            if(transportEvidence_.size()==8)transportEvidence_.erase(transportEvidence_.begin());
            transportEvidence_.push_back(line.str());
        }catch(...){/* Diagnostics cannot affect routing or authorization. */}
    }
    std::string diagnostics()const{
        std::ostringstream out;
        out<<"\n=== History Sync execution ===\nstate="<<state_<<" running="<<running_<<" restore_pending="<<restore_<<" completed_intent="<<completedIntent_
           <<"\nintent_issuer="<<intentIssuer_<<" intent_source="<<intentSource_;
        auto progress=snapshot();out<<"\nmessages_processed="<<progress.processed<<" conversations_completed="<<progress.conversations<<" scan_complete="<<progress.complete<<" page_pending="<<progress.pending<<" pending_request_attempt="<<pendingAttempt_;
        if(scope_){const auto& a=scope_->archive;
            out<<"\nactive_scope_issuer="<<a.issuer<<" source="<<a.source<<" reader="<<a.reader<<" source_generation="<<a.sourceGeneration<<" reader_generation="<<a.readerGeneration
               <<" source_grant_id="<<a.sourceGrantId<<" reader_grant_id="<<a.readerGrantId<<" scope_current="<<currentSessionAuthorization(policy_,*scope_);
            auto route=sessions_.snapshot(scope_->continuityKey());
            if(route){auto reason=route->latestFailureUtf8.substr(0,256);for(auto& c:reason)if(static_cast<unsigned char>(c)<32)c=' ';
                out<<"\narchive_route connected="<<route->connected<<" pending="<<route->pending<<" winner="<<route->activeTransport.value
                   <<" latest_failed_attempt="<<route->latestFailedTransport.value<<" retry_scheduled="<<route->retryScheduled<<" retry_suspended="<<route->retrySuspended
                   <<" retry_delay_ms="<<route->retryDelayMilliseconds<<" network_generation="<<route->networkRevision<<" last_failure="<<reason;
            }else out<<"\narchive_route=not_requested";
        }else out<<"\nactive_scope=none";
        out<<"\nlast_incoming_archive="<<lastIncoming_<<"\ndata_rejection_stage="<<rejectedStage_<<"\n";
        out<<"last_source_rejection_stage="<<sourceRejectedStage_<<'\n';
        for(const auto& line:transportEvidence_)out<<line<<'\n';
        return out.str();
    }
    // A corrupt checkpoint/page stops only archive work, never the profile's
    // ordinary messaging worker. The protected record is not overwritten.
    void failed(){restore_=false;running_=false;pendingAttempt_=0;if(reader_)reader_->cancel();state_="Archive synchronization failed closed";}
    SyncReader::Snapshot snapshot()const{return reader_?reader_->snapshot():SyncReader::Snapshot{0,0,false,false,false};}
    std::vector<session::TransportInstanceId> stop(){
        if(!intentIssuer_.empty())intent(intentIssuer_,intentSource_,false);
        running_=false;pendingAttempt_=0;state_="Paused";if(reader_)reader_->cancel();
        return scope_?sessions_.finishArchiveRoute(*scope_):std::vector<session::TransportInstanceId>{};
    }
    void start(const std::string& issuer,const std::string& source,bool newScan){
        require(!running_&&source!=local_.peerId);
        auto scope=sessionAuthorization(policy_,issuer,source,local_.peerId);
        auto reader=std::make_unique<SyncReader>(storage_,local_,policy_,deliveries_,scope,directory(),[this]{require(received_<INT64_MAX);++received_;});
        if(newScan&&reader->snapshot().complete)reader->rescan();
        intent(issuer,source,reader->snapshot().complete?2:1);
        scope_=std::move(scope);reader_=std::move(reader);pendingAttempt_=0;running_=!reader_->snapshot().complete;
        require(scanGeneration_<INT64_MAX);++scanGeneration_;received_=0;rejectedStage_="none";
        state_=running_?"Connecting to archive":"History synchronized";
    }
    void incoming(const session::ReliableAuthorization& scope,std::uint64_t now,std::uint64_t network){
        const auto& a=scope.archive;
        std::ostringstream detail;detail<<"issuer="<<a.issuer<<" source="<<a.source<<" reader="<<a.reader<<" source_generation="<<a.sourceGeneration<<" reader_generation="<<a.readerGeneration;
        if(a.account==policy_.account())detail<<" source_provide="<<policy_.admissionReason(a.issuer,a.source,ServeHistory)<<" reader_receive="<<policy_.admissionReason(a.issuer,a.reader,ReadHistory);
        detail<<" exact_scope_current="<<currentSessionAuthorization(policy_,scope);lastIncoming_=detail.str();
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
            reader_->disconnected();pendingAttempt_=0;state_=policy_.historySyncEnabled()?"Archive authorization unavailable":"History synchronization is off";
            retire(*scope_);return close;
        }
        if(reader_->snapshot().complete){intent(scope_->archive.issuer,scope_->archive.source,2);running_=false;state_="History synchronized";if(!serving_.count(scope_->continuityKey()))retire(*scope_);return close;}
        if(reader_->snapshot().pending&&now>=pendingAt_&&now-pendingAt_>=15000){
            auto route=sessions_.snapshot(scope_->continuityKey());
            if(route&&route->activeTransport.value==pendingAttempt_&&pendingAttempt_){auto id=route->activeTransport;sessions_.closeTransport(route->epoch,id,"Archive page response deadline",true);close.push_back(id);
                sessions_.scheduleRelayRetry(scope_->continuityKey(),now,static_cast<std::uint32_t>(now));}
            else recordTransport(pendingAttempt_,*scope_,"stale_deadline_ignored");
            reader_->disconnected();pendingAttempt_=0;state_="Archive source response timed out";
        }
        require(now<=UINT64_MAX-30000);
        if(sessions_.demandArchiveRoute(*scope_,now+30000,network,now)){
            auto route=sessions_.snapshot(scope_->continuityKey());
            state_=route&&route->connected?"Synchronizing history":"Connecting to archive";
        }else state_="Archive route unavailable";
        return close;
    }
    Blob request(const session::ReliableAuthorization& scope,std::uint64_t now,std::uint64_t attempt){
        require(attempt&&currentSessionAuthorization(policy_,scope));
        if(!running_||!scope_||sessionBinding(*scope_).data!=sessionBinding(scope).data||reader_->snapshot().pending)return {};
        auto bytes=reader_->nextRequest();pendingAt_=now;pendingAttempt_=bytes.empty()?0:attempt;return bytes;
    }
    Blob control(const session::ReliableAuthorization& scope,const Blob& bytes,std::uint64_t utc,std::uint64_t now){
        require(currentSessionAuthorization(policy_,scope));
        if(scope.archive.source==local_.peerId){
            const char* stage="source_serving_lease";
            try{auto found=serving_.find(scope.continuityKey());require(found!=serving_.end()&&now<found->second.second&&
                sessionBinding(found->second.first).data==sessionBinding(scope).data);
            auto response=serveSyncControl(storage_,local_,policy_,scope,bytes,directory(),utc,&stage);
            require(now<=UINT64_MAX-30000);found->second.second=now+30000;return response;
            }catch(...){sourceRejectedStage_=stage;recordTransport(0,scope,stage);throw;}
        }
        require(running_&&scope_&&sessionBinding(*scope_).data==sessionBinding(scope).data);
        try{
            reader_->accept(bytes);pendingAttempt_=0;
            if(reader_->snapshot().complete){
                require(utc<=INT64_MAX);protocol::Writer w(LocalFormat);w.string("PeerChat/ArchivePresentation/v1");w.number(policy_.account());w.string(local_.peerId);w.number(utc);
                storage_.saveArchiveSyncState(presentationContext_,savedPresentation_,w.data);savedPresentation_=std::move(w.data);lastSuccessful_=utc;
            }
            return {};
        }
        catch(...){rejectedStage_=reader_->acceptStage();intent(scope_->archive.issuer,scope_->archive.source,false);running_=false;pendingAttempt_=0;reader_->cancel();state_="Archive data rejected; synchronization stopped";throw;}
    }
    void disconnected(const session::ReliableAuthorization& scope,std::uint64_t attempt){
        if(scope_&&sessionBinding(*scope_).data==sessionBinding(scope).data&&reader_&&pendingAttempt_){
            // Scope identifies the job, not the socket that owns its in-flight
            // request. Late loser/old-winner cleanup must not release a new
            // winner's backpressure or replace its response correlation ID.
            if(attempt!=pendingAttempt_){recordTransport(attempt,scope,"stale_cleanup_ignored");return;}
            reader_->disconnected();pendingAttempt_=0;
        }
    }
};
}
