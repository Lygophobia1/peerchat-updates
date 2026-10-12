#pragma once
#include "group_key_handoff.hpp"
#include "group.hpp"
#include "platform/group_crypto.hpp"
#include <map>
#include <set>
#include <stdexcept>

namespace peerchat::group_keys {
struct RosterDevice {
    std::uint64_t accountId=0;
    std::string peerId;
    GroupRole role=GroupRole::Member;
    bool invited=false;
};
struct RosterSnapshot {
    std::string legacyGroupId,serverGroupId,distributorPeerId;
    std::uint64_t generation=0;
    std::vector<RosterDevice> devices;
};

// Profile-scoped group-key owner. The server roster is an authenticated control
// input, never a source of key bytes. Node forwards snapshots and peer-TLS frames;
// this owner checks the current roster and local signed ledger before key use.
class GroupKeyManager {
public:
    GroupKeyManager(Storage& storage,std::uint64_t localAccountId,std::string localPeerId)
        :store_(storage),localAccountId_(localAccountId),localPeerId_(std::move(localPeerId)){
        if(!localAccountId_||!protocol::id(localPeerId_))throw std::invalid_argument("Group-key profile binding is invalid");
    }
    void apply(RosterSnapshot snapshot){
        if(!protocol::id(snapshot.legacyGroupId,32)||!protocol::id(snapshot.serverGroupId,32)||
           !protocol::id(snapshot.distributorPeerId)||snapshot.generation==0||
           snapshot.generation>INT64_MAX||snapshot.devices.empty())throw std::runtime_error("Invalid authenticated group roster");
        std::set<std::string> identities;std::set<std::uint64_t> accounts;bool local=false,distributor=false;
        for(const auto& member:snapshot.devices){
            if(!member.accountId||!protocol::id(member.peerId)||!identities.insert(member.peerId).second||
               !accounts.insert(member.accountId).second)throw std::runtime_error("Invalid group roster device binding");
            if(member.accountId==localAccountId_&&member.peerId==localPeerId_)local=true;
            if(!member.invited&&member.peerId==snapshot.distributorPeerId)distributor=true;
        }
        if(!local||!distributor)throw std::runtime_error("Local profile or key distributor is not in the authenticated group roster");
        auto old=rosters_.find(snapshot.serverGroupId);
        if(old!=rosters_.end()){
            if(old->second.legacyGroupId!=snapshot.legacyGroupId||snapshot.generation<old->second.generation)
                throw std::runtime_error("Stale or conflicting group roster");
            if(snapshot.generation==old->second.generation){
                if(old->second.distributorPeerId!=snapshot.distributorPeerId||old->second.devices.size()!=snapshot.devices.size())
                    throw std::runtime_error("Conflicting group roster generation");
                for(size_t i=0;i<snapshot.devices.size();++i){const auto& a=old->second.devices[i];const auto& b=snapshot.devices[i];if(a.accountId!=b.accountId||a.peerId!=b.peerId||a.role!=b.role||a.invited!=b.invited)throw std::runtime_error("Conflicting group roster generation");}
            }
            for(const auto& previous:old->second.devices){
                if(previous.invited)continue;
                if(!currentMember(snapshot,previous.accountId,previous.peerId))
                    store_.cancelGroupKeyHandoffsFor(snapshot.legacyGroupId,previous.peerId);
            }
        }
        rosters_[snapshot.serverGroupId]=std::move(snapshot);
    }
    void revoke(const std::string& serverGroupId){rosters_.erase(serverGroupId);}
    const RosterSnapshot* roster(const std::string& serverGroupId)const{
        auto found=rosters_.find(serverGroupId);return found==rosters_.end()?nullptr:&found->second;
    }
    bool isAuthoritativeGroup(const std::string& legacyGroupId)const{
        for(const auto& item:rosters_)if(item.second.legacyGroupId==legacyGroupId)return true;
        return false;
    }
    const RosterSnapshot* rosterForLegacy(const std::string& legacyGroupId)const{
        for(const auto& item:rosters_)if(item.second.legacyGroupId==legacyGroupId)return &item.second;
        return nullptr;
    }
    static bool currentMember(const RosterSnapshot& snapshot,std::uint64_t account,const std::string& peer){
        for(const auto& member:snapshot.devices)if(!member.invited&&member.accountId==account&&member.peerId==peer)return true;
        return false;
    }
    static bool matchesSignedLedger(const RosterSnapshot& snapshot,const GroupState& ledger){
        if(!ledger.created||ledger.groupId!=snapshot.legacyGroupId)return false;
        std::size_t currentMembers=0;
        for(const auto& member:snapshot.devices)if(!member.invited){
            ++currentMembers;
            auto found=ledger.members.find(member.accountId);
            if(found==ledger.members.end()||found->second.peerId!=member.peerId||
               found->second.role!=member.role)return false;
        }
        // A partial server roster cannot authorize a key for a larger signed
        // group. Finish signed-roster reconciliation before distributing material.
        return currentMembers==ledger.members.size()+ledger.authorizedDevices.size();
    }
    // A restart reconciles any gap between committing the local key and queuing
    // each recipient. Durable receipts prevent repeated handoffs after ACK.
    bool ensureCurrentKey(const std::string& serverGroupId,const GroupState& signedLedger){
        auto snapshot=roster(serverGroupId);
        if(!snapshot||snapshot->distributorPeerId!=localPeerId_||
           !currentMember(*snapshot,localAccountId_,localPeerId_)||
           !matchesSignedLedger(*snapshot,signedLedger))return false;
        Blob key=store_.groupKey(snapshot->legacyGroupId,snapshot->generation);
        if(key.empty()){
            key=platform::group_crypto::random(32);
            store_.saveGroupKey(snapshot->legacyGroupId,snapshot->generation,key);
        }
        try{
            for(const auto& member:snapshot->devices){
                if(member.invited||member.peerId==localPeerId_||
                   store_.hasGroupKeyReceipt(snapshot->legacyGroupId,snapshot->generation,member.peerId)||
                   store_.hasPendingGroupKeyHandoff(snapshot->legacyGroupId,snapshot->generation,member.peerId))continue;
                GroupKeyHandoff offer;offer.id=Storage::newMessageId();offer.groupId=snapshot->legacyGroupId;
                offer.serverGroupId=snapshot->serverGroupId;offer.distributorPeerId=localPeerId_;
                offer.recipientPeerId=member.peerId;offer.rosterGeneration=offer.keyGeneration=snapshot->generation;
                offer.contentKey=key;store_.queueGroupKeyHandoff(offer);
                platform::group_crypto::wipe(offer.contentKey.data(),offer.contentKey.size());
            }
        }catch(...){platform::group_crypto::wipe(key.data(),key.size());throw;}
        platform::group_crypto::wipe(key.data(),key.size());return true;
    }
    bool accept(const GroupKeyHandoff& offer,const std::string& authenticatedTlsPeer,const GroupState& signedLedger){
        const auto* snapshot=roster(offer.serverGroupId);
        if(!snapshot||offer.groupId!=snapshot->legacyGroupId||
           offer.rosterGeneration!=snapshot->generation||offer.keyGeneration!=snapshot->generation||
           offer.distributorPeerId!=authenticatedTlsPeer||offer.distributorPeerId!=snapshot->distributorPeerId||
           offer.recipientPeerId!=localPeerId_||offer.contentKey.size()!=32||
           !currentMember(*snapshot,localAccountId_,localPeerId_)||
           !matchesSignedLedger(*snapshot,signedLedger))return false;
        store_.saveGroupKey(offer.groupId,offer.keyGeneration,offer.contentKey);return true;
    }
    bool maySend(const GroupKeyHandoff& offer,const std::string& authenticatedTlsPeer)const{
        const auto* snapshot=roster(offer.serverGroupId);
        if(!snapshot||offer.groupId!=snapshot->legacyGroupId||offer.distributorPeerId!=localPeerId_||
           offer.recipientPeerId!=authenticatedTlsPeer||!currentMember(*snapshot,localAccountId_,localPeerId_)||
           !currentMember(*snapshot,findAccount(*snapshot,authenticatedTlsPeer),authenticatedTlsPeer))return false;
        // Do not infer historic authorization from current membership. An old
        // pending generation could otherwise reach a removed-and-readded peer.
        return offer.rosterGeneration==snapshot->generation;
    }
private:
    static std::uint64_t findAccount(const RosterSnapshot& snapshot,const std::string& peer){for(const auto& member:snapshot.devices)if(member.peerId==peer)return member.accountId;return 0;}
    Storage& store_;std::uint64_t localAccountId_;std::string localPeerId_;
    std::map<std::string,RosterSnapshot> rosters_;
};
}
