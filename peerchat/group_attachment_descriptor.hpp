#pragma once
#include "group_attachment_blob.hpp"

namespace peerchat::group_attachments {
// Public upload evidence only. The original filename, plaintext hash, key and
// nonce prefix remain exclusively in protected local/encrypted group metadata.
inline Blob uploadDescriptor(const Manifest& manifest,const std::string& message,const Profile& identity){
 manifest.validate();protocol::check(protocol::id(message,32)&&manifest.binding.sender==identity.peerId&&identity.publicKey.size()==72&&Storage::publicKeyId(identity.publicKey)==identity.peerId);
 protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/group-cache-upload/v1");w.string(manifest.binding.attachment);w.string(manifest.binding.group);w.string(message);w.string(manifest.binding.sender);
 w.number(manifest.binding.account);w.number(manifest.binding.epoch);w.number(manifest.size);w.number(manifest.cipherSize());w.bytes(Blob(manifest.cipherDigest.begin(),manifest.cipherDigest.end()));w.bytes(identity.publicKey);
 auto signature=signDevicePayload(w.data,identity);protocol::check(signature.size()==64&&verifyDevicePayload(w.data,signature,identity.publicKey));w.bytes(signature);protocol::check(w.data.size()<=2048);return w.data;
}
}
