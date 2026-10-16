#pragma once
#include "transfer_manager.hpp"
#include "platform/group_crypto.hpp"
namespace peerchat::group_attachments {
// Application E2EE object codec only. No socket, grant, retry or transfer engine.
// The manifest is for an authenticated encrypted group body, NEVER a Pi request.
constexpr std::uint64_t HeaderBytes=20,TagBytes=16,BlockBytes=32768;
constexpr std::uint64_t MaxCipherBytes=transfer::MaxFileBytes+((transfer::MaxFileBytes+BlockBytes-1)/BlockBytes)*TagBytes+HeaderBytes;
struct Binding {
 std::string attachment,group,sender;std::uint64_t account=0,epoch=0;
 // Explicit local codec context. Legacy group bytes and default construction
 // remain unchanged; a DM never supplies a synthetic group or epoch.
 bool direct=false;std::uint64_t recipientAccount=0;std::string relationship{},audienceDigest{};
};
struct Manifest {
 Binding binding;std::string name;std::uint64_t size=0;
 Blob key,prefix;transfer::Digest plainDigest{},cipherDigest{};
 Manifest()=default;Manifest(const Manifest&)=delete;Manifest& operator=(const Manifest&)=delete;
 Manifest(Manifest&&)=default;Manifest& operator=(Manifest&&)=delete;
 ~Manifest(){platform::group_crypto::wipe(key.data(),key.size());}
 std::uint64_t blocks()const{return std::max<std::uint64_t>(1,(size+BlockBytes-1)/BlockBytes);}
 std::uint64_t cipherSize()const{return HeaderBytes+size+blocks()*TagBytes;}
 void validate()const{protocol::check(protocol::id(binding.attachment,32)&&protocol::id(binding.sender)&&binding.account&&binding.account<=INT64_MAX&&transfer::safeName(name)&&size<=transfer::MaxFileBytes&&key.size()==32&&prefix.size()==4);
  if(binding.direct)protocol::check(binding.group.empty()&&!binding.epoch&&binding.recipientAccount&&binding.recipientAccount<=INT64_MAX&&binding.recipientAccount!=binding.account&&protocol::id(binding.relationship,32)&&protocol::id(binding.audienceDigest));
  else protocol::check(protocol::id(binding.group,32)&&binding.epoch&&binding.epoch<=INT64_MAX&&!binding.recipientAccount&&binding.relationship.empty()&&binding.audienceDigest.empty());
 }
};
inline Blob header(const Manifest& m){Blob bytes{'P','C',static_cast<unsigned char>(m.binding.direct?'D':'G'),'A',1,0,0,0};for(unsigned n=0;n<8;++n)bytes.push_back(static_cast<unsigned char>(m.size>>(n*8)));bytes.insert(bytes.end(),m.prefix.begin(),m.prefix.end());return bytes;}
inline Blob nonce(const Manifest& m,std::uint64_t index){auto n=m.prefix;for(int bit=56;bit>=0;bit-=8)n.push_back(static_cast<unsigned char>(index>>bit));return n;}
inline Blob aad(const Manifest& m,std::uint64_t index,size_t count){protocol::Writer w(m.binding.direct?protocol::FileTransfer:protocol::GroupEvent);w.string(m.binding.direct?"PeerChat/DirectAttachmentBlob/v1":"PeerChat/GroupAttachmentBlob/v1");w.string(m.binding.attachment);
 if(m.binding.direct){w.number(m.binding.recipientAccount);w.string(m.binding.relationship);w.string(m.binding.audienceDigest);}else w.string(m.binding.group);
 w.string(m.binding.sender);w.number(m.binding.account);if(!m.binding.direct)w.number(m.binding.epoch);w.string(m.name);w.number(m.size);w.number(index);w.number(count);return w.data;}
inline void hash(transfer::Hash& h,const Blob& bytes){for(size_t off=0;off<bytes.size();off+=transfer::ChunkBytes)h.update(bytes.data()+off,std::min(transfer::ChunkBytes,bytes.size()-off));}
inline Blob read(transfer::Source& source,std::uint64_t offset,size_t count){Blob result;while(result.size()<count){auto part=source.read(offset+result.size(),std::min<size_t>(transfer::ChunkBytes,count-result.size()));protocol::check(!part.empty()&&part.size()<=count-result.size());result.insert(result.end(),part.begin(),part.end());}return result;}
inline void append(transfer::Sink& sink,const Blob& bytes){for(size_t off=0;off<bytes.size();off+=transfer::ChunkBytes)sink.append(Blob(bytes.begin()+off,bytes.begin()+std::min(bytes.size(),off+transfer::ChunkBytes)));}
struct PlainBuffer {Blob bytes;~PlainBuffer(){platform::group_crypto::wipe(bytes.data(),bytes.size());}};
// One header/block/final-flush step per call; borrowed I/O/hash owners must
// outlive this codec. No socket, journal, retry loop or background worker.
class Sealer {
 transfer::Source& source_;transfer::Sink& ciphertext_;transfer::Hash& plainHash_;transfer::Hash& cipherHash_;
 Manifest manifest_;std::uint64_t index_=0;bool header_=false,ready_=false,taken_=false,failed_=false;
public:
 Sealer(transfer::Source& source,transfer::Sink& ciphertext,Binding binding,transfer::Hash& plainHash,transfer::Hash& cipherHash):source_(source),ciphertext_(ciphertext),plainHash_(plainHash),cipherHash_(cipherHash){
  try{manifest_.binding=std::move(binding);manifest_.name=source.name();manifest_.size=source.size();manifest_.key=platform::group_crypto::random(32);manifest_.prefix=platform::group_crypto::random(4);manifest_.validate();}catch(...){ciphertext_.discard();throw;}
 }
 Sealer(const Sealer&)=delete;Sealer& operator=(const Sealer&)=delete;
 ~Sealer(){if(!taken_&&!failed_)try{ciphertext_.discard();}catch(...){}}
 std::uint64_t processedBytes()const{return std::min(manifest_.size,index_*BlockBytes);}
 std::uint64_t size()const{return manifest_.size;}
 bool step(){
  protocol::check(!taken_&&!failed_);if(ready_)return true;
  try{
   if(!header_){auto bytes=header(manifest_);hash(cipherHash_,bytes);append(ciphertext_,bytes);header_=true;return false;}
   if(index_<manifest_.blocks()){
    auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,manifest_.size-index_*BlockBytes));PlainBuffer plain{read(source_,index_*BlockBytes,count)};hash(plainHash_,plain.bytes);Blob encrypted,tag(16);
    protocol::check(platform::group_crypto::gcm(true,manifest_.key,aad(manifest_,index_,count),nonce(manifest_,index_),plain.bytes,encrypted,tag));encrypted.insert(encrypted.end(),tag.begin(),tag.end());hash(cipherHash_,encrypted);append(ciphertext_,encrypted);++index_;return false;
   }
   manifest_.plainDigest=plainHash_.finish();manifest_.cipherDigest=cipherHash_.finish();ciphertext_.flush();ready_=true;return true;
  }catch(...){failed_=true;ciphertext_.discard();throw;}
 }
 void cancel(){protocol::check(!taken_);failed_=true;ciphertext_.discard();}
 // Data is flushed, not renamed. The caller must durably retain this key,
 // immutable binding and owned sink token BEFORE committing the ciphertext.
 Manifest takeManifest(){protocol::check(ready_&&!failed_&&!taken_);taken_=true;return std::move(manifest_);}
};
// Synchronous codec convenience. Production preparation uses bounded Sealer
// steps and protected staging before rename, not this whole-file helper.
inline Manifest seal(transfer::Source& source,transfer::Sink& ciphertext,Binding binding,transfer::Hash& plainHash,transfer::Hash& cipherHash){
 try{Sealer sealer(source,ciphertext,std::move(binding),plainHash,cipherHash);while(!sealer.step()){}auto manifest=sealer.takeManifest();ciphertext.commit();return manifest;}catch(...){ciphertext.discard();throw;}
}
inline void open(transfer::Source& ciphertext,transfer::Sink& plaintext,const Manifest& m,transfer::Hash& plainHash,transfer::Hash& cipherHash){
 try{m.validate();protocol::check(ciphertext.size()==m.cipherSize());auto h=read(ciphertext,0,HeaderBytes);protocol::check(h==header(m));hash(cipherHash,h);std::uint64_t position=HeaderBytes;
  for(std::uint64_t index=0;index<m.blocks();++index){auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,m.size-index*BlockBytes));auto block=read(ciphertext,position,count+TagBytes);position+=block.size();hash(cipherHash,block);Blob tag(block.end()-TagBytes,block.end());block.resize(count);PlainBuffer plain;
   protocol::check(platform::group_crypto::gcm(false,m.key,aad(m,index,count),nonce(m,index),block,plain.bytes,tag));hash(plainHash,plain.bytes);append(plaintext,plain.bytes);
  }protocol::check(position==ciphertext.size()&&plainHash.finish()==m.plainDigest&&cipherHash.finish()==m.cipherDigest);plaintext.flush();plaintext.commit();
 }catch(...){plaintext.discard();throw;}
}
}
