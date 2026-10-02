#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace peerchat {
namespace platform { class IdentityProvider; class StoragePlatform; class SqlDatabase; }
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
enum class Delivery : unsigned char { Queued, Sent, Delivered };
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

// Thread-confined embedded SQLite store. Construct/use on a worker, not the GUI.
// Every profile/contact/message payload is protected with current-user DPAPI
// before binding it to SQLite. SQLite never receives plaintext message content.
class Storage {
public:
    explicit Storage(const std::wstring& directory);
    explicit Storage(std::shared_ptr<platform::SqlDatabase> database);
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
    void acknowledge(const std::string& messageId, Delivery delivery);
    std::uint64_t nextLogicalOrder();
    void observeLogicalOrder(std::uint64_t remoteOrder);
    std::vector<StoredMessage> history(const std::string& conversation,
        int limit=100, std::int64_t beforeSequence=INT64_MAX);
    // Removes only this local DM's encrypted message/outbox records. Contact, trust and account
    // identity remain intact so a later message starts a fresh conversation safely.
    void eraseHistory(const std::string& conversation);
    std::vector<StoredMessage> pending(const std::string& conversation,int limit=100);
    bool appendGroupEvent(const GroupEvent& event);
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
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
