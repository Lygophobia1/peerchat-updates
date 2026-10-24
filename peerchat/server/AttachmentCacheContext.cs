using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChat.Server {
// Authenticated metadata only: clients must independently match both snapshots
// to verified signed ledger states. No historical group key is disclosed.
internal static class AttachmentCacheContext {
 internal const int MaximumSnapshot=32768;
 internal static byte[] Snapshot(Dictionary<string,object> roster){
  if(roster==null)throw new InvalidOperationException("cache_context_unavailable");
  var members=new List<Dictionary<string,object>>();
  foreach(var item in (IEnumerable)roster["members"]){
   var member=(Dictionary<string,object>)item;
   if(members.Count>=256)throw new InvalidOperationException("cache_context_too_large");
   members.Add(new Dictionary<string,object>{{"accountId",member["accountId"]},{"publicIdentity",member["publicIdentity"]},{"role",member["role"]},{"state",member["state"]}});
  }
  var compact=new Dictionary<string,object>{{"groupId",roster["groupId"]},{"legacyGroupId",roster["legacyGroupId"]},{"rosterGeneration",roster["rosterGeneration"]},{"distributorIdentity",roster["distributorIdentity"]},{"members",members},{"creationProof",roster["creationProof"]}};
  var bytes=Encoding.UTF8.GetBytes(new JavaScriptSerializer{MaxJsonLength=MaximumSnapshot}.Serialize(compact));
  if(bytes.Length>MaximumSnapshot)throw new InvalidOperationException("cache_context_too_large");
  return bytes;
 }
 internal static Dictionary<string,object> Parse(byte[] bytes,AttachmentCacheDescriptor descriptor,bool original){
  if(bytes==null||bytes.Length==0||bytes.Length>MaximumSnapshot)throw new InvalidOperationException("cache_context_unavailable");
  var result=new JavaScriptSerializer{MaxJsonLength=MaximumSnapshot}.Deserialize<Dictionary<string,object>>(new UTF8Encoding(false,true).GetString(bytes));
  if(Convert.ToString(result["groupId"])!=descriptor.Group||(original?Convert.ToInt64(result["rosterGeneration"])!=descriptor.Epoch:Convert.ToInt64(result["rosterGeneration"])<descriptor.Epoch))throw new InvalidOperationException("cache_context_binding_invalid");
  return result;
 }
}
internal sealed partial class AttachmentCacheRuntime {
 void InitializeContexts(){
  Execute("CREATE TABLE IF NOT EXISTS attachment_contexts(object_id TEXT PRIMARY KEY REFERENCES objects(id) ON DELETE CASCADE,roster BLOB NOT NULL,digest TEXT NOT NULL);");
  var foreign=Rows("PRAGMA foreign_key_list(attachment_contexts)",8,8);
  Require(foreign.Count==1&&foreign[0][2]=="objects"&&foreign[0][3]=="object_id"&&foreign[0][4]=="id"&&foreign[0][6]=="CASCADE","cache_context_schema_review_required");
  Require(Scalar("SELECT count(*) FROM attachment_contexts WHERE length(roster)=0 OR length(roster)>32768 OR length(digest)<>64")==0,"cache_context_index_invalid");
 }
 void InsertContext(string id,byte[] context){
  if(context==null)return; // Component/legacy objects cannot manufacture evidence later.
  Require(context.Length>0&&context.Length<=AttachmentCacheContext.MaximumSnapshot,"cache_context_too_large");
  Execute("INSERT INTO attachment_contexts VALUES("+Quote(id)+","+Blob(context)+","+Quote(MailboxProtocol.Digest(context))+");");
 }
 internal Dictionary<string,object> OriginalRoster(string id){lock(gate){Check();var descriptor=Descriptor(id);
  var bytes=Binary("SELECT roster FROM attachment_contexts WHERE object_id="+Quote(id));
  Require(bytes!=null,"cache_context_unavailable");
  var digest=Rows("SELECT digest FROM attachment_contexts WHERE object_id="+Quote(id),1);
  Require(digest.Count==1&&digest[0][0]==MailboxProtocol.Digest(bytes),"cache_context_index_invalid");
  return AttachmentCacheContext.Parse(bytes,descriptor,true);
 }}
 internal long ContextExpires(string id,bool requireComplete=true){lock(gate){Check();var status=Inspect(id);
  Require(!requireComplete||status.Complete,"cache_upload_incomplete");Require(status.Expires>Now(),"cache_attachment_expired");
  // Early encrypted offers use the retention horizon, not the moving partial
  // idle deadline. The object itself still expires/reclaims on idle timeout.
  return status.Complete?status.Expires:checked(Now()+options.RetentionSeconds);
 }}
}
internal sealed partial class AttachmentCacheAdmission {
 internal Dictionary<string,object> Context(long account,string device,Func<bool> current,string id){return Authorized(account,device,current,id,d=>{
  long expires=cache.ContextExpires(id,false);
  var original=cache.OriginalRoster(id);
  var latest=AttachmentCacheContext.Parse(AttachmentCacheContext.Snapshot(accounts.GroupRoster(d.Group,account,device,false)),d,false);
  Require(Convert.ToString(original["legacyGroupId"])==Convert.ToString(latest["legacyGroupId"]),"cache_context_binding_invalid");
  var result=new Dictionary<string,object>{{"id",id},{"descriptor",AttachmentCacheRequests.Encode(d.Wire)},{"dataEncoding","base64url"},{"expiresUtc",expires},{"originalRoster",original},{"currentRoster",latest},{"sourceMembership",cache.OriginalMembership(id,d.Account,d.Sender)??""},{"recipientMembership",cache.OriginalMembership(id,account,device)??""},{"currentSourceMembership",accounts.AttachmentMembership(d.Group,d.Account,d.Sender)??""},{"currentRecipientMembership",accounts.AttachmentMembership(d.Group,account,device)??""}};
  Require(current(),"cache_session_invalid");return result;
 });}
}
}
