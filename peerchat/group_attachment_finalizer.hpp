#pragma once
#include "group_attachment_blob.hpp"

namespace peerchat::group_attachments {
// Borrowed owners must outlive the finalizer. Caller owns staging/journal and
// current authorization. No network, retry or export operations occur here.
// Completion means the verified internal sink was durably finalized. Export
// failures after completion must leave that internal copy intact.
template<bool Direct> class BlockFinalizer {
 transfer::Source& source_;transfer::Sink& sink_;const Manifest& manifest_;
 transfer::Hash& plainHash_;transfer::Hash& cipherHash_;
 std::uint64_t index_=0,position_=group_attachments::HeaderBytes;
 bool header_=false,complete_=false,failed_=false;
 void discard()noexcept{try{sink_.discard();}catch(...){} }
public:
 BlockFinalizer(transfer::Source& source,transfer::Sink& sink,const Manifest& manifest,transfer::Hash& plainHash,transfer::Hash& cipherHash):source_(source),sink_(sink),manifest_(manifest),plainHash_(plainHash),cipherHash_(cipherHash){
  try{manifest_.validate();protocol::check(manifest_.binding.direct==Direct&&source_.size()==manifest_.cipherSize());}catch(...){failed_=true;discard();throw;}
 }
 BlockFinalizer(const BlockFinalizer&)=delete;BlockFinalizer& operator=(const BlockFinalizer&)=delete;
 ~BlockFinalizer(){if(!complete_&&!failed_)discard();}
 bool completion()const{return complete_;}
 std::uint64_t processedBytes()const{return std::min(manifest_.size,index_*group_attachments::BlockBytes);}
 const Manifest& manifest()const{return manifest_;}
 void cancel(){protocol::check(!complete_);if(!failed_){failed_=true;discard();}}
 // At most one header, one <=32 KiB plaintext block, or final verification /
 // flush / commit in a call. True only after the full integrity checks commit.
 bool step(){
  using namespace group_attachments;protocol::check(!failed_);if(complete_)return true;
  try{
   if(!header_){auto bytes=read(source_,0,HeaderBytes);protocol::check(bytes==header(manifest_));hash(cipherHash_,bytes);header_=true;return false;}
   if(index_<manifest_.blocks()){
    auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,manifest_.size-index_*BlockBytes));auto block=read(source_,position_,count+TagBytes);position_+=block.size();hash(cipherHash_,block);Blob tag(block.end()-TagBytes,block.end());block.resize(count);PlainBuffer plain;
    protocol::check(platform::group_crypto::gcm(false,manifest_.key,aad(manifest_,index_,count),nonce(manifest_,index_),block,plain.bytes,tag));hash(plainHash_,plain.bytes);append(sink_,plain.bytes);++index_;return false;
   }
   protocol::check(position_==source_.size()&&plainHash_.finish()==manifest_.plainDigest&&cipherHash_.finish()==manifest_.cipherDigest);sink_.flush();sink_.commit();complete_=true;return true;
  }catch(...){failed_=true;discard();throw;}
 }
};
}
