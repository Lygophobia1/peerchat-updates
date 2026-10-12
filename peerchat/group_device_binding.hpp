#pragma once
#include "group.hpp"
#include "protocol.hpp"

namespace peerchat::group_devices {
inline Blob identityInput(std::uint64_t account,const std::string& device,const Blob& publicKey){
    if(!account||account>INT64_MAX||!protocol::id(device)||publicKey.size()!=72||Storage::publicKeyId(publicKey)!=device)
        throw std::runtime_error("Invalid group device identity binding");
    protocol::Writer writer(protocol::GroupEvent);writer.string("PeerChat/group-device-identity/v1");
    writer.number(account);writer.string(device);writer.bytes(publicKey);return writer.data;
}
// Signing-only provider operations. Neither identity's private key is serialized.
inline Blob identityProof(std::uint64_t account,const Profile& device){
    return signDevicePayload(identityInput(account,device.peerId,device.publicKey),device);
}
inline Blob approvalProof(const std::string& serverGroup,std::uint64_t generation,const GroupEvent& event,
                          const Blob& targetPublicKey,const Blob& targetIdentitySignature,const Profile& actor){
    if(!protocol::id(serverGroup,32)||!generation||generation>=INT64_MAX||
       !protocol::id(event.groupId,32)||event.actorAccountId!=event.subjectAccountId||
       event.actorPeerId!=actor.peerId||event.actorPeerId==event.subjectPeerId||
       (event.kind!=GroupEventKind::AuthorizeDevice&&event.kind!=GroupEventKind::RevokeDevice)||
       !event.subjectName.empty()||!event.message.empty()||!verifyGroupEventSignature(event,actor.publicKey)||
       !verifyDevicePayload(identityInput(event.subjectAccountId,event.subjectPeerId,targetPublicKey),targetIdentitySignature,targetPublicKey))
        throw std::runtime_error("Invalid signed group device approval");
    const auto purpose=event.kind==GroupEventKind::AuthorizeDevice?L"PeerChat/group-device-authorization/v1":L"PeerChat/group-device-revocation/v1";
    if(event.value!=purpose||(event.kind==GroupEventKind::AuthorizeDevice?event.subjectPublicKey!=targetPublicKey:!event.subjectPublicKey.empty()))
        throw std::runtime_error("Group device approval purpose mismatch");
    protocol::Writer writer(protocol::GroupEvent);writer.string("PeerChat/group-device-approval/v1");
    writer.string(serverGroup);writer.string(event.groupId);writer.number(generation);writer.number(event.actorAccountId);
    writer.string(actor.peerId);writer.bytes(actor.publicKey);writer.string(event.subjectPeerId);writer.bytes(targetPublicKey);
    writer.bytes(targetIdentitySignature);writer.bytes(encodeGroupEvent(event));
    auto signature=signDevicePayload(writer.data,actor);writer.bytes(signature);return writer.data;
}
}
