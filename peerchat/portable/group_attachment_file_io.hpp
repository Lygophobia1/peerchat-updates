#pragma once
#include "attachment_file_io.hpp"
#include "../group_attachment_finalizer.hpp"
#include "../group_attachment_staging.hpp"

namespace peerchat::group_attachments::posix {
// These adapters borrow the existing app-private cache and transfer owner.
// The caller persists the immutable manifest before publishing ciphertext and
// persists each plaintext intent before any decrypted bytes are written.
inline std::unique_ptr<transfer::Source> stagedSource(std::shared_ptr<transfer::posix::Cache> cache,const Staged& staged,const std::string& device){
 staged.validate(device);protocol::check(!staged.manifest.binding.direct);
 transfer::DurableState state;state.purpose=transfer::Purpose::GroupCacheCiphertext;
 state.metadata={staged.manifest.binding.attachment,"ciphertext.peerchat",staged.manifest.cipherSize(),staged.manifest.cipherDigest};
 state.done=state.metadata.size;state.location=staged.location;
 auto sink=transfer::posix::FileSink::reopen(cache,state);
 protocol::check(sink->read(0,HeaderBytes)==header(staged.manifest));sink->commit();state.location=sink->sourceToken();
 return transfer::posix::FileSource::reopen(std::move(cache),state);
}
class ReceiverFinalizer {
 std::shared_ptr<transfer::posix::Cache> cache_;const Manifest& manifest_;
 std::unique_ptr<transfer::Source> cipher_;std::unique_ptr<transfer::Sink> plain_;
 transfer::posix::Sha256 plainHash_,cipherHash_;std::unique_ptr<BlockFinalizer<false>> finalizer_;
 transfer::Metadata metadata_;Blob token_;bool verified_=false,failed_=false;
public:
 using PersistIntent=std::function<void(const Blob&,const Blob&)>;
 ReceiverFinalizer(std::shared_ptr<transfer::posix::Cache> cache,std::unique_ptr<transfer::Source> cipher,const Manifest& manifest,const Blob& previous,PersistIntent persist)
 :cache_(std::move(cache)),manifest_(manifest),cipher_(std::move(cipher)){
  manifest_.validate();protocol::check(cache_&&cipher_&&persist&&!manifest_.binding.direct&&cipher_->size()==manifest_.cipherSize());
  metadata_={Storage::newMessageId(),manifest_.name,manifest_.size,manifest_.plainDigest};
  if(previous.empty())plain_=std::make_unique<transfer::posix::FileSink>(cache_,metadata_);
  else {
   protocol::Reader r(previous);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1");metadata_.id=r.string();protocol::check(r.string()==metadata_.name);
   transfer::DurableState state;state.metadata=metadata_;state.location=previous;plain_=transfer::posix::replaySink(cache_,state);
  }
  token_=plain_->resumeToken();
  try{persist(previous,token_);}catch(...){plain_->discard();throw;}
  finalizer_=std::make_unique<BlockFinalizer<false>>(*cipher_,*plain_,manifest_,plainHash_,cipherHash_);
 }
 ReceiverFinalizer(const ReceiverFinalizer&)=delete;ReceiverFinalizer& operator=(const ReceiverFinalizer&)=delete;
 bool step(){protocol::check(!failed_);if(verified_)return true;try{verified_=finalizer_->step();return verified_;}catch(...){failed_=true;throw;}}
 void cancel(){protocol::check(!verified_);finalizer_->cancel();failed_=true;}
 bool verified()const{return verified_;}bool failed()const{return failed_;}
 std::uint64_t processedBytes()const{return finalizer_->processedBytes();}
 Blob token()const{protocol::check(verified_);return token_;}
 std::unique_ptr<transfer::posix::FileSource> output()const{
  protocol::check(verified_);transfer::DurableState state;state.metadata=metadata_;state.location=token_;state.done=metadata_.size;
  auto sink=transfer::posix::FileSink::reopen(cache_,state);protocol::check(sink->committed());state.location=sink->sourceToken();
  return transfer::posix::FileSource::reopen(cache_,state);
 }
};
}