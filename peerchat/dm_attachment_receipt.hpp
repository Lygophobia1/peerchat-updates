#pragma once
#include "dm_attachment_body.hpp"

namespace peerchat::dm_attachments {
// Only produce after shared blob open() verified both hashes and the final
// internal file was durably committed. Mailbox ACK never supplies this evidence.
struct VerifiedReceipt {
 std::uint64_t senderAccount=0,recipientAccount=0,keyGeneration=0,verifiedUtc=0;
 std::string senderDevice,recipientDevice,keyId,message,object,descriptorDigest,audienceDigest;
 Blob recipientIdentity,signature;
};
inline Blob receiptPayload(const VerifiedReceipt& r){
 protocol::check(mailbox::account(r.senderAccount)&&mailbox::account(r.recipientAccount)&&r.senderAccount!=r.recipientAccount&&protocol::id(r.senderDevice)&&protocol::id(r.recipientDevice)&&r.senderDevice!=r.recipientDevice&&protocol::id(r.keyId,32)&&mailbox::account(r.keyGeneration)&&protocol::id(r.message,32)&&protocol::id(r.object,32)&&protocol::id(r.descriptorDigest)&&protocol::id(r.audienceDigest)&&r.recipientIdentity.size()==72&&Storage::publicKeyId(r.recipientIdentity)==r.recipientDevice&&mailbox::account(r.verifiedUtc));
 protocol::Writer w(protocol::FileTransfer);w.string("PeerChat/dm-attachment-verified-file/v1");w.number(r.senderAccount);w.string(r.senderDevice);w.number(r.recipientAccount);w.string(r.recipientDevice);w.string(r.keyId);w.number(r.keyGeneration);w.string(r.message);w.string(r.object);w.string(r.descriptorDigest);w.string(r.audienceDigest);w.bytes(r.recipientIdentity);w.number(r.verifiedUtc);return w.data;
}
inline Blob serializeReceipt(const VerifiedReceipt& r){protocol::check(r.signature.size()==64);auto data=receiptPayload(r);protocol::Writer signature(protocol::FileTransfer);signature.bytes(r.signature);data.insert(data.end(),signature.data.begin()+4,signature.data.end());return data;}
inline VerifiedReceipt parseReceipt(const Blob& bytes){
 protocol::check(bytes.size()<=2048);protocol::Reader r(bytes);protocol::check(r.type()==protocol::FileTransfer&&r.string()=="PeerChat/dm-attachment-verified-file/v1");VerifiedReceipt v;
 v.senderAccount=r.number();v.senderDevice=r.string();v.recipientAccount=r.number();v.recipientDevice=r.string();v.keyId=r.string();v.keyGeneration=r.number();v.message=r.string();v.object=r.string();v.descriptorDigest=r.string();v.audienceDigest=r.string();v.recipientIdentity=r.bytes();v.verifiedUtc=r.number();v.signature=r.bytes();r.end();protocol::check(serializeReceipt(v)==bytes);return v;
}
inline VerifiedReceipt verifiedReceipt(const Body& b,const mailbox::Header& h,std::uint64_t now,const Profile& recipient){
 validateBody(b,h);protocol::check(recipient.peerId==h.recipientDevice&&Storage::publicKeyId(recipient.publicKey)==h.recipientDevice);VerifiedReceipt r;
 r.senderAccount=h.senderAccount;r.senderDevice=h.senderDevice;r.recipientAccount=h.recipientAccount;r.recipientDevice=h.recipientDevice;r.keyId=h.keyId;r.keyGeneration=h.keyGeneration;r.message=h.messageId;r.object=b.manifest.binding.attachment;r.descriptorDigest=Storage::publicKeyId(b.descriptor);r.audienceDigest=b.manifest.binding.audienceDigest;r.recipientIdentity=recipient.publicKey;r.verifiedUtc=now;r.signature=signDevicePayload(receiptPayload(r),recipient);return r;
}
inline bool verifyReceipt(const VerifiedReceipt& r,const Body& b,const mailbox::Header& h,const Evidence& evidence){
 try{
  validateBody(b,h,&evidence);auto key=evidence.identity(h.recipientAccount,h.recipientDevice);
  return r.senderAccount==h.senderAccount&&r.senderDevice==h.senderDevice&&r.recipientAccount==h.recipientAccount&&r.recipientDevice==h.recipientDevice&&r.keyId==h.keyId&&r.keyGeneration==h.keyGeneration&&r.message==h.messageId&&r.object==b.manifest.binding.attachment&&r.descriptorDigest==Storage::publicKeyId(b.descriptor)&&r.audienceDigest==b.manifest.binding.audienceDigest&&r.signature.size()==64&&r.recipientIdentity==key&&Storage::publicKeyId(key)==h.recipientDevice&&verifyDevicePayload(receiptPayload(r),r.signature,key);
 }catch(...){return false;}
}
}
