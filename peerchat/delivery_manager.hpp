#pragma once
#include "session_transport_types.hpp"
#include "storage.hpp"
#include "group.hpp"
#include "mailbox_key_coordinator.hpp"
#include "mailbox_scoped_enrollment.hpp"
#include "portable/utf8.hpp"
#include "archive_session.hpp"
#include "archive_provenance.hpp"
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <chrono>

namespace peerchat::delivery {

enum class MailboxRoutePolicy { Forbidden, LiveReady, FirstAttemptPending, Unavailable };
// Presence is deliberately not an input. Only the session owner's authenticated
// winner and bounded attempt/failure snapshot determine live-route usability.
inline MailboxRoutePolicy mailboxRoutePolicy(bool allowed,const session::SessionSnapshot* route) {
    if(!allowed)return MailboxRoutePolicy::Forbidden;
    if(route&&route->connected)return MailboxRoutePolicy::LiveReady;
    if(route&&!route->latestFailedTransport.value&&!route->retryDelayMilliseconds&&
       !route->retrySuspended&&(route->pending||route->retryScheduled))
        return MailboxRoutePolicy::FirstAttemptPending;
    // A failed first attempt permits upload even while the owner retries. No
    // additional backoff cycle, presence refresh, timer or retry owner is needed.
    return MailboxRoutePolicy::Unavailable;
}

// Profile-scoped durable delivery ownership. Storage remains the durable source of truth;
// transport instances only prove which authenticated destination was actually sent an ID.
// No packet type, ciphertext, message ID, ordering rule, or group-ledger rule lives here.
class DeliveryManager {
public:
    DeliveryManager(Storage& storage, std::string localDeviceId)
        : storage_(storage), localDeviceId_(std::move(localDeviceId)) {
        if (localDeviceId_.empty()) throw std::invalid_argument("Local delivery identity is required");
    }

    bool authorizeTransport(session::TransportInstanceId instance,
                            const std::string& authenticatedDestination) {
        if (!instance.value || authenticatedDestination.empty()) return false;
        auto found = transports_.find(instance.value);
        if (found != transports_.end())
            return found->second.destination == authenticatedDestination;
        transports_.emplace(instance.value,
                            TransportAuthorization{authenticatedDestination, false});
        return true;
    }

    bool beginDraining(session::TransportInstanceId instance) {
        auto found = transports_.find(instance.value);
        if (found == transports_.end()) return false;
        found->second.draining = true;
        return true;
    }

    bool restoreTransport(session::TransportInstanceId instance) {
        auto found=transports_.find(instance.value);if(found==transports_.end())return false;
        found->second.draining=false;return true;
    }

    void retireTransport(session::TransportInstanceId instance) {
        transports_.erase(instance.value);
        for (auto it = submissions_.begin(); it != submissions_.end();) {
            it->second.erase(instance.value);
            if (it->second.empty()) it = submissions_.erase(it); else ++it;
        }
    }

    std::vector<StoredMessage> pendingMessages(const std::string& destination,
                                                int limit = 100) {
        return storage_.pending(destination, limit);
    }
    std::vector<StoredMessage> mailboxCandidates(){
        auto page=storage_.mailboxCandidates(mailboxCursor_);
        if(page.size()<64&&mailboxCursor_){
            auto first=storage_.mailboxCandidates(0);std::set<std::string> seen;for(const auto& row:page)seen.insert(row.id);
            for(auto& row:first){if(page.size()==64)break;if(seen.insert(row.id).second)page.push_back(std::move(row));}
        }
        mailboxCursor_=page.empty()?0:page.back().sequence;
        return page;
    }

