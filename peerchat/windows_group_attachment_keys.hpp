#pragma once
#include "group_attachment_key_envelope.hpp"
#include "group_attachment_staging.hpp"
#include "windows_transfer_io.hpp"

namespace peerchat::group_attachments::windows {
// Thread-confined preparation only. No RPC, transfer, retry, event loop or history
// owner. Caller has loaded protected immutable staging and current authority.
class PreparedKeys {
 std::filesystem::path root_;std::string device_,server_,context_;HANDLE directory_=INVALID_HANDLE_VALUE,owner_=INVALID_HANDLE_VALUE;
 static void sameManifest(const Manifest& staged,const Manifest& body){
  protocol::check(staged.binding.attachment==body.binding.attachment&&staged.binding.group==body.binding.group&&staged.binding.sender==body.binding.sender&&staged.binding.account==body.binding.account&&staged.binding.epoch==body.binding.epoch&&staged.name==body.name&&staged.size==body.size&&staged.key==body.key&&staged.prefix==body.prefix&&staged.plainDigest==body.plainDigest&&staged.cipherDigest==body.cipherDigest);
 }
 std::string id(const keys::Header& header)const{
  auto descriptor=parseDescriptor(header.descriptor);protocol::Writer w(protocol::GroupEvent);w.string("PeerChat/AttachmentKeyPreparationPath/v1");w.string(device_);w.string(server_);w.bytes(header.descriptor);w.number(header.recipient.accountId);w.string(header.recipient.device);w.string(header.sourceMembership);w.string(header.recipientMembership);
  // Key generation and creation time are intentionally absent. A retry after
  // key rotation must inspect the prior immutable packet, not replace it.
  protocol::check(descriptor.sender==device_);return Storage::publicKeyId(w.data).substr(0,32);
 }
public:
 PreparedKeys(const std::wstring& profile,std::string device,std::string serverFingerprint):root_(std::filesystem::absolute(profile)/L"group-attachment-keys"),device_(std::move(device)),server_(std::move(serverFingerprint)){
  protocol::check(protocol::id(device_)&&protocol::id(server_));context_="PeerChat/AttachmentKeyPreparation/v1/"+device_+"/"+server_+"/"+unicode::encode(std::filesystem::absolute(profile).wstring());std::filesystem::create_directory(root_);
  directory_=CreateFileW(root_.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  transfer::windows::require(directory_!=INVALID_HANDLE_VALUE);BY_HANDLE_FILE_INFORMATION info{};
  if(!GetFileInformationByHandle(directory_,&info)||!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)){CloseHandle(directory_);directory_=INVALID_HANDLE_VALUE;throw std::runtime_error("Attachment preparation directory unavailable");}
  auto lock=root_/L"owner.lock";owner_=CreateFileW(lock.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(owner_==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(owner_,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||info.nNumberOfLinks!=1){if(owner_!=INVALID_HANDLE_VALUE)CloseHandle(owner_);CloseHandle(directory_);owner_=directory_=INVALID_HANDLE_VALUE;throw std::runtime_error("Attachment preparation already owned or unavailable");}
 }
 ~PreparedKeys(){if(owner_!=INVALID_HANDLE_VALUE)CloseHandle(owner_);if(directory_!=INVALID_HANDLE_VALUE)CloseHandle(directory_);}
 PreparedKeys(const PreparedKeys&)=delete;PreparedKeys& operator=(const PreparedKeys&)=delete;
 Blob ensure(const Staged& staged,const keys::Header& requested,const Body& body,const keys::Authority& authority,platform::IdentityProvider& identity,platform::HpkeProvider& hpke,std::uint64_t now,transfer::windows::CleanupJournal* cleanup=nullptr){
  staged.validate(device_);sameManifest(staged.manifest,body.manifest);keys::headerPayload(requested);auto descriptor=keys::authorize(requested,authority,true);protocol::check(descriptor.matches(staged.manifest)&&identity.publicKey()==descriptor.identity&&requested.created<=now&&now<requested.expires);
  const auto key=id(requested);auto final=root_/unicode::decode(key+"-key-envelope.protected");auto plain=encodeBody(body);const auto bodyDigest=Storage::publicKeyId(plain.bytes);auto attributes=GetFileAttributesW(final.c_str());const auto missing=GetLastError();platform::WindowsRecordProtector protection;
  if(attributes!=INVALID_FILE_ATTRIBUTES){
   transfer::windows::FileSource source(final.wstring());protocol::check(source.size()>0&&source.size()<=16384);auto bytes=source.read(0,static_cast<size_t>(source.size()));SecretRecord record(protection.unprotect(bytes,context_+"/"+key));protocol::Reader r(record.bytes);
   protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/PreparedAttachmentKey/v1"&&r.string()==key&&r.string()==bodyDigest);auto wire=r.bytes();r.end();auto saved=keys::parse(wire);auto prior=keys::authorize(saved.header,authority,true);
   protocol::check(prior.wire==descriptor.wire&&saved.header.recipient.accountId==requested.recipient.accountId&&saved.header.recipient.device==requested.recipient.device&&saved.header.created<=now&&now<saved.header.expires&&saved.signature.size()==64&&verifyDevicePayload(keys::signingPayload(saved),saved.signature,descriptor.identity));return wire;
  }
  transfer::windows::require(missing==ERROR_FILE_NOT_FOUND);
  // Finite on-disk preparation, including failed/orphan partials. The single
  // owner serializes admission. No expired/unknown record is overwritten merely
  // to make room; maintenance must verify ownership and active references first.
  size_t files=0;for(const auto& item:std::filesystem::directory_iterator(root_)){if(item.path().filename()==L"owner.lock")continue;protocol::check(++files<transfer::MaxRecords*64);}
  auto envelope=keys::seal(requested,body,authority,identity,hpke);auto wire=keys::serialize(envelope);protocol::Writer record(protocol::FileTransfer);record.string("PeerChat/PreparedAttachmentKey/v1");record.string(key);record.string(bodyDigest);record.bytes(wire);
  auto protectedBytes=protection.protect(record.data,context_+"/"+key);protocol::check(protectedBytes.size()<=16384);
  transfer::Metadata metadata{key,"key-envelope.protected",protectedBytes.size(),{}};transfer::windows::FileSink sink(root_.wstring(),metadata,cleanup);sink.append(protectedBytes);sink.flush();sink.commit();
  return wire; // No packet can be published before this durable commit.
 }
};
}
