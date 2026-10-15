#pragma once
#include "archive_history.hpp"

namespace peerchat::archive {
// One bounded application job on the existing profile worker. This is NOT a
// transport/retry owner: the caller supplies an admitted session, pulls at most
// one request and informs it of disconnect/cancel. DeliveryManager commits data.
class SyncReader {
    Storage& storage_;const Profile& local_;Authorization& authorization_;delivery::DeliveryManager& deliveries_;
    session::ReliableAuthorization scope_;DeviceAccounts directory_;std::function<void()> committed_;
    std::string context_;Blob saved_;ManifestRequest position_;
    std::optional<ManifestPage> page_;size_t index_=0;
    std::unique_ptr<HistoryReader> history_;
    std::unique_ptr<AttachmentReader> attachments_;
    std::unique_ptr<GroupHistoryReader> groups_;
    bool groupsUnsupported_=false;
    std::uint64_t processed_=0,conversations_=0,deferred_=0;
    bool complete_=false,pending_=false,cancelled_=false;
    const char* acceptStage_="not_started";
    Blob state()const{
        auto w=historyHeader(8);manifestFields(w,position_);w.number(processed_);w.number(conversations_);w.number(complete_);w.number(deferred_);return w.data;
    }
    void persist(){auto next=state();storage_.saveArchiveSyncState(context_,saved_,next);saved_=std::move(next);}
    void current()const{require(!cancelled_&&storage_.archiveSyncState(context_)==saved_&&currentSessionAuthorization(authorization_,scope_));}
    void advance(bool deferred=false){
        require(page_&&index_<page_->conversations.size()&&conversations_<UINT64_MAX);
        position_.after=page_->conversations[index_].cursor;++index_;if(!deferred)++conversations_;history_.reset();
        if(index_==page_->conversations.size()){
            position_.after=page_->next;complete_=page_->complete;page_.reset();index_=0;
        }
        persist();
    }
public:
    struct Snapshot {std::uint64_t processed,conversations;bool complete,pending,cancelled;std::uint64_t attachments=0,deferred=0;bool groupsUnsupported=false;};
    SyncReader(Storage& storage,const Profile& local,Authorization& authorization,delivery::DeliveryManager& deliveries,
        session::ReliableAuthorization scope,DeviceAccounts directory,std::function<void()> committed={},GroupHistoryAccess* groups=nullptr)
        :storage_(storage),local_(local),authorization_(authorization),deliveries_(deliveries),scope_(std::move(scope)),directory_(std::move(directory)),committed_(std::move(committed)){
        require(bool(directory_)&&scope_.archive.reader==local.peerId&&currentSessionAuthorization(authorization_,scope_));
        auto w=historyHeader(9);w.number(scope_.archive.account);w.string(local.peerId);w.string(scope_.archive.issuer);w.string(scope_.archive.source);
        static const char hex[]="0123456789abcdef";for(auto b:digestDevicePayload(w.data)){context_+=hex[b>>4];context_+=hex[b&15];}
        saved_=storage_.archiveSyncState(context_);
        if(saved_.empty()){position_={Storage::newMessageId(),{},0,0};persist();}
        else{
            protocol::Reader r(saved_);historyHeader(r,8);position_=manifestFields(r);processed_=r.number();conversations_=r.number();
            auto complete=r.number();require(complete<=1);complete_=complete!=0;bool legacy=!r.remaining();if(!legacy)deferred_=r.number();r.end();auto canonical=state();if(legacy)canonical.resize(canonical.size()-8);require(canonical==saved_&&(!complete_||position_.after==position_.through));
        }
        attachments_=std::make_unique<AttachmentReader>(storage_,authorization_,scope_,directory_);
        if(groups)groups_=std::make_unique<GroupHistoryReader>(storage_,authorization_,scope_,*groups);
    }
    // Explicit new scan preserves every per-conversation resume cursor. A
    // completed scan is never silently restarted by a transport poll.
    void rescan(){current();require(!pending_&&complete_&&attachments_->snapshot().complete);attachments_->rescan();if(groups_&&groups_->complete())groups_->rescan();groupsUnsupported_=false;position_.after=position_.through=0;processed_=conversations_=deferred_=0;complete_=false;persist();}
    Blob nextRequest(bool allowDeferred=false,bool allowGroups=false){
        current();require(!pending_);if(complete_){
            if(!attachments_->snapshot().complete)return attachments_->nextRequest();
            if(groups_&&!groups_->complete()){if(!allowGroups){groupsUnsupported_=true;return {};}return groups_->request();}
            return {};
        }
        Blob request;
        if(page_){
            require(index_<page_->conversations.size());
            if(!history_)history_=std::make_unique<HistoryReader>(storage_,local_,authorization_,deliveries_,scope_,page_->conversations[index_].conversation,directory_,committed_);
            request=history_->nextRequest(allowDeferred);
        }else{position_.id=Storage::newMessageId();persist();request=encodeManifestRequest(position_);}
        pending_=true;return request;
    }
    size_t accept(const Blob& bytes){
        acceptStage_="sync_current_authorization";current();if(complete_){
            if(!attachments_->snapshot().complete){acceptStage_="attachment_page_accept";attachments_->accept(bytes);return 0;}
            require(groups_&&groups_->pending());acceptStage_="group_history_page_accept";return groups_->accept(bytes,committed_);
        }require(pending_);
        if(history_){
            protocol::Reader header(bytes);require(header.type()==protocol::ArchiveControl&&header.string()=="PeerChat/ArchiveHistory/v1"&&header.number()==1);if(header.number()==10){auto count=history_->defer(bytes);require(deferred_<=UINT64_MAX-count);deferred_+=count;pending_=false;advance(true);return 0;}
            auto before=history_->snapshot().processed;auto inserted=history_->acceptPage(bytes);auto snapshot=history_->snapshot();
            require(snapshot.processed>=before&&processed_<=UINT64_MAX-(snapshot.processed-before));processed_+=snapshot.processed-before;
            pending_=false;if(snapshot.complete)advance();else persist();return inserted;
        }
        acceptStage_="manifest_page_decode";auto page=decodeManifestPage(bytes);const auto& q=page.request;
        acceptStage_="manifest_request_binding";
        require(q.id==position_.id&&q.after==position_.after&&(position_.store.empty()||q.store==position_.store)&&
            (!position_.through||q.through==position_.through));
        // No ownership is inferred from a conversation name or a remote claim.
        // Message account/device provenance is validated again by HistoryReader.
        acceptStage_="manifest_cursor_commit";current();position_=q;pending_=false;
        if(page.conversations.empty()){require(page.complete);position_.after=page.next;complete_=true;persist();}
        else{page_=std::move(page);index_=0;persist();}
        return 0;
    }
    void disconnected(){pending_=false;if(history_)history_->disconnected();attachments_->disconnected();if(groups_)groups_->disconnected();}
    const char* acceptStage()const{return history_?history_->acceptStage():acceptStage_;}
    void cancel(){cancelled_=true;disconnected();if(history_)history_->cancel();attachments_->cancel();}
    Snapshot snapshot()const{auto media=attachments_->snapshot();return {processed_+(groups_?groups_->processed():0),conversations_,complete_&&media.complete&&(!groups_||groups_->complete()||groupsUnsupported_),complete_?(media.pending||(groups_&&groups_->pending())):pending_,cancelled_,media.processed,deferred_+(groups_?groups_->deferred():0),groupsUnsupported_};}
};
inline Blob serveSyncControl(Storage& storage,const Profile& local,Authorization& authorization,
    const session::ReliableAuthorization& scope,const Blob& request,DeviceAccounts directory,std::uint64_t now,const char** stage=nullptr,GroupHistoryAccess* groups=nullptr){
    if(stage)*stage="source_control_decode";
    validateHistoryControl(request);protocol::Reader r(request);require(r.type()==protocol::ArchiveControl);auto domain=r.string();
    if(domain==AttachmentDomain){if(stage)*stage="source_attachment_catalog";return serveAttachmentPage(storage,local,authorization,scope,request,directory);}
    if(domain==GroupHistoryDomain){if(stage)*stage="source_group_history";require(groups);return serveGroupHistory(storage,local,authorization,scope,request,*groups);}
    r.number();auto kind=r.number();
    if(kind==4){if(stage)*stage="source_history_manifest";return serveManifestPage(storage,local,authorization,scope,request);}
    require(kind==2||kind==11);return serveHistoryPage(storage,local,authorization,scope,request,std::move(directory),now,stage);
}
}
