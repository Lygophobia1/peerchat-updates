#pragma once
#include "archive_history.hpp"

namespace peerchat::archive {
// One bounded application job on the existing profile worker. This is NOT a
// transport/retry owner: the caller supplies an admitted session, pulls at most
// one request and informs it of disconnect/cancel. DeliveryManager commits data.
class SyncReader {
    Storage& storage_;const Profile& local_;Authorization& authorization_;delivery::DeliveryManager& deliveries_;
    session::ReliableAuthorization scope_;DeviceAccounts directory_;
    std::string context_;Blob saved_;ManifestRequest position_;
    std::optional<ManifestPage> page_;size_t index_=0;
    std::unique_ptr<HistoryReader> history_;
    std::uint64_t processed_=0,conversations_=0;
    bool complete_=false,pending_=false,cancelled_=false;
    Blob state()const{
        auto w=historyHeader(8);manifestFields(w,position_);w.number(processed_);w.number(conversations_);w.number(complete_);return w.data;
    }
    void persist(){auto next=state();storage_.saveArchiveSyncState(context_,saved_,next);saved_=std::move(next);}
    void current()const{require(!cancelled_&&storage_.archiveSyncState(context_)==saved_&&currentSessionAuthorization(authorization_,scope_));}
    void advance(){
        require(page_&&index_<page_->conversations.size()&&conversations_<UINT64_MAX);
        position_.after=page_->conversations[index_].cursor;++index_;++conversations_;history_.reset();
        if(index_==page_->conversations.size()){
            position_.after=page_->next;complete_=page_->complete;page_.reset();index_=0;
        }
        persist();
    }
public:
    struct Snapshot {std::uint64_t processed,conversations;bool complete,pending,cancelled;};
    SyncReader(Storage& storage,const Profile& local,Authorization& authorization,delivery::DeliveryManager& deliveries,
        session::ReliableAuthorization scope,DeviceAccounts directory)
        :storage_(storage),local_(local),authorization_(authorization),deliveries_(deliveries),scope_(std::move(scope)),directory_(std::move(directory)){
        require(bool(directory_)&&scope_.archive.reader==local.peerId&&currentSessionAuthorization(authorization_,scope_));
        auto w=historyHeader(9);w.number(scope_.archive.account);w.string(local.peerId);w.string(scope_.archive.issuer);w.string(scope_.archive.source);
        static const char hex[]="0123456789abcdef";for(auto b:digestDevicePayload(w.data)){context_+=hex[b>>4];context_+=hex[b&15];}
        saved_=storage_.archiveSyncState(context_);
        if(saved_.empty()){position_={Storage::newMessageId(),{},0,0};persist();}
        else{
            protocol::Reader r(saved_);historyHeader(r,8);position_=manifestFields(r);processed_=r.number();conversations_=r.number();
            auto complete=r.number();require(complete<=1);complete_=complete!=0;r.end();require(state()==saved_&&(!complete_||position_.after==position_.through));
        }
    }
    // Explicit new scan preserves every per-conversation resume cursor. A
    // completed scan is never silently restarted by a transport poll.
    void rescan(){current();require(!pending_&&complete_);position_.after=position_.through=0;complete_=false;persist();}
    Blob nextRequest(){
        current();require(!pending_);if(complete_)return {};
        Blob request;
        if(page_){
            require(index_<page_->conversations.size());
            if(!history_)history_=std::make_unique<HistoryReader>(storage_,local_,authorization_,deliveries_,scope_,page_->conversations[index_].conversation,directory_);
            request=history_->nextRequest();
        }else{position_.id=Storage::newMessageId();persist();request=encodeManifestRequest(position_);}
        pending_=true;return request;
    }
    size_t accept(const Blob& bytes){
        current();require(pending_);
        if(history_){
            auto before=history_->snapshot().processed;auto inserted=history_->acceptPage(bytes);auto snapshot=history_->snapshot();
            require(snapshot.processed>=before&&processed_<=UINT64_MAX-(snapshot.processed-before));processed_+=snapshot.processed-before;
            pending_=false;if(snapshot.complete)advance();else persist();return inserted;
        }
        auto page=decodeManifestPage(bytes);const auto& q=page.request;
        require(q.id==position_.id&&q.after==position_.after&&(position_.store.empty()||q.store==position_.store)&&
            (!position_.through||q.through==position_.through));
        // No ownership is inferred from a conversation name or a remote claim.
        // Message account/device provenance is validated again by HistoryReader.
        current();position_=q;pending_=false;
        if(page.conversations.empty()){require(page.complete);position_.after=page.next;complete_=true;persist();}
        else{page_=std::move(page);index_=0;persist();}
        return 0;
    }
    void disconnected(){pending_=false;if(history_)history_->disconnected();}
    void cancel(){cancelled_=true;pending_=false;if(history_)history_->cancel();}
    Snapshot snapshot()const{return {processed_,conversations_,complete_,pending_,cancelled_};}
};
inline Blob serveSyncControl(Storage& storage,const Profile& local,Authorization& authorization,
    const session::ReliableAuthorization& scope,const Blob& request,DeviceAccounts directory,std::uint64_t now){
    validateHistoryControl(request);protocol::Reader r(request);require(r.type()==protocol::ArchiveControl);r.string();r.number();auto kind=r.number();
    if(kind==4)return serveManifestPage(storage,local,authorization,scope,request);
    require(kind==2);return serveHistoryPage(storage,local,authorization,scope,request,std::move(directory),now);
}
}