    // Called by the existing application/channel servicing loop, not a new
    // retry owner. Authorization arguments must come from its current verified
    // account/device friend snapshot, never from mailbox message metadata.
    Blob prepareMailbox(const std::string& id,std::uint64_t localAccount,std::uint64_t recipientAccount,
                        const mailbox::RecipientBinding& binding,const Contact& verifiedRecipient,
                        platform::IdentityProvider& identity,platform::HpkeProvider& hpke,
                        std::uint64_t now,bool serverStorageAllowed,bool liveRouteUsable,const Contact* canonicalRecipient=nullptr) {
        if(!serverStorageAllowed||liveRouteUsable)return {};
        auto message=storage_.message(id);
        mailbox::require(message&&message->sender==localDeviceId_&&
            (message->recipient==verifiedRecipient.peerId||(canonicalRecipient&&canonicalRecipient->verified&&canonicalRecipient->peerId==message->recipient&&Storage::publicKeyId(canonicalRecipient->publicKey)==message->recipient))&&
            verifiedRecipient.verified&&Storage::publicKeyId(verifiedRecipient.publicKey)==verifiedRecipient.peerId&&
            identity.publicKey().size()>0&&Storage::publicKeyId(identity.publicKey())==localDeviceId_);
        mailbox::require(mailbox::verifyBinding(binding,recipientAccount,verifiedRecipient.peerId,verifiedRecipient.publicKey,verifyDevicePayload,Storage::publicKeyId)&&binding.storageAllowed);
        auto existing=storage_.mailboxEnvelope(id,verifiedRecipient.peerId);
        if(!existing.empty()) {
            auto header=mailbox::parse(existing).header;
            mailbox::require(header.senderAccount==localAccount&&header.recipientAccount==recipientAccount);
            // An ambiguous accepted store must never be replaced under its old
            // immutable identity. Preserve ciphertext/outbox for reconciliation.
            if(header.expiresUtc<=now){storage_.mailboxIssue(id,verifiedRecipient.peerId,"attempt_expired_requires_review");throw std::runtime_error("attempt_expired_requires_review");}
            if(header.keyId!=binding.keyId||header.keyGeneration!=binding.generation){
                storage_.mailboxIssue(id,verifiedRecipient.peerId,"key_changed_requires_reconciliation");throw std::runtime_error("key_changed_requires_reconciliation");
            }
            return existing; // Reconnect preserves HPKE ciphertext and logical ID.
        }
        if(message->delivery==Delivery::Delivered)return {};
        mailbox::require(now>0&&now<=std::uint64_t(INT64_MAX)-mailbox::MaxTtlMilliseconds&&message->logicalOrder>0);
        mailbox::Header header;header.senderAccount=localAccount;header.recipientAccount=recipientAccount;header.senderDevice=localDeviceId_;
        header.recipientDevice=verifiedRecipient.peerId;header.keyId=binding.keyId;header.keyGeneration=binding.generation;
        header.messageId=id;header.createdUtc=now;header.expiresUtc=now+mailbox::MaxTtlMilliseconds;header.logicalOrder=message->logicalOrder;header.storageAllowed=true;
        auto text=unicode::encode(message->text);Blob body=mailbox::textBody(Blob(text.begin(),text.end()),message->utcMilliseconds);
        struct Erase {Blob& bytes;~Erase(){volatile unsigned char* p=bytes.data();for(std::size_t i=0;i<bytes.size();++i)p[i]=0;}} erase{body};
        auto envelope=mailbox::seal(header,body,binding,verifiedRecipient.publicKey,verifyDevicePayload,Storage::publicKeyId,identity,hpke);
        auto wire=mailbox::serialize(envelope);storage_.saveMailboxEnvelope(id,verifiedRecipient.peerId,wire);return wire;
    }
    void mailboxStored(const std::string& id,const std::string& destination,const Blob& exactSubmittedEnvelope) {
        storage_.mailboxStored(id,destination,exactSubmittedEnvelope);
    }
    static Blob mailboxDigest(const Blob& wire) {
        const auto text=Storage::publicKeyId(wire);Blob digest;
        for(std::size_t i=0;i<text.size();i+=2)digest.push_back(static_cast<unsigned char>(std::stoul(text.substr(i,2),nullptr,16)));
        return digest;
    }
    bool acknowledgeMailbox(const Blob& receiptWire,std::uint64_t verifiedRecipientAccount,const Contact& verifiedRecipient) {
        auto receipt=mailbox::parseReceipt(receiptWire);auto wire=storage_.mailboxEnvelope(receipt.messageId,verifiedRecipient.peerId);
        if(wire.empty()||!verifiedRecipient.verified)return false;
        auto envelope=mailbox::parse(wire);
        if(envelope.header.recipientAccount!=verifiedRecipientAccount||!mailbox::verifyReceipt(receipt,verifiedRecipientAccount,verifiedRecipient.peerId,
            localDeviceId_,envelope.header.messageId,mailboxDigest(wire),verifiedRecipient.publicKey,verifyDevicePayload,Storage::publicKeyId))return false;
        auto message=storage_.message(receipt.messageId);
        if(!message||message->sender!=localDeviceId_||message->delivery==Delivery::Delivered)return false;
        storage_.acknowledge(receipt.messageId,Delivery::Delivered);submissions_.erase(Key{Kind::Message,message->recipient,receipt.messageId});return true;
    }
    struct MailboxReceive {bool inserted=false;Blob receipt;};
    MailboxReceive acceptMailbox(const Blob& wire,std::uint64_t verifiedSenderAccount,const Contact& verifiedSender,
                                const mailbox::RecipientBinding& localBinding,DeviceKeyManager& keys,
                                platform::IdentityProvider& identity,platform::HpkeProvider& hpke,std::uint64_t now) {
        return acceptMailboxAuthorized(wire,verifiedSenderAccount,verifiedSender,localBinding,identity,hpke,now,[&](const auto& operation){
            keys.withRecipient(localBinding.keyId,true,[&](const DeviceKeyMetadata& metadata,const auto& handle){
                mailbox::require(metadata.registration==DeviceKeyRegistration::Accepted&&metadata.signedBinding==mailbox::serializeBinding(localBinding));operation(handle);
            });
        });
    }
    MailboxReceive acceptMailboxScoped(const Blob& wire,std::uint64_t verifiedSenderAccount,const Contact& verifiedSender,
                                      const mailbox::RecipientBinding& localBinding,mailbox::ScopedEnrollment& enrollment,
                                      const std::string& scope,std::uint64_t currentAccount,
                                      platform::IdentityProvider& identity,platform::HpkeProvider& hpke,std::uint64_t now) {
        mailbox::require(currentAccount==localBinding.accountId);
        return acceptMailboxAuthorized(wire,verifiedSenderAccount,verifiedSender,localBinding,identity,hpke,now,[&](const auto& operation){
            enrollment.withAccepted(scope,currentAccount,localBinding,operation);
        });
    }
private:
    struct MailboxPlainErase {Blob& bytes;~MailboxPlainErase(){volatile unsigned char* p=bytes.data();for(std::size_t i=0;i<bytes.size();++i)p[i]=0;}};
    template<class Lease> MailboxReceive acceptMailboxAuthorized(const Blob& wire,std::uint64_t verifiedSenderAccount,const Contact& verifiedSender,
                                const mailbox::RecipientBinding& localBinding,platform::IdentityProvider& identity,
                                platform::HpkeProvider& hpke,std::uint64_t now,Lease lease) {
        auto envelope=mailbox::parse(wire);mailbox::require(verifiedSender.verified&&verifiedSender.peerId==envelope.header.senderDevice&&
            Storage::publicKeyId(verifiedSender.publicKey)==verifiedSender.peerId&&Storage::publicKeyId(identity.publicKey())==localDeviceId_&&localBinding.device==localDeviceId_);
        MailboxReceive result;const auto digest=mailboxDigest(wire);
        lease([&](const auto& handle){
            auto plain=mailbox::open(envelope,localBinding,verifiedSenderAccount,verifiedSender.publicKey,verifyDevicePayload,Storage::publicKeyId,handle,hpke,now);
            // Erase transient decrypted buffers even on malformed text/storage failure.
            MailboxPlainErase erase{plain};
            auto body=mailbox::parseTextBody(plain);MailboxPlainErase eraseText{body.utf8};
            StoredMessage message;message.id=envelope.header.messageId;message.sender=envelope.header.senderDevice;message.recipient=localDeviceId_;
            message.text=unicode::decode(std::string(body.utf8.begin(),body.utf8.end()));message.utcMilliseconds=body.messageUtc;
            message.logicalOrder=envelope.header.logicalOrder;message.delivery=Delivery::Delivered;
            result.inserted=acceptIncomingMessage(verifiedSender.peerId,message,localBinding.accountId);storage_.observeLogicalOrder(message.logicalOrder);
            result.receipt=storage_.mailboxReceipt(message.id,verifiedSender.peerId,digest);
            if(result.receipt.empty()) {
                mailbox::Receipt receipt;receipt.recipientAccount=localBinding.accountId;receipt.recipientDevice=localDeviceId_;
                receipt.senderDevice=verifiedSender.peerId;receipt.messageId=message.id;receipt.envelopeDigest=digest;
                receipt.signature=identity.sign(mailbox::receiptPayload(receipt));result.receipt=mailbox::serializeReceipt(receipt);
                storage_.saveMailboxReceipt(message.id,verifiedSender.peerId,result.receipt);
            }
        });return result;
    }
public:

