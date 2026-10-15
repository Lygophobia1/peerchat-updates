#pragma once
#include "windows_transfer_io.hpp"
#include "group_attachment_staging.hpp"

namespace peerchat::group_attachments::windows {
// A source adapter, not a second hash, retry, journal or transfer owner.
// The wrapped sink token is saved with the protected key BEFORE rename. It
// remains valid after rename, so a process death at either boundary can reopen
// the same file. Upload preparation still verifies the expected cipher digest
// through the existing TransferManager before offering any bytes.
class StagedCipherSource final:public transfer::Source {
 std::unique_ptr<transfer::windows::FileSource> file_;Blob location_;
 StagedCipherSource(std::unique_ptr<transfer::windows::FileSource> file,Blob location):file_(std::move(file)),location_(std::move(location)){}
public:
 static Blob location(const transfer::Sink& sink){
  auto token=sink.resumeToken();protocol::check(!token.empty());
  protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/StagedCipherSource/v1");w.bytes(token);protocol::check(w.data.size()<=2048);return std::move(w.data);
 }
 static std::unique_ptr<StagedCipherSource> reopen(const Staged& staged,const std::string& localDevice){
  staged.validate(localDevice);protocol::Reader wrapper(staged.location);
  protocol::check(wrapper.type()==protocol::FileTransfer&&wrapper.string()=="PeerChat/StagedCipherSource/v1");auto token=wrapper.bytes();wrapper.end();
  transfer::DurableState state;state.purpose=transfer::Purpose::GroupCacheCiphertext;state.location=token;
  state.metadata={staged.manifest.binding.attachment,"ciphertext.peerchat",staged.manifest.cipherSize(),staged.manifest.cipherDigest};state.done=state.metadata.size;
  auto sink=transfer::windows::FileSink::reopen(state,true);
  protocol::check(sink->read(0,HeaderBytes)==header(staged.manifest));auto path=sink->path();sink->commit();sink.reset();
  auto file=std::make_unique<transfer::windows::FileSource>(path,"ciphertext.peerchat",transfer::Purpose::GroupCacheCiphertext);
  // Closing the sink and opening the source is not assumed atomic. Reject a
  // substituted file before exposing it; the read handle then denies deletion.
  protocol::Reader expected(token);expected.text();auto final=expected.text();
  auto sourceToken=file->resumeToken();protocol::Reader actual(sourceToken);protocol::check(actual.text()==final);
  for(unsigned n=0;n<3;++n)protocol::check(expected.number()==actual.number());
  expected.end();
  protocol::check(file->size()==state.metadata.size);
  return std::unique_ptr<StagedCipherSource>(new StagedCipherSource(std::move(file),staged.location));
 }
 std::uint64_t size()const override{return file_->size();}
 std::string name()const override{return "ciphertext.peerchat";}
 Blob resumeToken()const override{return location_;}
 Blob read(std::uint64_t offset,size_t count)override{return file_->read(offset,count);}
};
}
