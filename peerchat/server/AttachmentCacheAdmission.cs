using System;
using System.Collections.Generic;
using System.Linq;

namespace PeerChat.Server {
sealed partial class AccountDatabase {
 // Cache-local membership incarnation, not an identity or grant. A removed and
 // later re-added device cannot regain cached history merely by reusing its ID.
 // Triggers participate in the EXISTING roster mutation transaction.
 internal void EnsureAttachmentMembership(){lock(gate){TransactionCacheMembership();}}
 void TransactionCacheMembership(){Execute("BEGIN IMMEDIATE;");try{Execute(
  "CREATE TABLE IF NOT EXISTS attachment_membership(group_id TEXT NOT NULL,account INTEGER NOT NULL,device TEXT NOT NULL,binding TEXT NOT NULL,PRIMARY KEY(group_id,device));"+
  "CREATE TRIGGER IF NOT EXISTS attachment_member_insert AFTER INSERT ON group_roster_devices WHEN NEW.state='member' BEGIN INSERT INTO attachment_membership VALUES(NEW.group_id,NEW.account_id,NEW.public_identity,lower(hex(randomblob(16)))) ON CONFLICT(group_id,device) DO UPDATE SET account=excluded.account,binding=excluded.binding; END;"+
  "CREATE TRIGGER IF NOT EXISTS attachment_member_delete AFTER DELETE ON group_roster_devices BEGIN DELETE FROM attachment_membership WHERE group_id=OLD.group_id AND device=OLD.public_identity; END;"+
  "CREATE TRIGGER IF NOT EXISTS attachment_member_state AFTER UPDATE OF state ON group_roster_devices WHEN OLD.state<>NEW.state BEGIN DELETE FROM attachment_membership WHERE group_id=OLD.group_id AND device=OLD.public_identity; INSERT INTO attachment_membership SELECT NEW.group_id,NEW.account_id,NEW.public_identity,lower(hex(randomblob(16))) WHERE NEW.state='member'; END;"+
  "INSERT OR IGNORE INTO attachment_membership SELECT group_id,account_id,public_identity,lower(hex(randomblob(16))) FROM group_roster_devices WHERE state='member';COMMIT;");
 }catch{try{Execute("ROLLBACK;");}catch{}throw;}}
 internal string AttachmentMembership(string group,long account,string device){lock(gate){return ScalarText(database,"SELECT binding FROM attachment_membership WHERE group_id="+Quote(group)+" AND account="+account+" AND device="+Quote(device));}}
 internal List<CacheRecipient> AttachmentRecipients(string group){lock(gate){var result=new List<CacheRecipient>();IntPtr statement=IntPtr.Zero;try{
  if(SqliteNative.prepare(database,"SELECT m.account,m.device,m.binding FROM attachment_membership m JOIN group_roster_devices r ON r.group_id=m.group_id AND r.account_id=m.account AND r.public_identity=m.device AND r.state='member' WHERE m.group_id="+Quote(group)+" AND NOT EXISTS(SELECT 1 FROM device_authority a WHERE a.public_identity=m.device AND a.revoked=1) ORDER BY m.device LIMIT 65",out statement)!=0)throw new InvalidOperationException("cache_roster_unavailable");int code;while((code=SqliteNative.step(statement))==100){if(result.Count>=64)throw new InvalidOperationException("cache_group_recipient_limit");result.Add(new CacheRecipient(SqliteNative.columnLong(statement,0),SqliteNative.columnText(statement,1),SqliteNative.columnText(statement,2)));}if(code!=101)throw new InvalidOperationException("cache_roster_unavailable");return result;
 }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}}}
}
// No socket or retry ownership. Each operation runs under authoritative roster
// admission, through the durable storage operation, before its reply is sent.
internal sealed partial class AttachmentCacheAdmission {
 readonly AccountDatabase accounts;readonly AttachmentCacheRuntime cache;
 internal AttachmentCacheAdmission(AccountDatabase accounts,AttachmentCacheRuntime cache){this.accounts=accounts;this.cache=cache;accounts.EnsureAttachmentMembership();}
 static void Require(bool ok,string reason){if(!ok)throw new InvalidOperationException(reason);}
 internal AttachmentCacheRuntime.Status Admit(long account,string device,Func<bool> current,byte[] descriptor){
  var signed=AttachmentCacheDescriptor.Parse(descriptor);Require(!signed.Direct,"cache_scope_mismatch");Require(signed.Account==account&&signed.Sender==device,"cache_sender_binding_mismatch");AttachmentCacheRuntime.Status result=null;
  bool allowed=accounts.WithGroupRosterAuthorization(signed.Group,account,device,roster=>{
   Require(current()&&Convert.ToInt64(roster["rosterGeneration"])==signed.Epoch,"cache_session_or_epoch_stale");result=cache.Admit(signed,accounts.AttachmentRecipients(signed.Group),AttachmentCacheContext.Snapshot(roster));
   Require(cache.OriginalMembership(signed.Id,account,device)==accounts.AttachmentMembership(signed.Group,account,device),"cache_membership_replaced");
  });Require(allowed&&result!=null,"cache_group_not_authorized");return result;
 }
 T Authorized<T>(long account,string device,Func<bool> current,string id,Func<AttachmentCacheDescriptor,T> action){
  var descriptor=cache.Describe(id);Require(!descriptor.Direct,"cache_scope_mismatch");T result=default(T);bool executed=false;
  bool allowed=accounts.WithGroupRosterAuthorization(descriptor.Group,account,device,roster=>{
   Require(current(),"cache_session_invalid");string binding=accounts.AttachmentMembership(descriptor.Group,account,device);
   Require(!String.IsNullOrEmpty(binding)&&cache.OriginalMembership(id,account,device)==binding,"cache_original_membership_required");result=action(descriptor);executed=true;
  });Require(allowed&&executed,"cache_group_not_authorized");return result;
 }
 internal string Acquire(long account,string device,Func<bool> current,string id,bool upload){return Authorized(account,device,current,id,d=>{Require(!upload||d.Account==account&&d.Sender==device,"cache_sender_binding_mismatch");return cache.Acquire(id,device,upload);});}
 internal AttachmentCacheRuntime.Status Inspect(long account,string device,Func<bool> current,string id){return Authorized(account,device,current,id,d=>cache.Inspect(id));}
 internal long Append(long account,string device,Func<bool> current,string lease,long offset,byte[] bytes){string id=cache.LeaseObject(lease,device,true);return Authorized(account,device,current,id,d=>cache.Append(lease,offset,bytes));}
 internal bool Complete(long account,string device,Func<bool> current,string lease){
  string id=cache.LeaseObject(lease,device,true);Authorized(account,device,current,id,d=>true);
  var evidence=cache.VerifyCompletion(lease);
  // Recheck authority through the final durable commit. A revoked member or
  // superseded session cannot complete after verification began.
  return Authorized(account,device,current,id,d=>{Require(cache.LeaseObject(lease,device,true)==id,"cache_completion_evidence_mismatch");cache.CommitCompletion(lease,evidence);return true;});
 }
 internal byte[] Read(long account,string device,Func<bool> current,string lease,long offset,int maximum){string id=cache.LeaseObject(lease,device,false);return Authorized(account,device,current,id,d=>cache.Read(lease,offset,maximum));}
 internal bool Release(long account,string device,Func<bool> current,string lease,bool upload){string id=cache.LeaseObject(lease,device,upload);return Authorized(account,device,current,id,d=>{cache.Release(lease);return true;});}
 internal bool Progress(long account,string device,Func<bool> current,string lease,long offset){string id=cache.LeaseObject(lease,device,false);return Authorized(account,device,current,id,d=>{cache.RecipientProgress(lease,offset,false);return true;});}
 internal bool Publish(long account,string device,Func<bool> current,string id,byte[] wire){return Authorized(account,device,current,id,d=>{Require(d.Account==account&&d.Sender==device,"cache_sender_binding_mismatch");cache.Publish(id,wire);return true;});}
 internal AttachmentCacheRuntime.Announcement Announcement(long account,string device,Func<bool> current,string id){return Authorized(account,device,current,id,d=>cache.ReadAnnouncement(id));}
 internal bool AcknowledgeAnnouncement(long account,string device,Func<bool> current,string id,string digest){return Authorized(account,device,current,id,d=>cache.AcknowledgeAnnouncement(id,device,digest));}
 internal sealed class AnnouncementPage {internal readonly List<AttachmentCacheRuntime.Announcement> Items=new List<AttachmentCacheRuntime.Announcement>();internal string Cursor;internal bool More;}
 internal AnnouncementPage PendingAnnouncements(long account,string device,Func<bool> current,string after){
  Require(current(),"cache_session_invalid");var candidates=cache.PendingAnnouncements(account,device,after);var page=new AnnouncementPage{Cursor=after};int bytes=0;
  foreach(string id in candidates){AttachmentCacheRuntime.Announcement item;
   try{item=Announcement(account,device,current,id);}catch(InvalidOperationException error){
    if(error.Message!="cache_group_not_authorized"&&error.Message!="cache_original_membership_required"&&error.Message!="cache_attachment_expired"&&error.Message!="cache_attachment_unavailable")throw;
    page.Cursor=id;continue;
   }
   int cost=checked((item.Wire.Length*4+2)/3+(item.Descriptor.Length*4+2)/3+1024);
   if(page.Items.Count>=8||bytes+cost>94000){page.More=true;Require(current(),"cache_session_invalid");return page;}page.Items.Add(item);bytes+=cost;page.Cursor=id;
  }
  page.More=candidates.Count==64;Require(current(),"cache_session_invalid");return page;
 }
}
}
