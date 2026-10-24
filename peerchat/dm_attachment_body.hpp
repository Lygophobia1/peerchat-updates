#pragma once
#include "dm_attachment_descriptor.hpp"
#include "attachment_thumbnail.hpp"

namespace peerchat::dm_attachments {
// Identity resolves authenticated original device identities. Sender validation
// resolves the full frozen audience; receiving resolves only source + addressed
// local recipient. Current checks source + this local key/relationship.
// Neither callback may enroll a key,
// mutate history, ACK a message, or promote trust. Empty evidence fails closed.
struct Evidence {
 std::function<Blob(std::uint64_t,const std::string&)> identity;
 std::function<bool(const mailbox::RecipientBinding&,const std::string&,std::uint64_t)> current;
};
struct Body {
 std::uint64_t time=0,order=0;std::string contentType="application/octet-stream";
 Manifest manifest;Blob descriptor,audience,mailboxBinding;Blob thumbnail={};
};
enum class AudienceValidation { AllRecipients, AddressedRecipient };
inline void validateBody(const Body& body,const mailbox::Header& h,const Evidence* evidence=nullptr,AudienceValidation audienceValidation=AudienceValidation::AllRecipients){
 mailbox::validate(h);body.manifest.validate();const auto& b=body.manifest.binding;
 protocol::check(attachment_thumbnails::valid(body.thumbnail));
 protocol::check(b.direct&&body.time&&body.time<=INT64_MAX&&body.order==h.logicalOrder&&body.mailboxBinding==mailbox::headerPayload(h));
 protocol::check(body.contentType=="application/octet-stream"||body.contentType=="image/png"||body.contentType=="image/jpeg"||body.contentType=="image/webp"||body.contentType=="image/gif"||body.contentType=="image/bmp");
 auto d=parseDescriptor(body.descriptor);auto a=parseAudience(body.audience,d.account,d.sender,d.recipient);
 protocol::check(d.matches(body.manifest)&&d.message==h.messageId&&d.account==h.senderAccount&&d.sender==h.senderDevice&&d.recipient==h.recipientAccount&&a.relationship==d.relationship&&Storage::publicKeyId(body.audience)==d.audienceDigest);
 auto recipient=std::find_if(a.recipients.begin(),a.recipients.end(),[&](const auto& r){return r.device==h.recipientDevice;});
 protocol::check(recipient!=a.recipients.end()&&recipient->keyId==h.keyId&&recipient->generation==h.keyGeneration);
 if(evidence){
  protocol::check(bool(evidence->identity)&&bool(evidence->current));
  protocol::check(evidence->identity(d.account,d.sender)==d.identity);
  if(audienceValidation==AudienceValidation::AllRecipients)verifyAudience(a,evidence->identity);
  else {
   // The authenticated sender signed the immutable full audience digest. A
   // receiver does not enroll or authorize siblings; independently authenticate
   // the source and its own addressed binding, retaining every structural check.
   for(const auto* binding:{&a.source,&*recipient}){
    auto key=evidence->identity(binding->accountId,binding->device);
    protocol::check(mailbox::verifyBinding(*binding,binding->accountId,binding->device,key,verifyDevicePayload,Storage::publicKeyId));
   }
  }
  protocol::check(evidence->current(a.source,a.relationship,d.recipient));
  protocol::check(evidence->current(*recipient,a.relationship,d.account));
 }
}
inline void validateReceivedBody(const Body& body,const mailbox::Header& h,const Evidence& evidence){
 validateBody(body,h,&evidence,AudienceValidation::AddressedRecipient);
}
inline SecretRecord encodeBody(const Body& body,const mailbox::Header& header){
 validateBody(body,header);protocol::Writer w(protocol::FileTransfer);
 try{
  w.string(body.thumbnail.empty()?"PeerChat/dm-attachment-private/v1":"PeerChat/dm-attachment-private/v2");w.number(body.time);w.number(body.order);w.string(body.contentType);w.bytes(body.mailboxBinding);w.bytes(body.descriptor);w.bytes(body.audience);
  const auto& m=body.manifest;w.string(m.name);w.number(m.size);w.bytes(m.key);w.bytes(m.prefix);w.bytes(Blob(m.plainDigest.begin(),m.plainDigest.end()));
  if(!body.thumbnail.empty())w.bytes(body.thumbnail);
  protocol::check(w.data.size()<=mailbox::MaxBody);return SecretRecord(std::move(w.data));
 }catch(...){platform::group_crypto::wipe(w.data.data(),w.data.size());throw;}
}
// Parsing and validation are deliberately free of delivery/storage side effects.
namespace detail {
inline Body decode(const Blob& bytes,const mailbox::Header& header,const Evidence* evidence){
 protocol::check(bytes.size()<=mailbox::MaxBody);protocol::Reader r(bytes);protocol::check(r.type()==protocol::FileTransfer);auto domain=r.string();protocol::check(domain=="PeerChat/dm-attachment-private/v1"||domain=="PeerChat/dm-attachment-private/v2");Body body;
 body.time=r.number();body.order=r.number();body.contentType=r.string();body.mailboxBinding=r.bytes();body.descriptor=r.bytes();body.audience=r.bytes();
 auto d=parseDescriptor(body.descriptor);auto& m=body.manifest;m.binding.attachment=d.id;m.binding.sender=d.sender;m.binding.account=d.account;m.binding.direct=true;m.binding.recipientAccount=d.recipient;m.binding.relationship=d.relationship;m.binding.audienceDigest=d.audienceDigest;
 m.name=r.string();m.size=r.number();m.key=r.bytes();m.prefix=r.bytes();auto digest=r.bytes();protocol::check(digest.size()==32);std::copy(digest.begin(),digest.end(),m.plainDigest.begin());std::copy(d.cipherDigest.begin(),d.cipherDigest.end(),m.cipherDigest.begin());if(domain=="PeerChat/dm-attachment-private/v2"){body.thumbnail=r.bytes();protocol::check(!body.thumbnail.empty());}r.end();validateBody(body,header,evidence);return body;
}
}
inline Body decodeBody(const Blob& bytes,const mailbox::Header& header,const Evidence& evidence){return detail::decode(bytes,header,&evidence);}
// Protected LOCAL restart decoding only: no current authorization, enrollment,
// history provenance or network admission is established by this function.
// Call validateBody(body,header,&currentEvidence) before network operations.
inline Body decodeStoredBody(const Blob& bytes,const mailbox::Header& header){return detail::decode(bytes,header,nullptr);}
// The normal mailbox HPKE envelope/signature authenticates the private body.
// Caller retains the current recipient-key operation lease through this call.
inline Body openBody(const mailbox::Envelope& envelope,const mailbox::RecipientBinding& recipient,const Evidence& evidence,const std::shared_ptr<platform::RecipientKeyHandle>& handle,platform::HpkeProvider& hpke,std::uint64_t now){
 protocol::check(bool(evidence.identity)&&bool(evidence.current));auto identity=evidence.identity(envelope.header.senderAccount,envelope.header.senderDevice);
 SecretRecord plain(mailbox::open(envelope,recipient,envelope.header.senderAccount,identity,verifyDevicePayload,Storage::publicKeyId,handle,hpke,now));
 auto body=decodeStoredBody(plain.bytes,envelope.header);validateReceivedBody(body,envelope.header,evidence);auto a=parseAudience(body.audience,envelope.header.senderAccount,envelope.header.senderDevice,envelope.header.recipientAccount);
 auto exact=std::find_if(a.recipients.begin(),a.recipients.end(),[&](const auto& b){return mailbox::serializeBinding(b)==mailbox::serializeBinding(recipient);});protocol::check(exact!=a.recipients.end());return body;
}
}
