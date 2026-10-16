#pragma once
#include "windows_group_attachment_source.hpp"

namespace peerchat::group_attachments::windows {
// Profile-local persistence and bounded finalization only. TransferManager owns
// all transfer offsets, ACKs, retransmission, negotiation and source rehashing.
class CacheStateStore final:public transfer::StateStore {
public:
 using ReopenSource=std::function<std::unique_ptr<transfer::Source>(const transfer::DurableState&)>;
private:
 struct Output {std::string id;transfer::Digest manifest{};Blob token;};
 transfer::StateStore& ordinary_;ReopenSource reopen_;
 std::filesystem::path root_,path_;std::string context_;
 HANDLE directory_=INVALID_HANDLE_VALUE,owner_=INVALID_HANDLE_VALUE;
 std::unique_ptr<transfer::windows::CleanupJournal> cleanup_;
 std::vector<transfer::DurableState> cache_;std::vector<Output> outputs_;Blob previous_;SecretRecord app_;bool loaded_=false;
 static constexpr size_t MaxSnapshot=960*1024;
 static void valid(const transfer::DurableState& d){
  protocol::check(d.purpose==transfer::Purpose::GroupCacheCiphertext&&protocol::id(d.metadata.id,32)&&protocol::id(d.peer)&&d.metadata.name=="ciphertext.peerchat"&&d.metadata.size>=HeaderBytes+TagBytes&&d.metadata.size<=MaxCipherBytes&&d.done<=d.metadata.size&&d.highWaterSent<=d.metadata.size&&d.state<=transfer::State::Suspended&&d.created&&d.updated>=d.created&&d.expires>=d.created&&d.expires-d.created<=transfer::RetentionMilliseconds&&d.location.size()<=2048&&d.error.size()<=256);
 }
 static transfer::Digest fingerprint(const Manifest& m){m.validate();protocol::Writer w(protocol::GroupEvent);writePrivateManifest(w,m);SecretRecord secret(std::move(w.data));transfer::windows::Sha256 h;hash(h,secret.bytes);return h.finish();}
 static void digest(protocol::Writer& w,const transfer::Digest& d){w.bytes(Blob(d.begin(),d.end()));}
 static transfer::Digest digest(protocol::Reader& r){auto bytes=r.bytes();protocol::check(bytes.size()==32);transfer::Digest d{};std::copy(bytes.begin(),bytes.end(),d.begin());return d;}
 static void write(protocol::Writer& w,const transfer::DurableState& d){
  valid(d);w.string(d.metadata.id);w.string(d.peer);w.number(d.metadata.size);digest(w,d.metadata.digest);w.number(static_cast<unsigned>(d.state));w.number(d.sending);w.number(d.digestReady);w.number(d.done);w.number(d.highWaterSent);w.number(d.created);w.number(d.updated);w.number(d.expires);w.bytes(d.location);w.string(d.error);
 }
 static transfer::DurableState read(protocol::Reader& r){
  transfer::DurableState d;d.purpose=transfer::Purpose::GroupCacheCiphertext;d.metadata.id=r.string();d.peer=r.string();d.metadata.name="ciphertext.peerchat";d.metadata.size=r.number();d.metadata.digest=digest(r);auto state=r.number(),sending=r.number(),ready=r.number();protocol::check(state<=static_cast<unsigned>(transfer::State::Suspended)&&sending<=1&&ready<=1);d.state=static_cast<transfer::State>(state);d.sending=sending!=0;d.digestReady=ready!=0;d.done=r.number();d.highWaterSent=r.number();d.created=r.number();d.updated=r.number();d.expires=r.number();d.location=r.bytes();d.error=r.string();valid(d);return d;
 }
 Blob readSnapshot()const{
  HANDLE file=CreateFileW(path_.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE){transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);return {};}
  try{BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};transfer::windows::require(GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileSizeEx(file,&size)&&size.QuadPart>0&&static_cast<std::uint64_t>(size.QuadPart)<=MaxSnapshot+16384);
   Blob bytes(static_cast<size_t>(size.QuadPart));DWORD got=0;transfer::windows::require(ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr)&&got==bytes.size());CloseHandle(file);return bytes;
  }catch(...){CloseHandle(file);throw;}
 }
 void commit(const std::vector<transfer::DurableState>& cache,const std::vector<Output>& outputs,const Blob& app){
  protocol::check(loaded_&&cache.size()<=transfer::MaxRecords&&outputs.size()<=transfer::MaxRecords&&app.size()<=MaxAppState);protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/GroupAttachmentTransferState/v1");w.number(cache.size());for(const auto& d:cache)write(w,d);w.number(outputs.size());for(const auto& output:outputs){w.string(output.id);digest(w,output.manifest);w.bytes(output.token);}w.number(app.size());for(size_t off=0;off<app.size();off+=transfer::ChunkBytes)w.bytes(Blob(app.begin()+off,app.begin()+std::min(app.size(),off+transfer::ChunkBytes)));SecretRecord plain(std::move(w.data));protocol::check(plain.bytes.size()<=MaxSnapshot);
  if(plain.bytes==previous_)return;
  platform::WindowsRecordProtector protector;auto sealed=protector.protect(plain.bytes,context_);auto next=root_/L"state.next";
  // A fixed, exclusively owned scratch file bounds abandoned writes to one.
  // Never truncate a substituted reparse point or multiply linked file.
  HANDLE file=CreateFileW(next.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(file!=INVALID_HANDLE_VALUE);
  try{BY_HANDLE_FILE_INFORMATION info{};transfer::windows::require(GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));LARGE_INTEGER start{};transfer::windows::require(SetFilePointerEx(file,start,nullptr,FILE_BEGIN)&&SetEndOfFile(file));DWORD wrote=0;transfer::windows::require(WriteFile(file,sealed.data(),static_cast<DWORD>(sealed.size()),&wrote,nullptr)&&wrote==sealed.size()&&FlushFileBuffers(file));CloseHandle(file);file=INVALID_HANDLE_VALUE;
   transfer::windows::require(MoveFileExW(next.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0);previous_.swap(plain.bytes);
  }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);throw;}
 }
