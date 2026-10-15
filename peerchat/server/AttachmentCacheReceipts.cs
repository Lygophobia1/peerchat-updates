using System;
using System.Collections.Generic;
using System.Linq;

namespace PeerChat.Server {
// Recipient attestation after endpoint AEAD/hash verification and durable file
// finalization. This public receipt contains no plaintext hash, filename or key.
// The cache authenticates the signer, not the endpoint's plaintext calculation.
internal sealed class AttachmentVerificationReceipt {
 internal const int MaximumWire=4096;
 internal AttachmentCacheDescriptor Descriptor;internal long Account,VerifiedUtc;
 internal string Device,Digest;internal byte[] Wire;
 internal static AttachmentVerificationReceipt Parse(byte[] wire){try{
  if(wire==null||wire.Length>MaximumWire||wire.Length<4||wire[0]!=80||wire[1]!=84||wire[2]!=1||wire[3]!=30)throw new InvalidOperationException();
  int at=4;if(GroupRosterProof.Text(wire,ref at,64)!="PeerChat/group-attachment-verified/v1")throw new InvalidOperationException();
  var result=new AttachmentVerificationReceipt{Descriptor=AttachmentCacheDescriptor.Parse(GroupRosterProof.Bytes(wire,ref at,2048))};
  ulong account=GroupRosterProof.Number(wire,ref at);result.Device=GroupRosterProof.Text(wire,ref at,64);var key=GroupRosterProof.Bytes(wire,ref at,72);ulong verified=GroupRosterProof.Number(wire,ref at);int signedEnd=at;var signature=GroupRosterProof.Bytes(wire,ref at,64);
  if(at!=wire.Length||account==0||account>long.MaxValue||verified==0||verified>253402300799000UL||key.Length!=72||signature.Length!=64||!MailboxProtocol.Id(result.Device,64)||!MailboxProtocol.Verify(wire.Take(signedEnd).ToArray(),signature,key,result.Device))throw new InvalidOperationException();
  result.Account=(long)account;result.VerifiedUtc=(long)verified;result.Wire=(byte[])wire.Clone();result.Digest=MailboxProtocol.Digest(wire);return result;
 }catch(Exception error){if(error is OutOfMemoryException)throw;throw new InvalidOperationException("cache_verification_receipt_invalid");}}
}
internal sealed partial class AttachmentCacheRuntime {
 void InitializeVerificationReceipts(){
  Execute("CREATE TABLE IF NOT EXISTS attachment_verification_receipts(object_id TEXT NOT NULL REFERENCES objects(id) ON DELETE CASCADE,device TEXT NOT NULL,wire BLOB NOT NULL,digest TEXT NOT NULL,PRIMARY KEY(object_id,device));");
  var foreign=Rows("PRAGMA foreign_key_list(attachment_verification_receipts)",8,8);
  Require(foreign.Count==1&&foreign[0][2]=="objects"&&foreign[0][3]=="object_id"&&foreign[0][4]=="id"&&foreign[0][6]=="CASCADE","cache_verification_schema_review_required");
  Require(Scalar("SELECT count(*) FROM attachment_verification_receipts v WHERE length(v.wire)<128 OR length(v.wire)>4096 OR length(v.digest)<>64 OR NOT EXISTS(SELECT 1 FROM recipients r WHERE r.object_id=v.object_id AND r.device=v.device AND r.verified=1 AND r.durable_offset=(SELECT size FROM objects WHERE id=v.object_id))")==0,"cache_verification_index_invalid");
 }
 internal bool RecordVerification(string id,AttachmentVerificationReceipt receipt){lock(gate){Check();var descriptor=Descriptor(id);ContextExpires(id);
  Require(receipt.Descriptor.Wire.SequenceEqual(descriptor.Wire),"cache_verification_descriptor_mismatch");
  Require(OriginalMembership(id,receipt.Account,receipt.Device)!=null,"cache_original_membership_required");
  Require(RecipientOffset(id,receipt.Device)==descriptor.CipherBytes,"cache_recipient_ciphertext_incomplete");
  Require(receipt.VerifiedUtc<=checked(Now()*1000+300000),"cache_verification_time_invalid");
  var previous=Binary("SELECT wire FROM attachment_verification_receipts WHERE object_id="+Quote(id)+" AND device="+Quote(receipt.Device));
  if(previous!=null){Require(previous.SequenceEqual(receipt.Wire),"cache_conflicting_verification_receipt");return false;}
  Space(32768);Transaction(()=>{
   Execute("INSERT INTO attachment_verification_receipts VALUES("+Quote(id)+","+Quote(receipt.Device)+","+Blob(receipt.Wire)+","+Quote(receipt.Digest)+");");
   Execute("UPDATE recipients SET verified=1 WHERE object_id="+Quote(id)+" AND account="+receipt.Account+" AND device="+Quote(receipt.Device)+";");
  });return true;
 }}
 internal Dictionary<string,object> Verification(string id,CacheRecipient recipient){lock(gate){Check();var descriptor=Descriptor(id);ContextExpires(id);
  var wire=Binary("SELECT wire FROM attachment_verification_receipts WHERE object_id="+Quote(id)+" AND device="+Quote(recipient.Device));string digest="";
  if(wire!=null){var receipt=AttachmentVerificationReceipt.Parse(wire);var rows=Rows("SELECT digest FROM attachment_verification_receipts WHERE object_id="+Quote(id)+" AND device="+Quote(recipient.Device),1);
   Require(receipt.Descriptor.Wire.SequenceEqual(descriptor.Wire)&&receipt.Account==recipient.Account&&receipt.Device==recipient.Device&&rows.Count==1&&rows[0][0]==receipt.Digest&&RecipientOffset(id,recipient.Device)==descriptor.CipherBytes,"cache_verification_index_invalid");digest=receipt.Digest;
  }
  return new Dictionary<string,object>{{"accountId",recipient.Account},{"device",recipient.Device},{"durableOffset",RecipientOffset(id,recipient.Device)},{"plaintextVerified",wire!=null},{"receipt",wire==null?"":AttachmentCacheRequests.Encode(wire)},{"receiptDigest",digest}};
 }}
}
internal sealed partial class AttachmentCacheAdmission {
 internal bool VerifyReceipt(long account,string device,Func<bool> current,string id,byte[] wire){var receipt=AttachmentVerificationReceipt.Parse(wire);return Authorized(account,device,current,id,d=>{
  Require(receipt.Account==account&&receipt.Device==device,"cache_verification_device_mismatch");return cache.RecordVerification(id,receipt);
 });}
 internal Dictionary<string,object> VerificationReceipts(long account,string device,Func<bool> current,string id,string after){return Authorized(account,device,current,id,d=>{
  Require(after==""||MailboxProtocol.Id(after,64),"cache_verification_cursor_invalid");
  Require(d.Account==account&&d.Sender==device,"cache_sender_binding_mismatch");cache.ContextExpires(id);
  var items=new List<Dictionary<string,object>>();string cursor=after;bool more=false;
  foreach(var recipient in cache.OriginalRecipients(id)){
   if(String.CompareOrdinal(recipient.Device,after)<=0)continue;if(items.Count>=8){more=true;break;}
   accounts.WithGroupRosterAuthorization(d.Group,recipient.Account,recipient.Device,roster=>{
    if(accounts.AttachmentMembership(d.Group,recipient.Account,recipient.Device)==recipient.Membership)items.Add(cache.Verification(id,recipient));
   });cursor=recipient.Device;
  }
  Require(current(),"cache_session_invalid");return new Dictionary<string,object>{{"items",items},{"cursor",cursor},{"more",more},{"dataEncoding","base64url"}};
 });}
}
}
