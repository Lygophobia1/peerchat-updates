#pragma once
#include "group_attachment_metadata.hpp"
#include "group_message_envelope.hpp"
#include "attachment_thumbnail.hpp"

namespace peerchat::group_attachments {
// Additive encrypted body only. Outer PCGE encryption/signature, roster gates,
// replay/durable history ownership remain in the existing MessageLayer.
enum class Presentation:std::uint64_t { File=1, Picture=2 };
struct Body {
 std::uint64_t time=0,order=0;Presentation presentation=Presentation::File;
 std::string contentType="application/octet-stream";Manifest manifest;Blob thumbnail={};
 void validate()const{
  manifest.validate();protocol::check(time&&time<=INT64_MAX&&order&&order<=INT64_MAX);
  protocol::check(attachment_thumbnails::valid(thumbnail));
  protocol::check(presentation==Presentation::File?contentType=="application/octet-stream":presentation==Presentation::Picture&&(contentType=="image/png"||contentType=="image/jpeg"||contentType=="image/webp"||contentType=="image/gif"||contentType=="image/bmp"));
 }
};
inline SecretRecord encodeBody(const Body& body){
 body.validate();protocol::Writer w(protocol::FileTransfer);
 try{w.string(body.thumbnail.empty()?"PeerChat.GroupAttachment.v1":"PeerChat.GroupAttachment.v2");w.number(body.time);w.number(body.order);w.number(static_cast<std::uint64_t>(body.presentation));w.string(body.contentType);writePrivateManifest(w,body.manifest);if(!body.thumbnail.empty())w.bytes(body.thumbnail);protocol::check(w.data.size()<=4096);return SecretRecord(std::move(w.data));}
 catch(...){platform::group_crypto::wipe(w.data.data(),w.data.size());throw;}
}
inline Body decodeBody(const Blob& bytes){
 protocol::check(bytes.size()<=4096);protocol::Reader r(bytes);protocol::check(r.type()==protocol::FileTransfer);auto domain=r.string();protocol::check(domain=="PeerChat.GroupAttachment.v1"||domain=="PeerChat.GroupAttachment.v2");
 auto time=r.number(),order=r.number(),presentation=r.number();protocol::check(presentation==1||presentation==2);auto contentType=r.string();Body body{time,order,static_cast<Presentation>(presentation),std::move(contentType),readPrivateManifest(r)};if(domain=="PeerChat.GroupAttachment.v2"){body.thumbnail=r.bytes();protocol::check(!body.thumbnail.empty());}r.end();body.validate();return body;
}
inline bool validBody(const Blob& bytes,const group_messages::Envelope& envelope){
 try{auto body=decodeBody(bytes);const auto& b=body.manifest.binding;return b.group==envelope.groupId&&b.epoch==envelope.rosterGeneration&&b.epoch==envelope.keyGeneration&&b.account==envelope.senderAccount&&b.sender==envelope.senderDevice;}catch(...){return false;}
}
// Presentation hints never replace bounded image decoding or MIME/magic checks.
// Cache ciphertext completion must not be presented as plaintext verification.
}
