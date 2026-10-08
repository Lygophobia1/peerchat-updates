#pragma once
#include "archive_attachment_catalog.hpp"
#include <set>

namespace peerchat::archive {
struct AttachmentFetch {std::string transferId,attachmentId;transfer::Metadata expected;};
inline Blob encodeAttachmentFetch(const AttachmentFetch& f){
    require(protocol::id(f.transferId,32)&&protocol::id(f.attachmentId,32)&&f.expected.id==f.transferId&&transfer::safeName(f.expected.name)&&f.expected.size<=transfer::MaxFileBytes);
    auto w=attachmentHeader(3);w.string(f.transferId);w.string(f.attachmentId);w.string(f.expected.name);w.number(f.expected.size);w.bytes(Blob(f.expected.digest.begin(),f.expected.digest.end()));return w.data;
}
inline AttachmentFetch decodeAttachmentFetch(const Blob& bytes){
    require(bytes.size()<=1024);protocol::Reader r(bytes);attachmentHeader(r,3);AttachmentFetch f;f.transferId=r.string();f.attachmentId=r.string();f.expected.id=f.transferId;f.expected.name=r.string();f.expected.size=r.number();auto hash=r.bytes();require(hash.size()==32);std::copy(hash.begin(),hash.end(),f.expected.digest.begin());r.end();require(encodeAttachmentFetch(f)==bytes);return f;
}
inline std::string transferPacketId(const Blob& bytes){
    require(bytes.size()<=transfer::ChunkBytes+512);protocol::Reader r(bytes);require(r.type()==protocol::FileTransfer&&r.number()==1);
    auto kind=r.number();require(kind>=1&&kind<=6);auto id=r.string();require(protocol::id(id,32));return id;
}
inline Blob wrapAttachmentTransfer(const std::string& attachment,const Blob& frame){
    require(protocol::id(attachment,32));transferPacketId(frame);auto w=attachmentHeader(4);w.string(attachment);w.bytes(frame);return w.data;
}
inline std::pair<std::string,Blob> unwrapAttachmentTransfer(const Blob& bytes){
    require(bytes.size()<=transfer::ChunkBytes+1024);protocol::Reader r(bytes);attachmentHeader(r,4);auto id=r.string();auto frame=r.bytes();r.end();require(protocol::id(id,32));transferPacketId(frame);return {id,std::move(frame)};
}
inline Blob attachmentUnavailable(const std::string& transferId){require(protocol::id(transferId,32));auto w=attachmentHeader(5);w.string(transferId);w.number(1);return w.data;}
inline std::string decodeAttachmentUnavailable(const Blob& bytes){require(bytes.size()<=256);protocol::Reader r(bytes);attachmentHeader(r,5);auto id=r.string();require(r.number()==1&&protocol::id(id,32));r.end();return id;}
struct AttachmentRequest {std::string id,store;std::uint64_t after=0,through=0;};
inline void attachmentRequestFields(protocol::Writer& w,const AttachmentRequest& q){
    require(protocol::id(q.id,32)&&(q.store.empty()||protocol::id(q.store,32))&&q.after<=INT64_MAX&&q.through<=INT64_MAX&&
        (!q.through||q.after<=q.through)&&(!q.store.empty()||(!q.after&&!q.through)));
    w.string(q.id);w.string(q.store);w.number(q.after);w.number(q.through);
}
inline AttachmentRequest attachmentRequestFields(protocol::Reader& r){AttachmentRequest q{r.string(),r.string(),r.number(),r.number()};
    auto w=attachmentHeader(1);attachmentRequestFields(w,q);return q;}
inline Blob encodeAttachmentRequest(const AttachmentRequest& q){auto w=attachmentHeader(1);attachmentRequestFields(w,q);return w.data;}
inline AttachmentRequest decodeAttachmentRequest(const Blob& bytes){require(bytes.size()<=512);protocol::Reader r(bytes);attachmentHeader(r,1);auto q=attachmentRequestFields(r);r.end();return q;}
struct AttachmentPage {AttachmentRequest request;std::uint64_t next=0;bool complete=false;std::vector<std::pair<std::uint64_t,Blob>> records;};
inline Blob encodeAttachmentPage(const AttachmentPage& p){
    auto w=attachmentHeader(2);attachmentRequestFields(w,p.request);require(p.records.size()<=8&&p.next>=p.request.after&&p.next<=p.request.through&&(!p.complete||p.next==p.request.through));
    w.number(p.next);w.number(p.complete);w.number(p.records.size());auto previous=p.request.after;
    for(const auto& record:p.records){require(record.first>previous&&record.first<=p.next&&!record.second.empty()&&record.second.size()<=4096);previous=record.first;w.number(record.first);w.bytes(record.second);}
    require(p.complete||(!p.records.empty()&&p.next==previous));return w.data;
}
inline AttachmentPage decodeAttachmentPage(const Blob& bytes){
    require(bytes.size()<=40*1024);protocol::Reader r(bytes);attachmentHeader(r,2);AttachmentPage p;p.request=attachmentRequestFields(r);p.next=r.number();
    auto complete=r.number(),count=r.number();require(complete<=1&&count<=8);p.complete=complete!=0;
    for(std::uint64_t n=0;n<count;++n){auto cursor=r.number();auto proof=r.bytes();p.records.emplace_back(cursor,std::move(proof));}
    r.end();require(encodeAttachmentPage(p)==bytes);return p;
}
inline void validateAttachmentControl(const Blob& bytes){
    protocol::Reader r(bytes);require(r.type()==protocol::ArchiveControl&&r.string()==AttachmentDomain&&r.number()==1);
    auto kind=r.number();if(kind==1){decodeAttachmentRequest(bytes);return;}if(kind==2){decodeAttachmentPage(bytes);return;}
    if(kind==3){decodeAttachmentFetch(bytes);return;}if(kind==4){unwrapAttachmentTransfer(bytes);return;}require(kind==5);decodeAttachmentUnavailable(bytes);
}
inline Blob serveAttachmentPage(Storage& storage,const Profile& local,const Authorization& policy,const session::ReliableAuthorization& scope,const Blob& bytes,const AttachmentAccounts& directory){
    require(scope.archive.source==local.peerId&&currentSessionAuthorization(policy,scope));auto q=decodeAttachmentRequest(bytes);
    auto store=storage.archiveStoreId();require(q.store.empty()||q.store==store);q.store=store;
    if(!q.through)q.through=static_cast<std::uint64_t>(storage.attachmentMetadataWatermark());
    require(q.through>=q.after);
    auto records=storage.attachmentMetadataPage(static_cast<std::int64_t>(q.after),static_cast<std::int64_t>(q.through));
    AttachmentPage page;page.request=q;page.next=records.next;page.complete=records.complete;
    for(const auto& record:records.records){auto a=decodeLocalAttachment(record.record);require(a.id==record.id);page.records.emplace_back(record.sequence,attestAttachment(local,policy,scope,a,directory));}
    require(currentSessionAuthorization(policy,scope));return encodeAttachmentPage(page);
}
class AttachmentReader {
    Storage& storage_;const Authorization& policy_;session::ReliableAuthorization scope_;AttachmentAccounts directory_;
    std::string context_;Blob saved_;AttachmentRequest position_;std::uint64_t processed_=0;bool complete_=false,pending_=false,cancelled_=false;
    Blob state()const{auto w=attachmentHeader(8);attachmentRequestFields(w,position_);w.number(processed_);w.number(complete_);return w.data;}
    void save(){auto next=state();storage_.saveArchiveSyncState(context_,saved_,next);saved_=std::move(next);}
    void current()const{require(!cancelled_&&storage_.archiveSyncState(context_)==saved_&&currentSessionAuthorization(policy_,scope_));}
public:
    struct Snapshot {std::uint64_t processed;bool complete,pending;};
    AttachmentReader(Storage& storage,const Authorization& policy,session::ReliableAuthorization scope,AttachmentAccounts directory)
        :storage_(storage),policy_(policy),scope_(std::move(scope)),directory_(std::move(directory)){
        require(scope_.archive.reader==policy_.localDevice()&&currentSessionAuthorization(policy_,scope_)&&bool(directory_));
        auto key=attachmentHeader(9);key.number(scope_.archive.account);key.string(scope_.archive.issuer);key.string(scope_.archive.source);key.string(scope_.archive.reader);
        static const char hex[]="0123456789abcdef";for(auto b:digestDevicePayload(key.data)){context_+=hex[b>>4];context_+=hex[b&15];}
        saved_=storage_.archiveSyncState(context_);
        if(saved_.empty()){position_.id=Storage::newMessageId();save();}
        else{protocol::Reader r(saved_);attachmentHeader(r,8);position_=attachmentRequestFields(r);processed_=r.number();auto complete=r.number();require(complete<=1);complete_=complete!=0;r.end();require(state()==saved_&&(!complete_||position_.after==position_.through));}
    }
    Blob nextRequest(){current();require(!pending_);if(complete_)return {};position_.id=Storage::newMessageId();save();pending_=true;return encodeAttachmentRequest(position_);}
    void accept(const Blob& bytes){
        current();require(pending_);auto page=decodeAttachmentPage(bytes);auto q=page.request;
        require(q.id==position_.id&&q.after==position_.after&&(position_.store.empty()||q.store==position_.store)&&(!position_.through||q.through==position_.through));
        std::vector<Attachment> verified;std::set<std::string> ids;
        for(const auto& record:page.records){auto a=verifyAttachment(record.second,policy_,scope_,directory_);require(ids.insert(a.id).second);verified.push_back(std::move(a));}
        // Authenticate the entire page before mutating even one catalog entry.
        current();AttachmentCatalog catalog(storage_);for(const auto& a:verified)catalog.retain(a);
        require(processed_<=UINT64_MAX-verified.size());processed_+=verified.size();position_=q;position_.after=page.next;complete_=page.complete;pending_=false;save();
    }
    void rescan(){current();require(complete_&&!pending_);position_.through=0;complete_=false;save();}
    void disconnected(){pending_=false;}
    void cancel(){cancelled_=true;pending_=false;}
    Snapshot snapshot()const{return {processed_,complete_,pending_};}
};
}
