#pragma once
#include "android_group_attachment_history.hpp"
#include "group_attachment_file_io.hpp"
#include "group_attachment_prepared_keys.hpp"
#include "../group_attachment_application.hpp"
namespace peerchat::group_attachments::android {
class LocalState {
 Storage& storage_;std::string device_;
 static std::string key(const Manifest& m){return Storage::publicKeyId(Blob(m.binding.attachment.begin(),m.binding.attachment.end()));}
 static std::string fingerprint(const Manifest& m){protocol::Writer w(protocol::GroupEvent);writePrivateManifest(w,m);SecretRecord bytes(std::move(w.data));return Storage::publicKeyId(bytes.bytes);}
public:
 LocalState(Storage& storage,std::string device):storage_(storage),device_(std::move(device)){}
 Blob outputToken(const Manifest& m){SecretRecord bytes(storage_.groupAttachmentLocal(device_,key(m)));if(bytes.bytes.empty())return {};protocol::Reader r(bytes.bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/AndroidGroupOutput/v1"&&r.string()==fingerprint(m));auto token=r.bytes();r.end();return token;}
 void rememberOutput(const Manifest& m,const Blob& previous,const Blob& token){protocol::check(outputToken(m)==previous&&!token.empty());auto id=key(m);SecretRecord old(storage_.groupAttachmentLocal(device_,id));protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/AndroidGroupOutput/v1");w.string(fingerprint(m));w.bytes(token);storage_.saveGroupAttachmentLocal(device_,id,old.bytes,w.data);}
 void forgetOutput(const Manifest& m,const Blob& token){protocol::check(outputToken(m)==token);auto id=key(m);SecretRecord old(storage_.groupAttachmentLocal(device_,id));storage_.saveGroupAttachmentLocal(device_,id,old.bytes,{});}
};
class ReceiverFinalizer {
 group_attachments::posix::ReceiverFinalizer inner_;std::string root_;
public:
 ReceiverFinalizer(LocalState& store,std::shared_ptr<transfer::posix::Cache> cache,std::string root,std::unique_ptr<transfer::Source> source,const Manifest& m)
 :inner_(std::move(cache),std::move(source),m,store.outputToken(m),[&store,&m](const Blob& old,const Blob& next){store.rememberOutput(m,old,next);}),root_(std::move(root)){}
 void cancel(){inner_.cancel();}
 bool step(){return inner_.step();}bool verified()const{return inner_.verified();}bool failed()const{return inner_.failed();}
 std::uint64_t processedBytes()const{return inner_.processedBytes();}Blob token()const{return inner_.token();}
 std::wstring path()const{return unicode::decode(root_+"/"+inner_.output()->cacheName());}
};
class VerifiedOutput {
 std::unique_ptr<transfer::posix::FileSource> file_;transfer::posix::Sha256 hash_;transfer::Digest digest_;std::uint64_t at_=0;bool verified_=false,failed_=false;std::wstring path_;
public:
 VerifiedOutput(std::shared_ptr<transfer::posix::Cache> cache,const std::string& root,const Manifest& m,const Blob& token):digest_(m.plainDigest){
  protocol::Reader r(token);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/CacheSink/v1");auto id=r.string();protocol::check(r.string()==m.name);
  transfer::DurableState d;d.metadata={id,m.name,m.size,m.plainDigest};d.done=m.size;d.location=token;auto sink=transfer::posix::FileSink::reopen(cache,d);protocol::check(sink->committed());d.location=sink->sourceToken();file_=transfer::posix::FileSource::reopen(cache,d);path_=unicode::decode(root+"/"+file_->cacheName());
 }
 bool step(){protocol::check(!failed_);if(verified_)return true;try{if(at_<file_->size()){auto bytes=file_->read(at_,static_cast<size_t>(std::min<std::uint64_t>(transfer::ChunkBytes,file_->size()-at_)));hash_.update(bytes.data(),bytes.size());at_+=bytes.size();return false;}protocol::check(hash_.finish()==digest_);verified_=true;return true;}catch(...){failed_=true;throw;}}
 bool verified()const{return verified_;}bool failed()const{return failed_;}std::wstring path()const{protocol::check(verified_);return path_;}
};
// Android exports verified internal files through its existing SAF coordinator.
// Native desktop path-export operations are rejected by the Android bridge.
class OutputExport {
public:
 OutputExport(VerifiedOutput&,const std::wstring&,const std::string&,const Blob&,const Blob&){throw std::runtime_error("Use Android document export");}
 bool complete()const{return false;}bool failed()const{return true;}bool step(){return false;}Blob resumeToken()const{return {};}void finishCleanup(){}
};
class PreparationCleanup {
 Storage& storage_;std::string device_,cursor_;std::shared_ptr<transfer::posix::Cache> cache_;
 static std::string key(const std::string& id){std::string domain="PeerChat/AndroidGroupPreparation/v1/"+id;return Storage::publicKeyId(Blob(domain.begin(),domain.end()));}
public:
 PreparationCleanup(Storage& storage,std::string device,std::shared_ptr<transfer::posix::Cache> cache):storage_(storage),device_(std::move(device)),cache_(std::move(cache)){}
 void record(const transfer::Metadata& m,const Blob& token){auto id=key(m.id);SecretRecord old(storage_.groupAttachmentLocal(device_,id));protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/AndroidGroupPreparation/v1");w.string(m.id);w.number(m.size);w.bytes(token);storage_.saveGroupAttachmentLocal(device_,id,old.bytes,w.data);}
 void recover(const std::set<std::string>& retained){
  const auto page=storage_.groupAttachmentLocalKeys(device_,cursor_,64);
  for(const auto& keyId:page){cursor_=keyId;SecretRecord old(storage_.groupAttachmentLocal(device_,keyId));if(old.bytes.empty())continue;
   protocol::Reader r(old.bytes);protocol::check(r.type()==protocol::FileTransfer);if(r.string()!="PeerChat/AndroidGroupPreparation/v1")continue;
   auto id=r.string();auto size=r.number();auto token=r.bytes();r.end();protocol::check(protocol::id(id,32)&&key(id)==keyId&&size<=transfer::MaxCacheCipherBytes);
   if(retained.count(id))continue;
   // A committed ciphertext can be a sole recoverable copy. This orphan-partial
   // journal never removes one; explicit cancellation owns that separate action.
   struct stat committedEntry{};auto name=id+"-ciphertext.peerchat";
   if(fstatat(cache_->fd(),name.c_str(),&committedEntry,AT_SYMLINK_NOFOLLOW)!=0){protocol::check(errno==ENOENT);transfer::posix::discardCiphertext(cache_,token,{id,"ciphertext.peerchat",size,{}},transfer::Purpose::GroupCacheCiphertext);}
   storage_.saveGroupAttachmentLocal(device_,keyId,old.bytes,{});
  }if(page.size()<64)cursor_.clear();
 }
};
struct ApplicationPlatform {
 using CacheStateStore=LocalState;using HistoryStore=PortableHistoryStore;using PreparedKeys=group_attachments::portable::PreparedKeys;
 using StagedCipherSource=transfer::Source;using ReceiverFinalizer=android::ReceiverFinalizer;using VerifiedOutput=android::VerifiedOutput;using OutputExport=android::OutputExport;
 using FileSource=transfer::Source;using FileSink=transfer::Sink;using Sha256=transfer::posix::Sha256;using CleanupJournal=PreparationCleanup;
 struct Directories {explicit Directories(const std::filesystem::path&){throw std::runtime_error("Use Android document export");}};
 Storage& storage_;std::string device_;std::string root_;std::shared_ptr<transfer::posix::Cache> cache_;
 ApplicationPlatform(Storage& storage,const Profile& profile,const std::wstring& directory):storage_(storage),device_(profile.peerId),root_(unicode::encode(directory)),cache_(std::make_shared<transfer::posix::Cache>(root_)){}
 std::unique_ptr<FileSource> source(const std::wstring& path){auto p=std::filesystem::path(unicode::encode(path));protocol::check(p.parent_path().string()==root_);auto name=p.filename().string();protocol::check(name.size()>33);return std::make_unique<transfer::posix::FileSource>(cache_,name,name.substr(33));}
 std::unique_ptr<FileSink> sink(const std::wstring& path,const transfer::Metadata& m,CleanupJournal* cleanup){protocol::check(unicode::encode(path)==root_);auto result=std::make_unique<transfer::posix::FileSink>(cache_,m,transfer::Purpose::GroupCacheCiphertext);try{if(cleanup)cleanup->record(m,result->resumeToken());}catch(...){result->discard();throw;}return result;}
 void discardCipher(const Blob& token,const transfer::Metadata& m){transfer::posix::discardCiphertext(cache_,token,m,transfer::Purpose::GroupCacheCiphertext);}
 Blob location(const transfer::Sink& sink){return sink.resumeToken();}
 std::unique_ptr<transfer::Source> staged(const Staged& value,const std::string& device){return group_attachments::posix::stagedSource(cache_,value,device);}
 std::unique_ptr<ReceiverFinalizer> finalizer(LocalState& store,std::unique_ptr<transfer::Source> source,const Manifest& m,const std::wstring& path){protocol::check(unicode::encode(path)==root_);return std::make_unique<ReceiverFinalizer>(store,cache_,root_,std::move(source),m);}
 std::unique_ptr<ReceiverFinalizer> finalizer(LocalState& store,const transfer::DurableState& state,const Manifest& m,const std::wstring& path){auto sink=transfer::posix::FileSink::reopen(cache_,state);auto d=state;d.location=sink->sourceToken();return finalizer(store,transfer::posix::FileSource::reopen(cache_,d),m,path);}
 std::unique_ptr<VerifiedOutput> output(const Manifest& m,const Blob& token){return std::make_unique<VerifiedOutput>(cache_,root_,m,token);}
 std::pair<std::wstring,std::wstring> directories(const std::wstring& path){protocol::check(unicode::encode(path)==root_);return {path,path};}
 std::unique_ptr<CleanupJournal> cleanup(const std::wstring& path){protocol::check(unicode::encode(path)==root_);return std::make_unique<CleanupJournal>(storage_,device_,cache_);}
 static std::unique_ptr<HistoryStore> history(Storage&,const Profile& profile,const std::wstring& path){return openHistory(profile,std::make_shared<transfer::posix::Cache>(unicode::encode(path)));}
 static std::unique_ptr<PreparedKeys> preparedKeys(Storage& storage,const Profile& profile,const std::wstring&,const std::string& server){return std::make_unique<PreparedKeys>(storage,profile.peerId,server);}
 static std::uint64_t monotonic(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
 static void requireMissing(const std::filesystem::path&){throw std::runtime_error("Use Android document export");}
 static bool publishedExportMatches(const std::wstring&,const std::string&,const Blob&,const Blob&){return false;}
 static void cancelPendingExport(const std::wstring&,const std::string&,const Blob&,const Blob&){throw std::runtime_error("Use Android document export");}
 static std::string saveFailureReason(const std::exception&){return "Unable to save attachment";}
};
using Application=BasicApplication<ApplicationPlatform>;
}