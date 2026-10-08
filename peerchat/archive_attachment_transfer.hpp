#pragma once
#include "archive_attachment_sync.hpp"

namespace peerchat::archive {
// Purpose/admission adapter around the ONE profile TransferManager. It owns no
// bytes, chunking, partials, hashing, sockets, route retry or worker thread.
class AttachmentTransfers {
public:
    using OpenSource=std::function<std::unique_ptr<transfer::Source>(const Attachment&)>;
    using CreateSink=std::function<std::unique_ptr<transfer::Sink>(const transfer::Metadata&,const Blob&)>;
    using CompletedLocation=std::function<Blob(const transfer::DurableState&)>;
    using Lease=std::function<void(const session::ReliableAuthorization&,std::uint64_t)>;
private:
    struct Entry {session::ReliableAuthorization scope;Attachment attachment;std::string transferId;Blob destination;std::uint64_t lastRoute=0;bool unavailable=false;};
    Storage& storage_;const Profile& local_;const Authorization& policy_;transfer::TransferManager& transfers_;
    OpenSource open_;CreateSink sink_;CompletedLocation completed_;Lease lease_;
    std::map<std::string,Entry> entries_;std::string root_;Blob saved_;
    std::string context(const std::string& label)const{
        auto w=attachmentHeader(10);w.number(policy_.account());w.string(local_.peerId);w.string(label);
        static const char hex[]="0123456789abcdef";std::string out;for(auto b:digestDevicePayload(w.data)){out+=hex[b>>4];out+=hex[b&15];}return out;
    }
    Blob encode(const Entry& e)const{
        auto w=attachmentHeader(11);w.bytes(sessionBinding(e.scope).data);attachmentFields(w,e.attachment);w.string(e.transferId);w.bytes(e.destination);w.number(e.unavailable);require(w.data.size()<=4096);return w.data;
    }
    Entry decode(const Blob& bytes)const{
        protocol::Reader r(bytes);attachmentHeader(r,11);Entry e;e.scope=decodeSessionBinding(r.bytes(),local_.peerId);e.attachment=attachmentFields(r);e.transferId=r.string();e.destination=r.bytes();auto unavailable=r.number();require(unavailable<=1);e.unavailable=unavailable!=0;r.end();
        require(e.scope.archive.account==policy_.account()&&protocol::id(e.transferId,32)&&e.destination.size()<=2048&&encode(e)==bytes);return e;
    }
    void saveRoot(){auto w=attachmentHeader(12);w.number(entries_.size());for(const auto& e:entries_)w.string(e.first);storage_.saveArchiveSyncState(root_,saved_,w.data);saved_=std::move(w.data);}
    void retain(Entry e){
        require(entries_.size()<transfer::MaxTransfers&&currentSessionAuthorization(policy_,e.scope));
        for(const auto& item:entries_)if(item.second.scope.expectedRemoteDeviceId==e.scope.expectedRemoteDeviceId)
            require(sessionBinding(item.second.scope).data==sessionBinding(e.scope).data);
        auto id=e.transferId;auto key=context(id);auto previous=storage_.archiveSyncState(key);auto bytes=encode(e);require(previous.empty()||previous==bytes);
        if(previous.empty())storage_.saveArchiveSyncState(key,{},bytes);
        require(entries_.emplace(id,std::move(e)).second);try{saveRoot();}catch(...){entries_.erase(id);throw;}
    }
    std::optional<transfer::Snapshot> snapshot(const std::string& id)const{for(const auto& s:transfers_.snapshots())if(s.metadata.id==id)return s;return {};}
    void unavailable(Entry& e){auto previous=storage_.archiveSyncState(context(e.transferId));e.unavailable=true;storage_.saveArchiveSyncState(context(e.transferId),previous,encode(e));}
    transfer::Metadata expected(const Entry& e)const{return {e.transferId,e.attachment.name,e.attachment.size,e.attachment.digest};}
    void ensureSource(Entry& e,std::uint64_t now){
        if(snapshot(e.transferId))return;
        AttachmentCatalog catalog(storage_);auto local=catalog.find(e.attachment.id);require(local&&attachmentIdentity(*local)==attachmentIdentity(e.attachment)&&!local->location.empty());
        auto source=open_(*local);require(source&&source->name()==local->name&&source->size()==local->size);
        transfers_.offer(e.scope.expectedRemoteDeviceId,e.transferId,std::move(source),now,&local->digest);
    }
public:
    AttachmentTransfers(Storage& storage,const Profile& local,const Authorization& policy,transfer::TransferManager& transfers,
        OpenSource source,CreateSink sink,CompletedLocation completed,Lease lease)
        :storage_(storage),local_(local),policy_(policy),transfers_(transfers),open_(std::move(source)),sink_(std::move(sink)),completed_(std::move(completed)),lease_(std::move(lease)){
        require(bool(open_)&&bool(sink_)&&bool(completed_)&&bool(lease_));root_=context("active");saved_=storage_.archiveSyncState(root_);
        if(!saved_.empty()){protocol::Reader r(saved_);attachmentHeader(r,12);auto count=r.number();require(count<=transfer::MaxTransfers);
            for(std::uint64_t n=0;n<count;++n){auto id=r.string();require(protocol::id(id,32));auto e=decode(storage_.archiveSyncState(context(id)));require(e.transferId==id&&entries_.emplace(id,std::move(e)).second);}r.end();}
    }
    std::string download(const session::ReliableAuthorization& scope,const std::string& attachmentId,const Blob& localDestination){
        require(scope.archive.reader==local_.peerId&&currentSessionAuthorization(policy_,scope)&&!localDestination.empty()&&localDestination.size()<=2048);
        AttachmentCatalog catalog(storage_);auto a=catalog.find(attachmentId);require(a&&!a->proof.empty());
        for(const auto& item:entries_)if(item.second.scope.archive.reader==local_.peerId&&item.second.attachment.id==attachmentId&&sessionBinding(item.second.scope).data==sessionBinding(scope).data){require(!item.second.unavailable);return item.first;}
        if(!a->location.empty()){bool available=false;try{auto file=open_(*a);available=file&&file->size()==a->size&&file->name()==a->name;}catch(const std::exception&){}require(!available);}
        Entry e{scope,*a,Storage::newMessageId(),localDestination,0};e.attachment.location.clear();e.attachment.proof.clear();auto id=e.transferId;retain(std::move(e));return id;
    }
    bool owns(const std::string& transferId)const{return entries_.count(transferId)!=0;}
    std::vector<transfer::Snapshot> pending()const{
        std::vector<transfer::Snapshot> result;for(const auto& item:entries_)if(!snapshot(item.first)){
            const auto& e=item.second;transfer::Snapshot s;s.metadata=expected(e);s.peer=e.scope.expectedRemoteDeviceId;s.sending=e.scope.archive.source==local_.peerId;s.state=e.unavailable?transfer::State::Failed:transfer::State::Offered;
            if(e.unavailable)s.error="Archive file unavailable or changed; cancel and retry after checking the source";
            result.push_back(std::move(s));
        }
        return result;
    }
    // Persisted purpose survives completion; a completed archive transfer must
    // never be re-catalogued as a new original live attachment.
    bool wasArchiveTransfer(const std::string& transferId)const{return !storage_.archiveSyncState(context(transferId)).empty();}
    void cancel(const std::string& transferId){
        auto found=entries_.find(transferId);require(found!=entries_.end());
        transfers_.cancel(transferId);
        if(!snapshot(transferId)){entries_.erase(found);saveRoot();}
    }
    std::optional<session::ReliableAuthorization> scopeForPeer(const std::string& peer)const{
        for(const auto& item:entries_)if(item.second.scope.expectedRemoteDeviceId==peer&&currentSessionAuthorization(policy_,item.second.scope))return item.second.scope;
        return {};
    }
    void maintain(std::uint64_t now){
        for(auto it=entries_.begin();it!=entries_.end();){auto& e=it->second;
            auto s=snapshot(e.transferId);
            if(s&&(s->state==transfer::State::Failed||s->state==transfer::State::Cancelled)&&!transfers_.hasPendingControl(e.transferId)){it=entries_.erase(it);saveRoot();continue;}
            if(!currentSessionAuthorization(policy_,e.scope)){++it;continue;} // Keep partials; never send through revoked scope.
            if(s&&s->state==transfer::State::Complete&&!transfers_.hasPendingControl(e.transferId)){
                if(!s->sending){for(const auto& d:transfers_.completedRecords())if(d.metadata.id==e.transferId){auto a=e.attachment;a.location=completed_(d);require(!a.location.empty());AttachmentCatalog(storage_).verifiedCopy(a);}}
                it=entries_.erase(it);saveRoot();continue;
            }
            if(e.unavailable&&e.scope.archive.reader==local_.peerId){++it;continue;}
            if(e.unavailable&&e.lastRoute){++it;continue;}
            lease_(e.scope,now);if(e.scope.archive.source==local_.peerId&&!e.unavailable)try{ensureSource(e,now);}catch(const transfer::PersistenceError&){throw;}catch(const std::exception&){unavailable(e);}++it;
        }
    }
    Blob request(const session::ReliableAuthorization& scope,std::uint64_t route){
        require(currentSessionAuthorization(policy_,scope));
        for(auto& item:entries_){auto& e=item.second;
            if(e.unavailable&&e.scope.archive.source==local_.peerId&&sessionBinding(e.scope).data==sessionBinding(scope).data&&e.lastRoute!=route){e.lastRoute=route;return attachmentUnavailable(e.transferId);}
            if(e.unavailable||e.scope.archive.reader!=local_.peerId||sessionBinding(e.scope).data!=sessionBinding(scope).data||snapshot(e.transferId)||e.lastRoute==route)continue;
            require(route);e.lastRoute=route;return encodeAttachmentFetch({e.transferId,e.attachment.id,expected(e)});
        }return {};
    }
    Blob outgoing(const session::ReliableAuthorization& scope,const Blob& frame)const{
        require(currentSessionAuthorization(policy_,scope));auto id=transferPacketId(frame);auto found=entries_.find(id);
        require(found!=entries_.end()&&sessionBinding(found->second.scope).data==sessionBinding(scope).data);return wrapAttachmentTransfer(found->second.attachment.id,frame);
    }
    void control(const session::ReliableAuthorization& scope,const Blob& bytes,std::uint64_t now){
        require(currentSessionAuthorization(policy_,scope));protocol::Reader header(bytes);require(header.type()==protocol::ArchiveControl&&header.string()==AttachmentDomain&&header.number()==1);auto kind=header.number();
        if(kind==3){
            require(scope.archive.source==local_.peerId);auto fetch=decodeAttachmentFetch(bytes);AttachmentCatalog catalog(storage_);auto a=catalog.find(fetch.attachmentId);
            require(a&&a->name==fetch.expected.name&&a->size==fetch.expected.size&&a->digest==fetch.expected.digest);
            auto found=entries_.find(fetch.transferId);
            if(found==entries_.end()){Entry e{scope,*a,fetch.transferId,{},0};e.attachment.location.clear();e.attachment.proof.clear();retain(std::move(e));}
            else require(sessionBinding(found->second.scope).data==sessionBinding(scope).data&&attachmentIdentity(found->second.attachment)==attachmentIdentity(*a));
            auto& entry=entries_.at(fetch.transferId);try{ensureSource(entry,now);}catch(const transfer::PersistenceError&){throw;}catch(const std::exception&){unavailable(entry);}lease_(scope,now);return;
        }
        if(kind==5){require(scope.archive.reader==local_.peerId);auto id=decodeAttachmentUnavailable(bytes);auto found=entries_.find(id);require(found!=entries_.end()&&sessionBinding(found->second.scope).data==sessionBinding(scope).data);transfers_.cancel(id);unavailable(found->second);return;}
        require(kind==4);auto packet=unwrapAttachmentTransfer(bytes);auto id=transferPacketId(packet.second);auto found=entries_.find(id);
        if(found==entries_.end()){
            // A final ACK can be lost after local completion. Re-admit only the
            // exact protected completed binding so TransferManager can repeat
            // its idempotent receipt; never accept an unsolicited new file.
            auto previous=storage_.archiveSyncState(context(id));auto status=snapshot(id);require(!previous.empty()&&status&&status->state==transfer::State::Complete);
            auto e=decode(previous);require(e.attachment.id==packet.first&&sessionBinding(e.scope).data==sessionBinding(scope).data);retain(std::move(e));found=entries_.find(id);
        }
        require(found!=entries_.end()&&found->second.attachment.id==packet.first&&sessionBinding(found->second.scope).data==sessionBinding(scope).data);
        auto& e=found->second;
        if(scope.archive.reader==local_.peerId){protocol::Reader r(packet.second);r.number();auto transferKind=r.number();r.string();
            if(transferKind==static_cast<unsigned>(transfer::Kind::Cancel)&&!snapshot(id)){r.end();unavailable(e);return;}
            if(transferKind==static_cast<unsigned>(transfer::Kind::Offer)){transfer::Metadata m;m.id=id;m.name=r.string();m.size=r.number();auto digest=r.bytes();r.end();require(digest==Blob(e.attachment.digest.begin(),e.attachment.digest.end())&&m.name==e.attachment.name&&m.size==e.attachment.size);}}
        transfers_.receive(scope.expectedRemoteDeviceId,packet.second,now);
        auto status=snapshot(id);if(status&&!status->sending&&status->state==transfer::State::AwaitingAcceptance)transfers_.accept(id,sink_(expected(e),e.destination),now);
        lease_(scope,now);
    }
};
}
