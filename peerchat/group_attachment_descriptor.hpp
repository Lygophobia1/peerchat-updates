#pragma once
#include "group_attachment_blob.hpp"

namespace peerchat::group_attachments {
struct PublicDescriptor {
 std::string attachment,group,message,sender;std::uint64_t account=0,epoch=0,plainSize=0,cipherSize=0;Blob cipherDigest,identity,wire;
 bool matches(const Manifest& m)const{return attachment==m.binding.attachment&&group==m.binding.group&&sender==m.binding.sender&&account==m.binding.account&&epoch==m.binding.epoch&&plainSize==m.size&&cipherSize==m.cipherSize()&&cipherDigest==Blob(m.cipherDigest.begin(),m.cipherDigest.end());}
};
inline PublicDescriptor parseDescriptor(const Blob& wire){
 protocol::check(wire.size()<=2048);protocol::Reader r(wire);protocol::check(r.type()==protocol::GroupEvent&&r.string()=="PeerChat/group-cache-upload/v1");PublicDescriptor d;
 d.attachment=r.string();d.group=r.string();d.message=r.string();d.sender=r.string();d.account=r.number();d.epoch=r.number();d.plainSize=r.number();d.cipherSize=r.number();d.cipherDigest=r.bytes();d.identity=r.bytes();auto signedEnd=r.cursor;auto signature=r.bytes();r.end();
 protocol::check(protocol::id(d.attachment,32)&&protocol::id(d.group,32)&&protocol::id(d.message,32)&&protocol::id(d.sender)&&d.account&&d.account<=INT64_MAX&&d.epoch&&d.epoch<=INT64_MAX&&d.plainSize<=transfer::MaxCachedFileBytes&&d.cipherSize==HeaderBytes+d.plainSize+std::max<std::uint64_t>(1,(d.plainSize+BlockBytes-1)/BlockBytes)*TagBytes&&d.cipherDigest.size()==32&&d.identity.size()==72&&Storage::publicKeyId(d.identity)==d.sender&&verifyDevicePayload(Blob(wire.begin(),wire.begin()+signedEnd),signature,d.identity));
 d.wire=wire;return d;
}
// Public upload evidence only. The original filename, plaintext hash, key and
// nonce prefix remain exclusively in protected local/encrypted group metadata.
inline Blob uploadDescriptor(const Manifest& manifest,const std::string& message,const Profile& identity){
 manifest.validate();protocol::check(protocol::id(message,32)&&manifest.binding.sender==identity.peerId&&identity.publicKey.size()==72&&Storage::publicKeyId(identity.publicKey)==identity.peerId);
 protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/group-cache-upload/v1");w.string(manifest.binding.attachment);w.string(manifest.binding.group);w.string(message);w.string(manifest.binding.sender);
 w.number(manifest.binding.account);w.number(manifest.binding.epoch);w.number(manifest.size);w.number(manifest.cipherSize());w.bytes(Blob(manifest.cipherDigest.begin(),manifest.cipherDigest.end()));w.bytes(identity.publicKey);
 auto signature=signDevicePayload(w.data,identity);protocol::check(signature.size()==64&&verifyDevicePayload(w.data,signature,identity.publicKey));w.bytes(signature);protocol::check(w.data.size()<=2048);return w.data;
}
}
