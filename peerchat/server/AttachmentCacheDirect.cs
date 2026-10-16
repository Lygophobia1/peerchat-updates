using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace PeerChat.Server {
// Typed immutable public audience evidence. No private metadata, key material,
// enrollment or authorization mutation is accepted from an upload request.
internal sealed class DirectCacheAudience {
 internal string Relationship;
 internal MailboxProtocol.Binding Source;
 internal readonly List<MailboxProtocol.Binding> Recipients=new List<MailboxProtocol.Binding>();
 internal byte[] Wire;
 internal static DirectCacheAudience Parse(byte[] bytes,long sender,string device,long recipient){
  if(bytes==null||bytes.Length>32768)throw new InvalidOperationException("cache_dm_audience_invalid");
  try{
   if(bytes.Length<4||bytes[0]!=80||bytes[1]!=84||bytes[2]!=1||bytes[3]!=30)throw new InvalidOperationException();
   int at=4;if(GroupRosterProof.Text(bytes,ref at,64)!="PeerChat/dm-cache-audience/v1")throw new InvalidOperationException();
   var value=new DirectCacheAudience{Relationship=GroupRosterProof.Text(bytes,ref at,32),Source=MailboxProtocol.Binding.Parse(GroupRosterProof.Bytes(bytes,ref at,2048))};
   ulong count=GroupRosterProof.Number(bytes,ref at);if(!MailboxProtocol.Id(value.Relationship,32)||sender==recipient||value.Source.Account!=sender||value.Source.Device!=device||!value.Source.Allowed||count==0||count>32)throw new InvalidOperationException();
   string previous="";while(count-->0){var b=MailboxProtocol.Binding.Parse(GroupRosterProof.Bytes(bytes,ref at,2048));if(b.Account!=recipient||!b.Allowed||String.CompareOrdinal(b.Device,previous)<=0||b.Device==device)throw new InvalidOperationException();previous=b.Device;value.Recipients.Add(b);}
   if(at!=bytes.Length)throw new InvalidOperationException();value.Wire=(byte[])bytes.Clone();return value;
  }catch(Exception error){if(error is OutOfMemoryException)throw;throw new InvalidOperationException("cache_dm_audience_invalid");}
 }
 internal static byte[] Encode(string relationship,byte[] source,IEnumerable<byte[]> recipients){
  using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream)){
   Action<byte[]> field=b=>{writer.Write((ulong)b.Length);writer.Write(b);};Action<string> text=s=>field(Encoding.UTF8.GetBytes(s));
   writer.Write(new byte[]{80,84,1,30});text("PeerChat/dm-cache-audience/v1");text(relationship);field(source);
   var rows=recipients.ToArray();writer.Write((ulong)rows.Length);foreach(var row in rows)field(row);return stream.ToArray();
  }
 }
 internal string Digest {get{return MailboxProtocol.Digest(Wire);}}
 internal static string DeviceBinding(MailboxProtocol.Binding binding){return MailboxProtocol.Digest(binding.Wire).Substring(0,32);}
}
sealed partial class AccountDatabase {
 internal void EnsureDirectCacheRelationships(){lock(gate){Execute("BEGIN IMMEDIATE;");try{
  Execute("CREATE TABLE IF NOT EXISTS attachment_dm_relationships(account_low INTEGER NOT NULL,account_high INTEGER NOT NULL,binding TEXT NOT NULL,PRIMARY KEY(account_low,account_high));"+
   "CREATE TABLE IF NOT EXISTS attachment_dm_capabilities(account INTEGER NOT NULL,device TEXT NOT NULL,key_id TEXT NOT NULL,generation INTEGER NOT NULL,proof BLOB NOT NULL,PRIMARY KEY(device,key_id));"+
   "CREATE TRIGGER IF NOT EXISTS attachment_dm_friend_insert AFTER INSERT ON friendships BEGIN INSERT INTO attachment_dm_relationships VALUES(NEW.account_low,NEW.account_high,lower(hex(randomblob(16)))) ON CONFLICT(account_low,account_high) DO UPDATE SET binding=excluded.binding; END;"+
   "CREATE TRIGGER IF NOT EXISTS attachment_dm_friend_delete AFTER DELETE ON friendships BEGIN DELETE FROM attachment_dm_relationships WHERE account_low=OLD.account_low AND account_high=OLD.account_high; END;"+
   "CREATE TRIGGER IF NOT EXISTS attachment_dm_block_insert AFTER INSERT ON account_blocks BEGIN UPDATE attachment_dm_relationships SET binding=lower(hex(randomblob(16))) WHERE account_low=MIN(NEW.blocker_id,NEW.blocked_id) AND account_high=MAX(NEW.blocker_id,NEW.blocked_id); END;"+
   "CREATE TRIGGER IF NOT EXISTS attachment_dm_device_delete AFTER DELETE ON devices BEGIN UPDATE attachment_dm_relationships SET binding=lower(hex(randomblob(16))) WHERE account_low=OLD.account_id OR account_high=OLD.account_id; END;"+
   "CREATE TRIGGER IF NOT EXISTS attachment_dm_device_move AFTER UPDATE OF account_id ON devices WHEN OLD.account_id<>NEW.account_id BEGIN UPDATE attachment_dm_relationships SET binding=lower(hex(randomblob(16))) WHERE account_low IN (OLD.account_id,NEW.account_id) OR account_high IN (OLD.account_id,NEW.account_id); END;"+
   "INSERT OR IGNORE INTO attachment_dm_relationships SELECT account_low,account_high,lower(hex(randomblob(16))) FROM friendships;COMMIT;");
 }catch{try{Execute("ROLLBACK;");}catch{}throw;}}}
 string DirectCacheRelationship(long sender,long recipient){return ScalarText(database,"SELECT binding FROM attachment_dm_relationships WHERE account_low="+Low(sender,recipient)+" AND account_high="+High(sender,recipient));}
 bool DirectCacheKeyAvailable(MailboxProtocol.Binding binding,bool retired){return AttachmentKeyAvailable(binding,retired)&&AttachmentRecipientKey(binding.Account,binding.Device)!=null;}
 bool DirectCacheCapable(MailboxProtocol.Binding binding){return ScalarLong(database,"SELECT count(*) FROM attachment_dm_capabilities WHERE account="+binding.Account+" AND device="+Quote(binding.Device)+" AND key_id="+Quote(binding.Key)+" AND generation="+binding.Generation)==1;}
 internal bool RegisterDirectCache(long account,string device,Func<bool> current,byte[] wire){lock(gate){
  try{
   if(!current()||wire==null||wire.Length>1024||wire.Length<4||wire[0]!=80||wire[1]!=84||wire[2]!=1||wire[3]!=30)throw new InvalidOperationException();
   int at=4;if(GroupRosterProof.Text(wire,ref at,64)!="PeerChat/dm-cache-device/v1")throw new InvalidOperationException();
   ulong owner=GroupRosterProof.Number(wire,ref at);string who=GroupRosterProof.Text(wire,ref at,64),keyId=GroupRosterProof.Text(wire,ref at,32);ulong generation=GroupRosterProof.Number(wire,ref at),body=GroupRosterProof.Number(wire,ref at),codec=GroupRosterProof.Number(wire,ref at);var identity=GroupRosterProof.Bytes(wire,ref at,72);int end=at;var signature=GroupRosterProof.Bytes(wire,ref at,64);
   var key=AttachmentRecipientKey(account,device);if(key==null)throw new InvalidOperationException();var binding=MailboxProtocol.Binding.Parse(key);
   if(at!=wire.Length||owner!=(ulong)account||who!=device||binding.Key!=keyId||generation!=(ulong)binding.Generation||body!=1||codec!=1||!MailboxProtocol.Verify(wire.Take(end).ToArray(),signature,identity,device))throw new InvalidOperationException();
   var previous=MailboxBlob("SELECT proof FROM attachment_dm_capabilities WHERE device="+Quote(device)+" AND key_id="+Quote(keyId));
   // Different valid ECDSA retry signatures do not change capability semantics.
   if(previous==null)Execute("INSERT INTO attachment_dm_capabilities VALUES("+account+","+Quote(device)+","+Quote(keyId)+","+binding.Generation+","+MailboxData(wire)+");");
   return true;
  }catch(Exception error){if(error is OutOfMemoryException)throw;throw new InvalidOperationException("cache_dm_capability_binding_invalid");}
 }}
 internal byte[] PrepareDirectCache(long account,string device,long recipient,Func<bool> current){lock(gate){
  if(!current()||!MailboxFriendScope(account,device,recipient))throw new InvalidOperationException("cache_dm_not_authorized");
  string relationship=DirectCacheRelationship(account,recipient);var source=AttachmentRecipientKey(account,device);var recipients=EligibilityBindings(recipient);
  if(!MailboxProtocol.Id(relationship,32)||source==null||recipients.Count==0)throw new InvalidOperationException("cache_dm_verified_keys_required");
  if(!DirectCacheCapable(MailboxProtocol.Binding.Parse(source)))throw new InvalidOperationException("cache_dm_source_upgrade_required");
  if(recipients.Any(x=>!DirectCacheCapable(MailboxProtocol.Binding.Parse(x.Binding))))throw new InvalidOperationException("cache_dm_recipient_upgrade_required");
  var bytes=DirectCacheAudience.Encode(relationship,source,recipients.Select(x=>x.Binding));DirectCacheAudience.Parse(bytes,account,device,recipient);return bytes;
 }}
 internal T WithDirectCacheAuthority<T>(AttachmentCacheDescriptor descriptor,DirectCacheAudience audience,long account,string device,Func<bool> current,bool admission,Func<T> action){lock(gate){
  if(!descriptor.Direct||descriptor.Relationship!=audience.Relationship||descriptor.AudienceDigest!=audience.Digest||
     !current()||!MailboxFriendScope(descriptor.Account,descriptor.Sender,descriptor.RecipientAccount)||
     DirectCacheRelationship(descriptor.Account,descriptor.RecipientAccount)!=audience.Relationship||
     !DirectCacheKeyAvailable(audience.Source,!admission))throw new InvalidOperationException("cache_dm_authority_stale");
  if(account==descriptor.Account&&device==descriptor.Sender){
   if(admission&&!PrepareDirectCache(account,device,descriptor.RecipientAccount,current).SequenceEqual(audience.Wire))throw new InvalidOperationException("cache_dm_audience_changed");
  }else{
   var local=audience.Recipients.FirstOrDefault(x=>x.Account==account&&x.Device==device);
   if(account!=descriptor.RecipientAccount||local==null||!DirectCacheKeyAvailable(local,true))throw new InvalidOperationException("cache_dm_original_recipient_required");
  }
  if(admission&&audience.Recipients.Any(x=>!DirectCacheKeyAvailable(x,false)))throw new InvalidOperationException("cache_dm_audience_changed");
  return action();
 }}
}
internal sealed partial class AttachmentCacheRuntime {
 internal long DirectPreparationExpires(){lock(gate){Check();return checked(Now()+options.RetentionSeconds);}}
 internal DirectCacheAudience DirectAudience(string id){lock(gate){Check();var d=Descriptor(id);Require(d.Direct,"cache_scope_mismatch");
  var bytes=Binary("SELECT roster FROM attachment_contexts WHERE object_id="+Quote(id));Require(bytes!=null,"cache_dm_audience_unavailable");
  var rows=Rows("SELECT digest FROM attachment_contexts WHERE object_id="+Quote(id),1);Require(rows.Count==1&&rows[0][0]==MailboxProtocol.Digest(bytes)&&rows[0][0]==d.AudienceDigest,"cache_dm_audience_invalid");
  return DirectCacheAudience.Parse(bytes,d.Account,d.Sender,d.RecipientAccount);
 }}
}
internal sealed partial class AttachmentCacheAdmission {
 internal void EnableDirect(){accounts.EnsureDirectCacheRelationships();}
 internal bool RegisterDirect(long account,string device,Func<bool> current,byte[] proof){return accounts.RegisterDirectCache(account,device,current,proof);}
 internal byte[] PrepareDirect(long account,string device,Func<bool> current,long recipient){return accounts.PrepareDirectCache(account,device,recipient,current);}
 internal long DirectPreparationExpires(){return cache.DirectPreparationExpires();}
 internal AttachmentCacheRuntime.Status AdmitDirect(long account,string device,Func<bool> current,byte[] wire,byte[] audienceWire){
  var d=AttachmentCacheDescriptor.Parse(wire);Require(d.Direct&&d.Account==account&&d.Sender==device,"cache_scope_or_sender_mismatch");
  var audience=DirectCacheAudience.Parse(audienceWire,d.Account,d.Sender,d.RecipientAccount);
  return accounts.WithDirectCacheAuthority(d,audience,account,device,current,true,()=>{
   var recipients=audience.Recipients.Concat(new[]{audience.Source}).Select(x=>new CacheRecipient(x.Account,x.Device,DirectCacheAudience.DeviceBinding(x))).ToList();
   return cache.Admit(d,recipients,audience.Wire);
  });
 }
 T AuthorizedDirect<T>(long account,string device,Func<bool> current,string id,Func<AttachmentCacheDescriptor,T> action){
  var d=cache.Describe(id);Require(d.Direct,"cache_scope_mismatch");var audience=cache.DirectAudience(id);
  return accounts.WithDirectCacheAuthority(d,audience,account,device,current,false,()=>{
   var local=account==d.Account&&device==d.Sender?audience.Source:audience.Recipients.FirstOrDefault(x=>x.Account==account&&x.Device==device);
   Require(local!=null&&cache.OriginalMembership(id,account,device)==DirectCacheAudience.DeviceBinding(local),"cache_dm_original_recipient_required");return action(d);
  });
 }
 internal Dictionary<string,object> DirectContext(long account,string device,Func<bool> current,string id){return AuthorizedDirect(account,device,current,id,d=>{
  var status=cache.Inspect(id);return new Dictionary<string,object>{{"id",id},{"descriptor",AttachmentCacheRequests.Encode(d.Wire)},{"audience",AttachmentCacheRequests.Encode(cache.DirectAudience(id).Wire)},{"expiresUtc",cache.ContextExpires(id)},{"ciphertextSize",status.Size},{"durableOffset",status.Offset},{"ciphertextComplete",status.Complete},{"dataEncoding","base64url"}};
 });}
 internal AttachmentCacheRuntime.Status InspectDirectUpload(long account,string device,Func<bool> current,string lease){return InspectDirect(account,device,current,cache.LeaseObject(lease,device,true));}
 internal string AcquireDirect(long account,string device,Func<bool> current,string id,bool upload){return AuthorizedDirect(account,device,current,id,d=>{Require(!upload||d.Account==account&&d.Sender==device,"cache_sender_binding_mismatch");return cache.Acquire(id,device,upload);});}
 internal AttachmentCacheRuntime.Status InspectDirect(long account,string device,Func<bool> current,string id){return AuthorizedDirect(account,device,current,id,d=>cache.Inspect(id));}
 internal long AppendDirect(long account,string device,Func<bool> current,string lease,long offset,byte[] bytes){return AuthorizedDirect(account,device,current,cache.LeaseObject(lease,device,true),d=>cache.Append(lease,offset,bytes));}
 internal bool CompleteDirect(long account,string device,Func<bool> current,string lease){
  string id=cache.LeaseObject(lease,device,true);AuthorizedDirect(account,device,current,id,d=>true);var evidence=cache.VerifyCompletion(lease);
  return AuthorizedDirect(account,device,current,id,d=>{Require(cache.LeaseObject(lease,device,true)==id,"cache_completion_evidence_mismatch");cache.CommitCompletion(lease,evidence);return true;});
 }
 internal byte[] ReadDirect(long account,string device,Func<bool> current,string lease,long offset,int maximum){return AuthorizedDirect(account,device,current,cache.LeaseObject(lease,device,false),d=>cache.Read(lease,offset,maximum));}
 internal bool ReleaseDirect(long account,string device,Func<bool> current,string lease,bool upload){return AuthorizedDirect(account,device,current,cache.LeaseObject(lease,device,upload),d=>{cache.Release(lease);return true;});}
 internal bool ProgressDirect(long account,string device,Func<bool> current,string lease,long offset){return AuthorizedDirect(account,device,current,cache.LeaseObject(lease,device,false),d=>{cache.RecipientProgress(lease,offset,false);return true;});}
 internal bool PublishDirect(long account,string device,Func<bool> current,string id,byte[] wire){return AuthorizedDirect(account,device,current,id,d=>{
  Require(account==d.Account&&device==d.Sender,"cache_sender_binding_mismatch");cache.ContextExpires(id);
  var envelope=MailboxProtocol.Envelope.Parse(wire);var audience=cache.DirectAudience(id);var recipient=audience.Recipients.FirstOrDefault(x=>x.Device==envelope.RecipientDevice);
  Require(recipient!=null&&envelope.Sender==d.Account&&envelope.SenderDevice==d.Sender&&envelope.Recipient==d.RecipientAccount&&envelope.Message==d.Message&&envelope.Key==recipient.Key&&envelope.Generation==recipient.Generation&&envelope.Expires<=checked(cache.ContextExpires(id)*1000),"cache_dm_notice_binding_invalid");
  // Recheck the intended recipient, including revocation, under the account
  // lock through the existing mailbox FULL transaction. Lost replies retry the
  // exact immutable envelope, never retarget or regenerate it.
  return accounts.WithDirectCacheAuthority(d,audience,recipient.Account,recipient.Device,current,false,()=>{
   string state=accounts.StoreMailbox(account,device,d.PublicIdentity,wire);Require(state=="stored","cache_dm_notice_"+state);return true;
  });
 });}
 internal bool VerifyDirectReceipt(long account,string device,Func<bool> current,string id,byte[] wire){return AuthorizedDirect(account,device,current,id,d=>{
  var receipt=AttachmentVerificationReceipt.Parse(wire,d);var recipient=cache.DirectAudience(id).Recipients.FirstOrDefault(x=>x.Account==account&&x.Device==device);
  Require(recipient!=null&&receipt.Account==account&&receipt.Device==device&&receipt.RecipientKey==recipient.Key&&receipt.RecipientGeneration==recipient.Generation,"cache_verification_device_mismatch");return cache.RecordVerification(id,receipt);
 });}
 internal Dictionary<string,object> DirectReceipts(long account,string device,Func<bool> current,string id,string after){return AuthorizedDirect(account,device,current,id,d=>{
  Require(d.Account==account&&d.Sender==device,"cache_sender_binding_mismatch");Require(after==""||MailboxProtocol.Id(after,64),"cache_verification_cursor_invalid");cache.ContextExpires(id);var audience=cache.DirectAudience(id);
  var items=new List<Dictionary<string,object>>();string cursor=after;bool more=false;
  foreach(var recipient in audience.Recipients){if(String.CompareOrdinal(recipient.Device,after)<=0)continue;if(items.Count==8){more=true;break;}
   try{items.Add(accounts.WithDirectCacheAuthority(d,audience,recipient.Account,recipient.Device,current,false,()=>cache.Verification(id,new CacheRecipient(recipient.Account,recipient.Device,DirectCacheAudience.DeviceBinding(recipient)))));}
   catch(InvalidOperationException error){if(error.Message!="cache_dm_original_recipient_required")throw;}
   cursor=recipient.Device;
  }return new Dictionary<string,object>{{"items",items},{"cursor",cursor},{"more",more},{"dataEncoding","base64url"}};
 });}
}
}
