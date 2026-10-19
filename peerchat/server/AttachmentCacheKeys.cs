using System;
using System.Collections.Generic;
using System.Linq;

namespace PeerChat.Server {
// Public parser only. Decryption is endpoint-only through the existing HPKE
// provider. Separate domains prevent mailbox or full-group-key substitution.
internal sealed class AttachmentKeyEnvelope {
 internal const int MaximumWire=8192;
 internal AttachmentCacheDescriptor Descriptor;internal MailboxProtocol.Binding Recipient;
 internal string SourceMembership,RecipientMembership,Digest;internal long Created,Expires;internal byte[] Wire;
 internal static AttachmentKeyEnvelope Parse(byte[] wire){try{
  if(wire==null||wire.Length>MaximumWire)throw new InvalidOperationException();
  var r=new MailboxProtocol.Reader(wire,"PeerChat/group-attachment-key-signature/v1");var h=new MailboxProtocol.Reader(r.Field(3072),"PeerChat/group-attachment-key/v1");h.Suite();
  var value=new AttachmentKeyEnvelope{Descriptor=AttachmentCacheDescriptor.Parse(h.Field(2048)),SourceMembership=h.Text(32),RecipientMembership=h.Text(32),Recipient=MailboxProtocol.Binding.Parse(h.Field(2048)),Created=h.Positive(),Expires=h.Positive()};h.End();
  var enc=r.Field(65);var ciphertext=r.Field(4112);var original=r.Prefix();var signature=r.Field(64);r.End();
  if(!MailboxProtocol.Id(value.SourceMembership,32)||!MailboxProtocol.Id(value.RecipientMembership,32)||!value.Recipient.Allowed||value.Created<value.Recipient.Created||enc.Length!=65||enc[0]!=4||ciphertext.Length<16||value.Expires<=value.Created||value.Expires>253402300799000L||!value.Descriptor.VerifySource(original,signature))throw new InvalidOperationException();
  value.Wire=(byte[])wire.Clone();value.Digest=MailboxProtocol.Digest(wire);return value;
 }catch(Exception error){if(error is OutOfMemoryException)throw;throw new InvalidOperationException("cache_key_envelope_invalid");}}
}
sealed partial class AccountDatabase {
 // Called only inside cache group/original-member admission, not friendship
 // lookup. Never registers a key or changes ordinary mailbox authorization.
 internal byte[] AttachmentRecipientKey(long account,string device){lock(gate){
  if(!ActiveDevice(account,device))return null;
  return MailboxBlob("SELECT proof FROM mailbox_recipient_keys WHERE account_id="+account+" AND device="+Quote(device)+" AND state='active' AND allowed=1");
 }}
 internal bool AttachmentKeyAvailable(MailboxProtocol.Binding binding,bool allowRetired){lock(gate){
  if(!ActiveDevice(binding.Account,binding.Device))return false;
  var proof=MailboxBlob("SELECT proof FROM mailbox_recipient_keys WHERE account_id="+binding.Account+" AND device="+Quote(binding.Device)+" AND key_id="+Quote(binding.Key)+" AND generation="+binding.Generation+" AND allowed=1 AND state "+(allowRetired?"IN ('active','retired')":"='active'"));
  return proof!=null&&proof.SequenceEqual(binding.Wire);
 }}
}
internal sealed partial class AttachmentCacheRuntime {
 internal sealed class KeyEnvelope {internal string Id,Device,Digest;internal byte[] Wire;}
 void InitializeKeyEnvelopes(){
  Execute("CREATE TABLE IF NOT EXISTS attachment_key_envelopes(object_id TEXT NOT NULL REFERENCES objects(id) ON DELETE CASCADE,device TEXT NOT NULL,key_id TEXT NOT NULL,generation INTEGER NOT NULL,wire BLOB NOT NULL,digest TEXT NOT NULL,acked INTEGER NOT NULL DEFAULT 0 CHECK(acked IN (0,1)),PRIMARY KEY(object_id,device));");
  var foreign=Rows("PRAGMA foreign_key_list(attachment_key_envelopes)",8,8);Require(foreign.Count==1&&foreign[0][2]=="objects"&&foreign[0][3]=="object_id"&&foreign[0][4]=="id"&&foreign[0][6]=="CASCADE","cache_key_schema_review_required");
  // Never rebuild an existing incompatible pre-release table. Validate before
  // lease cleanup/ledger mutation or adding its index.
  Execute("CREATE INDEX IF NOT EXISTS attachment_keys_pending ON attachment_key_envelopes(device,object_id);");
 }
 void ReconcileKeyEnvelopes(){Require(Scalar("SELECT count(*) FROM attachment_key_envelopes k WHERE length(k.wire)>8192 OR length(k.wire)<128 OR length(k.digest)<>64 OR k.acked NOT IN (0,1) OR NOT EXISTS(SELECT 1 FROM recipients r WHERE r.object_id=k.object_id AND r.device=k.device)")==0,"cache_key_index_invalid");}
 internal List<CacheRecipient> OriginalRecipients(string id){lock(gate){Check();Descriptor(id);return Rows("SELECT account,device,membership FROM recipients WHERE object_id="+Quote(id)+" ORDER BY device LIMIT 64",64,3).Select(row=>new CacheRecipient(Number(row[0]),row[1],row[2])).ToList();}}
 internal bool ExistingKey(string id,string device,byte[] wire){lock(gate){Check();Descriptor(id);var prior=Binary("SELECT wire FROM attachment_key_envelopes WHERE object_id="+Quote(id)+" AND device="+Quote(device));if(prior==null)return false;Require(prior.SequenceEqual(wire),"cache_conflicting_key_envelope");return true;}}
 internal bool PublishKey(string id,AttachmentKeyEnvelope value){lock(gate){Check();var d=Descriptor(id);Require(value.Descriptor.Wire.SequenceEqual(d.Wire),"cache_key_descriptor_mismatch");
  Require(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1,"cache_upload_incomplete");long now=checked(Now()*1000),expires=checked(Scalar("SELECT expires FROM objects WHERE id="+Quote(id))*1000);
  Require(now<expires&&value.Expires>now&&value.Expires<=expires&&value.Created<=now+300000,"cache_key_expiry_invalid");
  Require(OriginalMembership(id,d.Account,d.Sender)==value.SourceMembership&&OriginalMembership(id,value.Recipient.Account,value.Recipient.Device)==value.RecipientMembership,"cache_key_membership_mismatch");
  if(ExistingKey(id,value.Recipient.Device,value.Wire))return false;
  // One bounded RPC under the cache lock reserves metadata space before its
  // atomic write. Existing ciphertext/reservations remain charged. No envelope
  // bytes are written if space cannot safely support them.
  Space(32768);Transaction(()=>Execute("INSERT INTO attachment_key_envelopes VALUES("+Quote(id)+","+Quote(value.Recipient.Device)+","+Quote(value.Recipient.Key)+","+value.Recipient.Generation+","+Blob(value.Wire)+","+Quote(value.Digest)+",0);"));return true;
 }}
 internal KeyEnvelope ReadKey(string id,string device){lock(gate){Check();Descriptor(id);Require(Scalar("SELECT expires FROM objects WHERE id="+Quote(id))>Now(),"cache_attachment_expired");
  var wire=Binary("SELECT wire FROM attachment_key_envelopes WHERE object_id="+Quote(id)+" AND device="+Quote(device));Require(wire!=null,"cache_key_unavailable");var parsed=AttachmentKeyEnvelope.Parse(wire);
  var descriptor=Descriptor(id);Require(parsed.Descriptor.Wire.SequenceEqual(descriptor.Wire)&&parsed.Recipient.Device==device&&parsed.SourceMembership==OriginalMembership(id,descriptor.Account,descriptor.Sender)&&parsed.RecipientMembership==OriginalMembership(id,parsed.Recipient.Account,device)&&Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1,"cache_key_binding_invalid");Require(parsed.Expires>checked(Now()*1000),"cache_key_expired");
  var rows=Rows("SELECT digest,key_id,generation FROM attachment_key_envelopes WHERE object_id="+Quote(id)+" AND device="+Quote(device),1,3);Require(rows.Count==1&&rows[0][0]==parsed.Digest&&rows[0][1]==parsed.Recipient.Key&&Number(rows[0][2])==parsed.Recipient.Generation,"cache_key_index_invalid");return new KeyEnvelope{Id=id,Device=device,Digest=parsed.Digest,Wire=wire};
 }}
 internal bool AcknowledgeKey(string id,string device,string digest){lock(gate){Check();Require(MailboxProtocol.Id(digest,64),"cache_key_ack_invalid");var value=ReadKey(id,device);Require(value.Digest==digest,"cache_key_ack_mismatch");if(Scalar("SELECT acked FROM attachment_key_envelopes WHERE object_id="+Quote(id)+" AND device="+Quote(device))==1)return false;
  Space(8192);Transaction(()=>Execute("UPDATE attachment_key_envelopes SET acked=1 WHERE object_id="+Quote(id)+" AND device="+Quote(device)+";"));return true;
 }}
 internal List<string> PendingKeys(long account,string device,string after){lock(gate){Check();Require(account>0&&MailboxProtocol.Id(device,64)&&(after==""||MailboxProtocol.Id(after,32)),"cache_key_cursor_invalid");return Rows("SELECT o.id FROM objects o JOIN attachment_key_envelopes k ON k.object_id=o.id JOIN recipients r ON r.object_id=o.id AND r.device=k.device WHERE r.account="+account+" AND r.device="+Quote(device)+" AND k.acked=0 AND o.complete=1 AND o.deleting=0 AND o.expires>"+Now()+" AND o.id>"+Quote(after)+" ORDER BY o.id LIMIT 64",64).Select(row=>row[0]).ToList();}}
}
internal sealed partial class AttachmentCacheAdmission {
 internal List<Dictionary<string,object>> RecipientKeys(long account,string device,Func<bool> current,string id){return Authorized(account,device,current,id,d=>{
  Require(d.Account==account&&d.Sender==device,"cache_sender_binding_mismatch");var result=new List<Dictionary<string,object>>();var source=accounts.AttachmentMembership(d.Group,account,device);
  foreach(var recipient in cache.OriginalRecipients(id)){
   accounts.WithCacheGroupAuthorization(d.Group,recipient.Account,recipient.Device,roster=>{
    if(accounts.AttachmentMembership(d.Group,recipient.Account,recipient.Device)!=recipient.Membership)return;
    var binding=accounts.AttachmentRecipientKey(recipient.Account,recipient.Device);
    result.Add(new Dictionary<string,object>{{"accountId",recipient.Account},{"device",recipient.Device},{"membership",recipient.Membership},{"sourceMembership",source},{"keyState",binding==null?"unavailable":"available"},{"binding",binding==null?"":AttachmentCacheRequests.Encode(binding)}});
   });
  }Require(current(),"cache_session_invalid");return result;
 });}
 internal bool PublishKey(long account,string device,Func<bool> current,string id,byte[] wire){var value=AttachmentKeyEnvelope.Parse(wire);return Authorized(account,device,current,id,d=>{
  Require(d.Account==account&&d.Sender==device,"cache_sender_binding_mismatch");Require(d.Wire.SequenceEqual(value.Descriptor.Wire),"cache_key_descriptor_mismatch");
  Require(accounts.AttachmentMembership(d.Group,account,device)==value.SourceMembership,"cache_key_membership_mismatch");bool published=false,executed=false;
  bool allowed=accounts.WithCacheGroupAuthorization(d.Group,value.Recipient.Account,value.Recipient.Device,roster=>{
   Require(accounts.AttachmentMembership(d.Group,value.Recipient.Account,value.Recipient.Device)==value.RecipientMembership&&cache.OriginalMembership(id,value.Recipient.Account,value.Recipient.Device)==value.RecipientMembership,"cache_original_membership_required");
   Require(accounts.AttachmentKeyAvailable(value.Recipient,cache.ExistingKey(id,value.Recipient.Device,wire)),"cache_recipient_key_unavailable");Require(current(),"cache_session_invalid");published=cache.PublishKey(id,value);executed=true;
  });Require(allowed&&executed,"cache_group_not_authorized");return published;
 });}
 internal AttachmentCacheRuntime.KeyEnvelope Key(long account,string device,Func<bool> current,string id){return Authorized(account,device,current,id,d=>{var value=cache.ReadKey(id,device);var parsed=AttachmentKeyEnvelope.Parse(value.Wire);Require(parsed.Recipient.Account==account&&accounts.AttachmentKeyAvailable(parsed.Recipient,true),"cache_recipient_key_unavailable");return value;});}
 internal bool AcknowledgeKey(long account,string device,Func<bool> current,string id,string digest){return Authorized(account,device,current,id,d=>{Key(account,device,current,id);return cache.AcknowledgeKey(id,device,digest);});}
 internal List<AttachmentCacheRuntime.KeyEnvelope> PendingKeys(long account,string device,Func<bool> current,string after,out string cursor,out bool more){Require(current(),"cache_session_invalid");var ids=cache.PendingKeys(account,device,after);var result=new List<AttachmentCacheRuntime.KeyEnvelope>();cursor=after;more=false;
  foreach(string id in ids){if(result.Count>=8){more=true;break;}try{result.Add(Key(account,device,current,id));}catch(InvalidOperationException error){if(error.Message!="cache_group_not_authorized"&&error.Message!="cache_original_membership_required"&&error.Message!="cache_attachment_expired"&&error.Message!="cache_key_expired"&&error.Message!="cache_recipient_key_unavailable")throw;}cursor=id;}more=more||ids.Count==64;Require(current(),"cache_session_invalid");return result;
 }
}
}
