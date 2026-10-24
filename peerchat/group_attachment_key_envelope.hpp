#pragma once
#include "group_attachment_body.hpp"
#include "group_attachment_descriptor.hpp"
#include "mailbox_protocol.hpp"
#include "device_key_manager.hpp"

namespace peerchat::group_attachments::keys {
constexpr size_t MaxWire=8192,MaxHeader=3072;
// Attachment-only authorization context from verified historical/current signed
// ledgers and authenticated cache admission. This codec does not establish trust,
// fetch roster evidence, advance history cursors or acknowledge a transfer.
struct Authority {
 const GroupState& originalLedger;const group_keys::RosterSnapshot& originalRoster;
 const GroupState& currentLedger;const group_keys::RosterSnapshot& currentRoster;
 std::string sourceMembership,recipientMembership,currentRecipientMembership,currentSourceMembership;
 std::uint64_t cacheExpires=0; // Authenticated immutable object deadline, ms UTC.
 std::uint64_t bodyVersion=1; // Optional presentation capability, never authority.
};
struct Header {
 Blob descriptor;std::string sourceMembership,recipientMembership;mailbox::RecipientBinding recipient;std::uint64_t created=0,expires=0;
};
struct Envelope {Header header;platform::HpkeEnvelope encrypted;Blob signature;};
inline Blob headerPayload(const Header& h){
 parseDescriptor(h.descriptor);mailbox::validate(h.recipient);
 protocol::check(protocol::id(h.sourceMembership,32)&&protocol::id(h.recipientMembership,32)&&h.recipient.storageAllowed&&h.recipient.signature.size()==64&&h.created>=h.recipient.createdUtc&&h.created<=INT64_MAX&&h.expires>h.created&&h.expires<=253402300799000ull);
 mailbox::Writer w("PeerChat/group-attachment-key/v1");w.number(1,2);w.number(platform::HpkeSuite::kem,2);w.number(platform::HpkeSuite::kdf,2);w.number(platform::HpkeSuite::aead,2);
 w.field(h.descriptor);w.text(h.sourceMembership);w.text(h.recipientMembership);w.field(mailbox::serializeBinding(h.recipient));w.number(h.created,8);w.number(h.expires,8);protocol::check(w.bytes.size()<=MaxHeader);return std::move(w.bytes);
}
inline Blob info(){mailbox::Writer w("PeerChat/group-attachment-key-HPKE/Base/v1");return std::move(w.bytes);}
inline Blob signingPayload(const Envelope& e){
 mailbox::Writer w("PeerChat/group-attachment-key-signature/v1");w.field(headerPayload(e.header));
 protocol::check(e.encrypted.enc.size()==65&&e.encrypted.enc[0]==4&&e.encrypted.ciphertext.size()>=16&&e.encrypted.ciphertext.size()<=4096+16);w.field(e.encrypted.enc);w.field(e.encrypted.ciphertext);return std::move(w.bytes);
}
inline Blob serialize(const Envelope& e){protocol::check(e.signature.size()==64);auto result=signingPayload(e);mailbox::Writer tail("");tail.bytes.clear();tail.field(e.signature);result.insert(result.end(),tail.bytes.begin(),tail.bytes.end());protocol::check(result.size()<=MaxWire);return result;}
inline Envelope parse(const Blob& wire){
 protocol::check(wire.size()<=MaxWire);mailbox::Reader r(wire,"PeerChat/group-attachment-key-signature/v1");auto bytes=r.field(MaxHeader);mailbox::Reader h(bytes,"PeerChat/group-attachment-key/v1");
 protocol::check(h.number(2)==1&&h.number(2)==platform::HpkeSuite::kem&&h.number(2)==platform::HpkeSuite::kdf&&h.number(2)==platform::HpkeSuite::aead);Envelope e;
 e.header.descriptor=h.field(2048);e.header.sourceMembership=h.text(32);e.header.recipientMembership=h.text(32);e.header.recipient=mailbox::parseBinding(h.field(2048));e.header.created=h.number(8);e.header.expires=h.number(8);h.end();
 e.encrypted.enc=r.field(65);e.encrypted.ciphertext=r.field(4112);e.signature=r.field(64);r.end();protocol::check(serialize(e)==wire);return e;
}
inline PublicDescriptor authorize(const Header& h,const Authority& a,bool sending){
 auto d=parseDescriptor(h.descriptor);auto& b=h.recipient;
 protocol::check(a.cacheExpires&&h.expires<=a.cacheExpires);
 protocol::check(a.originalRoster.serverGroupId==d.group&&a.currentRoster.serverGroupId==d.group&&a.originalRoster.legacyGroupId==a.currentRoster.legacyGroupId&&a.originalRoster.generation==d.epoch&&a.currentRoster.generation>=d.epoch&&group_keys::GroupKeyManager::matchesSignedLedger(a.originalRoster,a.originalLedger)&&group_keys::GroupKeyManager::matchesSignedLedger(a.currentRoster,a.currentLedger));
 auto source=a.originalLedger.device(d.account,d.sender);auto recipient=a.originalLedger.device(b.accountId,b.device);auto current=a.currentLedger.device(b.accountId,b.device);
 protocol::check(source&&recipient&&current&&source->publicKey==d.identity&&recipient->publicKey==current->publicKey&&group_keys::GroupKeyManager::currentMember(a.originalRoster,d.account,d.sender)&&group_keys::GroupKeyManager::currentMember(a.originalRoster,b.accountId,b.device)&&group_keys::GroupKeyManager::currentMember(a.currentRoster,b.accountId,b.device));
 protocol::check(h.sourceMembership==a.sourceMembership&&h.recipientMembership==a.recipientMembership&&a.recipientMembership==a.currentRecipientMembership&&mailbox::verifyBinding(b,b.accountId,b.device,recipient->publicKey,verifyDevicePayload,Storage::publicKeyId));
 if(sending){auto currentSource=a.currentLedger.device(d.account,d.sender);protocol::check(currentSource&&currentSource->publicKey==d.identity&&a.sourceMembership==a.currentSourceMembership&&group_keys::GroupKeyManager::currentMember(a.currentRoster,d.account,d.sender));}
 return d;
}
inline SecretRecord recipientBody(const Body& body,const Authority& authority){
 auto copy=decodeBody(encodeBody(body).bytes);if(authority.bodyVersion<2)copy.thumbnail.clear();return encodeBody(copy);
}
inline Envelope seal(const Header& h,const Body& body,const Authority& authority,platform::IdentityProvider& sender,platform::HpkeProvider& hpke){
 auto d=authorize(h,authority,true);protocol::check(d.matches(body.manifest)&&sender.publicKey()==d.identity);auto plain=recipientBody(body,authority);Envelope e;e.header=h;
 e.encrypted=hpke.seal(h.recipient.publicKey,info(),headerPayload(h),plain.bytes);e.signature=sender.sign(signingPayload(e));protocol::check(e.signature.size()==64);return e;
}
inline Body open(const Envelope& e,const Authority& authority,const std::string& localDevice,std::uint64_t localAccount,DeviceKeyManager& manager,platform::HpkeProvider& hpke,std::uint64_t now){
 auto d=authorize(e.header,authority,false);auto& b=e.header.recipient;protocol::check(b.accountId==localAccount&&b.device==localDevice&&e.header.created<=now&&now<e.header.expires&&e.signature.size()==64&&verifyDevicePayload(signingPayload(e),e.signature,d.identity));SecretRecord plain;
 // Preserve the existing key-manager lock/lease across cryptographic execution.
 manager.withRecipient(b.keyId,true,[&](const DeviceKeyMetadata& metadata,const std::shared_ptr<platform::RecipientKeyHandle>& key){
  protocol::check(metadata.registration==DeviceKeyRegistration::Accepted&&metadata.generation==b.generation&&metadata.publicKey==b.publicKey&&metadata.signedBinding==mailbox::serializeBinding(b)&&manager.identityPublicKey()==authority.currentLedger.device(localAccount,localDevice)->publicKey);
  plain.bytes=hpke.open(key,b.publicKey,info(),headerPayload(e.header),e.encrypted);
 });
 auto result=decodeBody(plain.bytes);protocol::check(d.matches(result.manifest));return result;
}
}