public:
 static constexpr size_t MaxAppState=512*1024;
 // Node can open its transfer owner before discovery. In this form the
 // application state must retain the immutable authenticated server per job.
 CacheStateStore(const std::wstring& profile,const std::string& device,transfer::StateStore& ordinary,ReopenSource reopen={}):CacheStateStore(profile,device,std::string{},ordinary,std::move(reopen)){}
 CacheStateStore(const std::wstring& profile,const std::string& device,const std::string& serverFingerprint,transfer::StateStore& ordinary,ReopenSource reopen={}):ordinary_(ordinary),reopen_(std::move(reopen)),root_(std::filesystem::absolute(profile)/L"group-attachment-transfers"),path_(root_/L"state.protected"){
  protocol::check(protocol::id(device)&&(serverFingerprint.empty()||protocol::id(serverFingerprint)));context_="PeerChat/GroupAttachmentTransferState/v1/"+device+"/"+serverFingerprint+"/"+unicode::encode(std::filesystem::absolute(profile).wstring());std::filesystem::create_directory(root_);
  try{directory_=CreateFileW(root_.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(directory_!=INVALID_HANDLE_VALUE);BY_HANDLE_FILE_INFORMATION info{};transfer::windows::require(GetFileInformationByHandle(directory_,&info)&&(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT));auto lock=root_/L"owner.lock";owner_=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(owner_!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(owner_,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));cleanup_=std::make_unique<transfer::windows::CleanupJournal>(root_.wstring());
  }catch(...){if(owner_!=INVALID_HANDLE_VALUE)CloseHandle(owner_);if(directory_!=INVALID_HANDLE_VALUE)CloseHandle(directory_);throw;}
 }
 ~CacheStateStore(){platform::group_crypto::wipe(previous_.data(),previous_.size());if(owner_!=INVALID_HANDLE_VALUE)CloseHandle(owner_);if(directory_!=INVALID_HANDLE_VALUE)CloseHandle(directory_);}
 CacheStateStore(const CacheStateStore&)=delete;CacheStateStore& operator=(const CacheStateStore&)=delete;
 bool supports(transfer::Purpose p)const override{return p==transfer::Purpose::GroupCacheCiphertext||ordinary_.supports(p);}
 std::uint64_t wallTime()const override{return ordinary_.wallTime();}
 std::map<std::string,transfer::Options> loadOptions()override{return ordinary_.loadOptions();}
 void saveOptions(const std::map<std::string,transfer::Options>& rows)override{ordinary_.saveOptions(rows);}
 std::optional<std::uint64_t> loadDownloadLimit()override{return ordinary_.loadDownloadLimit();}
 void saveDownloadLimit(std::uint64_t bits)override{ordinary_.saveDownloadLimit(bits);}
 std::vector<transfer::DurableState> load()override{
  loaded_=false;
  auto legacy=ordinary_.load();std::set<std::string> ids;for(const auto& d:legacy)protocol::check(d.purpose==transfer::Purpose::PeerFile&&ids.insert(d.metadata.id).second);
  auto sealed=readSnapshot();SecretRecord plain,app;std::vector<transfer::DurableState> cache;std::vector<Output> outputs;std::set<std::string> outputIds;
  if(!sealed.empty()){platform::WindowsRecordProtector protector;plain.bytes=protector.unprotect(sealed,context_);protocol::check(plain.bytes.size()<=MaxSnapshot);protocol::Reader r(plain.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/GroupAttachmentTransferState/v1");auto count=r.number();protocol::check(count<=transfer::MaxRecords);while(count--){auto d=read(r);protocol::check(ids.insert(d.metadata.id).second);cache.push_back(std::move(d));}count=r.number();protocol::check(count<=transfer::MaxRecords);while(count--){Output output;output.id=r.string();output.manifest=digest(r);output.token=r.bytes();protocol::check(protocol::id(output.id,32)&&!output.token.empty()&&output.token.size()<=2048&&outputIds.insert(output.id).second);outputs.push_back(std::move(output));}auto size=r.number();protocol::check(size<=MaxAppState);while(app.bytes.size()<size){auto part=r.bytes();protocol::check(part.size()==std::min<std::uint64_t>(transfer::ChunkBytes,size-app.bytes.size()));app.bytes.insert(app.bytes.end(),part.begin(),part.end());platform::group_crypto::wipe(part.data(),part.size());}r.end();}
  protocol::check(legacy.size()+cache.size()<=transfer::MaxRecords);legacy.insert(legacy.end(),cache.begin(),cache.end());cleanup_->recover(outputIds);cache_=std::move(cache);outputs_=std::move(outputs);previous_.swap(plain.bytes);app_.bytes.swap(app.bytes);loaded_=true;return legacy;
 }
 void save(const std::vector<transfer::DurableState>& records)override{
  protocol::check(loaded_&&records.size()<=transfer::MaxRecords);std::vector<transfer::DurableState> legacy,cache;std::set<std::string> ids;
  for(const auto& d:records){protocol::check(ids.insert(d.metadata.id).second);if(d.purpose==transfer::Purpose::PeerFile){protocol::check(std::none_of(cache_.begin(),cache_.end(),[&](const auto& old){return old.metadata.id==d.metadata.id;}));legacy.push_back(d);}else{valid(d);auto old=std::find_if(cache_.begin(),cache_.end(),[&](const auto& value){return value.metadata.id==d.metadata.id;});if(old!=cache_.end())protocol::check(old->metadata==d.metadata&&old->peer==d.peer&&old->sending==d.sending&&old->created==d.created&&old->expires==d.expires&&(old->location.empty()||old->location==d.location));cache.push_back(d);}}
  // Each record has exactly one durable owner. A partially successful split
  // save never grants an ACK: TransferManager waits for this call to succeed.
  ordinary_.save(legacy);commit(cache,outputs_,app_.bytes);cache_=std::move(cache);
 }
 std::unique_ptr<transfer::Source> source(const transfer::DurableState& d)override{if(d.purpose==transfer::Purpose::PeerFile)return ordinary_.source(d);valid(d);return reopen_?reopen_(d):transfer::windows::FileSource::reopen(d);}
 std::unique_ptr<transfer::Sink> sink(const transfer::DurableState& d)override{if(d.purpose==transfer::Purpose::PeerFile)return ordinary_.sink(d);valid(d);return transfer::windows::FileSink::reopen(d);}
 Blob outputToken(const Manifest& m)const{protocol::check(loaded_);auto found=std::find_if(outputs_.begin(),outputs_.end(),[&](const auto& output){return output.id==m.binding.attachment;});if(found==outputs_.end())return {};protocol::check(found->manifest==fingerprint(m));return found->token;}
 void rememberOutput(const Manifest& m,const Blob& token){protocol::check(loaded_&&!token.empty()&&token.size()<=2048);auto prior=outputToken(m);if(!prior.empty()){protocol::check(prior==token);return;}protocol::check(outputs_.size()<transfer::MaxRecords);auto next=outputs_;next.push_back({m.binding.attachment,fingerprint(m),token});commit(cache_,next,app_.bytes);outputs_=std::move(next);}
 Blob appState()const{protocol::check(loaded_);return app_.bytes;}
 void saveAppState(const Blob& expected,const Blob& next){protocol::check(loaded_&&expected==app_.bytes&&next.size()<=MaxAppState);SecretRecord value(next);commit(cache_,outputs_,value.bytes);app_.bytes.swap(value.bytes);}
 // Call only after a verified output receipt/token is durably retained by the
 // application, with no active finalizer for this object. Retires metadata,
 // never the completed output. Unknown/mismatched intents cannot be removed.
 void forgetOutput(const Manifest& m,const Blob& expected){protocol::check(!expected.empty()&&outputToken(m)==expected);auto next=outputs_;next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& output){return output.id==m.binding.attachment;}),next.end());commit(cache_,next,app_.bytes);outputs_=std::move(next);}
 transfer::windows::CleanupJournal& outputCleanup(){protocol::check(loaded_);return *cleanup_;}
};

