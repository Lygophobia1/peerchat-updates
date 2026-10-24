#pragma once
#include "archive_session.hpp"
#include "transfer_manager.hpp"

namespace peerchat::archive {
// Catalog/provenance only. No byte-transfer engine, socket or reconnect owner.
constexpr const char* AttachmentDomain="PeerChat/ArchiveAttachments/v1";
struct Attachment {
    std::string id,conversation,sender,recipient,name;
    std::uint64_t senderAccount=0,recipientAccount=0,size=0,utc=0;
    transfer::Digest digest{};
    Blob location,proof; // Protected LOCAL fields; never part of routing/catalog metadata.
};
inline void validateAttachment(const Attachment& a){
    require(protocol::id(a.id,32)&&protocol::id(a.conversation)&&protocol::id(a.sender)&&protocol::id(a.recipient)&&
        (a.conversation==a.sender||a.conversation==a.recipient)&&a.senderAccount&&a.recipientAccount&&
        transfer::safeName(a.name)&&a.size<=transfer::MaxFileBytes&&a.utc&&a.location.size()<=8192&&a.proof.size()<=4096);
}
inline void attachmentFields(protocol::Writer& w,const Attachment& a){
    validateAttachment(a);w.string(a.id);w.string(a.conversation);w.string(a.sender);w.string(a.recipient);w.string(a.name);
    w.number(a.senderAccount);w.number(a.recipientAccount);w.number(a.size);w.number(a.utc);w.bytes(Blob(a.digest.begin(),a.digest.end()));
}
inline Attachment attachmentFields(protocol::Reader& r){
    Attachment a;a.id=r.string();a.conversation=r.string();a.sender=r.string();a.recipient=r.string();a.name=r.string();
    a.senderAccount=r.number();a.recipientAccount=r.number();a.size=r.number();a.utc=r.number();auto hash=r.bytes();require(hash.size()==32);
    std::copy(hash.begin(),hash.end(),a.digest.begin());validateAttachment(a);return a;
}
inline Blob attachmentIdentity(const Attachment& a){protocol::Writer w(LocalFormat);w.string("PeerChat/AttachmentMetadata/v1");attachmentFields(w,a);return w.data;}
inline Blob encodeLocalAttachment(const Attachment& a){
    protocol::Writer w(LocalFormat);w.string("PeerChat/LocalAttachment/v1");attachmentFields(w,a);w.bytes(a.location);w.bytes(a.proof);return w.data;
}
inline Attachment decodeLocalAttachment(const Blob& bytes){
    require(bytes.size()<=16384);protocol::Reader r(bytes);require(r.type()==LocalFormat&&r.string()=="PeerChat/LocalAttachment/v1");
    auto a=attachmentFields(r);a.location=r.bytes();a.proof=r.bytes();r.end();validateAttachment(a);require(encodeLocalAttachment(a)==bytes);return a;
}
using AttachmentAccounts=std::function<std::optional<std::uint64_t>(const std::string&)>;
inline void attachmentAccounts(const Attachment& a,const AttachmentAccounts& directory,std::uint64_t account){
    require(bool(directory));auto sender=directory(a.sender),recipient=directory(a.recipient);
    require(sender&&recipient&&*sender==a.senderAccount&&*recipient==a.recipientAccount&&
        (a.senderAccount==account||a.recipientAccount==account));
    // The conversation is the other account's authenticated device, never a
    // remote display name or an arbitrary requested storage key.
    require((a.senderAccount==account&&a.conversation==a.recipient)||(a.recipientAccount==account&&a.conversation==a.sender));
}
class AttachmentCatalog {
    Storage& storage_;
public:
    explicit AttachmentCatalog(Storage& storage):storage_(storage){}
    std::optional<Attachment> find(const std::string& id){auto bytes=storage_.attachmentMetadata(id);if(bytes.empty())return {};auto a=decodeLocalAttachment(bytes);require(a.id==id);return a;}
    // Bounded local presentation window, independent of the expiring transfer
    // journal. No network admission, remote paths or new history cursor owner.
    std::vector<Attachment> recent(size_t limit=64){
        require(limit>0&&limit<=64);auto through=storage_.attachmentMetadataWatermark();
        auto after=std::max<std::int64_t>(0,through-static_cast<std::int64_t>(limit));
        std::vector<Attachment> result;
        while(after<through){auto page=storage_.attachmentMetadataPage(after,through);
            for(const auto& record:page.records){auto a=decodeLocalAttachment(record.record);require(a.id==record.id);result.push_back(std::move(a));}
            require(page.next>after);after=page.next;if(page.complete)break;
        }
        require(result.size()<=limit);return result;
    }
    void eraseLocalHistory(const std::string& conversation){
        const auto through=storage_.attachmentMetadataWatermark();std::int64_t after=0;
        while(after<through){auto page=storage_.attachmentMetadataPage(after,through);for(const auto& record:page.records){auto row=decodeLocalAttachment(record.record);require(row.id==record.id);if(row.conversation==conversation)storage_.eraseMessage(conversation,row.id);}require(page.next>after);after=page.next;if(page.complete)break;}
    }
    static transfer::DurableState localCopy(const Attachment& a,const std::string& local,const AttachmentAccounts& directory){
        auto account=directory(local);require(account.has_value());attachmentAccounts(a,directory,*account);
        require(!a.location.empty());transfer::DurableState row;row.metadata={a.id,a.name,a.size,a.digest};row.peer=a.conversation;
        row.state=transfer::State::Complete;row.sending=a.senderAccount==*account;row.done=a.size;row.created=a.utc;
        row.digestReady=true;row.location=a.location;return row;
    }
    bool retain(const Attachment& incoming){
        validateAttachment(incoming);auto previous=storage_.attachmentMetadata(incoming.id);auto next=incoming;
        if(!previous.empty()){
            auto existing=decodeLocalAttachment(previous);require(attachmentIdentity(existing)==attachmentIdentity(incoming));
            // Never replace a locally possessed file with a remote path or a
            // missing-file projection. First durable provenance is retained.
            if(!existing.location.empty())next.location=existing.location;
            if(!existing.proof.empty())next.proof=existing.proof;
        }
        auto bytes=encodeLocalAttachment(next);if(bytes==previous)return false;
        storage_.saveAttachmentMetadata(incoming.id,previous,bytes);return previous.empty();
    }
    void verifiedCopy(const Attachment& incoming){
        validateAttachment(incoming);require(!incoming.location.empty());auto previous=storage_.attachmentMetadata(incoming.id);require(!previous.empty());
        auto existing=decodeLocalAttachment(previous);require(attachmentIdentity(existing)==attachmentIdentity(incoming));
        existing.location=incoming.location;auto next=encodeLocalAttachment(existing);if(next!=previous)storage_.saveAttachmentMetadata(incoming.id,previous,next);
    }
    void completed(const transfer::DurableState& d,const std::string& local,const AttachmentAccounts& directory){
        require(d.state==transfer::State::Complete&&d.digestReady);
        Attachment a;a.id=d.metadata.id;a.conversation=d.peer;a.sender=d.sending?local:d.peer;a.recipient=d.sending?d.peer:local;
        a.name=d.metadata.name;a.size=d.metadata.size;a.digest=d.metadata.digest;a.utc=d.created;a.location=d.location;
        auto sender=directory(a.sender),recipient=directory(a.recipient);require(sender&&recipient);a.senderAccount=*sender;a.recipientAccount=*recipient;retain(a);
    }
};
inline protocol::Writer attachmentHeader(std::uint64_t kind){protocol::Writer w(protocol::ArchiveControl);w.string(AttachmentDomain);w.number(1);w.number(kind);return w;}
inline void attachmentHeader(protocol::Reader& r,std::uint64_t kind){require(r.type()==protocol::ArchiveControl&&r.string()==AttachmentDomain&&r.number()==1&&r.number()==kind);}
inline Blob attachmentAssertionPayload(const session::ReliableAuthorization& scope,const Attachment& a){
    auto w=attachmentHeader(6);w.bytes(sessionBinding(scope).data);attachmentFields(w,a);return w.data;
}
inline Blob attestAttachment(const Profile& local,const Authorization& policy,const session::ReliableAuthorization& scope,const Attachment& a,const AttachmentAccounts& directory){
    require(local.peerId==scope.archive.source&&currentSessionAuthorization(policy,scope));attachmentAccounts(a,directory,scope.archive.account);
    auto payload=attachmentAssertionPayload(scope,a);auto w=attachmentHeader(7);w.bytes(payload);w.bytes(signDevicePayload(payload,local));return w.data;
}
inline Attachment verifyAttachment(const Blob& proof,const Authorization& policy,const session::ReliableAuthorization& scope,const AttachmentAccounts& directory){
    require(proof.size()<=4096&&currentSessionAuthorization(policy,scope));protocol::Reader outer(proof);attachmentHeader(outer,7);
    auto payload=outer.bytes(),signature=outer.bytes();outer.end();require(signature.size()==64);
    auto source=policy.grant(scope.archive.issuer,scope.archive.source);
    require(verifyDevicePayload(payload,signature,source.publicKey));protocol::Reader r(payload);attachmentHeader(r,6);
    require(r.bytes()==sessionBinding(scope).data);auto a=attachmentFields(r);r.end();
    require(payload==attachmentAssertionPayload(scope,a));attachmentAccounts(a,directory,scope.archive.account);a.proof=proof;return a;
}
}