    std::vector<GroupEvent> pendingGroupEvents(const std::string& destination,
                                                int limit = 64) {
        return storage_.pendingGroupEvents(destination, limit);
    }

    std::vector<GroupKeyHandoff> pendingGroupKeyHandoffs(const std::string& destination,
                                                         int limit = 64) {
        return storage_.pendingGroupKeyHandoffs(destination, limit);
    }
    std::vector<GroupKeyHandoff> pendingGroupKeyHandoffs(const std::string& destination,const std::string& group,std::uint64_t generation,int limit=64){
        return storage_.pendingGroupKeyHandoffs(destination,group,generation,limit);
    }

    bool submittedMessage(const std::string& destination, const std::string& messageId,
                          session::TransportInstanceId instance,std::uint64_t now=0) {
        if(!now)now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        return submitted(Kind::Message, destination, messageId, instance,now);
    }

    // Application progress, not socket activity. Only the transport owner acts
    // on expiry; no second retry owner and no change to normal ACK latency.
    bool messageAckExpired(session::TransportInstanceId instance,std::uint64_t now)const {
        for(const auto& item:submissions_)if(item.first.kind==Kind::Message){
            auto sent=item.second.find(instance.value);
            if(sent!=item.second.end()&&now>=sent->second&&now-sent->second>=30000)return true;
        }
        return false;
    }

