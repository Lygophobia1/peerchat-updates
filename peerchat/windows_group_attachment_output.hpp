#pragma once
#include "windows_group_attachment_transfer.hpp"
#include <cstring>

namespace peerchat::group_attachments::windows {
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
class VerifiedOutput final:public transfer::Source {
 std::unique_ptr<output_detail::Directories> directories_;std::unique_ptr<transfer::windows::FileSource> file_;
 transfer::Metadata expected_;transfer::windows::Sha256 hash_;std::wstring path_;std::uint64_t processed_=0;bool verified_=false,failed_=false;
public:
 VerifiedOutput(const Manifest& manifest,const Blob& sinkToken):expected_{manifest.binding.attachment,manifest.name,manifest.size,manifest.plainDigest}{
  manifest.validate();protocol::Reader token(sinkToken);protocol::check(token.type()==protocol::FileTransfer);auto partial=std::filesystem::path(token.text());auto final=std::filesystem::path(token.text());protocol::check(partial.is_absolute()&&final.is_absolute()&&partial.parent_path()==final.parent_path()&&partial.filename()==expected_.id+".peerchat-part"&&final.filename()==unicode::decode(expected_.id+"-"+expected_.name));directories_=std::make_unique<output_detail::Directories>(final.parent_path());
  // Opening the final name directly refuses an uncommitted partial. FileSource
  // also rejects reparse points and holds the read handle throughout hashing.
  file_=std::make_unique<transfer::windows::FileSource>(final.wstring(),expected_.name);protocol::check(file_->size()==expected_.size);auto sourceToken=file_->resumeToken();protocol::Reader actual(sourceToken);protocol::check(actual.type()==protocol::FileTransfer&&actual.text()==final.wstring());for(unsigned n=0;n<3;++n)protocol::check(token.number()==actual.number());token.end();path_=final.wstring();
 }
 VerifiedOutput(const VerifiedOutput&)=delete;VerifiedOutput& operator=(const VerifiedOutput&)=delete;
 bool step(){protocol::check(!failed_);if(verified_)return true;try{if(processed_<expected_.size){auto count=static_cast<size_t>(std::min<std::uint64_t>(transfer::ChunkBytes,expected_.size-processed_));PlainBuffer bytes{file_->read(processed_,count)};protocol::check(bytes.bytes.size()==count);hash(hash_,bytes.bytes);processed_+=count;return false;}protocol::check(hash_.finish()==expected_.digest);verified_=true;return true;}catch(...){failed_=true;throw;}}
 bool verified()const{return verified_;}bool failed()const{return failed_;}std::uint64_t processedBytes()const{return processed_;}
 std::wstring path()const{protocol::check(verified_);return path_;}
 std::uint64_t size()const override{return expected_.size;}std::string name()const override{return expected_.name;}
 const transfer::Digest& expectedDigest()const{return expected_.digest;}
 Blob resumeToken()const override{protocol::check(verified_);return file_->resumeToken();}
 Blob read(std::uint64_t offset,size_t count)override{protocol::check(verified_);return file_->read(offset,count);}
};

// A local bounded copy, not a transfer owner. The selected exact destination is
// never replaced. A held verified input and second copy hash gate publication.
class OutputExport {
 VerifiedOutput& source_;std::unique_ptr<output_detail::Directories> directories_;
 HANDLE file_=INVALID_HANDLE_VALUE;std::wstring final_;transfer::windows::Sha256 hash_;
 std::uint64_t processed_=0;bool committed_=false,failed_=false;
 void abandon()noexcept{if(file_!=INVALID_HANDLE_VALUE){if(!committed_){FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(file_,FileDispositionInfo,&remove,sizeof(remove));}CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}}
public:
 OutputExport(VerifiedOutput& source,const std::wstring& destination,const std::string& operationId):source_(source){
  protocol::check(source_.verified()&&protocol::id(operationId,32));auto path=std::filesystem::path(destination);protocol::check(path.is_absolute()&&path==path.lexically_normal()&&transfer::safeName(unicode::encode(path.filename().wstring())));directories_=std::make_unique<output_detail::Directories>(path.parent_path());final_=path.wstring();auto attributes=GetFileAttributesW(final_.c_str());protocol::check(attributes==INVALID_FILE_ATTRIBUTES);transfer::windows::require(GetLastError()==ERROR_FILE_NOT_FOUND);auto partial=path.parent_path()/unicode::decode(operationId+".peerchat-export-part");
  file_=CreateFileW(partial.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);transfer::windows::require(file_!=INVALID_HANDLE_VALUE);
 }
 ~OutputExport(){abandon();}
 OutputExport(const OutputExport&)=delete;OutputExport& operator=(const OutputExport&)=delete;
 bool step(){protocol::check(!failed_);if(committed_)return true;try{
  if(processed_<source_.size()){auto count=static_cast<size_t>(std::min<std::uint64_t>(transfer::ChunkBytes,source_.size()-processed_));PlainBuffer bytes{source_.read(processed_,count)};protocol::check(bytes.bytes.size()==count);hash(hash_,bytes.bytes);DWORD wrote=0;transfer::windows::require(WriteFile(file_,bytes.bytes.data(),static_cast<DWORD>(bytes.bytes.size()),&wrote,nullptr)&&wrote==bytes.bytes.size());processed_+=count;return false;}
  protocol::check(hash_.finish()==source_.expectedDigest());transfer::windows::require(FlushFileBuffers(file_)!=0);auto filenameBytes=final_.size()*sizeof(wchar_t);protocol::check(filenameBytes<=32767*sizeof(wchar_t));std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO,FileName)+filenameBytes+sizeof(wchar_t));auto rename=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());rename->ReplaceIfExists=FALSE;rename->RootDirectory=nullptr;rename->FileNameLength=static_cast<DWORD>(filenameBytes);std::memcpy(rename->FileName,final_.c_str(),filenameBytes+sizeof(wchar_t));transfer::windows::require(SetFileInformationByHandle(file_,FileRenameInfo,rename,static_cast<DWORD>(storage.size()))!=0);committed_=true;transfer::windows::require(FlushFileBuffers(file_)!=0);abandon();return true;
 }catch(...){failed_=true;abandon();throw;}}
 void cancel(){protocol::check(!committed_);failed_=true;abandon();}
 bool complete()const{return committed_&&!failed_;}bool failed()const{return failed_;}std::uint64_t processedBytes()const{return processed_;}
 std::wstring path()const{protocol::check(complete());return final_;}
};
}
