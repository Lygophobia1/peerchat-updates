#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include "group.hpp"
#include "protocol.hpp"
#include <algorithm>
#include <stdexcept>

namespace peerchat {
namespace {
struct Algorithm { BCRYPT_ALG_HANDLE value{};~Algorithm(){if(value)BCryptCloseAlgorithmProvider(value,0);} };
struct Key { BCRYPT_KEY_HANDLE value{};Key()=default;Key(const Key&)=delete;Key& operator=(const Key&)=delete;Key(Key&& other)noexcept:value(other.value){other.value=nullptr;}~Key(){if(value)BCryptDestroyKey(value);} };
void requireNt(NTSTATUS status){if(status<0)throw std::runtime_error("Group signature operation failed");}
std::string hex(const Blob& value){static const char digits[]="0123456789abcdef";std::string result;result.reserve(value.size()*2);for(auto byte:value){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}return result;}
Blob sha256(const Blob& value){Algorithm algorithm;requireNt(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_SHA256_ALGORITHM,nullptr,0));DWORD objectBytes=0,resultBytes=0;requireNt(BCryptGetProperty(algorithm.value,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectBytes),sizeof(objectBytes),&resultBytes,0));Blob object(objectBytes),digest(32);BCRYPT_HASH_HANDLE hash{};requireNt(BCryptCreateHash(algorithm.value,&hash,object.data(),static_cast<ULONG>(object.size()),nullptr,0,0));NTSTATUS status=BCryptHashData(hash,const_cast<PUCHAR>(value.data()),static_cast<ULONG>(value.size()),0);if(status>=0)status=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0);BCryptDestroyHash(hash);requireNt(status);return digest;}
Key importKey(const Blob& blob,bool privateKey){Algorithm algorithm;requireNt(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_ECDSA_P256_ALGORITHM,nullptr,0));Key key;requireNt(BCryptImportKeyPair(algorithm.value,nullptr,privateKey?BCRYPT_ECCPRIVATE_BLOB:BCRYPT_ECCPUBLIC_BLOB,&key.value,const_cast<PUCHAR>(blob.data()),static_cast<ULONG>(blob.size()),0));return key;}
bool validName(const std::wstring& value){return !value.empty()&&value.size()<=80&&std::none_of(value.begin(),value.end(),[](wchar_t c){return c<32;});}
bool validPeer(const std::string& value){return protocol::id(value);}

void writeUnsigned(protocol::Writer& writer,const GroupEvent& event){
    writer.string(event.groupId);writer.number(static_cast<unsigned char>(event.kind));writer.number(event.logicalOrder);writer.number(event.createdMilliseconds);
    writer.number(event.actorAccountId);writer.string(event.actorPeerId);writer.number(event.subjectAccountId);writer.string(event.subjectPeerId);
    writer.text(event.subjectName);writer.bytes(event.subjectPublicKey);writer.text(event.value);writer.text(event.message);
}
Blob unsignedPayload(const GroupEvent& event){protocol::Writer writer(protocol::GroupEvent);writeUnsigned(writer,event);return writer.data;}
bool sameEvent(const GroupEvent& left,const GroupEvent& right){return left.eventId==right.eventId&&unsignedPayload(left)==unsignedPayload(right)&&left.signature==right.signature;}
bool signedBy(const GroupEvent& event,const GroupMember& member){return event.actorAccountId==member.accountId&&event.actorPeerId==member.peerId&&verifyGroupEventSignature(event,member.publicKey);}
bool before(const GroupEvent& left,const GroupEvent& right){return left.logicalOrder!=right.logicalOrder?left.logicalOrder<right.logicalOrder:left.eventId<right.eventId;}
}

std::string newGroupId(){return Storage::newMessageId();}

void signGroupEvent(GroupEvent& event,const Profile& device){
    if(event.actorPeerId!=device.peerId||Storage::publicKeyId(device.publicKey)!=device.peerId)throw std::runtime_error("Group actor does not match this device");
    Blob digest=sha256(unsignedPayload(event));event.eventId=hex(digest);Key key=importKey(device.privateKey,true);ULONG bytes=0;requireNt(BCryptSignHash(key.value,nullptr,digest.data(),static_cast<ULONG>(digest.size()),nullptr,0,&bytes,0));event.signature.resize(bytes);requireNt(BCryptSignHash(key.value,nullptr,digest.data(),static_cast<ULONG>(digest.size()),event.signature.data(),bytes,&bytes,0));event.signature.resize(bytes);
}

bool verifyGroupEventSignature(const GroupEvent& event,const Blob& publicKey){
    try{Blob digest=sha256(unsignedPayload(event));if(event.eventId!=hex(digest)||Storage::publicKeyId(publicKey)!=event.actorPeerId)return false;Key key=importKey(publicKey,false);return BCryptVerifySignature(key.value,nullptr,digest.data(),static_cast<ULONG>(digest.size()),const_cast<PUCHAR>(event.signature.data()),static_cast<ULONG>(event.signature.size()),0)>=0;}catch(...){return false;}
}

Blob encodeGroupEvent(const GroupEvent& event){protocol::Writer writer(protocol::GroupEvent);writer.string(event.eventId);writeUnsigned(writer,event);writer.bytes(event.signature);return writer.data;}
GroupEvent decodeGroupEvent(const Blob& packet){protocol::Reader reader(packet);protocol::check(reader.type()==protocol::GroupEvent);GroupEvent event;event.eventId=reader.string();event.groupId=reader.string();auto kind=reader.number();protocol::check(kind>=1&&kind<=9);event.kind=static_cast<GroupEventKind>(kind);event.logicalOrder=reader.number();event.createdMilliseconds=reader.number();event.actorAccountId=reader.number();event.actorPeerId=reader.string();event.subjectAccountId=reader.number();event.subjectPeerId=reader.string();event.subjectName=reader.text();event.subjectPublicKey=reader.bytes();event.value=reader.text();event.message=reader.text();event.signature=reader.bytes();reader.end();return event;}

