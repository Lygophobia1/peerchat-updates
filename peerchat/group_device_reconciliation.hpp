#pragma once
#include "group_device_binding.hpp"
#include "group_key_manager.hpp"
#include <algorithm>

namespace peerchat::group_devices {
enum class Reconciliation { Accepted,InvalidProof,WrongGroup,StaleGeneration,ActorNotAuthorized,RosterMismatch,LedgerRejected };
// Caller supplies consecutive authenticated snapshots. This helper neither
// trusts discovery nor enables enrollment: a default-closed ledger still rejects.
// No partial mutation, persistence, key distribution or retry ownership occurs.
inline Reconciliation reconcileApproval(const group_keys::RosterSnapshot& before,const group_keys::RosterSnapshot& after,
                                        const Blob& wire,GroupLedger& ledger){
    SignedApproval proof;
    try{proof=readApproval(wire);}catch(...){return Reconciliation::InvalidProof;}
    const auto& event=proof.event;
    if(before.serverGroupId!=proof.serverGroup||after.serverGroupId!=proof.serverGroup||
       before.legacyGroupId!=event.groupId||after.legacyGroupId!=event.groupId)return Reconciliation::WrongGroup;
    if(before.generation!=proof.generation||after.generation!=proof.generation+1)return Reconciliation::StaleGeneration;
    const auto* actor=ledger.state().device(proof.account,event.actorPeerId);
    if(!actor||actor->publicKey!=proof.actorPublicKey||
       !group_keys::GroupKeyManager::currentMember(before,proof.account,event.actorPeerId))return Reconciliation::ActorNotAuthorized;
    if(!group_keys::GroupKeyManager::matchesSignedLedger(before,ledger.state())||after.distributorPeerId!=event.actorPeerId)
        return Reconciliation::RosterMismatch;
    auto target=std::find_if(before.devices.begin(),before.devices.end(),[&](const auto& device){return device.peerId==event.subjectPeerId;});
    const bool revoke=event.kind==GroupEventKind::RevokeDevice;
    if(revoke?(target==before.devices.end()||target->invited||target->accountId!=proof.account):target!=before.devices.end())
        return Reconciliation::RosterMismatch;
    auto expected=before.devices;
    if(revoke)expected.erase(expected.begin()+(target-before.devices.begin()));
    else expected.push_back({proof.account,event.subjectPeerId,actor->role,false});
    auto ordered=[](std::vector<group_keys::RosterDevice> devices){
        std::sort(devices.begin(),devices.end(),[](const auto& a,const auto& b){return a.peerId<b.peerId;});return devices;
    };
    expected=ordered(std::move(expected));auto actual=ordered(after.devices);
    if(expected.size()!=actual.size())return Reconciliation::RosterMismatch;
    for(std::size_t i=0;i<expected.size();++i){const auto& a=expected[i];const auto& b=actual[i];
        if(a.accountId!=b.accountId||a.peerId!=b.peerId||a.role!=b.role||a.invited!=b.invited)return Reconciliation::RosterMismatch;
    }
    auto candidate=ledger;
    if(!candidate.append(event)||!group_keys::GroupKeyManager::matchesSignedLedger(after,candidate.state()))
        return Reconciliation::LedgerRejected;
    ledger=std::move(candidate);return Reconciliation::Accepted;
}
}
