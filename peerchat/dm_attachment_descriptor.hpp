#pragma once
#include "group_attachment_metadata.hpp"
#include "mailbox_protocol.hpp"

namespace peerchat::dm_attachments {
using group_attachments::Manifest;
using group_attachments::SecretRecord;
struct Descriptor {
 std::string id,message,sender,relationship,audienceDigest;std::uint64_t account=0,recipient=0,cipherSize=0;
 Blob cipherDigest,identity,wire;
 bool matches(const Manifest& m)const{return m.binding.direct&&id==m.binding.attachment&&sender==m.binding.sender&&account==m.binding.account&&recipient==m.binding.recipientAccount&&relationship==m.binding.relationship&&audienceDigest==m.binding.audienceDigest&&cipherSize==m.cipherSize()&&cipherDigest==Blob(m.cipherDigest.begin(),m.cipherDigest.end());}
};
inline Descriptor parseDescriptor(const Blob& wire){
 protocol::check(wire.size()<=2048);protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/dm-cache-upload/v1");Descriptor d;
 d.id=r.string();d.message=r.string();d.sender=r.string();d.account=r.number();d.recipient=r.number();d.relationship=r.string();d.audienceDigest=r.string();auto codec=r.number();d.cipherSize=r.number();d.cipherDigest=r.bytes();d.identity=r.bytes();auto end=r.cursor;auto signature=r.bytes();r.end();
 protocol::check(protocol::id(d.id,32)&&protocol::id(d.message,32)&&protocol::id(d.sender)&&mailbox::account(d.account)&&mailbox::account(d.recipient)&&d.account!=d.recipient&&protocol::id(d.relationship,32)&&protocol::id(d.audienceDigest)&&codec==1&&d.cipherSize>=36&&d.cipherSize<=transfer::MaxCacheCipherBytes&&d.cipherDigest.size()==32&&d.identity.size()==72&&Storage::publicKeyId(d.identity)==d.sender&&signature.size()==64&&verifyDevicePayload(Blob(wire.begin(),wire.begin()+end),signature,d.identity));d.wire=wire;return d;
}
inline Blob descriptor(const Manifest& m,const std::string& message,const Profile& identity){
 m.validate();protocol::check(m.binding.direct&&m.binding.sender==identity.peerId&&Storage::publicKeyId(identity.publicKey)==identity.peerId&&protocol::id(message,32));
 protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/dm-cache-upload/v1");w.string(m.binding.attachment);w.string(message);w.string(m.binding.sender);w.number(m.binding.account);w.number(m.binding.recipientAccount);w.string(m.binding.relationship);w.string(m.binding.audienceDigest);w.number(1);w.number(m.cipherSize());w.bytes(Blob(m.cipherDigest.begin(),m.cipherDigest.end()));w.bytes(identity.publicKey);w.bytes(signDevicePayload(w.data,identity));parseDescriptor(w.data);return w.data;
}
struct Audience {std::string relationship;mailbox::RecipientBinding source;std::vector<mailbox::RecipientBinding> recipients;Blob wire;};
inline Audience parseAudience(const Blob& wire,std::uint64_t sourceAccount,const std::string& sourceDevice,std::uint64_t recipientAccount){
 protocol::check(wire.size()<=32768);protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/dm-cache-audience/v1");Audience a;a.relationship=r.string();a.source=mailbox::parseBinding(r.bytes());auto count=r.number();protocol::check(protocol::id(a.relationship,32)&&count>0&&count<=32&&sourceAccount!=recipientAccount&&a.source.accountId==sourceAccount&&a.source.device==sourceDevice&&a.source.storageAllowed);
 std::string previous;while(count--){auto b=mailbox::parseBinding(r.bytes());protocol::check(b.accountId==recipientAccount&&b.storageAllowed&&b.device>previous&&b.device!=sourceDevice);previous=b.device;a.recipients.push_back(std::move(b));}r.end();a.wire=wire;return a;
}
// The caller must verify each binding's signature against authenticated current
// account/device evidence. Parsing alone is not enrollment or trust promotion.
inline void verifyAudience(const Audience& a,const std::function<Blob(std::uint64_t,const std::string&)>& identity){
 auto verify=[&](const auto& b){auto key=identity(b.accountId,b.device);protocol::check(mailbox::verifyBinding(b,b.accountId,b.device,key,verifyDevicePayload,Storage::publicKeyId));};verify(a.source);for(const auto& b:a.recipients)verify(b);
}
}