bool GroupLedger::contains(const std::string& eventId)const{return std::any_of(events_.begin(),events_.end(),[&](const GroupEvent& event){return event.eventId==eventId;});}

bool GroupLedger::append(const GroupEvent& event){
    auto duplicate=std::find_if(events_.begin(),events_.end(),[&](const GroupEvent& existing){return existing.eventId==event.eventId;});
    if(duplicate!=events_.end()){if(!sameEvent(*duplicate,event))throw std::runtime_error("Conflicting group event ID");return rebuild(event.eventId);}
    // Keep well-formed but not-yet-authorized events so causal predecessors arriving later can
    // make them valid. The deterministic rebuild still excludes them from visible group state.
    events_.push_back(event);return rebuild(event.eventId);
}

bool GroupLedger::rebuild(const std::string& requiredEventId){
    std::sort(events_.begin(),events_.end(),before);GroupState next;std::set<std::string> accepted;
    for(const auto& event:events_){
        if(!protocol::id(event.groupId,32)||!protocol::id(event.eventId)||!validPeer(event.actorPeerId)||event.logicalOrder==0||event.signature.empty())continue;
        bool allowed=false;
        if(event.kind==GroupEventKind::Create){
            allowed=!next.created&&event.logicalOrder==1&&event.actorAccountId>0&&event.subjectAccountId==event.actorAccountId&&event.subjectPeerId==event.actorPeerId&&validName(event.value)&&!event.subjectPublicKey.empty()&&verifyGroupEventSignature(event,event.subjectPublicKey);
            if(allowed){next.created=true;next.groupId=event.groupId;next.name=event.value;next.ownerAccountId=event.actorAccountId;next.members[event.actorAccountId]={event.actorAccountId,event.subjectName,event.actorPeerId,event.subjectPublicKey,GroupRole::Owner};}
        }else if(next.created&&event.groupId==next.groupId){
            auto actor=next.members.find(event.actorAccountId);auto invitation=next.invitations.find(event.actorAccountId);
            bool actorMember=actor!=next.members.end()&&signedBy(event,actor->second);bool invitedActor=invitation!=next.invitations.end()&&signedBy(event,invitation->second);
            if(event.kind==GroupEventKind::Rename){allowed=actorMember&&actor->second.role>=GroupRole::Admin&&validName(event.value);if(allowed)next.name=event.value;}
            else if(event.kind==GroupEventKind::Invite){allowed=actorMember&&actor->second.role>=GroupRole::Admin&&event.subjectAccountId>0&&event.subjectAccountId!=event.actorAccountId&&next.members.count(event.subjectAccountId)==0&&next.invitations.count(event.subjectAccountId)==0&&validPeer(event.subjectPeerId)&&!event.subjectPublicKey.empty()&&Storage::publicKeyId(event.subjectPublicKey)==event.subjectPeerId&&!event.subjectName.empty();if(allowed)next.invitations[event.subjectAccountId]={event.subjectAccountId,event.subjectName,event.subjectPeerId,event.subjectPublicKey,GroupRole::Member};}
            else if(event.kind==GroupEventKind::Accept){allowed=invitedActor&&event.subjectAccountId==event.actorAccountId&&event.subjectPeerId==event.actorPeerId;if(allowed){next.members[event.actorAccountId]=invitation->second;next.invitations.erase(invitation);}}
            else if(event.kind==GroupEventKind::Decline){allowed=invitedActor&&event.subjectAccountId==event.actorAccountId&&event.subjectPeerId==event.actorPeerId;if(allowed)next.invitations.erase(invitation);}
            else if(event.kind==GroupEventKind::Leave){allowed=actorMember&&actor->second.role!=GroupRole::Owner;if(allowed)next.members.erase(actor);}
            else if(event.kind==GroupEventKind::Remove){auto subject=next.members.find(event.subjectAccountId);allowed=actorMember&&subject!=next.members.end()&&subject->second.role!=GroupRole::Owner&&event.subjectAccountId!=event.actorAccountId&&(actor->second.role==GroupRole::Owner||(actor->second.role==GroupRole::Admin&&subject->second.role==GroupRole::Member));if(allowed)next.members.erase(subject);}
            else if(event.kind==GroupEventKind::SetRole){auto subject=next.members.find(event.subjectAccountId);allowed=actorMember&&actor->second.role==GroupRole::Owner&&subject!=next.members.end()&&subject->second.role!=GroupRole::Owner&&event.subjectAccountId!=event.actorAccountId&&(event.value==L"admin"||event.value==L"member");if(allowed)subject->second.role=event.value==L"admin"?GroupRole::Admin:GroupRole::Member;}
            else if(event.kind==GroupEventKind::Message){allowed=actorMember&&!event.message.empty()&&event.message.size()<=16384;if(allowed)next.messages.push_back(event);}
        }
        if(allowed){next.maxLogicalOrder=std::max(next.maxLogicalOrder,event.logicalOrder);accepted.insert(event.eventId);}
    }
    state_=std::move(next);accepted_=std::move(accepted);return requiredEventId.empty()||accepted_.count(requiredEventId)!=0;
}
}
