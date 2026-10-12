#pragma once
#include "group.hpp"
#include "protocol.hpp"
#include "group_device_binding.hpp"

namespace peerchat::group_keys {
struct PublicDeviceApproval {std::uint64_t appliedGeneration=0;Blob proof;};
// Validates signed evidence in an authenticated snapshot only. This never
// admits an event, trusts an authorizer, persists a receipt or releases a key.
inline void verifyDeviceApprovals(const std::string& server,const std::string& legacy,std::uint64_t generation,const std::vector<PublicDeviceApproval>& rows){
    protocol::check(protocol::id(server,32)&&protocol::id(legacy,32)&&generation>0&&generation<=INT64_MAX&&rows.size()<=32);
    std::uint64_t previous=0;std::set<std::string> ids;
    for(const auto& row:rows){auto proof=group_devices::readApproval(row.proof);
        protocol::check(proof.serverGroup==server&&proof.event.groupId==legacy&&row.appliedGeneration==proof.generation+1&&
            row.appliedGeneration<=generation&&row.appliedGeneration>previous&&ids.insert(proof.event.eventId).second);previous=row.appliedGeneration;
    }
}
// Immutable signed creation evidence is not current membership, a history grant,
// or permission to release keys. Current roster and full ledger checks remain.
inline GroupEvent verifiedCreation(const std::string& server,const std::string& legacy,const Blob& wire){
    protocol::check(protocol::id(server,32)&&protocol::id(legacy,32)&&!wire.empty()&&wire.size()<=2048);
    auto event=decodeGroupEvent(wire);
    protocol::check(event.groupId==legacy&&event.kind==GroupEventKind::Create&&event.logicalOrder==1&&
        event.actorAccountId==event.subjectAccountId&&event.actorPeerId==event.subjectPeerId&&event.message.empty());
    GroupLedger root;protocol::check(root.append(event)&&root.state().created);
    auto domain=std::string("PeerChat group roster v1|")+legacy+"|"+event.actorPeerId;
    protocol::check(Storage::publicKeyId(Blob(domain.begin(),domain.end())).substr(0,32)==server);
    return event;
}
struct PublicRosterEvidence {Blob root;std::vector<PublicDeviceApproval> approvals;};
inline PublicRosterEvidence readRosterEvidence(const Blob& wire,const std::string& server,const std::string& legacy,std::uint64_t generation){
    protocol::check(wire.size()<=135168);protocol::Reader r(wire);protocol::check(r.type()==protocol::GroupEvent&&r.string()=="PeerChat/group-roster-evidence/v1");
    PublicRosterEvidence evidence;evidence.root=r.bytes();protocol::check(evidence.root.size()<=2048);
    auto count=r.number();protocol::check(count<=32);
    for(std::uint64_t n=0;n<count;++n){PublicDeviceApproval row;row.appliedGeneration=r.number();row.proof=r.bytes();protocol::check(row.proof.size()<=4096);evidence.approvals.push_back(std::move(row));}
    r.end();if(!evidence.root.empty())verifiedCreation(server,legacy,evidence.root);
    protocol::check(evidence.approvals.empty()||!evidence.root.empty());verifyDeviceApprovals(server,legacy,generation,evidence.approvals);return evidence;
}
}
