#pragma once
#include "windows_group_attachment_transfer.hpp"
#include "windows_group_attachment_keys.hpp"
#include "windows_group_attachment_history.hpp"
#include "windows_group_attachment_output.hpp"
#include "group_attachment_application.hpp"

namespace peerchat::group_attachments::windows {
// The common owner retains protocol, membership and per-device receipt logic.
// Only local persistence, files and output publication vary by platform.
struct ApplicationPlatform {
 using CacheStateStore=windows::CacheStateStore;
 using HistoryStore=windows::HistoryStore;
 using PreparedKeys=windows::PreparedKeys;
 using StagedCipherSource=windows::StagedCipherSource;
 using ReceiverFinalizer=windows::ReceiverFinalizer;
 using VerifiedOutput=windows::VerifiedOutput;
 using OutputExport=windows::OutputExport;
 using FileSource=transfer::windows::FileSource;
 using FileSink=transfer::windows::FileSink;
 using Sha256=transfer::windows::Sha256;
 using CleanupJournal=transfer::windows::CleanupJournal;
 using Directories=output_detail::Directories;
 ApplicationPlatform(Storage&,const Profile&,const std::wstring&){}
 static std::unique_ptr<FileSource> source(const std::wstring& path){return std::make_unique<FileSource>(path);}
 static std::unique_ptr<FileSink> sink(const std::wstring& path,const transfer::Metadata& m,CleanupJournal* cleanup){return std::make_unique<FileSink>(path,m,cleanup,true);}
 static void discardCipher(const Blob& location,const transfer::Metadata& metadata){
  protocol::Reader r(location);protocol::check(r.type()==protocol::FileTransfer);auto domain=r.string();
  if(domain=="PeerChat/StagedCipherSource/v1"){auto token=r.bytes();r.end();transfer::windows::discardCiphertext(token,metadata);}
  else transfer::windows::discardCiphertext(location,metadata);
 }
 static Blob location(const transfer::Sink& sink){return StagedCipherSource::location(sink);}
 static std::unique_ptr<transfer::Source> staged(const Staged& value,const std::string& device){return StagedCipherSource::reopen(value,device);}
 static std::unique_ptr<ReceiverFinalizer> finalizer(CacheStateStore& journal,std::unique_ptr<transfer::Source> source,const Manifest& manifest,const std::wstring& path){return std::make_unique<ReceiverFinalizer>(journal,std::move(source),manifest,path);}
 static std::unique_ptr<ReceiverFinalizer> finalizer(CacheStateStore& journal,const transfer::DurableState& state,const Manifest& manifest,const std::wstring& path){return std::make_unique<ReceiverFinalizer>(journal,state,manifest,path);}
 static std::unique_ptr<VerifiedOutput> output(const Manifest& manifest,const Blob& token){return std::make_unique<VerifiedOutput>(manifest,token);}
 static std::pair<std::wstring,std::wstring> directories(const std::wstring& root){auto cipher=(std::filesystem::path(root)/L"group-attachment-ciphertext").wstring(),received=(std::filesystem::path(root)/L"group-attachment-received").wstring();std::filesystem::create_directory(cipher);std::filesystem::create_directory(received);return {cipher,received};}
 static std::unique_ptr<CleanupJournal> cleanup(const std::wstring& root){auto path=(std::filesystem::path(root)/L"group-attachment-preparation").wstring();std::filesystem::create_directory(path);return std::make_unique<CleanupJournal>(path);}
 static std::unique_ptr<HistoryStore> history(Storage&,const Profile& profile,const std::wstring& directory){return std::make_unique<HistoryStore>(directory,profile.peerId);}
 static std::unique_ptr<PreparedKeys> preparedKeys(Storage&,const Profile& profile,const std::wstring& directory,const std::string& server){return std::make_unique<PreparedKeys>(directory,profile.peerId,server);}
 static std::uint64_t monotonic(){return GetTickCount64();}
 static void requireMissing(const std::filesystem::path& path){protocol::check(GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND);}
 static bool publishedExportMatches(const std::wstring& path,const std::string& id,const Blob& approval,const Blob& token){return windows::publishedExportMatches(path,id,approval,token);}
 static void cancelPendingExport(const std::wstring& path,const std::string& id,const Blob& approval,const Blob& token){windows::cancelPendingExport(path,id,approval,token);}
 static std::string saveFailureReason(const std::exception& error){return windows::saveFailureReason(error);}
};
using Application=BasicApplication<ApplicationPlatform>;
}