#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace peerchat {
using Blob = std::vector<unsigned char>;
struct GroupEvent;
struct GroupAccount { std::uint64_t accountId=0;std::wstring name; };
struct Profile {
    std::string peerId;
    std::wstring name = L"You";
    std::uint16_t listenPort = 7777;
    Blob publicKey, privateKey; // P-256 CNG blobs; private material is protected on disk.
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
    std::vector<StoredMessage> pending(const std::string& conversation,int limit=100);
    bool appendGroupEvent(const GroupEvent& event);
    std::vector<GroupEvent> groupEvents(const std::string& groupId);
    std::vector<std::string> groupIds();
    GroupAccount groupAccount();
    void saveGroupAccount(const GroupAccount& account);
    void queueGroupDelivery(const std::string& eventId,const std::string& peerId);
    void acknowledgeGroupDelivery(const std::string& eventId,const std::string& peerId);
    std::vector<GroupEvent> pendingGroupEvents(const std::string& peerId,int limit=64);
    static std::string newMessageId();
    static std::string publicKeyId(const Blob& publicKey);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
