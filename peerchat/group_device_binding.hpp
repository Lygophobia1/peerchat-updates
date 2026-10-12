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
struct SignedIdentity {
    std::uint64_t account=0;
    std::string device;
    Blob publicKey,signature;
};
// Public possession evidence only, never a group membership grant or credential.
inline Blob identityRegistration(std::uint64_t account,const Profile& device){
    auto input=identityInput(account,device.peerId,device.publicKey);
    auto signature=signDevicePayload(input,device);
    protocol::check(signature.size()==64&&verifyDevicePayload(input,signature,device.publicKey));
    protocol::Writer writer(protocol::GroupEvent);writer.data=std::move(input);writer.bytes(signature);return writer.data;
}
inline SignedIdentity readIdentityRegistration(const Blob& wire){
    protocol::check(wire.size()<=512);
    protocol::Reader reader(wire);protocol::check(reader.type()==protocol::GroupEvent);
    protocol::check(reader.string()=="PeerChat/group-device-identity/v1");
    SignedIdentity result;result.account=reader.number();result.device=reader.string();result.publicKey=reader.bytes();
    auto signedEnd=reader.cursor;result.signature=reader.bytes();reader.end();
    auto input=identityInput(result.account,result.device,result.publicKey);
    protocol::check(result.signature.size()==64&&input==Blob(wire.begin(),wire.begin()+signedEnd)&&
        verifyDevicePayload(input,result.signature,result.publicKey));return result;
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
struct SignedApproval {
    std::string serverGroup;
    std::uint64_t generation=0,account=0;
    Blob actorPublicKey,targetPublicKey,targetIdentitySignature;
    GroupEvent event;
};
// Verifies public cryptographic evidence only. Callers must separately admit
// authenticated current account/device mappings and exact server roster epoch.
// Parsing never issues a grant, mutates a ledger or releases a group key.
inline SignedApproval readApproval(const Blob& wire){
    protocol::check(wire.size()<=4096);
    protocol::Reader reader(wire);protocol::check(reader.type()==protocol::GroupEvent);
    auto text=[&](std::size_t limit){auto value=reader.string();protocol::check(value.size()<=limit);return value;};
    auto bytes=[&](std::size_t limit){auto value=reader.bytes();protocol::check(value.size()<=limit);return value;};
    protocol::check(text(64)=="PeerChat/group-device-approval/v1");
    SignedApproval result;result.serverGroup=text(32);auto legacy=text(32);
    result.generation=reader.number();result.account=reader.number();
    auto actor=text(64);result.actorPublicKey=bytes(72);
    auto target=text(64);result.targetPublicKey=bytes(72);result.targetIdentitySignature=bytes(64);
    auto eventWire=bytes(2048);auto signedEnd=reader.cursor;auto signature=bytes(64);reader.end();
    protocol::check(protocol::id(result.serverGroup,32)&&protocol::id(legacy,32)&&protocol::id(actor)&&protocol::id(target)&&
        actor!=target&&result.generation>0&&result.generation<INT64_MAX&&result.account>0&&result.account<=INT64_MAX&&
        result.actorPublicKey.size()==72&&result.targetPublicKey.size()==72&&Storage::publicKeyId(result.actorPublicKey)==actor);
    protocol::check(verifyDevicePayload(identityInput(result.account,target,result.targetPublicKey),result.targetIdentitySignature,result.targetPublicKey));
    protocol::check(verifyDevicePayload(Blob(wire.begin(),wire.begin()+signedEnd),signature,result.actorPublicKey));
    result.event=decodeGroupEvent(eventWire);const auto& event=result.event;
    protocol::check(event.groupId==legacy&&event.logicalOrder>0&&event.actorAccountId==result.account&&
        event.subjectAccountId==result.account&&event.actorPeerId==actor&&event.subjectPeerId==target&&
        event.subjectName.empty()&&event.message.empty()&&verifyGroupEventSignature(event,result.actorPublicKey));
    protocol::check((event.kind==GroupEventKind::AuthorizeDevice&&event.value==L"PeerChat/group-device-authorization/v1"&&event.subjectPublicKey==result.targetPublicKey)||
        (event.kind==GroupEventKind::RevokeDevice&&event.value==L"PeerChat/group-device-revocation/v1"&&event.subjectPublicKey.empty()));
    return result;
}
}
