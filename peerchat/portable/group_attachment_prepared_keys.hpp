#pragma once
#include "../group_attachment_key_envelope.hpp"
#include "../group_attachment_staging.hpp"
namespace peerchat::group_attachments::portable {
class PreparedKeys {
 Storage& storage_;std::string device_,server_,cursor_;
 std::string id(const keys::Header& h)const{protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/PortablePreparedGroupKey/v1");w.string(server_);w.bytes(h.descriptor);w.number(h.recipient.accountId);w.string(h.recipient.device);w.string(h.sourceMembership);w.string(h.recipientMembership);return Storage::publicKeyId(w.data);}
public:
 PreparedKeys(Storage& storage,const std::string& device,const std::string& server):storage_(storage),device_(device),server_(server){protocol::check(protocol::id(device)&&protocol::id(server));}
 Blob ensure(const Staged& staged,const keys::Header& requested,const Body& body,const keys::Authority& authority,platform::IdentityProvider& identity,platform::HpkeProvider& hpke,std::uint64_t now){
  staged.validate(device_);auto descriptor=keys::authorize(requested,authority,true);protocol::check(descriptor.matches(staged.manifest)&&descriptor.matches(body.manifest)&&identity.publicKey()==descriptor.identity&&requested.created<=now&&now<requested.expires);
  protocol::Writer a(protocol::GroupEvent),b(protocol::GroupEvent);writePrivateManifest(a,staged.manifest);writePrivateManifest(b,body.manifest);SecretRecord left(std::move(a.data)),right(std::move(b.data));protocol::check(left.bytes==right.bytes);
  auto key=id(requested);auto encoded=keys::recipientBody(body,authority);auto digest=Storage::publicKeyId(encoded.bytes);auto legacy=decodeBody(encodeBody(body).bytes);legacy.thumbnail.clear();auto legacyDigest=Storage::publicKeyId(encodeBody(legacy).bytes);auto fullDigest=Storage::publicKeyId(encodeBody(body).bytes);SecretRecord prior(storage_.groupAttachmentLocal(device_,key));
  if(!prior.bytes.empty()){protocol::Reader r(prior.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/PortablePreparedGroupKey/v1"&&r.string()==key );auto savedDigest=r.string();protocol::check(savedDigest==digest||savedDigest==legacyDigest||savedDigest==fullDigest);auto wire=r.bytes();r.end();auto saved=keys::parse(wire);auto approved=keys::authorize(saved.header,authority,true);protocol::check(id(saved.header)==key&&approved.wire==descriptor.wire&&saved.header.created<=now&&now<saved.header.expires&&saved.signature.size()==64&&verifyDevicePayload(keys::signingPayload(saved),saved.signature,descriptor.identity));return wire;}
  auto packet=keys::seal(requested,body,authority,identity,hpke);auto wire=keys::serialize(packet);protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/PortablePreparedGroupKey/v1");w.string(key);w.string(digest);w.bytes(wire);SecretRecord record(std::move(w.data));storage_.saveGroupAttachmentLocal(device_,key,{},record.bytes);return wire;
 }
 void expire(const std::set<std::string>& protectedObjects,std::uint64_t now,size_t budget){
  protocol::check(budget>0&&budget<=64);auto page=storage_.groupAttachmentLocalKeys(device_,cursor_,static_cast<int>(budget));
  for(const auto& key:page){cursor_=key;SecretRecord old(storage_.groupAttachmentLocal(device_,key));if(old.bytes.empty())continue;protocol::Reader r(old.bytes);protocol::check(r.type()==protocol::FileTransfer);if(r.string()!="PeerChat/PortablePreparedGroupKey/v1")continue;protocol::check(r.string()==key);r.string();auto packet=keys::parse(r.bytes());r.end();auto descriptor=parseDescriptor(packet.header.descriptor);if(id(packet.header)!=key)continue;protocol::check(descriptor.sender==device_&&packet.signature.size()==64&&verifyDevicePayload(keys::signingPayload(packet),packet.signature,descriptor.identity));if(now>=packet.header.expires&&!protectedObjects.count(descriptor.attachment))storage_.saveGroupAttachmentLocal(device_,key,old.bytes,{});}
  if(page.size()<budget)cursor_.clear();
 }
};
}