    bool submittedGroupEvent(const std::string& destination, const std::string& eventId,
                             session::TransportInstanceId instance) {
        return submitted(Kind::GroupEvent, destination, eventId, instance);
    }

    bool submittedGroupKeyHandoff(const std::string& destination, const std::string& handoffId,
                                  session::TransportInstanceId instance) {
        return submitted(Kind::GroupKey, destination, handoffId, instance);
    }

    bool wasSubmittedMessage(const std::string& destination, const std::string& messageId,
                             session::TransportInstanceId instance) const {
        return wasSubmitted(Kind::Message, destination, messageId, instance);
    }

    bool wasSubmittedGroupEvent(const std::string& destination, const std::string& eventId,
                                session::TransportInstanceId instance) const {
        return wasSubmitted(Kind::GroupEvent, destination, eventId, instance);
    }

    bool wasSubmittedGroupKeyHandoff(const std::string& destination, const std::string& handoffId,
                                     session::TransportInstanceId instance) const {
        return wasSubmitted(Kind::GroupKey, destination, handoffId, instance);
    }

    bool acknowledgeMessage(const std::string& authenticatedDestination,
                            session::TransportInstanceId instance,
                            const std::string& messageId) {
        if (!acknowledges(Kind::Message, authenticatedDestination, messageId, instance))
            return false;
        storage_.acknowledge(messageId, Delivery::Delivered);
        submissions_.erase(Key{Kind::Message, authenticatedDestination, messageId});
        return true;
    }

    bool acknowledgeGroupEvent(const std::string& authenticatedDestination,
                               session::TransportInstanceId instance,
                               const std::string& eventId) {
        if (!acknowledges(Kind::GroupEvent, authenticatedDestination, eventId, instance))
            return false;
        storage_.acknowledgeGroupDelivery(eventId, authenticatedDestination);
        submissions_.erase(Key{Kind::GroupEvent, authenticatedDestination, eventId});
        return true;
    }

    bool acknowledgeGroupKeyHandoff(const std::string& authenticatedDestination,
                                    session::TransportInstanceId instance,
                                    const std::string& handoffId) {
        if (!acknowledges(Kind::GroupKey, authenticatedDestination, handoffId, instance))
            return false;
        storage_.acknowledgeGroupKeyHandoff(handoffId, authenticatedDestination);
        submissions_.erase(Key{Kind::GroupKey, authenticatedDestination, handoffId});
        return true;
    }

