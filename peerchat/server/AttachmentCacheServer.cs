using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace PeerChat.Server {
sealed partial class DiscoveryServer {
 readonly object attachmentCacheGate=new object();
 AttachmentCacheRuntime attachmentCache;
 AttachmentCacheRequests attachmentCacheRequests;
 long attachmentCacheCleanupAt;
 partial void ProcessAttachmentCacheBatch(long account,string device,Func<bool> current,IList<Dictionary<string,object>> requests,ref List<Dictionary<string,object>> replies,ref long[] components){
  using(var appendTiming=new CacheAppendTimings())try{lock(attachmentCacheGate){
   if(!config.AttachmentCache.Enabled||!AttachmentCacheTransportReady||!DirectCacheTransportReady)return;
   if(attachmentCache==null){
    try{var opened=new AttachmentCacheRuntime(config.AttachmentCache,()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds());try{attachmentCacheRequests=new AttachmentCacheRequests(new AttachmentCacheAdmission(accounts,opened),DirectCacheTransportReady);attachmentCache=opened;}catch{opened.Dispose();throw;}}
    catch(Exception error){if(error is OutOfMemoryException)throw;return;}
   }
   replies=attachmentCacheRequests.ExecuteDirectAppendBatch(account,device,current,requests);
   MaintainAttachmentCache();
  }}finally{components=appendTiming.Snapshot();}
 }
 // Ordinary shipping builds stay closed. The explicitly built candidate is
 // usable only with administrator-enabled, ownership-checked cache storage.
 // It includes no PEERCHAT_CACHE_TESTS fault injection.
#if PEERCHAT_CACHE_TESTS || PEERCHAT_CACHE_CANDIDATE
 const bool AttachmentCacheTransportReady=true;
#else
 const bool AttachmentCacheTransportReady=false;
#endif
#if PEERCHAT_CACHE_TESTS || PEERCHAT_DM_CACHE_CANDIDATE
 const bool DirectCacheTransportReady=true;
#else
 const bool DirectCacheTransportReady=false;
#endif
 partial void ProcessAttachmentCacheRequest(long account,string device,Func<bool> current,Dictionary<string,object> request,ref Dictionary<string,object> reply){
  lock(attachmentCacheGate){
   reply=new Dictionary<string,object>{{"state",config.AttachmentCache.Enabled?"cache_feature_gated":"cache_disabled"}};
   if(!config.AttachmentCache.Enabled||!AttachmentCacheTransportReady)return;
   if(attachmentCache==null){
    try{
     var opened=new AttachmentCacheRuntime(config.AttachmentCache,()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds());
     try{attachmentCacheRequests=new AttachmentCacheRequests(new AttachmentCacheAdmission(accounts,opened),DirectCacheTransportReady);attachmentCache=opened;}
     catch{opened.Dispose();throw;}
    }catch(Exception error){if(error is OutOfMemoryException)throw;reply["state"]="cache_storage_unavailable";return;}
   }
   string operation=Text(request,"type");
   using(var appendTiming=operation=="cache_append"||operation=="cache_dm_append"?new CacheAppendTimings():null){
    long started=appendTiming==null?0:Stopwatch.GetTimestamp();
    try{reply=attachmentCacheRequests.Execute(account,device,current,request);MaintainAttachmentCache();}
    finally{if(appendTiming!=null)RecordTiming("cache_append_components",operation,Text(request,"rpcRequestId"),started,appendTiming.Snapshot());}
   }
  }
  // Dispatch outside the storage gate and only after authenticated publication
  // committed. Rejected requests cannot wake or acknowledge a recipient.
  object stored;
  if(Text(request,"type")=="cache_dm_publish"&&Text(reply,"state")=="accepted"&&
     reply.TryGetValue("newNoticeStored",out stored)&&stored is bool&&(bool)stored)
   NotifyStoredMailbox(AttachmentCacheRequests.Decode(Text(request,"envelope")));
 }
 partial void MaintainAttachmentCache(){
  lock(attachmentCacheGate){
   long now=Stopwatch.GetTimestamp();if(attachmentCache==null||now<attachmentCacheCleanupAt)return;
   attachmentCacheCleanupAt=now+60*Stopwatch.Frequency;
   try{attachmentCache.Cleanup(16);}
   catch(Exception error){if(error is OutOfMemoryException)throw;Log("WARNING","Encrypted cache cleanup unavailable; no storage fallback.");}
  }
 }
 partial void CloseAttachmentCache(){lock(attachmentCacheGate){if(attachmentCache!=null)attachmentCache.Dispose();attachmentCache=null;attachmentCacheRequests=null;}}
}
}
