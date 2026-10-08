#pragma once
#include "archive_authorization.hpp"

namespace peerchat::archive {
struct OriginalRecord {
    StoredMessage message; // Delivery/sequence are local state, NOT archive proof.
    std::uint64_t senderAccount=0,recipientAccount=0;
};
inline Blob recordPayload(const OriginalRecord& record){
    auto& m=record.message;
    require(record.senderAccount>0&&record.recipientAccount>0&&record.senderAccount!=record.recipientAccount&&
        protocol::id(m.id,32)&&protocol::id(m.sender)&&protocol::id(m.recipient)&&m.sender!=m.recipient&&
        !m.text.empty()&&unicode::utf16Units(m.text)<=protocol::MaxAcceptedChatCharacters);
    protocol::Writer w(LocalFormat);w.string("PeerChat/ArchivedOriginalRecord/v1");w.number(1);
    w.string(m.id);w.number(record.senderAccount);w.string(m.sender);w.number(record.recipientAccount);w.string(m.recipient);
    w.number(m.utcMilliseconds);w.number(m.logicalOrder);w.text(m.text);
    require(w.data.size()<=70000);return w.data;
}
struct Assertion {
    OriginalRecord original;
    std::string issuer,archiveDevice;
    std::uint64_t account=0,grantGeneration=0,archived=0;
    Blob digest,signature;
};
inline OriginalRecord decodeRecord(const Blob& bytes){
    require(bytes.size()<=protocol::MaxPacket);protocol::Reader r(bytes);
    require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchivedOriginalRecord/v1"&&r.number()==1);
    OriginalRecord record;auto& m=record.message;
    m.id=r.string();record.senderAccount=r.number();m.sender=r.string();record.recipientAccount=r.number();m.recipient=r.string();
    m.utcMilliseconds=r.number();m.logicalOrder=r.number();m.text=r.text();r.end();
    // No delivery state, local DB cursor, or sender-evidence label is accepted
    // from an archive. Import ownership decides those separately.
    require(recordPayload(record)==bytes);return record;
}
inline Blob assertionPayload(const Assertion& a){
    require(a.account>0&&a.grantGeneration>0&&a.archived>0&&protocol::id(a.issuer)&&protocol::id(a.archiveDevice)&&a.digest.size()==32&&
        (a.original.senderAccount==a.account||a.original.recipientAccount==a.account));
    protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveStoredRecordAttestation/v1");w.number(1);
    w.number(a.account);w.string(a.issuer);w.string(a.archiveDevice);w.number(a.grantGeneration);w.number(a.archived);w.bytes(a.digest);
    return w.data;
}
inline Blob encodeAssertion(const Assertion& a){
    require(a.signature.size()==64&&a.digest==digestDevicePayload(recordPayload(a.original)));
    protocol::Writer w(LocalFormat);w.string("PeerChat/ArchiveRecordEnvelope/v1");w.number(1);
    // Inline the record to preserve the existing 65,536-byte text ceiling:
    // Writer::bytes has its own 65,536 bound, smaller than a maximum record.
    auto record=recordPayload(a.original);w.number(record.size());w.data.insert(w.data.end(),record.begin(),record.end());
    w.bytes(assertionPayload(a));w.bytes(a.signature);require(w.data.size()<=protocol::MaxPacket);return w.data;
}
inline Assertion decodeAssertion(const Blob& bytes){
    require(bytes.size()<=protocol::MaxPacket);protocol::Reader r(bytes);
    require(r.type()==LocalFormat&&r.string()=="PeerChat/ArchiveRecordEnvelope/v1"&&r.number()==1);
    auto length=r.number();require(length<=protocol::MaxPacket&&length<=r.remaining());
    Blob record(bytes.begin()+r.cursor,bytes.begin()+r.cursor+static_cast<size_t>(length));r.cursor+=static_cast<size_t>(length);
    Assertion a;a.original=decodeRecord(record);auto payload=r.bytes();a.signature=r.bytes();r.end();
    protocol::Reader p(payload);require(p.type()==LocalFormat&&p.string()=="PeerChat/ArchiveStoredRecordAttestation/v1"&&p.number()==1);
    a.account=p.number();a.issuer=p.string();a.archiveDevice=p.string();a.grantGeneration=p.number();a.archived=p.number();a.digest=p.bytes();p.end();
    require(assertionPayload(a)==payload&&encodeAssertion(a)==bytes);return a;
}
enum class Provenance { ArchiveAttested,LocallyOriginated,OriginalSigned };

class ProvenanceVerifier {
    Authorization& authorization_;Storage& storage_;const Profile& local_;std::uint64_t account_;
    std::function<std::optional<std::uint64_t>(const std::string&)> deviceAccount_;
    void validateContext(const OriginalRecord& record)const{
        const auto sender=deviceAccount_(record.message.sender),recipient=deviceAccount_(record.message.recipient);
        require(sender&&recipient&&*sender==record.senderAccount&&*recipient==record.recipientAccount&&
            (*sender==account_||*recipient==account_));
    }
public:
    // Accepted account/device identity directory, including retained historical
    // bindings. Never resolve account context from message-supplied metadata.
    ProvenanceVerifier(Authorization& authorization,Storage& storage,const Profile& local,std::uint64_t account,
        std::function<std::optional<std::uint64_t>(const std::string&)> deviceAccount)
        :authorization_(authorization),storage_(storage),local_(local),account_(account),deviceAccount_(std::move(deviceAccount)){
        require(account>0&&bool(deviceAccount_));
    }
    Assertion attest(const OriginalRecord& record,const std::string& issuer,std::uint64_t now)const{
        validateContext(record);
        auto stored=storage_.message(record.message.id);
        require(stored&&recordPayload(OriginalRecord{*stored,record.senderAccount,record.recipientAccount})==recordPayload(record));
        require(authorization_.permits(issuer,local_.peerId,ServeHistory));Assertion a;a.original=record;a.account=account_;a.issuer=issuer;
        a.archiveDevice=local_.peerId;a.grantGeneration=authorization_.grant(issuer,local_.peerId).generation;a.archived=now;
        a.digest=digestDevicePayload(recordPayload(record));a.signature=signDevicePayload(assertionPayload(a),local_);return a;
    }
    Provenance verify(const Assertion& a)const{
        validateContext(a.original);
        require(a.account==account_&&authorization_.permits(a.issuer,local_.peerId,ReadHistory)&&authorization_.permits(a.issuer,a.archiveDevice,ServeHistory));
        auto g=authorization_.grant(a.issuer,a.archiveDevice);require(g.generation==a.grantGeneration&&a.digest==digestDevicePayload(recordPayload(a.original))&&
            verifyDevicePayload(assertionPayload(a),a.signature,g.publicKey));
        // No original-sender signature exists for legacy DMs. A source-controlled
        // label can NEVER upgrade this result to OriginalSigned.
        auto existing=storage_.message(a.original.message.id);
        if(a.original.message.sender==local_.peerId&&existing&&recordPayload(OriginalRecord{*existing,a.original.senderAccount,a.original.recipientAccount})==recordPayload(a.original))
            return Provenance::LocallyOriginated;
        return Provenance::ArchiveAttested;
    }
};
}
