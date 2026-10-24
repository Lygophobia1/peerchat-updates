#pragma once
#include "dm_attachment_application.hpp"
#include "windows_attachment_thumbnail.hpp"
#include "windows_download_save.hpp"
#include "portable/dm_attachment_transfer_storage.hpp"

namespace peerchat::dm_attachments::windows {
// Filesystem adapter only. The portable controller and TransferManager retain
// crypto, durable ACK, retry and transfer lifecycle ownership.
class Adapter {
 using FileSink=transfer::windows::FileSink;
 std::wstring cipher_,received_,plain_;
 std::map<std::string,transfer::DurableState> completed_;
 std::map<std::string,std::unique_ptr<group_attachments::windows::VerifiedLocalSource>> readers_;
 static constexpr const char* Domain="PeerChat/DMStagedCipherSource/v1";
 static Blob unwrap(const Blob& location){protocol::Reader r(location);protocol::check(r.type()==protocol::FileTransfer);if(r.string()!=Domain)return location;auto token=r.bytes();r.end();return token;}
 static Blob wrap(const Blob& token){protocol::Writer w(protocol::FileTransfer);w.string(Domain);w.bytes(token);protocol::check(w.data.size()<=2048);return w.data;}
 void confined(const Blob& token,const std::wstring& root)const{protocol::Reader r(token);protocol::check(r.type()==protocol::FileTransfer);auto partial=std::filesystem::path(r.text()),final=std::filesystem::path(r.text());auto base=std::filesystem::absolute(root);protocol::check(partial.is_absolute()&&final.is_absolute()&&partial.parent_path()==base&&final.parent_path()==base);}
 class StagingSink final:public transfer::Sink {
  std::unique_ptr<FileSink> file_;
 public:
  explicit StagingSink(std::unique_ptr<FileSink> file):file_(std::move(file)){}
  void append(const Blob& b)override{file_->append(b);}void flush()override{file_->flush();}void commit()override{file_->commit();}void discard()override{file_->discard();}
  Blob resumeToken()const override{return wrap(file_->resumeToken());}
 };
 class CipherSource final:public transfer::Source {
  std::unique_ptr<transfer::windows::FileSource> file_;Blob token_;
 public:
  CipherSource(std::unique_ptr<transfer::windows::FileSource> file,Blob token):file_(std::move(file)),token_(std::move(token)){}
  std::uint64_t size()const override{return file_->size();}std::string name()const override{return "ciphertext.peerchat";}Blob resumeToken()const override{return token_;}Blob read(std::uint64_t at,size_t count)override{return file_->read(at,count);}
 };
 // Restart may find a full plaintext partial/final from before the protected
 // completion record. Re-decrypt and compare every byte before re-committing.
 class ComparingSink final:public transfer::Sink {
  std::unique_ptr<FileSink> file_;std::uint64_t at_=0,size_;
 public:
  ComparingSink(std::unique_ptr<FileSink> file,std::uint64_t size):file_(std::move(file)),size_(size){}
  void append(const Blob& b)override{protocol::check(at_<=size_&&b.size()<=size_-at_&&file_->read(at_,b.size())==b);at_+=b.size();}
  void flush()override{}void commit()override{protocol::check(at_==size_);file_->commit();}void discard()override{}Blob resumeToken()const override{return file_->resumeToken();}
 };
 static void directory(const std::wstring& path){std::filesystem::create_directory(path);auto attrs=GetFileAttributesW(path.c_str());transfer::windows::require(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_DIRECTORY)&&!(attrs&FILE_ATTRIBUTE_REPARSE_POINT));}
public:
 explicit Adapter(const std::wstring& root){auto base=std::filesystem::absolute(root);cipher_=(base/L"dm-attachment-ciphertext").wstring();received_=(base/L"dm-attachment-received").wstring();plain_=(base/L"dm-attachment-plaintext").wstring();directory(cipher_);directory(received_);directory(plain_);}
 std::unique_ptr<transfer::Source> reopen(const Blob& location,const transfer::Metadata& metadata){
  auto token=unwrap(location);protocol::Reader tag(location);bool staged=tag.string()==Domain;confined(token,staged?cipher_:received_);
  transfer::DurableState d;d.purpose=transfer::Purpose::DirectCacheCiphertext;d.metadata=metadata;d.location=token;d.done=metadata.size;
  auto sink=FileSink::reopen(d,true);auto path=sink->path();sink->commit();sink.reset();auto source=std::make_unique<transfer::windows::FileSource>(path,"ciphertext.peerchat",transfer::Purpose::DirectCacheCiphertext);
  protocol::Reader expected(token);expected.text();auto final=expected.text();auto sourceToken=source->resumeToken();protocol::Reader actual(sourceToken);protocol::check(actual.text()==final);for(unsigned n=0;n<3;++n)protocol::check(expected.number()==actual.number());expected.end();protocol::check(source->size()==metadata.size);
  return std::make_unique<CipherSource>(std::move(source),location);
 }
 std::unique_ptr<transfer::Sink> receive(const transfer::DurableState& d){protocol::check(d.purpose==transfer::Purpose::DirectCacheCiphertext&&!d.sending);confined(d.location,received_);return FileSink::reopen(d);}
 Application::Platform platform(){Application::Platform io;io.thumbnail=[](transfer::Source& source){return attachment_thumbnails::create(source);};
  io.wall=[](){return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());};io.monotonic=[](){return static_cast<std::uint64_t>(GetTickCount64());};io.hash=[](){return std::make_unique<transfer::windows::Sha256>();};
  io.stage=[this](const transfer::Metadata& m){return std::make_unique<StagingSink>(std::make_unique<FileSink>(cipher_,m,nullptr,true));};io.receiveSink=[this](const transfer::Metadata& m){return std::make_unique<FileSink>(received_,m,nullptr,true);};io.location=[](transfer::Sink& sink){return sink.resumeToken();};io.reopen=[this](const Blob& token,const transfer::Metadata& m){return reopen(token,m);};
  io.discardCipher=[this](const Blob& location,const transfer::Metadata& m){auto token=unwrap(location);protocol::Reader tag(location);confined(token,tag.string()==Domain?cipher_:received_);transfer::windows::discardCiphertext(token,m);};
  io.reopenPlain=[](const Blob& token,const transfer::Metadata& m){transfer::DurableState d;d.purpose=transfer::Purpose::PeerFile;d.metadata=m;d.location=token;auto source=transfer::windows::reopenSource(d);protocol::check(source&&source->name()==m.name&&source->size()==m.size);return source;};
  io.resumeStage=[this](const transfer::Metadata& m,const Blob& wrapped)->std::unique_ptr<transfer::Sink>{protocol::check(protocol::id(m.id,32)&&m.name=="ciphertext.peerchat"&&m.size>=36&&m.size<=transfer::MaxCacheCipherBytes);protocol::Reader wrapper(wrapped);protocol::check(wrapper.type()==protocol::FileTransfer&&wrapper.string()==Domain);auto token=wrapper.bytes();wrapper.end();confined(token,cipher_);protocol::Reader paths(token);auto partial=paths.text(),final=paths.text();protocol::check(std::filesystem::path(partial).filename()==unicode::decode(m.id+".peerchat-part")&&std::filesystem::path(final).filename()==unicode::decode(m.id+"-ciphertext.peerchat"));
   auto attrs=GetFileAttributesW(partial.c_str());if(attrs==INVALID_FILE_ATTRIBUTES){transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);transfer::windows::require(GetFileAttributesW(final.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND);return std::make_unique<StagingSink>(std::make_unique<FileSink>(cipher_,m,nullptr,true));}
   transfer::DurableState d;d.purpose=transfer::Purpose::DirectCacheCiphertext;d.metadata=m;d.location=token;return std::make_unique<StagingSink>(FileSink::reopen(d));
  };
  io.plainSink=[this](const Manifest& m,const Blob& token)->std::unique_ptr<transfer::Sink>{transfer::Metadata metadata{m.binding.attachment,m.name,m.size,m.plainDigest};if(token.empty())return std::make_unique<FileSink>(plain_,metadata,nullptr,true);confined(token,plain_);protocol::Reader paths(token);auto partial=paths.text(),final=paths.text();protocol::check(protocol::id(metadata.id,32)&&std::filesystem::path(partial).filename()==unicode::decode(metadata.id+".peerchat-part")&&std::filesystem::path(final).filename()==unicode::decode(metadata.id+"-"+metadata.name));
   for(unsigned n=0;n<3;++n){paths.number();}paths.end();auto attrs=GetFileAttributesW(partial.c_str());if(attrs==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND){auto finished=GetFileAttributesW(final.c_str());if(finished==INVALID_FILE_ATTRIBUTES){transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);return std::make_unique<FileSink>(plain_,metadata,nullptr,true);}}
   transfer::DurableState d;d.metadata=metadata;d.location=token;d.done=m.size;try{return std::make_unique<ComparingSink>(FileSink::reopen(d,true),m.size);}catch(...){d.done=0;return FileSink::reopen(d);}};
  io.verified=[this](const Manifest& m,const Blob& token){confined(token,plain_);transfer::DurableState record;record.metadata={m.binding.attachment,m.name,m.size,m.plainDigest};record.done=m.size;record.state=transfer::State::Complete;record.location=token;
   // This is a verified plaintext source for the existing export coordinator,
   // never a peer transfer or a record submitted to a transfer journal.
   completed_[record.metadata.id]=record;readers_[record.metadata.id]=std::make_unique<group_attachments::windows::VerifiedLocalSource>(record.metadata,token);
  };return io;
 }
 void tick(){for(auto& entry:readers_)if(!entry.second->verified()){entry.second->step();break;}}
 std::vector<transfer::DurableState> plaintextRecords()const{std::vector<transfer::DurableState> out;for(auto& entry:completed_)out.push_back(entry.second);return out;}
 std::wstring path(const std::string& id)const{auto p=readers_.find(id);return p!=readers_.end()&&p->second->verified()?p->second->path():std::wstring{};}
};
}