// Reopens the same ciphertext identity retained by TransferManager. It does not
// accept an arbitrary path or turn a ciphertext ACK into plaintext verification.
class CompletedCipherSource final:public transfer::Source {
 std::unique_ptr<transfer::windows::FileSink> file_;transfer::Metadata metadata_;Blob token_;
public:
 explicit CompletedCipherSource(const transfer::DurableState& d,const Manifest& m):metadata_(d.metadata),token_(d.location){
  m.validate();protocol::check(d.purpose==transfer::Purpose::GroupCacheCiphertext&&!d.sending&&d.state==transfer::State::Complete&&d.done==d.metadata.size&&d.metadata.id==m.binding.attachment&&d.metadata.name=="ciphertext.peerchat"&&d.metadata.size==m.cipherSize()&&d.metadata.digest==m.cipherDigest);file_=transfer::windows::FileSink::reopen(d,true);
 }
 std::uint64_t size()const override{return metadata_.size;}std::string name()const override{return metadata_.name;}Blob resumeToken()const override{return token_;}
 Blob read(std::uint64_t offset,size_t count)override{protocol::check(offset<=size()&&count<=transfer::ChunkBytes&&count<=size()-offset);return file_->read(offset,count);}
};

// Borrowed immutable manifest must outlive this job. One header, AEAD block,
// recovery hash chunk or commit per call. At most a single block is buffered.
// Caller queues at most TransferManager::MaxTransfers jobs on its existing loop.
class ReceiverFinalizer {
 const Manifest& manifest_;std::unique_ptr<transfer::Source> ciphertext_;
 std::unique_ptr<transfer::windows::FileSink> plaintext_;transfer::windows::Sha256 plainHash_,cipherHash_;
 std::uint64_t index_=0,position_=HeaderBytes,processed_=0,recoveryCipherOffset_=0;bool header_=false,recovering_=false,recoveryPlainReady_=false,verified_=false,failed_=false;std::string error_;
 std::wstring outputPath_;Blob outputToken_;
 void complete(){plaintext_->commit();verified_=true;plaintext_.reset();ciphertext_.reset();}
public:
 ReceiverFinalizer(CacheStateStore& store,const transfer::DurableState& completed,const Manifest& manifest,const std::wstring& destination,const std::string& selectedBasename={}):ReceiverFinalizer(store,std::make_unique<CompletedCipherSource>(completed,manifest),manifest,destination,selectedBasename){}
 // Sender preview uses its exact protected StagedCipherSource. The caller
 // supplies immutable local ciphertext; both full hashes still gate commit.
 ReceiverFinalizer(CacheStateStore& store,std::unique_ptr<transfer::Source> ciphertext,const Manifest& manifest,const std::wstring& destination,const std::string& selectedBasename={}):manifest_(manifest),ciphertext_(std::move(ciphertext)){
  manifest_.validate();protocol::check(ciphertext_&&ciphertext_->size()==manifest_.cipherSize()&&ciphertext_->name()=="ciphertext.peerchat"&&(selectedBasename.empty()||transfer::safeName(selectedBasename)));auto token=store.outputToken(manifest_);transfer::Metadata metadata{manifest_.binding.attachment,manifest_.name,manifest_.size,manifest_.plainDigest};
  const auto expected=std::filesystem::absolute(std::filesystem::path(destination)/unicode::decode(selectedBasename.empty()?metadata.id+"-"+metadata.name:selectedBasename));
  if(token.empty()){
   plaintext_=std::make_unique<transfer::windows::FileSink>(destination,metadata,&store.outputCleanup(),true,false,selectedBasename);
   try{store.rememberOutput(manifest_,plaintext_->resumeToken());}catch(...){plaintext_->discard();throw;}
  }else{
   // Reject a changed chooser target before reopening a partial: that reopen
   // may truncate unverified bytes back to its last durable cursor.
   protocol::Reader intent(token);protocol::check(intent.type()==protocol::FileTransfer);intent.text();protocol::check(std::filesystem::path(intent.text())==expected);for(unsigned n=0;n<3;++n)intent.number();
   if(selectedBasename.empty())protocol::check(!intent.remaining());
   else protocol::check(intent.remaining()&&intent.string()=="PeerChat/SelectedFileName/v1"&&intent.string()==metadata.name&&intent.string()==selectedBasename);
   intent.end();
   transfer::DurableState output;output.metadata=metadata;output.location=token;output.done=metadata.size;
   try{plaintext_=transfer::windows::FileSink::reopen(output,true);recovering_=true;}
   catch(...){output.done=0;plaintext_=transfer::windows::FileSink::reopen(output);}
  }
  protocol::check(std::filesystem::path(plaintext_->path())==expected);
  outputPath_=plaintext_->path();outputToken_=plaintext_->resumeToken();
 }
 ReceiverFinalizer(const ReceiverFinalizer&)=delete;ReceiverFinalizer& operator=(const ReceiverFinalizer&)=delete;
 // Normal shutdown preserves the exact protected intent/partial for recovery.
 bool verified()const{return verified_;}bool failed()const{return failed_;}const std::string& error()const{return error_;}
 std::uint64_t processedBytes()const{return processed_;}std::uint64_t size()const{return manifest_.size;}
 std::wstring path()const{protocol::check(verified_);return outputPath_;}
 Blob token()const{protocol::check(verified_);return outputToken_;}
 bool step(){
  protocol::check(!failed_);if(verified_)return true;
  try{
   if(recovering_){
    if(processed_<manifest_.size){auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,manifest_.size-processed_));PlainBuffer bytes{plaintext_->read(processed_,count)};hash(plainHash_,bytes.bytes);processed_+=count;return false;}
    if(!recoveryPlainReady_){protocol::check(plainHash_.finish()==manifest_.plainDigest);recoveryPlainReady_=true;return false;}
    if(recoveryCipherOffset_<ciphertext_->size()){auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,ciphertext_->size()-recoveryCipherOffset_));auto bytes=group_attachments::read(*ciphertext_,recoveryCipherOffset_,count);hash(cipherHash_,bytes);recoveryCipherOffset_+=count;return false;}
    protocol::check(cipherHash_.finish()==manifest_.cipherDigest);complete();return true;
   }
   if(!header_){auto bytes=group_attachments::read(*ciphertext_,0,HeaderBytes);protocol::check(bytes==header(manifest_));hash(cipherHash_,bytes);header_=true;return false;}
   if(index_<manifest_.blocks()){
    auto count=static_cast<size_t>(std::min<std::uint64_t>(BlockBytes,manifest_.size-index_*BlockBytes));auto block=group_attachments::read(*ciphertext_,position_,count+TagBytes);position_+=block.size();hash(cipherHash_,block);Blob tag(block.end()-TagBytes,block.end());block.resize(count);PlainBuffer plain;
    protocol::check(platform::group_crypto::gcm(false,manifest_.key,aad(manifest_,index_,count),nonce(manifest_,index_),block,plain.bytes,tag));protocol::check(plain.bytes.size()==count);hash(plainHash_,plain.bytes);append(*plaintext_,plain.bytes);++index_;processed_+=count;return false;
   }
   protocol::check(position_==ciphertext_->size()&&plainHash_.finish()==manifest_.plainDigest&&cipherHash_.finish()==manifest_.cipherDigest);plaintext_->flush();complete();return true;
  }catch(...){failed_=true;error_="Attachment plaintext verification or commit failed";throw;}
 }
};
}
