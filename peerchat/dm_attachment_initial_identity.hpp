#pragma once
#include "group.hpp"
#include "dm_attachment_descriptor.hpp"
namespace peerchat::dm_attachments {
// Select only locally authorized devices. Unfamiliar siblings never gain trust
// from being listed in a cache audience and do not block known recipients.
inline Audience selectKnownAudience(const Audience& offered,const std::string& selected,
 const std::vector<Contact>& existing,const std::function<std::uint64_t(const std::string&)>& accountFor,
 const std::function<bool(const mailbox::RecipientBinding&)>& verified) {
 protocol::check(!offered.recipients.empty());const auto account=offered.recipients.front().accountId;
 bool historical=false;for(const auto& c:existing)if(c.verified&&accountFor(c.peerId)==account)historical=true;
 Audience result=offered;result.recipients.clear();
 for(const auto& b:offered.recipients)if(accountFor(b.device)==account&&
    (verified(b)||(!historical&&b.device==selected)))result.recipients.push_back(b);
 protocol::check(!result.recipients.empty());
 protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/dm-cache-audience/v1");w.string(result.relationship);
 w.bytes(mailbox::serializeBinding(result.source));w.number(result.recipients.size());
 for(const auto& b:result.recipients){w.bytes(mailbox::serializeBinding(b));}
 result.wire=w.data;return result;
}
// Bind a new account-scoped send to a device in its already verified audience.
// The conversation representative may be an older cache-incapable sibling.
inline std::string verifiedAudiencePeer(const Audience& audience,const std::string& selected,
 std::uint64_t recipient,const std::function<std::uint64_t(const std::string&)>& accountFor,
 const std::function<bool(const mailbox::RecipientBinding&)>& verified) {
 protocol::check(mailbox::account(recipient)&&protocol::id(selected)&&accountFor(selected)==recipient&&
  !audience.recipients.empty()&&audience.recipients.size()<=32);
 std::string peer;
 for(const auto& b:audience.recipients){
  protocol::check(b.accountId==recipient&&accountFor(b.device)==recipient&&verified(b));
  if(peer.empty()||b.device==selected)peer=b.device;
 }
 return peer;
}
// Explicit first cached send uses the same first-friend rule as peer TLS: no
// previous verified device for this account. Server data cannot replace a pin.
inline std::vector<Contact> initialIdentities(const Audience& audience,const std::vector<Blob>& proofs,
 const std::vector<Contact>& existing,const std::function<std::uint64_t(const std::string&)>& accountFor,
 const std::function<void(const char*)>& trace=[](const char*){}) {
 trace("identity_proof_count");
 protocol::check(!audience.recipients.empty()&&proofs.size()==audience.recipients.size()&&proofs.size()<=32);
 const auto account=audience.recipients.front().accountId;
 for(const auto& c:existing)if(c.verified&&accountFor(c.peerId)==account)return {};
 std::vector<Contact> result;
 for(size_t i=0;i<proofs.size();++i){
  trace("identity_device_ownership");const auto& wire=proofs[i];const auto& binding=audience.recipients[i];protocol::check(wire.size()<=1024&&accountFor(binding.device)==account);
  trace("identity_proof_fields");protocol::Reader r(wire);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/dm-cache-device/v1");
  protocol::check(r.number()==account&&r.string()==binding.device&&r.string()==binding.keyId&&r.number()==binding.generation);auto bodyVersion=r.number();protocol::check((bodyVersion==1||bodyVersion==2)&&r.number()==1);
  auto key=r.bytes();auto end=r.cursor;auto signature=r.bytes();r.end();
  trace("identity_proof_signatures");protocol::check(Storage::publicKeyId(key)==binding.device&&verifyDevicePayload(Blob(wire.begin(),wire.begin()+end),signature,key)&&mailbox::verifyBinding(binding,account,binding.device,key,verifyDevicePayload,Storage::publicKeyId));
  trace("identity_existing_key");auto prior=std::find_if(existing.begin(),existing.end(),[&](const auto& c){return c.peerId==binding.device;});
  if(prior!=existing.end())protocol::check(prior->publicKey.empty()||prior->publicKey==key);
  auto contact=prior==existing.end()?Contact{binding.device,L"Friend device",key,{},true}:*prior;
  contact.publicKey=key;contact.verified=true;result.push_back(std::move(contact));
 }
 return result;
}
}
