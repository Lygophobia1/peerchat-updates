#pragma once
#include "delivery_manager.hpp"
#include "archive_attachment_sync.hpp"
#include "archive_group_history.hpp"
#include <limits>
#include <set>

namespace peerchat::archive {
// A bounded request/response protocol carried ONLY inside an admitted archive
// peer-TLS session. This component owns no socket, retry timer, or history DB.
constexpr size_t HistoryPageRecords=8;
using DeviceAccounts=std::function<std::optional<std::uint64_t>(const std::string&)>;
struct HistoryRequest {
    std::string id,store,conversation;
    std::uint64_t after=0,through=0;
    bool allowDeferred=false;
};
struct HistoryRecord {std::uint64_t cursor=0;Assertion assertion;};
struct HistoryPage {
    HistoryRequest request;
    std::uint64_t next=0;
    bool complete=false;
    std::vector<HistoryRecord> records;
};
struct ManifestRequest {
    std::string id,store;
    std::uint64_t after=0,through=0;
};
struct ManifestEntry {std::uint64_t cursor=0;std::string conversation;};
struct ManifestPage {
    ManifestRequest request;
    std::uint64_t next=0;
    bool complete=false;
    std::vector<ManifestEntry> conversations;
};
inline void validHistoryRequest(const HistoryRequest& q){
    require(protocol::id(q.id,32)&&(q.store.empty()||protocol::id(q.store,32))&&protocol::id(q.conversation)&&
        q.after<=static_cast<std::uint64_t>(INT64_MAX)&&q.through<=static_cast<std::uint64_t>(INT64_MAX)&&
        (!q.through||q.through>=q.after)&&(!q.store.empty()||(!q.after&&!q.through)));
}
inline void historyRequestFields(protocol::Writer& w,const HistoryRequest& q){
    validHistoryRequest(q);w.string(q.id);w.string(q.store);w.string(q.conversation);w.number(q.after);w.number(q.through);
}
inline HistoryRequest historyRequestFields(protocol::Reader& r){
    HistoryRequest q;q.id=r.string();q.store=r.string();q.conversation=r.string();q.after=r.number();q.through=r.number();
    validHistoryRequest(q);return q;
}
inline protocol::Writer historyHeader(std::uint64_t kind){
    protocol::Writer w(protocol::ArchiveControl);w.string("PeerChat/ArchiveHistory/v1");w.number(1);w.number(kind);return w;
}
inline void historyHeader(protocol::Reader& r,std::uint64_t kind){
    require(r.type()==protocol::ArchiveControl&&r.string()=="PeerChat/ArchiveHistory/v1"&&r.number()==1&&r.number()==kind);
}
inline Blob encodeHistoryRequest(const HistoryRequest& q){
    auto w=historyHeader(q.allowDeferred?11:2);historyRequestFields(w,q);return w.data;
}
inline HistoryRequest decodeHistoryRequest(const Blob& bytes){
    require(bytes.size()<=1024);protocol::Reader r(bytes);require(r.type()==protocol::ArchiveControl&&r.string()=="PeerChat/ArchiveHistory/v1"&&r.number()==1);
    auto kind=r.number();require(kind==2||kind==11);auto q=historyRequestFields(r);q.allowDeferred=kind==11;r.end();require(encodeHistoryRequest(q)==bytes);return q;
}
struct DeferredConversation {HistoryRequest request;std::uint64_t records=0;};
inline Blob encodeDeferredConversation(const DeferredConversation& d){
    require(!d.request.store.empty()&&d.request.through>d.request.after&&d.records>0&&d.records<=d.request.through-d.request.after);
    auto w=historyHeader(10);historyRequestFields(w,d.request);w.number(d.records);w.string("device_mapping_unresolved");return w.data;
}
inline DeferredConversation decodeDeferredConversation(const Blob& bytes){
    require(bytes.size()<=1024);protocol::Reader r(bytes);historyHeader(r,10);DeferredConversation d;d.request=historyRequestFields(r);d.request.allowDeferred=true;
    d.records=r.number();require(r.string()=="device_mapping_unresolved");r.end();require(encodeDeferredConversation(d)==bytes);return d;
}
inline Blob encodeHistoryPage(const HistoryPage& page){
    const auto& q=page.request;validHistoryRequest(q);
    require(!q.store.empty()&&q.through>=q.after&&page.next>=q.after&&page.next<=q.through&&
        page.records.size()<=HistoryPageRecords&&(!page.complete||page.next==q.through)&&
        (page.complete||!page.records.empty()));
    auto w=historyHeader(3);historyRequestFields(w,q);w.number(page.next);w.number(page.complete?1:0);w.number(page.records.size());
    auto previous=q.after;
    for(const auto& record:page.records){
        require(record.cursor>previous&&record.cursor<=page.next);previous=record.cursor;
        auto bytes=encodeAssertion(record.assertion);w.number(record.cursor);w.number(bytes.size());
        w.data.insert(w.data.end(),bytes.begin(),bytes.end());require(w.data.size()<=protocol::MaxPacket);
    }
    require(page.complete||page.next==previous);return w.data;
}
inline HistoryPage decodeHistoryPage(const Blob& bytes){
    require(bytes.size()<=protocol::MaxPacket);protocol::Reader r(bytes);historyHeader(r,3);
    HistoryPage page;page.request=historyRequestFields(r);page.next=r.number();auto complete=r.number();require(complete<=1);
    page.complete=complete!=0;auto count=r.number();require(count<=HistoryPageRecords);
    for(std::uint64_t i=0;i<count;++i){
        auto cursor=r.number(),length=r.number();require(length<=protocol::MaxPacket&&length<=r.remaining());
        Blob assertion(bytes.begin()+r.cursor,bytes.begin()+r.cursor+static_cast<size_t>(length));r.cursor+=static_cast<size_t>(length);
        page.records.push_back({cursor,decodeAssertion(assertion)});
    }
    r.end();require(encodeHistoryPage(page)==bytes);return page;
}
inline void manifestFields(protocol::Writer& w,const ManifestRequest& q){
    require(protocol::id(q.id,32)&&(q.store.empty()||protocol::id(q.store,32))&&
        q.after<=static_cast<std::uint64_t>(INT64_MAX)&&q.through<=static_cast<std::uint64_t>(INT64_MAX)&&
        (!q.through||q.after<=q.through)&&(!q.store.empty()||(!q.after&&!q.through)));
    w.string(q.id);w.string(q.store);w.number(q.after);w.number(q.through);
}
inline ManifestRequest manifestFields(protocol::Reader& r){
    ManifestRequest q;q.id=r.string();q.store=r.string();q.after=r.number();q.through=r.number();return q;
}
inline Blob encodeManifestRequest(const ManifestRequest& q){auto w=historyHeader(4);manifestFields(w,q);return w.data;}
inline ManifestRequest decodeManifestRequest(const Blob& bytes){
    require(bytes.size()<=512);protocol::Reader r(bytes);historyHeader(r,4);auto q=manifestFields(r);r.end();
    require(encodeManifestRequest(q)==bytes);return q;
}
inline Blob encodeManifestPage(const ManifestPage& page){
    const auto& q=page.request;
    require(!q.store.empty()&&q.through>=q.after&&page.next>=q.after&&page.next<=q.through&&
        page.conversations.size()<=HistoryPageRecords&&(!page.complete||page.next==q.through)&&
        (page.complete||!page.conversations.empty()));
    auto w=historyHeader(5);manifestFields(w,q);w.number(page.next);w.number(page.complete);w.number(page.conversations.size());
    std::uint64_t previous=q.after;std::set<std::string> seen;
    for(const auto& entry:page.conversations){
        require(entry.cursor>previous&&entry.cursor<=page.next&&protocol::id(entry.conversation)&&seen.insert(entry.conversation).second);
        previous=entry.cursor;w.number(entry.cursor);w.string(entry.conversation);
    }
    require(page.complete||page.next==previous);require(w.data.size()<=2048);return w.data;
}
inline ManifestPage decodeManifestPage(const Blob& bytes){
    require(bytes.size()<=2048);protocol::Reader r(bytes);historyHeader(r,5);ManifestPage page;page.request=manifestFields(r);
    page.next=r.number();auto complete=r.number();require(complete<=1);page.complete=complete!=0;
    auto count=r.number();require(count<=HistoryPageRecords);
    for(std::uint64_t n=0;n<count;++n){auto cursor=r.number();page.conversations.push_back({cursor,r.string()});}
    r.end();require(encodeManifestPage(page)==bytes);return page;
}
inline void validateHistoryControl(const Blob& bytes){
    require(bytes.size()<=protocol::MaxPacket);protocol::Reader r(bytes);
    if(r.type()==protocol::ArchiveControl&&r.string()==AttachmentDomain){validateAttachmentControl(bytes);return;}
    protocol::Reader group(bytes);
    if(group.type()==protocol::ArchiveControl&&groupHistoryDomain(group.string())){
        require(group.number()==1);auto kind=group.number();if(kind==1)decodeGroupHistoryRequest(bytes);else{require(kind==2);decodeGroupHistoryPage(bytes);}return;
    }
    protocol::Reader history(bytes);
    require(history.type()==protocol::ArchiveControl&&history.string()=="PeerChat/ArchiveHistory/v1"&&history.number()==1);
    auto kind=history.number();if(kind==2||kind==11){decodeHistoryRequest(bytes);return;}
    if(kind==10){decodeDeferredConversation(bytes);return;}
    if(kind==3){decodeHistoryPage(bytes);return;}
    if(kind==4){decodeManifestRequest(bytes);return;}
    require(kind==5);decodeManifestPage(bytes);
}
inline Blob serveManifestPage(Storage& storage,const Profile& local,Authorization& authorization,
        const session::ReliableAuthorization& scope,const Blob& request){
    require(scope.archive.source==local.peerId&&currentSessionAuthorization(authorization,scope));
    auto q=decodeManifestRequest(request);auto store=storage.archiveStoreId();require(q.store.empty()||q.store==store);q.store=store;
    if(!q.through)q.through=static_cast<std::uint64_t>(storage.archiveHistoryWatermark());
    require(q.through>=q.after);
    auto source=storage.archiveConversationPage(static_cast<std::int64_t>(q.after),static_cast<std::int64_t>(q.through),HistoryPageRecords);
    require(source.cursors.size()==source.conversations.size());ManifestPage page;page.request=q;
    page.next=static_cast<std::uint64_t>(source.nextCursor);page.complete=source.complete;
    for(size_t n=0;n<source.conversations.size();++n)page.conversations.push_back({static_cast<std::uint64_t>(source.cursors[n]),source.conversations[n]});
    require(currentSessionAuthorization(authorization,scope));return encodeManifestPage(page);
}
inline Blob serveHistoryPage(Storage& storage,const Profile& local,Authorization& authorization,
        const session::ReliableAuthorization& scope,const Blob& request,DeviceAccounts directory,std::uint64_t now,const char** stage=nullptr){
    if(stage)*stage="source_history_authorization";
    require(bool(directory)&&scope.archive.source==local.peerId&&currentSessionAuthorization(authorization,scope));
    auto q=decodeHistoryRequest(request);auto store=storage.archiveStoreId();
    require(q.store.empty()||q.store==store);q.store=store;
    if(!q.through)q.through=static_cast<std::uint64_t>(storage.archiveHistoryWatermark());
    require(q.through>=q.after);
    if(stage)*stage="source_history_storage";
    auto candidates=storage.archiveHistoryPage(q.conversation,static_cast<std::int64_t>(q.after),
        static_cast<std::int64_t>(q.through),HistoryPageRecords,128*1024);
    require(candidates.cursors.size()==candidates.messages.size());
    ProvenanceVerifier verifier(authorization,storage,local,scope.archive.account,directory);
    HistoryPage page;page.request=q;page.next=q.after;size_t bytes=1024; // Conservative bounded header allowance.
    for(size_t i=0;i<candidates.messages.size();++i){
        if(stage)*stage="source_record_device_mapping";
        const auto& m=candidates.messages[i];auto sender=directory(m.sender),recipient=directory(m.recipient);
        if(!sender||!recipient){
            if(!q.allowDeferred)require(false);
            // Defer the entire remaining conversation window, including any
            // otherwise valid prefix of this page. No assertion or import is
            // fabricated, and the reader's history cursor remains untouched.
            require(currentSessionAuthorization(authorization,scope));
            return encodeDeferredConversation({q,storage.archiveHistoryCount(q.conversation,q.after,q.through)});
        }
        Assertion assertion;try{assertion=verifier.attest({m,*sender,*recipient},scope.archive.issuer,now);}
        catch(...){if(stage)*stage=verifier.stage();throw;}
        auto size=encodeAssertion(assertion).size()+16;
        if(size>protocol::MaxPacket-bytes){require(!page.records.empty());break;}
        bytes+=size;page.next=static_cast<std::uint64_t>(candidates.cursors[i]);page.records.push_back({page.next,std::move(assertion)});
    }
    page.complete=candidates.complete&&page.records.size()==candidates.messages.size();
    if(page.complete)page.next=q.through;
    if(stage)*stage="source_history_encode";
    require(currentSessionAuthorization(authorization,scope));return encodeHistoryPage(page);
}

// One outstanding page gives backpressure. A protected CAS checkpoint advances
// only AFTER all records have reached DeliveryManager's durable import path.
// Crash between records replays/dedupes that page; disconnect never loses cursor.
class HistoryReader {
    Storage& storage_;const Profile& local_;Authorization& authorization_;delivery::DeliveryManager& deliveries_;
    session::ReliableAuthorization scope_;DeviceAccounts directory_;std::function<void()> committed_;
    HistoryRequest position_;std::string context_;Blob saved_;
    std::uint64_t processed_=0;bool complete_=false,pending_=false,cancelled_=false;
    const char* acceptStage_="not_started";
    Blob state()const{
        auto w=historyHeader(6);historyRequestFields(w,position_);w.number(processed_);w.number(complete_?1:0);return w.data;
    }
    void persist(){
        auto next=state();storage_.saveArchiveSyncState(context_,saved_,next);saved_=std::move(next);
    }
    void current()const{
        require(!cancelled_&&storage_.archiveSyncState(context_)==saved_&&currentSessionAuthorization(authorization_,scope_));
    }
public:
    struct Snapshot {std::uint64_t cursor,through,processed;bool complete,pending,cancelled;};
    HistoryReader(Storage& storage,const Profile& local,Authorization& authorization,delivery::DeliveryManager& deliveries,
            session::ReliableAuthorization scope,const std::string& conversation,DeviceAccounts directory,std::function<void()> committed={})
        :storage_(storage),local_(local),authorization_(authorization),deliveries_(deliveries),scope_(std::move(scope)),directory_(std::move(directory)),committed_(std::move(committed)){
        require(bool(directory_)&&protocol::id(conversation)&&scope_.archive.reader==local.peerId&&currentSessionAuthorization(authorization_,scope_));
        auto w=historyHeader(7);w.number(scope_.archive.account);w.string(local.peerId);w.string(scope_.archive.issuer);
        w.string(scope_.archive.source);w.string(conversation);auto digest=digestDevicePayload(w.data);
        static const char hex[]="0123456789abcdef";for(auto b:digest){context_+=hex[b>>4];context_+=hex[b&15];}
        saved_=storage_.archiveSyncState(context_);
        if(saved_.empty()){
            position_={Storage::newMessageId(),{},conversation,0,0};persist();
        }else{
            protocol::Reader r(saved_);historyHeader(r,6);position_=historyRequestFields(r);processed_=r.number();
            auto complete=r.number();require(complete<=1);complete_=complete!=0;r.end();
            require(position_.conversation==conversation&&(!complete_||position_.after==position_.through)&&state()==saved_);
        }
    }
    Blob nextRequest(bool allowDeferred=false){
        current();require(!pending_);
        if(complete_){position_.through=0;complete_=false;}
        position_.id=Storage::newMessageId();position_.allowDeferred=allowDeferred;persist();pending_=true;return encodeHistoryRequest(position_);
    }
    std::uint64_t defer(const Blob& bytes){
        current();require(pending_&&position_.allowDeferred);auto d=decodeDeferredConversation(bytes);const auto& q=d.request;
        require(q.id==position_.id&&q.conversation==position_.conversation&&q.after==position_.after&&
            (position_.store.empty()||q.store==position_.store)&&(!position_.through||q.through==position_.through));
        auto key=historyHeader(12);key.string(context_);auto hash=digestDevicePayload(key.data);static const char hex[]="0123456789abcdef";std::string id;
        for(auto b:hash){id+=hex[b>>4];id+=hex[b&15];}
        auto previous=storage_.archiveSyncState(id);storage_.saveArchiveSyncState(id,previous,bytes);
        // Only accounting is durable here. No history cursor advances and no
        // rejected body/ownership is stored. A later scan retries this window.
        position_.store=q.store;position_.through=q.through;persist();pending_=false;return d.records;
    }
    size_t acceptPage(const Blob& bytes){
        acceptStage_="history_current_authorization";current();require(pending_);acceptStage_="history_page_decode";auto page=decodeHistoryPage(bytes);const auto& q=page.request;
        acceptStage_="history_request_binding";
        require(q.id==position_.id&&q.conversation==position_.conversation&&q.after==position_.after&&
            (position_.store.empty()||q.store==position_.store)&&(!position_.through||q.through==position_.through));
        ProvenanceVerifier verifier(authorization_,storage_,local_,scope_.archive.account,directory_);
        for(const auto& item:page.records){
            acceptStage_="history_record_scope";
            const auto& a=item.assertion;const auto& m=a.original.message;
            require(a.archiveDevice==scope_.archive.source&&a.issuer==scope_.archive.issuer&&
                (a.original.senderAccount==scope_.archive.account?m.recipient:m.sender)==q.conversation);
            acceptStage_="history_record_provenance";try{verifier.verify(a);}catch(...){acceptStage_=verifier.stage();throw;}
        }
        require(processed_<=std::numeric_limits<std::uint64_t>::max()-page.records.size());
        size_t inserted=0;
        for(const auto& item:page.records){
            acceptStage_="history_durable_import";
            current();if(deliveries_.acceptArchiveHistory(local_,authorization_,scope_,item.assertion,directory_)){++inserted;if(committed_)committed_();}
        }
        acceptStage_="history_cursor_commit";current();position_=q;position_.after=page.next;complete_=page.complete;processed_+=page.records.size();persist();pending_=false;acceptStage_="history_page_accepted";
        return inserted;
    }
    void disconnected(){pending_=false;}
    const char* acceptStage()const{return acceptStage_;}
    void cancel(){cancelled_=true;pending_=false;}
    Snapshot snapshot()const{return {position_.after,position_.through,processed_,complete_,pending_,cancelled_};}
};
}
