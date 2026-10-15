#pragma once
#include "windows_group_attachment_transfer.hpp"
#include <cstring>

namespace peerchat::group_attachments::windows {
// Do not leak arbitrary exception text into UI/logs. These messages originate
// only in the local copy layer; the OS code distinguishes denied/missing/full.
inline std::string saveFailureReason(const std::exception& error){std::string reason=error.what();if(reason.rfind("File I/O failed (Win32 ",0)==0&&reason.size()<80)return reason+". Verified internal copy retained; use Retry Save.";if(reason=="Selected file changed since overwrite confirmation")return reason+". Confirm the current file again or choose another name.";return "Destination unavailable, changed or not writable. Verified internal copy retained; use Retry Save.";}
namespace output_detail {
// Hold each directory without delete sharing so a verified path cannot be
// redirected through a renamed ancestor while the caller uses the result.
class Directories {
 std::vector<HANDLE> handles_;
public:
 explicit Directories(std::filesystem::path directory){
  protocol::check(directory.is_absolute()&&directory.wstring().size()<=16384);handles_.reserve(256);
  try{for(;;){protocol::check(handles_.size()<256);HANDLE handle=CreateFileW(directory.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(handle!=INVALID_HANDLE_VALUE);BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(handle,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){CloseHandle(handle);throw std::runtime_error("Unsafe attachment output directory");}handles_.push_back(handle);auto parent=directory.parent_path();if(parent.empty()||parent==directory)break;directory=std::move(parent);}}
  catch(...){for(auto handle:handles_)CloseHandle(handle);throw;}
 }
 ~Directories(){for(auto handle:handles_)CloseHandle(handle);}
 Directories(const Directories&)=delete;Directories& operator=(const Directories&)=delete;
};
}

// Shared read handle denies mutation/deletion while this verified access lease
// is retained. No plaintext path or bytes are exposed before a fresh full hash.
class VerifiedLocalSource:public transfer::Source {
 std::unique_ptr<output_detail::Directories> directories_;std::unique_ptr<transfer::windows::FileSource> file_;
 transfer::Metadata expected_;transfer::windows::Sha256 hash_;std::wstring path_;std::uint64_t processed_=0;bool verified_=false,failed_=false;
public:
 VerifiedLocalSource(const transfer::Metadata& metadata,const Blob& sinkToken,bool fromSourceToken=false):expected_(metadata){
  protocol::check(protocol::id(expected_.id,32)&&transfer::safeName(expected_.name));
  if(fromSourceToken){protocol::Reader source(sinkToken);protocol::check(source.type()==protocol::FileTransfer);auto path=std::filesystem::path(source.text());protocol::check(path.is_absolute()&&transfer::safeName(unicode::encode(path.filename().wstring())));directories_=std::make_unique<output_detail::Directories>(path.parent_path());transfer::DurableState state;state.location=sinkToken;file_=transfer::windows::FileSource::reopen(state);protocol::check(file_->name()==expected_.name&&file_->size()==expected_.size);path_=path.wstring();return;}
  protocol::Reader token(sinkToken);protocol::check(token.type()==protocol::FileTransfer);auto partial=std::filesystem::path(token.text());auto final=std::filesystem::path(token.text());protocol::check(partial.is_absolute()&&final.is_absolute()&&partial.parent_path()==final.parent_path()&&partial.filename()==expected_.id+".peerchat-part");directories_=std::make_unique<output_detail::Directories>(final.parent_path());
  // Opening the final name directly refuses an uncommitted partial. FileSource
  // also rejects reparse points and holds the read handle throughout hashing.
  file_=std::make_unique<transfer::windows::FileSource>(final.wstring(),expected_.name);protocol::check(file_->size()==expected_.size);auto sourceToken=file_->resumeToken();protocol::Reader actual(sourceToken);protocol::check(actual.type()==protocol::FileTransfer&&actual.text()==final.wstring());for(unsigned n=0;n<3;++n)protocol::check(token.number()==actual.number());
  if(token.remaining()){protocol::check(token.string()=="PeerChat/SelectedFileName/v1"&&token.string()==expected_.name);auto selected=token.string();auto name=unicode::decode(selected),part=unicode::decode(expected_.id+".peerchat-part");protocol::check(transfer::safeName(selected)&&final.filename()==name&&CompareStringOrdinal(name.c_str(),-1,part.c_str(),-1,TRUE)!=CSTR_EQUAL);}
  else protocol::check(final.filename()==unicode::decode(expected_.id+"-"+expected_.name)||(partial.parent_path().filename()==unicode::decode(expected_.id)+L".peerchat-received"&&final.filename()==unicode::decode(expected_.name)));
  token.end();path_=final.wstring();
 }
 VerifiedLocalSource(const VerifiedLocalSource&)=delete;VerifiedLocalSource& operator=(const VerifiedLocalSource&)=delete;
 bool step(){protocol::check(!failed_);if(verified_)return true;try{if(processed_<expected_.size){auto count=static_cast<size_t>(std::min<std::uint64_t>(transfer::ChunkBytes,expected_.size-processed_));PlainBuffer bytes{file_->read(processed_,count)};protocol::check(bytes.bytes.size()==count);hash(hash_,bytes.bytes);processed_+=count;return false;}protocol::check(hash_.finish()==expected_.digest);verified_=true;return true;}catch(...){failed_=true;throw;}}
 bool verified()const{return verified_;}bool failed()const{return failed_;}std::uint64_t processedBytes()const{return processed_;}
 std::wstring path()const{protocol::check(verified_);return path_;}
 std::uint64_t size()const override{return expected_.size;}std::string name()const override{return expected_.name;}
 const transfer::Digest& expectedDigest()const{return expected_.digest;}
 Blob resumeToken()const override{protocol::check(verified_);return file_->resumeToken();}
 Blob read(std::uint64_t offset,size_t count)override{protocol::check(verified_);return file_->read(offset,count);}
};
class VerifiedOutput final:public VerifiedLocalSource {
public:
 VerifiedOutput(const Manifest& manifest,const Blob& sinkToken):VerifiedLocalSource({manifest.binding.attachment,manifest.name,manifest.size,manifest.plainDigest},sinkToken){manifest.validate();}
};

namespace output_detail {
inline void renameOwned(HANDLE file,const std::wstring& target){auto length=target.size()*sizeof(wchar_t);protocol::check(length<=32767*sizeof(wchar_t));std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO,FileName)+length+sizeof(wchar_t));auto info=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());info->ReplaceIfExists=FALSE;info->RootDirectory=nullptr;info->FileNameLength=static_cast<DWORD>(length);std::memcpy(info->FileName,target.c_str(),length+sizeof(wchar_t));if(!SetFileInformationByHandle(file,FileRenameInfo,info,static_cast<DWORD>(storage.size()))){auto error=GetLastError();if(error==ERROR_ALREADY_EXISTS||error==ERROR_FILE_EXISTS)throw std::runtime_error("Save destination already exists");transfer::windows::require(false);}}
inline void identity(protocol::Writer& writer,HANDLE file){BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};transfer::windows::require(GetFileInformationByHandle(file,&info)&&GetFileSizeEx(file,&size)&&size.QuadPart>=0&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));writer.number(info.dwVolumeSerialNumber);writer.number(info.nFileIndexHigh);writer.number(info.nFileIndexLow);writer.number(static_cast<std::uint64_t>(size.QuadPart));writer.number(info.ftLastWriteTime.dwHighDateTime);writer.number(info.ftLastWriteTime.dwLowDateTime);}
inline void checkApproval(const Blob& approval,const std::wstring& target,HANDLE file){try{protocol::Reader reader(approval);protocol::check(reader.type()==protocol::FileTransfer&&reader.string()=="PeerChat/ExportOverwriteApproval/v1"&&reader.text()==target);protocol::Writer actual(protocol::FileTransfer);identity(actual,file);protocol::Reader expected(actual.data);for(unsigned n=0;n<6;++n)protocol::check(reader.number()==expected.number());reader.end();}catch(...){throw std::runtime_error("Selected file changed since overwrite confirmation");}}
}
inline Blob captureOverwriteTarget(const std::wstring& target){auto path=std::filesystem::path(target);protocol::check(path.is_absolute()&&path==path.lexically_normal()&&transfer::safeName(unicode::encode(path.filename().wstring())));output_detail::Directories held(path.parent_path());HANDLE file=CreateFileW(target.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(file!=INVALID_HANDLE_VALUE);try{protocol::Writer writer(protocol::FileTransfer);writer.string("PeerChat/ExportOverwriteApproval/v1");writer.text(target);output_detail::identity(writer,file);CloseHandle(file);return std::move(writer.data);}catch(...){CloseHandle(file);throw;}}

// Read-only evidence for cleanup of an ALREADY published replacement. This is
// not overwrite authorization: both the new inode and old approved backup must
// match, and OutputExport must rehash the published file before backup cleanup.
inline bool publishedExportMatches(const std::wstring& target,const std::string& id,const Blob& approval,const Blob& token){
 HANDLE current=INVALID_HANDLE_VALUE,old=INVALID_HANDLE_VALUE;
 try{protocol::check(protocol::id(id,32)&&!approval.empty());auto parent=std::filesystem::path(target).parent_path();output_detail::Directories held(parent);auto part=(parent/unicode::decode(id+".peerchat-export-part")).wstring();auto backup=(parent/unicode::decode(id+".peerchat-replaced")).wstring();protocol::Reader r(token);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/OutputExport/v1"&&r.text()==part&&r.text()==target&&r.string()==id);auto volume=r.number(),high=r.number(),low=r.number();protocol::check(r.bytes()==approval);auto length=r.number();protocol::check(r.bytes().size()==32);r.end();protocol::check(GetFileAttributesW(part.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND);
  current=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(current!=INVALID_HANDLE_VALUE);protocol::Writer identity(protocol::FileTransfer);output_detail::identity(identity,current);protocol::Reader actual(identity.data);protocol::check(actual.number()==volume&&actual.number()==high&&actual.number()==low&&actual.number()==length);
  old=CreateFileW(backup.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(old!=INVALID_HANDLE_VALUE);output_detail::checkApproval(approval,target,old);CloseHandle(old);CloseHandle(current);return true;
 }catch(...){if(old!=INVALID_HANDLE_VALUE)CloseHandle(old);if(current!=INVALID_HANDLE_VALUE)CloseHandle(current);return false;}
}

// A local bounded copy, not a transfer owner. The selected exact destination is
// never replaced. A held verified input and second copy hash gate publication.
class OutputExport {
 VerifiedLocalSource& source_;std::unique_ptr<output_detail::Directories> directories_;
 HANDLE file_=INVALID_HANDLE_VALUE;std::wstring final_,partial_,backup_;std::string id_;Blob approval_,token_;transfer::windows::Sha256 hash_;
 std::uint64_t processed_=0;bool committed_=false,failed_=false,durable_=false,checkingFinal_=false;
 void abandon()noexcept{if(file_!=INVALID_HANDLE_VALUE){if(!committed_&&!durable_){FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(file_,FileDispositionInfo,&remove,sizeof(remove));}CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}}
 static bool exists(const std::wstring& path){auto attributes=GetFileAttributesW(path.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES)return true;transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);return false;}
 void restoreBackup(){if(approval_.empty()||!exists(backup_))return;if(exists(final_))throw std::runtime_error("Overwrite conflict; previous file retained at backup path");HANDLE old=CreateFileW(backup_.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(old!=INVALID_HANDLE_VALUE);try{output_detail::checkApproval(approval_,final_,old);output_detail::renameOwned(old,final_);CloseHandle(old);}catch(...){CloseHandle(old);throw;}}
 void setup(const std::wstring& destination,const std::string& id,const Blob& approval,const Blob& resume,bool durable){
  protocol::check(source_.verified()&&protocol::id(id,32));auto path=std::filesystem::path(destination);protocol::check(path.is_absolute()&&path==path.lexically_normal()&&transfer::safeName(unicode::encode(path.filename().wstring())));directories_=std::make_unique<output_detail::Directories>(path.parent_path());final_=path.wstring();id_=id;approval_=approval;durable_=durable;partial_=(path.parent_path()/unicode::decode(id+".peerchat-export-part")).wstring();backup_=(path.parent_path()/unicode::decode(id+".peerchat-replaced")).wstring();protocol::check(CompareStringOrdinal(final_.c_str(),-1,partial_.c_str(),-1,TRUE)!=CSTR_EQUAL&&CompareStringOrdinal(final_.c_str(),-1,backup_.c_str(),-1,TRUE)!=CSTR_EQUAL);
  if(!approval_.empty()){protocol::Reader r(approval_);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/ExportOverwriteApproval/v1"&&r.text()==final_);for(unsigned n=0;n<6;++n)r.number();r.end();}
  if(resume.empty()){
   protocol::check(!exists(partial_)&&!exists(backup_));if(approval_.empty()){if(exists(final_))throw std::runtime_error("Save destination already exists; overwrite was not confirmed");}else{HANDLE old=CreateFileW(final_.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(old!=INVALID_HANDLE_VALUE);try{output_detail::checkApproval(approval_,final_,old);CloseHandle(old);}catch(...){CloseHandle(old);throw;}}
   file_=CreateFileW(partial_.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(file_!=INVALID_HANDLE_VALUE);protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/OutputExport/v1");w.text(partial_);w.text(final_);w.string(id_);transfer::windows::fileIdentity(w,file_,false);w.bytes(approval_);w.number(source_.size());w.bytes(Blob(source_.expectedDigest().begin(),source_.expectedDigest().end()));token_=std::move(w.data);
  }else{
   token_=resume;protocol::Reader r(token_);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/OutputExport/v1"&&r.text()==partial_&&r.text()==final_&&r.string()==id_);auto vol=r.number(),hi=r.number(),lo=r.number();protocol::check(r.bytes()==approval_&&r.number()==source_.size()&&r.bytes()==Blob(source_.expectedDigest().begin(),source_.expectedDigest().end()));r.end();
   if(!exists(partial_)){protocol::check(exists(final_));file_=CreateFileW(final_.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(file_!=INVALID_HANDLE_VALUE);checkingFinal_=true;}
   else{restoreBackup();file_=CreateFileW(partial_.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(file_!=INVALID_HANDLE_VALUE);}
   protocol::Writer actual(protocol::FileTransfer);transfer::windows::fileIdentity(actual,file_,false);protocol::Reader expected(actual.data);protocol::check(vol==expected.number()&&hi==expected.number()&&lo==expected.number());if(!checkingFinal_){LARGE_INTEGER zero{};transfer::windows::require(SetFilePointerEx(file_,zero,nullptr,FILE_BEGIN)&&SetEndOfFile(file_));}
  }
 }
public:
 OutputExport(VerifiedLocalSource& source,const std::wstring& destination,const std::string& operationId):source_(source){try{setup(destination,operationId,{}, {},false);}catch(...){abandon();throw;}}
 OutputExport(VerifiedLocalSource& source,const std::wstring& destination,const std::string& operationId,const Blob& approval,const Blob& resume={}):source_(source){try{setup(destination,operationId,approval,resume,true);}catch(...){if(file_!=INVALID_HANDLE_VALUE){if(resume.empty()){FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(file_,FileDispositionInfo,&remove,sizeof(remove));}CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}throw;}}
 ~OutputExport(){abandon();}
 OutputExport(const OutputExport&)=delete;OutputExport& operator=(const OutputExport&)=delete;
 bool step(){protocol::check(!failed_);if(committed_)return true;try{
  if(processed_<source_.size()){auto count=static_cast<size_t>(std::min<std::uint64_t>(transfer::ChunkBytes,source_.size()-processed_));Blob bytes;if(checkingFinal_){bytes.resize(count);LARGE_INTEGER at{};at.QuadPart=processed_;transfer::windows::require(SetFilePointerEx(file_,at,nullptr,FILE_BEGIN));DWORD got=0;transfer::windows::require(ReadFile(file_,bytes.data(),static_cast<DWORD>(count),&got,nullptr)&&got==count);}else bytes=source_.read(processed_,count);protocol::check(bytes.size()==count);hash(hash_,bytes);if(!checkingFinal_){DWORD wrote=0;transfer::windows::require(WriteFile(file_,bytes.data(),static_cast<DWORD>(bytes.size()),&wrote,nullptr)&&wrote==bytes.size());}processed_+=count;return false;}
  protocol::check(hash_.finish()==source_.expectedDigest());if(checkingFinal_){committed_=true;if(approval_.empty())abandon();return true;}transfer::windows::require(FlushFileBuffers(file_)!=0);
  if(!approval_.empty()){HANDLE old=CreateFileW(final_.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(old!=INVALID_HANDLE_VALUE);try{output_detail::checkApproval(approval_,final_,old);protocol::check(!exists(backup_));output_detail::renameOwned(old,backup_);CloseHandle(old);}catch(...){CloseHandle(old);throw;}}
  try{output_detail::renameOwned(file_,final_);}catch(...){if(!approval_.empty())try{restoreBackup();}catch(...){}throw;}
  committed_=true;transfer::windows::require(FlushFileBuffers(file_)!=0);if(approval_.empty())abandon();return true;
 }catch(...){failed_=true;abandon();throw;}}
 void cancel(){protocol::check(!committed_);if(durable_)restoreBackup();if(file_!=INVALID_HANDLE_VALUE){durable_=false;abandon();}
  else if(durable_&&exists(partial_)){HANDLE part=CreateFileW(partial_.c_str(),DELETE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(part!=INVALID_HANDLE_VALUE);try{protocol::Reader r(token_);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/OutputExport/v1"&&r.text()==partial_&&r.text()==final_&&r.string()==id_);transfer::windows::checkIdentity(r,part,false);FILE_DISPOSITION_INFO remove{TRUE};transfer::windows::require(SetFileInformationByHandle(part,FileDispositionInfo,&remove,sizeof(remove)));CloseHandle(part);}catch(...){CloseHandle(part);throw;}}
  failed_=true;}
 bool complete()const{return committed_&&!failed_;}bool failed()const{return failed_;}std::uint64_t processedBytes()const{return processed_;}
 std::wstring path()const{protocol::check(complete());return final_;}
 Blob resumeToken()const{return token_;}
 std::wstring previousBackup()const{return approval_.empty()?L"":exists(backup_)?backup_:L"";}
 void finishCleanup(){protocol::check(complete());if(approval_.empty())return;protocol::check(file_!=INVALID_HANDLE_VALUE);if(!exists(backup_)){abandon();return;}HANDLE old=CreateFileW(backup_.c_str(),GENERIC_READ|DELETE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(old!=INVALID_HANDLE_VALUE);try{output_detail::checkApproval(approval_,final_,old);FILE_DISPOSITION_INFO remove{TRUE};transfer::windows::require(SetFileInformationByHandle(old,FileDispositionInfo,&remove,sizeof(remove)));CloseHandle(old);abandon();}catch(...){CloseHandle(old);throw;}}
};

// Retire an incomplete, owner-token-bound copy without reopening its verified
// source. Used only when the user chooses a different destination on Retry Save.
// Retirement is idempotent across a crash after deleting our partial. It never
// claims save completion or touches a completed destination. setup()/step()
// still require identity-bound consent for every actual replacement.
inline void cancelPendingExport(const std::wstring& destination,const std::string& id,const Blob& approval,const Blob& token){
 protocol::check(protocol::id(id,32)&&!token.empty());auto target=std::filesystem::path(destination);protocol::check(target.is_absolute()&&target==target.lexically_normal()&&transfer::safeName(unicode::encode(target.filename().wstring())));output_detail::Directories held(target.parent_path());auto partial=(target.parent_path()/unicode::decode(id+".peerchat-export-part")).wstring(),backup=(target.parent_path()/unicode::decode(id+".peerchat-replaced")).wstring();protocol::check(CompareStringOrdinal(destination.c_str(),-1,partial.c_str(),-1,TRUE)!=CSTR_EQUAL&&CompareStringOrdinal(destination.c_str(),-1,backup.c_str(),-1,TRUE)!=CSTR_EQUAL);
 protocol::Reader intent(token);protocol::check(intent.type()==protocol::FileTransfer&&intent.string()=="PeerChat/OutputExport/v1"&&intent.text()==partial&&intent.text()==destination&&intent.string()==id);auto volume=intent.number(),high=intent.number(),low=intent.number();protocol::check(intent.bytes()==approval);intent.number();protocol::check(intent.bytes().size()==32);intent.end();
 HANDLE owned=CreateFileW(partial.c_str(),DELETE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(owned==INVALID_HANDLE_VALUE){transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);auto attributes=GetFileAttributesW(backup.c_str());protocol::check(attributes==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND);return;}
 try{
  protocol::Writer current(protocol::FileTransfer);transfer::windows::fileIdentity(current,owned,false);protocol::Reader identity(current.data);protocol::check(volume==identity.number()&&high==identity.number()&&low==identity.number());
  auto attributes=GetFileAttributesW(destination.c_str());bool finalExists=attributes!=INVALID_FILE_ATTRIBUTES;if(!finalExists)transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);
  attributes=GetFileAttributesW(backup.c_str());bool backupExists=attributes!=INVALID_FILE_ATTRIBUTES;if(!backupExists)transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);
  if(approval.empty()){if(backupExists)throw std::runtime_error("Save destination changed; incomplete copy retained");}
  else if(backupExists){if(finalExists)throw std::runtime_error("Overwrite conflict; previous file retained at backup path");HANDLE old=CreateFileW(backup.c_str(),GENERIC_READ|DELETE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(old!=INVALID_HANDLE_VALUE);try{output_detail::checkApproval(approval,destination,old);output_detail::renameOwned(old,destination);CloseHandle(old);}catch(...){CloseHandle(old);throw;}}
  // No backup: do not open, delete, rename or replace the destination at all.
  // A rival file or changed old file is preserved. Only the separately opened
  // and identity-validated incomplete partial handle below is deleted.
  FILE_DISPOSITION_INFO remove{TRUE};transfer::windows::require(SetFileInformationByHandle(owned,FileDispositionInfo,&remove,sizeof(remove)));CloseHandle(owned);
 }catch(...){CloseHandle(owned);throw;}
}
}