    // Returns false for an identical replay and lets Storage reject conflicting ID reuse.
    bool acceptIncomingMessage(const std::string& authenticatedSource,
                               const StoredMessage& message,std::uint64_t authenticatedLocalAccount=0) {
        if (authenticatedSource.empty() || message.sender != authenticatedSource ||
            message.recipient != localDeviceId_)
            throw std::runtime_error("Incoming message destination is not authenticated");
        auto previous=storage_.message(message.id);
        if(previous&&authenticatedLocalAccount){
            auto proof=storage_.archiveHistoryProof(message.id);
            if(!proof.empty()){
                // This protected proof was already verified on archive import.
                // Revocation prevents future sync, not reading/deduping history
                // legitimately received earlier. Live TLS/mailbox authentication
                // above remains mandatory; archived attribution is not a pin.
                auto a=archive::decodeAssertion(proof);const auto& original=a.original.message;
                if(a.account==authenticatedLocalAccount&&a.original.recipientAccount==authenticatedLocalAccount&&
                   a.original.senderAccount!=authenticatedLocalAccount&&original.sender==authenticatedSource&&
                   previous->sender==authenticatedSource&&original.text==message.text&&previous->text==message.text&&
                   original.utcMilliseconds==message.utcMilliseconds&&previous->utcMilliseconds==message.utcMilliseconds&&
                   original.logicalOrder==message.logicalOrder&&previous->logicalOrder==message.logicalOrder)return false;
            }
        }
        if(previous&&previous->delivery==Delivery::Delivered&&previous->logicalOrder==0&&message.logicalOrder>0){
            // A previously accepted legacy frame did not carry an order. Keep
            // that already-durable history position on a modern/mailbox retry;
            // exact content/time/source/destination checks remain in Storage.
            auto compatible=message;compatible.logicalOrder=0;
            return storage_.append(authenticatedSource,compatible);
        }
        return storage_.append(authenticatedSource, message);
    }

    bool acceptArchiveHistory(const Profile& local,archive::Authorization& policy,
            const session::ReliableAuthorization& session,const archive::Assertion& assertion,
            const std::function<std::optional<std::uint64_t>(const std::string&)>& deviceAccount){
        archive::require(local.peerId==localDeviceId_&&policy.localDevice()==localDeviceId_&&
            session.archive.reader==localDeviceId_&&session.expectedRemoteDeviceId==assertion.archiveDevice&&
            assertion.issuer==session.archive.issuer&&archive::currentSessionAuthorization(policy,session));
        archive::ProvenanceVerifier verifier(policy,storage_,local,policy.account(),deviceAccount);
        archive::require(verifier.verify(assertion)!=archive::Provenance::OriginalSigned);
        const auto& original=assertion.original;const auto& m=original.message;
        auto previous=storage_.message(m.id);bool recipientAlias=false;
        if(previous){
            auto sender=deviceAccount(previous->sender),recipient=deviceAccount(previous->recipient);
            archive::require(sender&&recipient&&*sender==original.senderAccount&&*recipient==original.recipientAccount);
            recipientAlias=previous->recipient!=m.recipient;
        }
        const auto conversation=original.senderAccount==policy.account()?m.recipient:m.sender;
        // Recheck immediately before the commit; no live/DM transport authority
        // or recipient ACK is created by an archive assertion.
        archive::require(archive::currentSessionAuthorization(policy,session));
        return storage_.appendArchiveHistory(conversation,m,archive::encodeAssertion(assertion),recipientAlias);
    }

    std::size_t inFlightCount(session::TransportInstanceId instance) const {
        std::size_t count = 0;
        for (const auto& item : submissions_)
            if (item.second.count(instance.value)) ++count;
        return count;
    }

private:
    enum class Kind : unsigned char { Message, GroupEvent, GroupKey };

    struct Key {
        Kind kind;
        std::string destination;
        std::string id;
        bool operator<(const Key& other) const {
            return std::tie(kind, destination, id) <
                   std::tie(other.kind, other.destination, other.id);
        }
    };

    struct TransportAuthorization {
        std::string destination;
        bool draining = false;
    };

    bool submitted(Kind kind, const std::string& destination, const std::string& id,
                   session::TransportInstanceId instance,std::uint64_t now=0) {
        auto transport = transports_.find(instance.value);
        if (transport == transports_.end() || transport->second.draining ||
            transport->second.destination != destination || id.empty()) return false;
        return submissions_[Key{kind, destination, id}].emplace(instance.value,now).second;
    }

    bool wasSubmitted(Kind kind, const std::string& destination, const std::string& id,
                      session::TransportInstanceId instance) const {
        auto found = submissions_.find(Key{kind, destination, id});
        return found != submissions_.end() && found->second.count(instance.value) != 0;
    }

    bool acknowledges(Kind kind, const std::string& destination, const std::string& id,
                      session::TransportInstanceId instance) const {
        auto transport = transports_.find(instance.value);
        if (transport == transports_.end() || transport->second.destination != destination)
            return false;
        return wasSubmitted(kind, destination, id, instance);
    }

    Storage& storage_;
    std::int64_t mailboxCursor_=0;
    std::string localDeviceId_;
    std::map<std::uint64_t, TransportAuthorization> transports_;
    std::map<Key, std::map<std::uint64_t,std::uint64_t>> submissions_;
};

} // namespace peerchat::delivery
