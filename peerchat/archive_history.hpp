#pragma once
#include "delivery_manager.hpp"
#include <limits>

namespace peerchat::archive {
// A bounded request/response protocol carried ONLY inside an admitted archive
// peer-TLS session. This component owns no socket, retry timer, or history DB.
constexpr size_t HistoryPageRecords=8;
using DeviceAccounts=std::function<std::optional<std::uint64_t>(const std::string&)>;
struct HistoryRequest {
    std::string id,store,conversation;
    std::uint64_t after=0,through=0;
};
struct HistoryRecord {std::uint64_t cursor=0;Assertion assertion;};
struct HistoryPage {
    HistoryRequest request;
    std::uint64_t next=0;
    bool complete=false;
    std::vector<HistoryRecord> records;
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
    auto w=historyHeader(2);historyRequestFields(w,q);return w.data;
}
inline HistoryRequest decodeHistoryRequest(const Blob& bytes){
    require(bytes.size()<=1024);protocol::Reader r(bytes);historyHeader(r,2);
    auto q=historyRequestFields(r);r.end();require(encodeHistoryRequest(q)==bytes);return q;
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
inline void validateHistoryControl(const Blob& bytes){
    require(bytes.size()<=protocol::MaxPacket);protocol::Reader r(bytes);
    require(r.type()==protocol::ArchiveControl&&r.string()=="PeerChat/ArchiveHistory/v1"&&r.number()==1);
    auto kind=r.number();if(kind==2){decodeHistoryRequest(bytes);return;}
    require(kind==3);decodeHistoryPage(bytes);
}
inline Blob serveHistoryPage(Storage& storage,const Profile& local,Authorization& authorization,
        const session::ReliableAuthorization& scope,const Blob& request,DeviceAccounts directory,std::uint64_t now){
    require(bool(directory)&&scope.archive.source==local.peerId&&currentSessionAuthorization(authorization,scope));
    auto q=decodeHistoryRequest(request);auto store=storage.archiveStoreId();
    require(q.store.empty()||q.store==store);q.store=store;
    if(!q.through)q.through=static_cast<std::uint64_t>(storage.archiveHistoryWatermark());
    require(q.through>=q.after);
    auto candidates=storage.archiveHistoryPage(q.conversation,static_cast<std::int64_t>(q.after),
        static_cast<std::int64_t>(q.through),HistoryPageRecords,128*1024);
    require(candidates.cursors.size()==candidates.messages.size());
    ProvenanceVerifier verifier(authorization,storage,local,scope.archive.account,directory);
    HistoryPage page;page.request=q;page.next=q.after;size_t bytes=1024; // Conservative bounded header allowance.
    for(size_t i=0;i<candidates.messages.size();++i){
        const auto& m=candidates.messages[i];auto sender=directory(m.sender),recipient=directory(m.recipient);
        require(sender&&recipient);
        auto assertion=verifier.attest({m,*sender,*recipient},scope.archive.issuer,now);
        auto size=encodeAssertion(assertion).size()+16;
        if(size>protocol::MaxPacket-bytes){require(!page.records.empty());break;}
        bytes+=size;page.next=static_cast<std::uint64_t>(candidates.cursors[i]);page.records.push_back({page.next,std::move(assertion)});
    }
    page.complete=candidates.complete&&page.records.size()==candidates.messages.size();
    if(page.complete)page.next=q.through;
    require(currentSessionAuthorization(authorization,scope));return encodeHistoryPage(page);
}

// One outstanding page gives backpressure. A protected CAS checkpoint advances
// only AFTER all records have reached DeliveryManager's durable import path.
// Crash between records replays/dedupes that page; disconnect never loses cursor.
class HistoryReader {
    Storage& storage_;const Profile& local_;Authorization& authorization_;delivery::DeliveryManager& deliveries_;
    session::ReliableAuthorization scope_;DeviceAccounts directory_;
    HistoryRequest position_;std::string context_;Blob saved_;
    std::uint64_t processed_=0;bool complete_=false,pending_=false,cancelled_=false;
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
            session::ReliableAuthorization scope,const std::string& conversation,DeviceAccounts directory)
        :storage_(storage),local_(local),authorization_(authorization),deliveries_(deliveries),scope_(std::move(scope)),directory_(std::move(directory)){
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
    Blob nextRequest(){
        current();require(!pending_);
        if(complete_){position_.through=0;complete_=false;}
        position_.id=Storage::newMessageId();persist();pending_=true;return encodeHistoryRequest(position_);
    }
    size_t acceptPage(const Blob& bytes){
        current();require(pending_);auto page=decodeHistoryPage(bytes);const auto& q=page.request;
        require(q.id==position_.id&&q.conversation==position_.conversation&&q.after==position_.after&&
            (position_.store.empty()||q.store==position_.store)&&(!position_.through||q.through==position_.through));
        ProvenanceVerifier verifier(authorization_,storage_,local_,scope_.archive.account,directory_);
        for(const auto& item:page.records){
            const auto& a=item.assertion;const auto& m=a.original.message;
            require(a.archiveDevice==scope_.archive.source&&a.issuer==scope_.archive.issuer&&
                (a.original.senderAccount==scope_.archive.account?m.recipient:m.sender)==q.conversation);
            verifier.verify(a);
        }
        require(processed_<=std::numeric_limits<std::uint64_t>::max()-page.records.size());
        size_t inserted=0;
        for(const auto& item:page.records){
            current();if(deliveries_.acceptArchiveHistory(local_,authorization_,scope_,item.assertion,directory_))++inserted;
        }
        current();position_=q;position_.after=page.next;complete_=page.complete;processed_+=page.records.size();persist();pending_=false;
        return inserted;
    }
    void disconnected(){pending_=false;}
    void cancel(){cancelled_=true;pending_=false;}
    Snapshot snapshot()const{return {position_.after,position_.through,processed_,complete_,pending_,cancelled_};}
};
}
