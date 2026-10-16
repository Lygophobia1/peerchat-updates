#pragma once
#include "protocol.hpp"
#include "group.hpp"
#include "delivery_manager.hpp"
#include <map>
#include <algorithm>
#include <set>

namespace peerchat::reliable {
// Shared reliable wire/commit seam below Node/platform adapters. The caller must
// already have authorized the authenticated TLS device and winning transport.
// This component owns no socket, trust decision, retry loop or delivery state.
class FrameQueue {
    Blob pending_;
public:
    void receive(const Blob& bytes) {
        protocol::check(bytes.size()<=256*1024&&pending_.size()<=256*1024-bytes.size());
        pending_.insert(pending_.end(),bytes.begin(),bytes.end());
    }
    std::optional<Blob> take() {
        if(pending_.size()<4)return {};
        size_t length=(size_t(pending_[0])<<24)|(size_t(pending_[1])<<16)|(size_t(pending_[2])<<8)|pending_[3];
        protocol::check(length>=4&&length<=protocol::MaxPacket);
        if(pending_.size()<length+4)return {};
        Blob frame(pending_.begin()+4,pending_.begin()+4+length);
        pending_.erase(pending_.begin(),pending_.begin()+4+length);
        return frame;
    }
};
struct Hello { std::uint64_t capabilities=0,port=0;std::wstring name; };
inline Hello receiveHello(protocol::Reader& reader,bool relay) {
    protocol::check(reader.type()==protocol::Hello);
    Hello hello{reader.number(),reader.number(),reader.text()};reader.end();
    protocol::check((hello.capabilities&protocol::CapabilityMutualTls)!=0&&
        hello.port<=65535&&(relay||hello.port>0)&&unicode::utf16Units(hello.name)<=64);
    return hello;
}
inline protocol::Writer helloPacket(std::uint64_t capabilities,std::uint16_t port,const std::wstring& name) {
    protocol::Writer hello(protocol::Hello);hello.number(capabilities);hello.number(port);hello.text(name);return hello;
}
// Per-transport protocol state only. Session activation, trust persistence and
// route arbitration remain with the existing owners/platform execution adapter.
// Windows and portable adapters use this same Hello/Accepted implementation.
struct PeerProtocolState {
    bool hello=false,approved=false,remoteApproved=false,logicalOrdering=false,
        signedGroups=false,voiceCalls=false,mediaRecovery=false,screenShare=false,
        screenBackendNegotiation=false,identityMismatch=false,directHandoff=false;
    std::wstring name;
    std::uint16_t port=0;
    std::string peerId,expected;
    std::uint64_t remoteCapabilities=0;
    FrameQueue input;
    std::uint64_t observeHello(protocol::Reader& reader,bool relay,
                              const Blob& tlsPublicKey,const std::string& localId) {
        protocol::check(!hello);
        auto value=receiveHello(reader,relay);
        auto presented=Storage::publicKeyId(tlsPublicKey);
        protocol::check(presented!=localId);
        auto capabilities=value.capabilities;
        remoteCapabilities=capabilities;
        logicalOrdering=(capabilities&protocol::CapabilityLogicalOrdering)!=0;
        signedGroups=(capabilities&protocol::CapabilitySignedGroups)!=0;
        voiceCalls=(capabilities&protocol::CapabilityVoiceCalls)!=0;
        mediaRecovery=(capabilities&protocol::CapabilityMediaRecovery)!=0;
        screenShare=(capabilities&protocol::CapabilityScreenShare)!=0;
        screenBackendNegotiation=(capabilities&protocol::CapabilityScreenBackendNegotiation)!=0;
        directHandoff=(capabilities&protocol::CapabilityDirectHandoff)!=0;
        peerId=std::move(presented);name=std::move(value.name);
        port=static_cast<std::uint16_t>(value.port);hello=true;
        // A changed identity remains an explicit verification event, not TOFU.
        identityMismatch=!expected.empty()&&expected!=peerId;
        return capabilities;
    }
    void observeAccepted(protocol::Reader& reader) {
        protocol::check(hello&&reader.type()==protocol::Accepted&&!remoteApproved);
        reader.end();remoteApproved=true;
    }
    void locallyTrusted(){protocol::check(hello);approved=true;}
    bool verifiedContactMatches(const Contact* contact,const Blob& tlsPublicKey)const {
        if(!contact||!contact->verified)return false;
        protocol::check(contact->peerId==peerId&&contact->publicKey==tlsPublicKey);
        return true;
    }
};
struct FriendIdentity {
    std::uint64_t accountId=0;
    std::string expectedPeerId;
    std::vector<std::string> knownPeerIds;
};
enum class InitialPinDecision { Allowed, StaleIdentity, NoCurrentFriendship, HistoricalPin, StoredKeyMismatch };
// An instance-local projection of authenticated account-service data, not trust
// derived from a route or a substitute for stored device pins.
class AuthenticatedFriendSnapshot {
    struct Friend { std::uint64_t accountId=0;std::set<std::string> knownPeerIds; };
    std::uint64_t account_=0,generation_=0;
    std::map<std::string,Friend> friends_;
public:
    bool contains(const std::string& peer)const{return account_&&friends_.count(peer)!=0;}
    std::uint64_t localAccount()const noexcept{return account_;}
    std::uint64_t accountFor(const std::string& peer)const {
        if(!account_)return 0;
        std::uint64_t account=0;
        for(const auto& value:friends_)if(value.second.knownPeerIds.count(peer)){
            if(account&&account!=value.second.accountId)return 0;
            account=value.second.accountId;
        }
        return account;
    }
    std::uint64_t generation()const noexcept{return generation_;}
    void replace(std::uint64_t account,std::uint64_t generation,const std::vector<FriendIdentity>& values) {
        account_=account;generation_=generation;friends_.clear();
        if(!account_)return;
        for(const auto& value:values){
            if(!value.accountId||!protocol::id(value.expectedPeerId))continue;
            Friend state;state.accountId=value.accountId;state.knownPeerIds.insert(value.expectedPeerId);
            for(const auto& peer:value.knownPeerIds)if(protocol::id(peer))state.knownPeerIds.insert(peer);
            friends_.emplace(value.expectedPeerId,std::move(state));
        }
    }
    InitialPinDecision initialPin(const PeerProtocolState* state,bool live,
            const std::string& requested,std::uint64_t generation,const Blob* tlsKey,
            const std::map<std::string,Contact>& contacts)const {
        if(!state||!live||!state->hello||state->approved||state->identityMismatch||
           requested.empty()||state->peerId!=requested||
           (!state->expected.empty()&&state->expected!=state->peerId)||
           generation!=generation_||!tlsKey||Storage::publicKeyId(*tlsKey)!=state->peerId)
            return InitialPinDecision::StaleIdentity;
        auto found=friends_.find(state->peerId);
        if(!account_||found==friends_.end()||!found->second.accountId)
            return InitialPinDecision::NoCurrentFriendship;
        for(const auto& contact:contacts)
            if(found->second.knownPeerIds.count(contact.first)&&contact.second.verified)
                return InitialPinDecision::HistoricalPin;
        auto known=contacts.find(state->peerId);
        if(known!=contacts.end()&&!known->second.publicKey.empty()&&known->second.publicKey!=*tlsKey)
            return InitialPinDecision::StoredKeyMismatch;
        return InitialPinDecision::Allowed;
    }
};
// Caller must first pass either stored-pin verification, the constrained initial
// friend-pin guard, or an explicit user verification decision. Group membership
// alone must never call this function or create ordinary contact trust.
inline Contact persistAuthenticatedContact(Storage& store,const PeerProtocolState& state,
        const Contact* existing,const Blob& tlsKey,std::optional<Endpoint> endpoint={}) {
    protocol::check(state.hello&&Storage::publicKeyId(tlsKey)==state.peerId);
    Contact contact=existing?*existing:Contact{state.peerId,state.name,tlsKey,{},true};
    contact.name=state.name;contact.publicKey=tlsKey;contact.verified=true;
    if(endpoint&&contact.endpoints.size()<128&&std::none_of(contact.endpoints.begin(),contact.endpoints.end(),
        [&](const auto& known){return known.host==endpoint->host&&known.port==endpoint->port;}))contact.endpoints.push_back(*endpoint);
    store.saveContact(contact);return contact;
}
struct TextReceipt {
    std::string messageId;
    std::optional<StoredMessage> message;
    bool inserted=false;
};
// Local account admission is not device trust. An existing pin of the same
// account remains usable when presence selects a different, unverified device.
inline std::string queueRecipient(const std::string& requested,const std::vector<Contact>& contacts,
                                  const AuthenticatedFriendSnapshot& friends) {
    auto pinned=[&](const Contact& value){return value.verified&&protocol::id(value.peerId)&&
        !value.publicKey.empty()&&Storage::publicKeyId(value.publicKey)==value.peerId;};
    for(const auto& value:contacts)if(value.peerId==requested&&pinned(value))return requested;
    const auto account=friends.accountFor(requested);
    if(friends.contains(requested)&&account)for(const auto& value:contacts)
        if(pinned(value)&&friends.accountFor(value.peerId)==account)return value.peerId;
    return requested;
}
inline StoredMessage queueText(Storage& store,const Profile& local,const Contact* contact,
                              const std::string& destination,const std::string& messageId,
                              const std::wstring& text,std::uint64_t utcMilliseconds,
                              const AuthenticatedFriendSnapshot* friends=nullptr,delivery::DeliveryManager* deliveryOwner=nullptr) {
    const bool pinned=contact&&contact->peerId==destination&&contact->verified&&
        !contact->publicKey.empty()&&Storage::publicKeyId(contact->publicKey)==destination;
    const bool accountAdmitted=friends&&friends->contains(destination)&&friends->accountFor(destination)>0;
    protocol::check((pinned||accountAdmitted)&&protocol::id(destination)&&
        protocol::id(messageId,32)&&!text.empty()&&unicode::utf16Units(text)<=protocol::MaxChatCharacters);
    StoredMessage message{messageId,local.peerId,destination,text,utcMilliseconds,Delivery::Queued,0,store.nextLogicalOrder()};
    if(friends&&deliveryOwner&&friends->localAccount()>0&&friends->accountFor(destination)>0){
        auto monotonic=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
        deliveryOwner->queueMessageWithCachedSnapshot(message,friends->localAccount(),friends->accountFor(destination),friends->generation(),utcMilliseconds,monotonic);
    }else store.append(destination,message);
    return message;
}

inline protocol::Writer textPacket(const StoredMessage& message,bool logicalOrdering) {
    protocol::Writer packet(protocol::Text);
    packet.string(message.id);
    if(logicalOrdering)packet.number(message.logicalOrder);
    packet.number(message.utcMilliseconds);
    packet.text(message.text);
    return packet;
}

inline TextReceipt receiveText(protocol::Reader& reader,bool logicalOrdering,
                               const std::string& authenticatedSource,
                               const Profile& local,Storage& storage,
                               delivery::DeliveryManager& deliveries,std::uint64_t authenticatedLocalAccount=0) {
    protocol::check(reader.type()==protocol::Text);
    StoredMessage message;
    message.id=reader.string();
    if(logicalOrdering)message.logicalOrder=reader.number();
    message.utcMilliseconds=reader.number();
    auto text=reader.chatText();reader.end();
    protocol::check(protocol::id(message.id,32));
    TextReceipt receipt;receipt.messageId=message.id;
    if(!text)return receipt;
    message.text=std::move(*text);message.sender=authenticatedSource;
    message.recipient=local.peerId;message.delivery=Delivery::Delivered;
    // Legacy wire has no logical-order field. An identical authenticated retry
    // must preserve the durable order, not pretend it explicitly changed to 0.
    // Storage still compares identity, timestamp and content exactly.
    if(!logicalOrdering){auto existing=storage.message(message.id);if(existing)message.logicalOrder=existing->logicalOrder;}
    storage.observeLogicalOrder(message.logicalOrder);
    receipt.inserted=deliveries.acceptIncomingMessage(authenticatedSource,message,authenticatedLocalAccount);
    receipt.message=std::move(message);
    return receipt;
}

inline std::string receiveAck(protocol::Reader& reader) {
    protocol::check(reader.type()==protocol::Ack);
    auto id=reader.string();reader.end();protocol::check(protocol::id(id,32));
    return id;
}
}
