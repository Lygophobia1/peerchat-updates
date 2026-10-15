#pragma once
#include "group_attachment_blob.hpp"
#include <set>

namespace peerchat::group_attachments {
// Local protected metadata only, not another transfer/retry owner. No paths,
// keys or plaintext manifest bytes may be sent to the Pi. The opaque location
// token is interpreted only by the existing platform file adapter.
struct Staged {
 Manifest manifest;Blob location;std::uint64_t created=0,expires=0;
 void validate(const std::string& device)const{
  manifest.validate();protocol::check(manifest.binding.sender==device&&!location.empty()&&location.size()<=2048&&created&&expires>=created&&expires-created<=transfer::RetentionMilliseconds);
 }
};
struct SecretRecord {
 Blob bytes;
 explicit SecretRecord(Blob value={}):bytes(std::move(value)){}
 SecretRecord(const SecretRecord&)=delete;SecretRecord& operator=(const SecretRecord&)=delete;
 SecretRecord(SecretRecord&& other)noexcept:bytes(std::move(other.bytes)){}
 ~SecretRecord(){platform::group_crypto::wipe(bytes.data(),bytes.size());}
};
// Immutable sealed objects only. A caller must verify/reopen the ciphertext
// against the saved digest before resume. This journal grants no group access
// and cannot imply remote upload or recipient plaintext completion.
class StagingJournal {
 Storage& storage_;std::string device_;SecretRecord previous_;bool loaded_=false;
 static bool same(const Staged& a,const Staged& b){
  const auto& x=a.manifest;const auto& y=b.manifest;
  return x.binding.attachment==y.binding.attachment&&x.binding.group==y.binding.group&&x.binding.sender==y.binding.sender&&x.binding.account==y.binding.account&&x.binding.epoch==y.binding.epoch&&x.name==y.name&&x.size==y.size&&x.key==y.key&&x.prefix==y.prefix&&x.plainDigest==y.plainDigest&&x.cipherDigest==y.cipherDigest&&a.location==b.location&&a.created==b.created&&a.expires==b.expires;
 }
 static void write(protocol::Writer& w,const Staged& value){
  const auto& m=value.manifest;w.string(m.binding.attachment);w.string(m.binding.group);w.string(m.binding.sender);w.number(m.binding.account);w.number(m.binding.epoch);
  w.string(m.name);w.number(m.size);w.bytes(m.key);w.bytes(m.prefix);w.bytes(Blob(m.plainDigest.begin(),m.plainDigest.end()));w.bytes(Blob(m.cipherDigest.begin(),m.cipherDigest.end()));
  w.bytes(value.location);w.number(value.created);w.number(value.expires);
 }
 static Staged read(protocol::Reader& r){
  Staged value;auto& m=value.manifest;m.binding.attachment=r.string();m.binding.group=r.string();m.binding.sender=r.string();m.binding.account=r.number();m.binding.epoch=r.number();m.name=r.string();m.size=r.number();m.key=r.bytes();m.prefix=r.bytes();
  auto plain=r.bytes(),cipher=r.bytes();protocol::check(plain.size()==32&&cipher.size()==32);std::copy(plain.begin(),plain.end(),m.plainDigest.begin());std::copy(cipher.begin(),cipher.end(),m.cipherDigest.begin());
  value.location=r.bytes();value.created=r.number();value.expires=r.number();return value;
 }
public:
 StagingJournal(Storage& storage,std::string device):storage_(storage),device_(std::move(device)){protocol::check(protocol::id(device_));}
 std::vector<Staged> load(){
  SecretRecord current(storage_.groupAttachmentStaging(device_));std::vector<Staged> result;
  if(!current.bytes.empty()){
   protocol::check(current.bytes.size()<=262144);protocol::Reader r(current.bytes);protocol::check(r.type()==protocol::GroupEvent&&r.string()=="PeerChat/GroupAttachmentStaging/v1"&&r.string()==device_);
   auto count=r.number();protocol::check(count<=transfer::MaxRecords);std::set<std::string> ids;
   for(std::uint64_t n=0;n<count;++n){auto value=read(r);value.validate(device_);protocol::check(ids.insert(value.manifest.binding.attachment).second);result.push_back(std::move(value));}r.end();
  }
  previous_.bytes.swap(current.bytes);loaded_=true;return result;
 }
 void save(const std::vector<Staged>& values){
  protocol::check(loaded_&&values.size()<=transfer::MaxRecords);std::set<std::string> ids;
  // Validate before serializing any secret material.
  for(const auto& value:values){value.validate(device_);protocol::check(ids.insert(value.manifest.binding.attachment).second);}
  // Retrying an existing object must never rewrite its key, nonce, binding or
  // source token. A changed source requires a fresh object, not reencryption.
  if(!previous_.bytes.empty()){
   protocol::Reader old(previous_.bytes);old.string();old.string();auto count=old.number();
   for(std::uint64_t n=0;n<count;++n){auto prior=read(old);auto found=std::find_if(values.begin(),values.end(),[&](const Staged& value){return value.manifest.binding.attachment==prior.manifest.binding.attachment;});protocol::check(found==values.end()||same(prior,*found));}old.end();
  }
  protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/GroupAttachmentStaging/v1");w.string(device_);w.number(values.size());
  SecretRecord next;try{for(const auto& value:values)write(w,value);next.bytes=std::move(w.data);}catch(...){platform::group_crypto::wipe(w.data.data(),w.data.size());throw;}
  protocol::check(next.bytes.size()<=262144);storage_.saveGroupAttachmentStaging(device_,previous_.bytes,next.bytes);previous_.bytes.swap(next.bytes);
 }
};
}
