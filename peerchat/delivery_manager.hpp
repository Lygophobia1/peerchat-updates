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
#include <algorithm>
#include <functional>
#include <optional>

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

    // Call only on the existing authenticated account-service response path.
    // Bytes are parsed here but their server/TLS provenance belongs to that
    // owner; a peer frame or local cache is not an eligibility capability.
    bool installEligibility(const Blob& wire,std::uint64_t localAccount,std::uint64_t friendGeneration,
                            const std::function<std::uint64_t(const std::string&)>& accountFor,
                            const std::vector<Contact>& pinnedContacts,std::uint64_t nowUtcMs,
                            std::uint64_t monotonicNowMs){
        eligibilityStage_="wire_domain";eligibilityNow_=nowUtcMs;eligibilityIssued_=eligibilityFresh_=0;
        struct Timing {std::uint64_t& out;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();~Timing(){out=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());}} timing{eligibilityValidationUs_};
        mailbox::Reader reader(wire,"PeerChat/mailbox-eligibility-snapshot/v1");
        eligibilityStage_="local_account_mismatch";mailbox::require(reader.number(8)==localAccount);
        eligibilityStage_="friend_generation_mismatch";mailbox::require(reader.number(8)==friendGeneration);
        eligibilityStage_="local_device_mismatch";mailbox::require(reader.text(64)==localDeviceId_);
        eligibilityStage_="snapshot_header";
        const auto recipientAccount=reader.number(8),version=reader.number(4);
        const auto id=reader.text(32);const auto sequence=reader.number(8),issued=reader.number(8),fresh=reader.number(8);
        eligibilityIssued_=issued;eligibilityFresh_=fresh;
        mailbox::require(mailbox::account(recipientAccount)&&version==1&&mailbox::id(id,32)&&sequence>0);
        eligibilityStage_="snapshot_issued_in_future";mailbox::require(issued>0&&issued<=nowUtcMs);
        eligibilityStage_="snapshot_expired";mailbox::require(fresh>nowUtcMs);
        eligibilityStage_="snapshot_freshness_bounds";mailbox::require(fresh>issued&&fresh-issued<=120000&&monotonicNowMs>0);
        eligibilityStage_="candidate_count";
        const auto count=reader.number(1);mailbox::require(count<=32);
        EligibilitySnapshot incoming;incoming.id=id;incoming.sequence=sequence;incoming.account=recipientAccount;incoming.raw=wire;
        incoming.issued=issued;incoming.fresh=fresh;incoming.installedMonotonic=monotonicNowMs;
        std::string previous;
        for(std::uint64_t i=0;i<count;++i){
            eligibilityStage_="candidate_encoding";
            const auto device=reader.text(64);
            const auto online=reader.number(1);
            const auto proof=reader.field(4096);
            eligibilityStage_="candidate_order_or_identity";mailbox::require(mailbox::id(device,64)&&device>previous&&online<=1);previous=device;
            auto pinned=std::find_if(pinnedContacts.begin(),pinnedContacts.end(),[&](const Contact& contact){
                return contact.peerId==device&&contact.verified&&!contact.publicKey.empty()&&
                    Storage::publicKeyId(contact.publicKey)==device;});
            if(pinned==pinnedContacts.end()||accountFor(device)!=recipientAccount)continue;
            EligibleDevice target;target.device=device;target.publicKey=pinned->publicKey;target.online=online!=0;
            if(!proof.empty()){
                eligibilityStage_="binding_encoding";
                auto binding=mailbox::parseBinding(proof);
                eligibilityStage_="binding_authorization_or_signature";
                mailbox::require(binding.storageAllowed&&mailbox::verifyBinding(binding,recipientAccount,device,
                    pinned->publicKey,verifyDevicePayload,Storage::publicKeyId));
                target.binding=std::move(binding);
            }
            if(target.online||target.binding)incoming.devices.push_back(std::move(target));
        }
        eligibilityStage_="trailing_bytes";
        reader.end();
        if(cacheFriendGeneration_!=friendGeneration||cacheLocalAccount_!=localAccount){
            eligibility_.clear();cacheFriendGeneration_=friendGeneration;cacheLocalAccount_=localAccount;
        }
        auto old=eligibility_.find(recipientAccount);
        if(old!=eligibility_.end()){
            eligibilityStage_="snapshot_sequence_rollback";
            mailbox::require(sequence>=old->second.sequence);
            if(sequence==old->second.sequence){eligibilityStage_="snapshot_sequence_conflict";mailbox::require(wire==old->second.raw);eligibilityStage_="cached_replay";return true;}
        }
        eligibility_[recipientAccount]=std::move(incoming);eligibilityStage_="cached";return true;
    }
    std::string eligibilityDiagnostic()const{return "native_eligibility="+eligibilityStage_+" native_now_utc_ms="+std::to_string(eligibilityNow_)+" snapshot_issued_utc_ms="+std::to_string(eligibilityIssued_)+" snapshot_fresh_until_utc_ms="+std::to_string(eligibilityFresh_)+" validation_us="+std::to_string(eligibilityValidationUs_);}

    // Call at every authoritative friendship/account generation change, not
    // only when a new eligibility response arrives.
    void invalidateEligibility(std::uint64_t localAccount,std::uint64_t friendGeneration){
        if(localAccount!=cacheLocalAccount_||friendGeneration!=cacheFriendGeneration_){
            eligibility_.clear();cacheLocalAccount_=localAccount;cacheFriendGeneration_=friendGeneration;
        }
    }

    bool queueMessageWithCachedSnapshot(const StoredMessage& message,std::uint64_t localAccount,std::uint64_t recipientAccount,
                                        std::uint64_t friendGeneration,std::uint64_t nowUtcMs,
                                        std::uint64_t monotonicNowMs){
        mailbox::require(message.sender==localDeviceId_&&mailbox::id(message.id,32)&&
            mailbox::id(message.recipient,64)&&localAccount>0&&recipientAccount>0&&
            localAccount!=recipientAccount&&message.delivery==Delivery::Queued);
        invalidateEligibility(localAccount,friendGeneration);
        DeliveryTargetManifest manifest;manifest.messageId=message.id;manifest.canonicalRecipient=message.recipient;
        manifest.senderAccount=localAccount;manifest.recipientAccount=recipientAccount;manifest.selectionUnresolved=true;
        manifest.targets.push_back({message.recipient,{ },0,DeliveryTargetStatus::Pending});
        auto cached=eligibility_.find(recipientAccount);
        if(cacheLocalAccount_==localAccount&&cacheFriendGeneration_==friendGeneration&&cached!=eligibility_.end()&&
            nowUtcMs>=cached->second.issued&&nowUtcMs<=cached->second.fresh&&
            monotonicNowMs>=cached->second.installedMonotonic&&
            monotonicNowMs-cached->second.installedMonotonic<=cached->second.fresh-cached->second.issued){
            const auto contacts=storage_.contacts();std::vector<DeliveryTargetState> eligible;
            for(const auto& candidate:cached->second.devices){
                auto pinned=std::find_if(contacts.begin(),contacts.end(),[&](const Contact& contact){
                    return contact.peerId==candidate.device&&contact.verified&&contact.publicKey==candidate.publicKey&&
                        Storage::publicKeyId(contact.publicKey)==candidate.device;});
                if(pinned==contacts.end())continue;
                DeliveryTargetState target;target.device=candidate.device;
                if(candidate.binding){target.bindingKeyId=candidate.binding->keyId;target.bindingGeneration=candidate.binding->generation;}
                eligible.push_back(std::move(target));
            }
            if(!eligible.empty()){
                manifest.selectionUnresolved=false;manifest.snapshotId=cached->second.id;
                manifest.snapshotSequence=cached->second.sequence;manifest.issuedUtcMs=cached->second.issued;
                manifest.freshUntilUtcMs=cached->second.fresh;manifest.targets=std::move(eligible);
            }
        }
        return storage_.appendWithTargets(message.recipient,message,manifest);
    }

    bool eligibleForMailbox(const std::string& id,const std::string& device,
                            const mailbox::RecipientBinding& binding,std::uint64_t nowUtcMs) {
        auto manifest=storage_.deliveryTargets(id);if(!manifest)return true; // Legacy policy stays unchanged.
        if(cacheLocalAccount_!=manifest->senderAccount)return false;
        if(!storage_.mailboxUploadAccepted(id,device))if(auto retry=storage_.targetRetry(id,device))
            if(nowUtcMs<retry->retryUtcMs)return false;
        if(manifest->selectionUnresolved){
            // Preserve the preexisting single-device offline route. It can
            // prove a canonical receipt but can never resolve the unknown
            // sibling set or claim aggregate Delivered.
            return device==manifest->canonicalRecipient&&binding.device==device&&
                binding.accountId==manifest->recipientAccount&&
                manifest->targets.size()==1&&manifest->targets.front().status==DeliveryTargetStatus::Pending;
        }
        auto target=std::find_if(manifest->targets.begin(),manifest->targets.end(),[&](const auto& value){return value.device==device;});
        if(target==manifest->targets.end()||target->status!=DeliveryTargetStatus::Pending||
           target->bindingKeyId!=binding.keyId||target->bindingGeneration!=binding.generation)return false;
        const auto monotonic=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if(!currentTargetEligible(*manifest,device,nowUtcMs,monotonic))return false;
        auto cached=eligibility_.find(manifest->recipientAccount);
        if(cached==eligibility_.end())return false;
        auto current=std::find_if(cached->second.devices.begin(),cached->second.devices.end(),[&](const auto& value){
            return value.device==device&&value.binding&&value.binding->keyId==binding.keyId&&
                value.binding->generation==binding.generation;});
        return current!=cached->second.devices.end();
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
        auto pending=storage_.pending(destination,limit);
        const auto nowUtc=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        const auto monotonic=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        pending.erase(std::remove_if(pending.begin(),pending.end(),[&](const StoredMessage& message){
            auto manifest=storage_.deliveryTargets(message.id);
            return manifest&&(manifest->senderAccount!=cacheLocalAccount_||
                (!manifest->selectionUnresolved&&!currentTargetEligible(*manifest,destination,nowUtc,monotonic)));
        }),pending.end());
        return pending;
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
        if(auto manifest=storage_.deliveryTargets(id))mailbox::require(
            manifest->senderAccount==localAccount&&manifest->recipientAccount==recipientAccount);
        if(!eligibleForMailbox(id,verifiedRecipient.peerId,binding,now))return {};
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
    bool noteMailboxStoreFailure(const Blob& exactDurableEnvelope,const std::string& reason,
                                 std::uint64_t retryUtcMs,std::uint64_t nowUtcMs){
        mailbox::require(!exactDurableEnvelope.empty()&&reason.size()>0&&reason.size()<=64&&
            nowUtcMs>0&&retryUtcMs>=nowUtcMs&&retryUtcMs-nowUtcMs<=900000);
        const auto header=mailbox::parse(exactDurableEnvelope).header;
        mailbox::require(header.senderDevice==localDeviceId_);
        auto manifest=storage_.deliveryTargets(header.messageId);
        mailbox::require(manifest&&manifest->senderAccount==cacheLocalAccount_&&
            manifest->senderAccount==header.senderAccount&&manifest->recipientAccount==header.recipientAccount&&
            storage_.mailboxEnvelope(header.messageId,header.recipientDevice)==exactDurableEnvelope);
        return storage_.saveTargetRetry(header.messageId,header.recipientDevice,exactDurableEnvelope,
            DeliveryTargetRetry{reason,retryUtcMs});
    }
    static Blob mailboxDigest(const Blob& wire) {
        const auto text=Storage::publicKeyId(wire);Blob digest;
        for(std::size_t i=0;i<text.size();i+=2)digest.push_back(static_cast<unsigned char>(std::stoul(text.substr(i,2),nullptr,16)));
        return digest;
    }
    bool acknowledgeMailbox(const Blob& receiptWire,std::uint64_t verifiedRecipientAccount,const Contact& verifiedRecipient,bool* authenticatedReceipt=nullptr) {
        if(authenticatedReceipt)*authenticatedReceipt=false;
        auto receipt=mailbox::parseReceipt(receiptWire);auto wire=storage_.mailboxEnvelope(receipt.messageId,verifiedRecipient.peerId);
        if(wire.empty()||!verifiedRecipient.verified)return false;
        auto envelope=mailbox::parse(wire);
        if(envelope.header.recipientAccount!=verifiedRecipientAccount||!mailbox::verifyReceipt(receipt,verifiedRecipientAccount,verifiedRecipient.peerId,
            localDeviceId_,envelope.header.messageId,mailboxDigest(wire),verifiedRecipient.publicKey,verifyDevicePayload,Storage::publicKeyId))return false;
        auto message=storage_.message(receipt.messageId);
        if(!message||message->sender!=localDeviceId_)return false;
        if(authenticatedReceipt)*authenticatedReceipt=true;
        if(message->delivery==Delivery::Delivered)return false;
        const bool settled=storage_.deliveryTargets(receipt.messageId)?
            storage_.settleTarget(receipt.messageId,verifiedRecipient.peerId):
            (storage_.acknowledge(receipt.messageId,Delivery::Delivered),true);
        submissions_.erase(Key{Kind::Message,verifiedRecipient.peerId,receipt.messageId});return settled;
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
                storage_.saveAuthenticatedMailboxReceipt(message,localBinding.accountId,result.receipt);
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
        if(auto manifest=storage_.deliveryTargets(messageId)){
            if(manifest->senderAccount!=cacheLocalAccount_)return false;
            auto target=std::find_if(manifest->targets.begin(),manifest->targets.end(),[&](const auto& value){return value.device==destination;});
            if(target==manifest->targets.end()||target->status!=DeliveryTargetStatus::Pending)return false;
            if(!manifest->selectionUnresolved){
                const auto nowUtc=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                const auto monotonic=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
                if(!currentTargetEligible(*manifest,destination,nowUtc,monotonic))return false;
            }
        }
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
        const bool settled=storage_.deliveryTargets(messageId)?
            storage_.settleTarget(messageId,authenticatedDestination):
            (storage_.acknowledge(messageId,Delivery::Delivered),true);
        submissions_.erase(Key{Kind::Message, authenticatedDestination, messageId});
        return settled;
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
    bool currentTargetEligible(const DeliveryTargetManifest& manifest,const std::string& device,
                               std::uint64_t nowUtcMs,std::uint64_t monotonicNowMs){
        if(manifest.selectionUnresolved||cacheLocalAccount_!=manifest.senderAccount)return false;
        auto snapshot=eligibility_.find(manifest.recipientAccount);
        if(snapshot==eligibility_.end()||nowUtcMs<snapshot->second.issued||nowUtcMs>snapshot->second.fresh||
           monotonicNowMs<snapshot->second.installedMonotonic||
           monotonicNowMs-snapshot->second.installedMonotonic>snapshot->second.fresh-snapshot->second.issued)return false;
        auto target=std::find_if(manifest.targets.begin(),manifest.targets.end(),[&](const auto& value){return value.device==device;});
        if(target==manifest.targets.end()||target->status!=DeliveryTargetStatus::Pending)return false;
        auto candidate=std::find_if(snapshot->second.devices.begin(),snapshot->second.devices.end(),[&](const auto& value){return value.device==device;});
        if(candidate==snapshot->second.devices.end())return false;
        auto contacts=storage_.contacts();auto pin=std::find_if(contacts.begin(),contacts.end(),[&](const Contact& value){
            return value.peerId==device&&value.verified&&value.publicKey==candidate->publicKey&&
                Storage::publicKeyId(value.publicKey)==device;});
        return pin!=contacts.end();
    }
    struct EligibleDevice {
        std::string device;Blob publicKey;bool online=false;
        std::optional<mailbox::RecipientBinding> binding;
    };
    struct EligibilitySnapshot {
        std::string id;std::uint64_t sequence=0,account=0,issued=0,fresh=0,installedMonotonic=0;
        Blob raw;
        std::vector<EligibleDevice> devices;
    };
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
    std::uint64_t cacheFriendGeneration_=0;
    std::uint64_t cacheLocalAccount_=0;
    std::map<std::uint64_t,EligibilitySnapshot> eligibility_;
    std::string eligibilityStage_="not_requested";
    std::uint64_t eligibilityNow_=0,eligibilityIssued_=0,eligibilityFresh_=0,eligibilityValidationUs_=0;
};

} // namespace peerchat::delivery
