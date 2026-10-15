#pragma once
#include "windows_group_attachment_output.hpp"
#include <map>
#include <filesystem>

namespace peerchat::download_save {
using transfer::windows::require;
using group_attachments::windows::VerifiedLocalSource;
using group_attachments::windows::OutputExport;

// TransferManager retains the authenticated, ID-owned plaintext staging file.
// This separate protected intent records only where a verified copy should go.
class Coordinator {
 struct Entry {std::wstring target;Blob approval,exportToken,sourceLocation;transfer::Metadata metadata;std::string operation,state,error;bool sourceToken=false,cleaned=false;};
 std::filesystem::path root_,state_;std::string context_;std::map<std::string,Entry> entries_;
 std::unique_ptr<VerifiedLocalSource> source_;std::unique_ptr<OutputExport> export_;std::string active_;
 static constexpr size_t Limit=transfer::MaxRecords;
 static Blob read(const std::filesystem::path& path){
  HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(file==INVALID_HANDLE_VALUE){require(GetLastError()==ERROR_FILE_NOT_FOUND);return {};}
  try{BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};require(GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))&&GetFileSizeEx(file,&size)&&size.QuadPart>0&&size.QuadPart<=1024*1024);Blob data(static_cast<size_t>(size.QuadPart));DWORD done=0;require(ReadFile(file,data.data(),static_cast<DWORD>(data.size()),&done,nullptr)&&done==data.size());CloseHandle(file);return data;}catch(...){CloseHandle(file);throw;}
 }
 void save(const std::map<std::string,Entry>* archive=nullptr,const std::filesystem::path& archivePath={}){
  const auto& values=archive?*archive:entries_;
  protocol::check(values.size()<=Limit);protocol::Writer writer(protocol::FileTransfer);writer.string("PeerChat/DownloadSaveState/v1");writer.number(values.size());
  for(const auto& pair:values){const auto& row=pair.second;writer.string(pair.first);writer.text(row.target);writer.bytes(row.approval);writer.bytes(row.exportToken);writer.string(row.operation);writer.string(row.state);writer.string(row.error);writer.number(row.cleaned);writer.bytes(row.sourceLocation);writer.number(row.sourceToken);writer.string(row.metadata.name);writer.number(row.metadata.size);writer.bytes(Blob(row.metadata.digest.begin(),row.metadata.digest.end()));}
  protocol::check(writer.data.size()<=512*1024);platform::WindowsRecordProtector protector;auto sealed=protector.protect(writer.data,context_);auto temp=root_/L"state.next";
  HANDLE file=CreateFileW(temp.c_str(),GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);require(file!=INVALID_HANDLE_VALUE);
  try{BY_HANDLE_FILE_INFORMATION info{};require(GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)));LARGE_INTEGER start{};require(SetFilePointerEx(file,start,nullptr,FILE_BEGIN)&&SetEndOfFile(file));DWORD done=0;require(WriteFile(file,sealed.data(),static_cast<DWORD>(sealed.size()),&done,nullptr)&&done==sealed.size()&&FlushFileBuffers(file));CloseHandle(file);file=INVALID_HANDLE_VALUE;require(MoveFileExW(temp.c_str(),(archive?archivePath:state_).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0);}catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);throw;}
 }
 std::map<std::string,Entry> decode(const std::filesystem::path& path)const{std::map<std::string,Entry> result;auto sealed=read(path);if(sealed.empty())return result;platform::WindowsRecordProtector protector;auto plain=protector.unprotect(sealed,context_);protocol::Reader reader(plain);protocol::check(reader.type()==protocol::FileTransfer&&reader.string()=="PeerChat/DownloadSaveState/v1");auto count=reader.number();protocol::check(count<=Limit);while(count--){auto id=reader.string();Entry row;row.target=reader.text();row.approval=reader.bytes();row.exportToken=reader.bytes();row.operation=reader.string();row.state=reader.string();row.error=reader.string();row.cleaned=reader.number()!=0;row.sourceLocation=reader.bytes();row.sourceToken=reader.number()!=0;row.metadata.id=id;row.metadata.name=reader.string();row.metadata.size=reader.number();auto digest=reader.bytes();protocol::check(digest.size()==row.metadata.digest.size());std::copy(digest.begin(),digest.end(),row.metadata.digest.begin());protocol::check(row.sourceLocation.empty()||(transfer::safeName(row.metadata.name)&&row.metadata.size<=transfer::MaxFileBytes));protocol::check(protocol::id(id,32)&&row.target.size()<=16384&&row.approval.size()<=2048&&row.exportToken.size()<=8192&&protocol::id(row.operation,32)&&(row.state=="pending"||row.state=="copying"||row.state=="complete"||row.state=="failed")&&row.error.size()<=256&&result.emplace(id,std::move(row)).second);}reader.end();return result;}
 static void validate(const std::wstring& target,const Blob& approval){auto path=std::filesystem::path(target);protocol::check(path.is_absolute()&&path==path.lexically_normal()&&target.size()<=16384&&transfer::safeName(unicode::encode(path.filename().wstring()))&&approval.size()<=2048);if(!approval.empty()){protocol::Reader r(approval);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/ExportOverwriteApproval/v1"&&r.text()==target);for(int n=0;n<6;++n)r.number();r.end();}}
 void clearActive(){export_.reset();source_.reset();active_.clear();}
 // Completed choices remain addressable by stable transfer ID without a
 // lifetime download-count limit or an unbounded continuously loaded index.
 void room(){if(entries_.size()<Limit)return;auto old=std::find_if(entries_.begin(),entries_.end(),[](const auto& p){return p.second.state=="complete"&&p.second.cleaned;});protocol::check(old!=entries_.end());std::map<std::string,Entry> archived{{old->first,old->second}};save(&archived,root_/(old->first+".protected"));entries_.erase(old);save();}
public:
 Coordinator(const std::wstring& profile,const std::string& device):root_(std::filesystem::absolute(profile)/L"attachment-download-save"),state_(root_/L"state.protected"){
  protocol::check(protocol::id(device,64));std::filesystem::create_directory(root_);auto attrs=GetFileAttributesW(root_.c_str());require(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_DIRECTORY)&&!(attrs&FILE_ATTRIBUTE_REPARSE_POINT));context_="PeerChat/DownloadSave/v1/"+device+"/"+unicode::encode(std::filesystem::absolute(profile).wstring());entries_=decode(state_);
 }
 std::wstring staging(const std::wstring& profile)const{auto path=std::filesystem::absolute(std::filesystem::path(profile)/L"attachment-received");std::filesystem::create_directory(path);auto attrs=GetFileAttributesW(path.c_str());require(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_DIRECTORY)&&!(attrs&FILE_ATTRIBUTE_REPARSE_POINT));return path.wstring();}
 bool has(const std::string& id)const{return entries_.count(id)!=0;}
 void select(const std::string& id,const std::wstring& target,const Blob& approval){
  protocol::check(protocol::id(id,32)&&!entries_.count(id));room();validate(target,approval);Entry row;row.target=target;row.approval=approval;row.operation=Storage::newMessageId();row.state="pending";entries_.emplace(id,std::move(row));try{save();}catch(...){entries_.erase(id);throw;}
 }
 void abandon(const std::string& id){auto found=entries_.find(id);if(found==entries_.end())return;protocol::check(found->second.state=="pending"&&found->second.exportToken.empty());entries_.erase(found);save();}
 void retry(const std::string& id,const std::wstring& target,const Blob& approval,const transfer::DurableState* record=nullptr,bool sourceToken=false){
  protocol::check(protocol::id(id,32));validate(target,approval);if(!entries_.count(id)){room();auto archived=decode(root_/(id+".protected"));if(!archived.empty()){protocol::check(archived.size()==1&&archived.count(id));entries_.emplace(id,std::move(archived.begin()->second));}}auto& row=entries_[id];protocol::check(row.state.empty()||row.state=="failed"||row.state=="complete");
  // A failed durable export must be resumed on its original target first;
  // changing paths cannot silently orphan its owned partial/backup.
  if(!row.exportToken.empty()&&row.target==target&&row.state!="complete"&&(row.approval==approval||group_attachments::windows::publishedExportMatches(row.target,row.operation,row.approval,row.exportToken))){} // Resume the original operation; never substitute a new overwrite approval.
  else{if(!row.exportToken.empty()&&!row.cleaned){auto backup=std::filesystem::path(row.target).parent_path()/unicode::decode(row.operation+".peerchat-replaced");protocol::check(GetFileAttributesW(backup.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND);clearActive();group_attachments::windows::cancelPendingExport(row.target,row.operation,row.approval,row.exportToken);}row.target=target;row.approval=approval;row.operation=Storage::newMessageId();row.exportToken.clear();}
  if(record){row.metadata=record->metadata;row.sourceLocation=record->location;row.sourceToken=sourceToken;}
  row.state="pending";row.cleaned=false;row.error.clear();save();clearActive();
 }
 struct Status {std::string state,error;std::wstring path;};
 Status status(const std::string& id,bool completedIncoming)const{auto found=entries_.find(id);if(found!=entries_.end()){const auto& row=found->second;return {row.state,row.error,row.state=="complete"?row.target:std::wstring{}};}if(protocol::id(id,32))try{auto archived=decode(root_/(id+".protected"));if(archived.size()==1&&archived.count(id)){const auto& row=archived.at(id);return {row.state,row.error,row.state=="complete"?row.target:std::wstring{}};}}catch(...){}return completedIncoming?Status{"failed","Choose a location with Retry Save",{}}:Status{};}
 void tick(const std::vector<transfer::DurableState>& completed){
  for(auto& pair:entries_){auto& row=pair.second;if(row.state!="pending"&&row.state!="copying"&&!(row.state=="complete"&&!row.cleaned))continue;
   auto found=std::find_if(completed.begin(),completed.end(),[&](const auto& d){return d.metadata.id==pair.first&&!d.sending&&d.purpose==transfer::Purpose::PeerFile&&d.state==transfer::State::Complete;});if(found==completed.end()&&row.sourceLocation.empty())continue;
   try{
    if(active_!=pair.first){clearActive();if(row.sourceLocation.empty()){row.metadata=found->metadata;row.sourceLocation=found->location;save();}source_=std::make_unique<VerifiedLocalSource>(row.metadata,row.sourceLocation,row.sourceToken);active_=pair.first;}
    if(!source_->verified()){source_->step();return;}
    if(!export_){export_=std::make_unique<OutputExport>(*source_,row.target,row.operation,row.approval,row.exportToken);if(row.exportToken.empty()){row.exportToken=export_->resumeToken();row.state="copying";try{save();}catch(...){export_->cancel();row.exportToken.clear();row.state="pending";throw;}}}
    if(!export_->step())return;
    row.state="complete";row.error.clear();save();export_->finishCleanup();row.cleaned=true;save();clearActive();return;
   }catch(const std::exception& error){row.state="failed";row.error=(!source_||!source_->verified())?"Retained internal file is unavailable or changed; save blocked. Original download data has not been replaced.":group_attachments::windows::saveFailureReason(error);clearActive();save();return;}
  }
 }
};
}
