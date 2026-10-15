#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>

namespace peerchat {
namespace platform { class IdentityProvider; class StoragePlatform; class SqlDatabase; class RecordProtector; }
using Blob = std::vector<unsigned char>;
struct GroupEvent;
struct GroupAccount { std::uint64_t accountId=0;std::wstring name; };
struct AuthenticatedGroupMessage { Blob envelope,plaintext;std::int64_t sequence=0;std::string status; };
// Sensitive group-key handoffs are persisted only in the sender's DPAPI-protected
// profile store and travel only inside an authenticated peer-TLS control stream.
struct GroupKeyHandoff {
    std::string id,groupId,serverGroupId,distributorPeerId,recipientPeerId;
    std::uint64_t rosterGeneration=0,keyGeneration=0;
    Blob contentKey;
};
struct Profile {
    std::string peerId;
    std::wstring name = L"You";
    std::uint16_t listenPort = 7777;
    Blob publicKey, privateKey; // Legacy Windows CNG blobs; unchanged on disk.
    // Opaque, instance-owned key operations. Never serialized. When set, the
    // privateKey field must be empty; public/wire identity stays exactly the same.
    std::shared_ptr<platform::IdentityProvider> identity;
};
struct Endpoint { std::wstring host; std::uint16_t port=7777; };
struct Contact {
    std::string peerId;
    std::wstring name;
    Blob publicKey;
    std::vector<Endpoint> endpoints;
    bool verified=false;
};
// Preserve existing persisted/C ABI values. Stored is not a recipient ACK.
enum class Delivery : unsigned char { Queued=0, Sent=1, Delivered=2, Stored=3 };
inline unsigned deliveryRank(Delivery value){return value==Delivery::Delivered?3:value==Delivery::Stored?2:static_cast<unsigned>(value);}
struct StoredMessage {
    std::string id, sender, recipient;
    std::wstring text;
    std::uint64_t utcMilliseconds=0;
    Delivery delivery=Delivery::Queued;
    std::int64_t sequence=0; // Local database cursor; never a network identity.
    // Lamport-style conversation position assigned by the sender. It is persisted so
    // queued/retried messages retain their original order across reconnects and restarts.
    std::uint64_t logicalOrder=0;
};
struct ArchiveHistoryPage {
    std::vector<StoredMessage> messages;
    std::vector<std::int64_t> cursors; // Archive positions, never message identities or UI history cursors.
    std::int64_t nextCursor=0,throughCursor=0;
    bool complete=false;
};
struct ArchiveConversationPage {
    std::vector<std::string> conversations;
    std::vector<std::int64_t> cursors;
    std::int64_t nextCursor=0,throughCursor=0;
    bool complete=false;
};
// Protected attachment metadata only. Content and resumable byte transfer remain
// TransferManager responsibilities. Sequence is a local monotonic catalog cursor.
struct AttachmentMetadataRecord {std::string id;Blob record;std::int64_t sequence=0;};
struct AttachmentMetadataPage {std::vector<AttachmentMetadataRecord> records;std::int64_t next=0,through=0;bool complete=false;};

// Thread-confined embedded SQLite store. Construct/use on a worker, not the GUI.
// Every profile/contact/message payload is protected before binding to SQLite:
// current-user DPAPI on Windows, an explicit profile-bound provider elsewhere.
// SQLite never receives plaintext message content.
class Storage {
public:
    explicit Storage(const std::wstring& directory);
    explicit Storage(std::shared_ptr<platform::SqlDatabase> database);
    Storage(std::shared_ptr<platform::SqlDatabase> database,std::shared_ptr<platform::RecordProtector> records);
    explicit Storage(std::shared_ptr<platform::StoragePlatform> platform);
    ~Storage();
    Storage(const Storage&)=delete;
    Storage& operator=(const Storage&)=delete;
    Profile profile();
    void saveProfile(const Profile& profile);
    std::vector<Contact> contacts();
    void saveContact(const Contact& contact);
    void removeContact(const std::string& peerId);
    // False means an identical ID/payload is already durable; mismatched reuse throws.
    bool append(const std::string& conversation, const StoredMessage& message);
    std::optional<StoredMessage> message(const std::string& messageId);
    // Called only after DeliveryManager validates current archive authorization
    // and provenance. Does not authorize transport, generate ACKs or queue sends.
    bool appendArchiveHistory(const std::string& conversation,const StoredMessage& original,
        const Blob& verifiedAssertion,bool verifiedRecipientAlias=false);
    Blob archiveHistoryProof(const std::string& messageId);
    std::string archiveStoreId();
    std::int64_t archiveHistoryWatermark();
    ArchiveConversationPage archiveConversationPage(std::int64_t afterCursor,std::int64_t throughCursor,int limit=8);
    // Retained authenticated directory evidence, NOT friendship/route permission.
    // Call only with a server-authenticated account/device binding. Contradictory
    // ownership fails closed; deleting a contact does not erase historical proof.
    void rememberAuthenticatedDeviceAccount(const std::string& device,std::uint64_t account);
    std::optional<std::uint64_t> historicalDeviceAccount(const std::string& device);
    ArchiveHistoryPage archiveHistoryPage(const std::string& conversation,std::int64_t afterCursor,
        std::int64_t throughCursor,int limit=8,size_t maximumBytes=256*1024);
    Blob archiveSyncState(const std::string& contextId);
    std::uint64_t archiveHistoryCount(const std::string& conversation,std::uint64_t after,std::uint64_t through);
    Blob transferJournal(const std::string& device);
    void saveTransferJournal(const std::string& device,const Blob& expected,const Blob& next);
    // Supplemental immutable encrypted-group-attachment staging only. Uses the
    // existing protected object store, never the legacy transfer journal format.
    Blob groupAttachmentStaging(const std::string& device);
    void saveGroupAttachmentStaging(const std::string& device,const Blob& expected,const Blob& next);
    void saveArchiveSyncState(const std::string& contextId,const Blob& expected,const Blob& next);
    Blob attachmentMetadata(const std::string& id);
    void saveAttachmentMetadata(const std::string& id,const Blob& expected,const Blob& record);
    std::int64_t attachmentMetadataWatermark();
    AttachmentMetadataPage attachmentMetadataPage(std::int64_t after,std::int64_t through,int limit=8);
    void acknowledge(const std::string& messageId, Delivery delivery);
    Blob mailboxEnvelope(const std::string& messageId,const std::string& recipientDevice);
    void saveMailboxEnvelope(const std::string& messageId,const std::string& recipientDevice,const Blob& envelope);
    void mailboxStored(const std::string& messageId,const std::string& recipientDevice,const Blob& expectedEnvelope);
    std::vector<StoredMessage> pendingMailboxUploads(int limit=64);
    bool mailboxUploadAccepted(const std::string& messageId,const std::string& recipientDevice);
    Blob mailboxReceipt(const std::string& messageId,const std::string& senderDevice,const Blob& envelopeDigest);
    void saveMailboxReceipt(const std::string& messageId,const std::string& senderDevice,const Blob& receipt);
    std::uint64_t nextLogicalOrder();
    void observeLogicalOrder(std::uint64_t remoteOrder);
    std::vector<StoredMessage> history(const std::string& conversation,
        int limit=100, std::int64_t beforeSequence=INT64_MAX);
    // Removes only this local DM's encrypted message/outbox records. Contact, trust and account
    // identity remain intact so a later message starts a fresh conversation safely.
    void eraseHistory(const std::string& conversation);
    std::vector<StoredMessage> pending(const std::string& conversation,int limit=100);
    bool appendGroupEvent(const GroupEvent& event);
    Blob groupReplicaReceipt(const std::string& groupId,const std::string& eventId);
    void commitGroupReplicaReceipt(const GroupEvent& event,const Blob& receipt);
    // One bounded verified recovery page: no partially persisted epoch chain.
    void commitGroupReplicaReceipts(const std::vector<std::pair<GroupEvent,Blob>>& receipts,
                                   const std::vector<GroupEvent>& bootstrapMetadata={});
    std::vector<GroupEvent> groupEvents(const std::string& groupId);
    std::vector<std::string> groupIds();
    GroupAccount groupAccount();
    void saveGroupAccount(const GroupAccount& account);
    void queueGroupDelivery(const std::string& eventId,const std::string& peerId);
    void acknowledgeGroupDelivery(const std::string& eventId,const std::string& peerId);
    std::vector<GroupEvent> pendingGroupEvents(const std::string& peerId,int limit=64);
    void saveGroupKey(const std::string& groupId,std::uint64_t generation,const Blob& key);
    Blob groupKey(const std::string& groupId,std::uint64_t generation);
    // Stage 1B has no delivery queue. Commit only after complete message authentication.
    // Identical replay returns false; conflicting reuse of an ID throws.
    bool appendAuthenticatedGroupMessage(const std::string& serverGroupId,const std::string& messageId,
        const Blob& envelope,const Blob& plaintext,const std::string& localGroupId={});
    AuthenticatedGroupMessage authenticatedGroupMessage(const std::string& serverGroupId,const std::string& messageId);
    std::vector<AuthenticatedGroupMessage> authenticatedGroupHistory(const std::string& localGroupId,int limit=100,std::int64_t before=INT64_MAX);
    bool setGroupMessageStatus(const std::string& groupId,const std::string& messageId,const std::string& status,std::uint32_t stage=0);
    // Durable local nonce reservation, scoped to the actual key fingerprint (not the group name).
    bool reserveGroupNonce(const std::string& keyFingerprint,const Blob& nonce);
    void queueGroupKeyHandoff(const GroupKeyHandoff& handoff);
    std::vector<GroupKeyHandoff> pendingGroupKeyHandoffs(const std::string& recipientPeerId,int limit=64);
    void acknowledgeGroupKeyHandoff(const std::string& id,const std::string& recipientPeerId);
    bool hasGroupKeyReceipt(const std::string& groupId,std::uint64_t generation,const std::string& recipientPeerId);
    bool hasPendingGroupKeyHandoff(const std::string& groupId,std::uint64_t generation,const std::string& recipientPeerId);
    void cancelGroupKeyHandoffsFor(const std::string& groupId,const std::string& recipientPeerId);
    static std::string newMessageId();
    static std::string publicKeyId(const Blob& publicKey);
    // Archive authorization/provenance policy, not message/outbox ownership.
    // Protected, profile-bound CAS prevents concurrent stale writers.
    Blob archiveAuthorizationState();
    void saveArchiveAuthorizationState(const Blob& expected,const Blob& next);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
