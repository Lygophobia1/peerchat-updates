using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace PeerChat.Server {
sealed partial class DiscoveryServer {
 readonly object attachmentCacheGate=new object();
 AttachmentCacheRuntime attachmentCache;
 AttachmentCacheRequests attachmentCacheRequests;
 long attachmentCacheCleanupAt;
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
   reply=attachmentCacheRequests.Execute(account,device,current,request);
   MaintainAttachmentCache();
  }
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
