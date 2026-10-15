using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text.RegularExpressions;

namespace PeerChat.Server {
// Bounded storage RPC adapter only. TransferManager remains the client chunk,
// retry, hash, ACK and journal owner. No sockets, timers or retry queues here.
// Production dispatch remains closed behind the server readiness gate.
internal sealed class AttachmentCacheRequests {
 readonly AttachmentCacheAdmission admission;
 internal AttachmentCacheRequests(AttachmentCacheAdmission value){if(value==null)throw new ArgumentNullException("value");admission=value;}
 static void Require(bool valid){if(!valid)throw new InvalidOperationException("cache_malformed_request");}
 static string Text(Dictionary<string,object> request,string name,int maximum){object value;Require(request.TryGetValue(name,out value)&&value is string);string result=(string)value;Require(result.Length<=maximum);return result;}
 static string Id(Dictionary<string,object> request,string name){string value=Text(request,name,32);Require(MailboxProtocol.Id(value,32));return value;}
 static long Number(Dictionary<string,object> request,string name){object value;Require(request.TryGetValue(name,out value)&&value!=null);string text=Convert.ToString(value,CultureInfo.InvariantCulture);long result;Require(text.Length<=19&&Regex.IsMatch(text,"^[0-9]+$")&&Int64.TryParse(text,NumberStyles.None,CultureInfo.InvariantCulture,out result));return Int64.Parse(text,CultureInfo.InvariantCulture);}
 static bool Flag(Dictionary<string,object> request,string name){object value;Require(request.TryGetValue(name,out value)&&value is bool);return (bool)value;}
 // Canonical base64url prevents JSON HTML escaping of '+' from expanding a
 // valid 64 KiB chunk beyond the existing 100000-byte RPC frame bound.
 internal static string Encode(byte[] bytes){return Convert.ToBase64String(bytes).TrimEnd('=').Replace('+','-').Replace('/','_');}
 internal static byte[] Decode(string text){Require(text.Length<=87382&&text.Length%4!=1&&Regex.IsMatch(text,"^[A-Za-z0-9_-]*$"));var bytes=Convert.FromBase64String(text.Replace('-','+').Replace('_','/')+new string('=',(4-text.Length%4)%4));Require(bytes.Length<=65536&&Encode(bytes)==text);return bytes;}
 static void Status(Dictionary<string,object> reply,AttachmentCacheRuntime.Status value){reply["ciphertextSize"]=value.Size;reply["durableOffset"]=value.Offset;reply["ciphertextComplete"]=value.Complete;reply["expiresUtc"]=value.Expires;}
 internal Dictionary<string,object> Execute(long account,string device,Func<bool> current,Dictionary<string,object> request){
  var reply=new Dictionary<string,object>{{"state","cache_malformed_request"},{"cacheVersion",1}};
  try{
   Require(request!=null&&current!=null&&account>0&&MailboxProtocol.Id(device,64));
   string operation=Text(request,"type",32),correlation=Id(request,"requestId");reply["type"]="cache_result";reply["operation"]=operation;reply["requestId"]=correlation;
   switch(operation){
    case "cache_admit":Status(reply,admission.Admit(account,device,current,MailboxProtocol.Decode(Text(request,"descriptor",2732),2048)));break;
    case "cache_status":Status(reply,admission.Inspect(account,device,current,Id(request,"id")));break;
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
