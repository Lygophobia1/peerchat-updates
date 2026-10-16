using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.RegularExpressions;

namespace PeerChat.Server {
// Bounded storage RPC adapter only. TransferManager remains the client chunk,
// retry, hash, ACK and journal owner. No sockets, timers or retry queues here.
// Production dispatch remains closed behind the server readiness gate.
internal sealed class AttachmentCacheRequests {
 readonly AttachmentCacheAdmission admission;readonly bool directReady;
 internal AttachmentCacheRequests(AttachmentCacheAdmission value,bool directReady=false){if(value==null)throw new ArgumentNullException("value");admission=value;this.directReady=directReady;if(directReady)admission.EnableDirect();}
 static void Require(bool valid){if(!valid)throw new InvalidOperationException("cache_malformed_request");}
 static string Text(Dictionary<string,object> request,string name,int maximum){object value;Require(request.TryGetValue(name,out value)&&value is string);string result=(string)value;Require(result.Length<=maximum);return result;}
 static string Id(Dictionary<string,object> request,string name){string value=Text(request,name,32);Require(MailboxProtocol.Id(value,32));return value;}
 static long Number(Dictionary<string,object> request,string name){object value;Require(request.TryGetValue(name,out value)&&value!=null);string text=Convert.ToString(value,CultureInfo.InvariantCulture);long result;Require(text.Length<=19&&Regex.IsMatch(text,"^[0-9]+$")&&Int64.TryParse(text,NumberStyles.None,CultureInfo.InvariantCulture,out result));return Int64.Parse(text,CultureInfo.InvariantCulture);}
 static bool Flag(Dictionary<string,object> request,string name){object value;Require(request.TryGetValue(name,out value)&&value is bool);return (bool)value;}
 // Canonical base64url prevents JSON HTML escaping of '+' from expanding a
 // valid 64 KiB chunk beyond the existing 100000-byte RPC frame bound.
 internal static string Encode(byte[] bytes){return Convert.ToBase64String(bytes).TrimEnd('=').Replace('+','-').Replace('/','_');}
 internal static byte[] Decode(string text,int maximum=65536){Require(maximum>0&&maximum<=GroupCiphertextFanout.MaxEnvelope&&text.Length<=(maximum*4+2)/3&&text.Length%4!=1&&Regex.IsMatch(text,"^[A-Za-z0-9_-]*$"));var bytes=Convert.FromBase64String(text.Replace('-','+').Replace('_','/')+new string('=',(4-text.Length%4)%4));Require(bytes.Length<=maximum&&Encode(bytes)==text);return bytes;}
 static Dictionary<string,object> Announcement(AttachmentCacheRuntime.Announcement value){return new Dictionary<string,object>{{"id",value.Id},{"messageId",value.Message},{"digest",value.Digest},{"envelope",Encode(value.Wire)},{"descriptor",Encode(value.Descriptor)},{"dataEncoding","base64url"}};}
 static Dictionary<string,object> Key(AttachmentCacheRuntime.KeyEnvelope value){return new Dictionary<string,object>{{"id",value.Id},{"device",value.Device},{"digest",value.Digest},{"envelope",Encode(value.Wire)},{"dataEncoding","base64url"}};}
 static void Status(Dictionary<string,object> reply,AttachmentCacheRuntime.Status value){reply["ciphertextSize"]=value.Size;reply["durableOffset"]=value.Offset;reply["ciphertextComplete"]=value.Complete;reply["expiresUtc"]=value.Expires;}
 internal Dictionary<string,object> Execute(long account,string device,Func<bool> current,Dictionary<string,object> request){
  var reply=new Dictionary<string,object>{{"state","cache_malformed_request"},{"cacheVersion",1}};
  try{
   Require(request!=null&&current!=null&&account>0&&MailboxProtocol.Id(device,64));
   string operation=Text(request,"type",32),correlation=Id(request,"requestId");reply["type"]="cache_result";reply["operation"]=operation;reply["requestId"]=correlation;
   if(operation.StartsWith("cache_dm_",StringComparison.Ordinal)){
    reply["cacheVersion"]=2;reply["dmAttachmentVersion"]=directReady?1:0;
    if(!directReady)throw new InvalidOperationException("cache_dm_feature_gated");
   }
   switch(operation){
    case "cache_dm_capability":break;
    case "cache_dm_register":reply["deviceCapabilityRegistered"]=admission.RegisterDirect(account,device,current,Decode(Text(request,"proof",1366),1024));break;
    case "cache_dm_prepare":reply["audience"]=Encode(admission.PrepareDirect(account,device,current,Number(request,"recipientAccountId")));reply["expiresUtc"]=admission.DirectPreparationExpires();reply["dataEncoding"]="base64url";break;
    case "cache_dm_admit":Status(reply,admission.AdmitDirect(account,device,current,Decode(Text(request,"descriptor",2731),2048),Decode(Text(request,"audience",43691),32768)));break;
    case "cache_dm_status":Status(reply,admission.InspectDirect(account,device,current,Id(request,"id")));break;
    case "cache_dm_context":foreach(var item in admission.DirectContext(account,device,current,Id(request,"id")))reply[item.Key]=item.Value;break;
    case "cache_dm_publish":reply["newNoticeStored"]=admission.PublishDirect(account,device,current,Id(request,"id"),Decode(Text(request,"envelope",87382)));break;
    case "cache_dm_verified":reply["newVerificationReceipt"]=admission.VerifyDirectReceipt(account,device,current,Id(request,"id"),Decode(Text(request,"receipt",5462),AttachmentVerificationReceipt.MaximumWire));break;
    case "cache_dm_receipts":foreach(var item in admission.DirectReceipts(account,device,current,Id(request,"id"),Text(request,"after",64)))reply[item.Key]=item.Value;break;
    case "cache_dm_acquire":reply["lease"]=admission.AcquireDirect(account,device,current,Id(request,"id"),Flag(request,"upload"));break;
    case "cache_dm_append":{byte[] bytes=Decode(Text(request,"data",87382));Require(bytes.Length>0);reply["durableOffset"]=admission.AppendDirect(account,device,current,Id(request,"lease"),Number(request,"offset"),bytes);break;}
    case "cache_dm_complete":admission.CompleteDirect(account,device,current,Id(request,"lease"));Status(reply,admission.InspectDirectUpload(account,device,current,Id(request,"lease")));break;
    case "cache_dm_read":{long maximum=Number(request,"maximum");Require(maximum>0&&maximum<=65536);reply["data"]=Encode(admission.ReadDirect(account,device,current,Id(request,"lease"),Number(request,"offset"),(int)maximum));reply["dataEncoding"]="base64url";break;}
    case "cache_dm_progress":{long offset=Number(request,"offset");admission.ProgressDirect(account,device,current,Id(request,"lease"),offset);reply["durableOffset"]=offset;break;}
    case "cache_dm_release":admission.ReleaseDirect(account,device,current,Id(request,"lease"),Flag(request,"upload"));break;
    case "cache_admit":Status(reply,admission.Admit(account,device,current,MailboxProtocol.Decode(Text(request,"descriptor",2732),2048)));break;
    case "cache_status":Status(reply,admission.Inspect(account,device,current,Id(request,"id")));break;
    case "cache_context":foreach(var item in admission.Context(account,device,current,Id(request,"id")))reply[item.Key]=item.Value;break;
    case "cache_verified":reply["newVerificationReceipt"]=admission.VerifyReceipt(account,device,current,Id(request,"id"),Decode(Text(request,"receipt",5462),AttachmentVerificationReceipt.MaximumWire));break;
    case "cache_receipts":foreach(var item in admission.VerificationReceipts(account,device,current,Id(request,"id"),Text(request,"after",64)))reply[item.Key]=item.Value;break;
    case "cache_acquire":reply["lease"]=admission.Acquire(account,device,current,Id(request,"id"),Flag(request,"upload"));break;
    case "cache_append":{
     byte[] bytes=Decode(Text(request,"data",87382));Require(bytes.Length>0);
     reply["durableOffset"]=admission.Append(account,device,current,Id(request,"lease"),Number(request,"offset"),bytes);break;
    }
    case "cache_complete":admission.Complete(account,device,current,Id(request,"lease"));reply["ciphertextComplete"]=true;break;
    case "cache_read":{
     long maximum=Number(request,"maximum");Require(maximum>0&&maximum<=65536);
     byte[] bytes=admission.Read(account,device,current,Id(request,"lease"),Number(request,"offset"),(int)maximum);reply["data"]=Encode(bytes);reply["dataEncoding"]="base64url";break;
    }
    case "cache_progress":admission.Progress(account,device,current,Id(request,"lease"),Number(request,"offset"));break;
    case "cache_release":admission.Release(account,device,current,Id(request,"lease"),Flag(request,"upload"));break;
    case "cache_publish":admission.Publish(account,device,current,Id(request,"id"),Decode(Text(request,"envelope",87720),GroupCiphertextFanout.MaxEnvelope));break;
    case "cache_announcement":reply["announcement"]=Announcement(admission.Announcement(account,device,current,Id(request,"id")));break;
    case "cache_announcement_ack":reply["newAnnouncementReceipt"]=admission.AcknowledgeAnnouncement(account,device,current,Id(request,"id"),Text(request,"digest",64));break;
    case "cache_recipient_keys":reply["recipients"]=admission.RecipientKeys(account,device,current,Id(request,"id"));reply["dataEncoding"]="base64url";break;
    case "cache_publish_key":reply["newKeyEnvelope"]=admission.PublishKey(account,device,current,Id(request,"id"),Decode(Text(request,"envelope",10923),AttachmentKeyEnvelope.MaximumWire));break;
    case "cache_key":reply["keyEnvelope"]=Key(admission.Key(account,device,current,Id(request,"id")));break;
    case "cache_key_ack":reply["newKeyReceipt"]=admission.AcknowledgeKey(account,device,current,Id(request,"id"),Text(request,"digest",64));break;
    case "cache_pending_keys":{
     string cursor;bool more;var page=admission.PendingKeys(account,device,current,Text(request,"after",32),out cursor,out more);var items=new List<Dictionary<string,object>>();foreach(var item in page)items.Add(Key(item));reply["items"]=items;reply["cursor"]=cursor;reply["more"]=more;break;
    }
    case "cache_pending":{
     var page=admission.PendingAnnouncements(account,device,current,Text(request,"after",32));var items=new List<Dictionary<string,object>>();foreach(var item in page.Items)items.Add(Announcement(item));reply["items"]=items;reply["cursor"]=page.Cursor;reply["more"]=page.More;break;
    }
    default:throw new InvalidOperationException("cache_unsupported_operation");
   }
   reply["state"]="accepted";
  }catch(Exception error){
   if(error is OutOfMemoryException)throw;
   // Never expose arbitrary SQL, filesystem, provider or request text.
   reply["state"]=error is InvalidOperationException&&Regex.IsMatch(error.Message??"","^cache_[a-z_]{1,80}$")?error.Message:
    error is FormatException?"cache_malformed_request":"cache_storage_or_validation_unavailable";
  }
  return reply;
 }
}
}
