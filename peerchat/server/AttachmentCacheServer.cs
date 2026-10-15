using System;
using System.Collections.Generic;
using System.Diagnostics;

namespace PeerChat.Server {
sealed partial class DiscoveryServer {
 readonly object attachmentCacheGate=new object();
 AttachmentCacheRuntime attachmentCache;
 AttachmentCacheRequests attachmentCacheRequests;
 long attachmentCacheCleanupAt;
 // Keep shipping builds closed until client TransferManager and complete
 // encrypted-manifest/offline-delivery validation pass. Test builds exercise
 // the identical authenticated RPC/storage path against disposable fixtures.
#if PEERCHAT_CACHE_TESTS
 const bool AttachmentCacheTransportReady=true;
#else
 const bool AttachmentCacheTransportReady=false;
#endif
 partial void ProcessAttachmentCacheRequest(long account,string device,Func<bool> current,Dictionary<string,object> request,ref Dictionary<string,object> reply){
  lock(attachmentCacheGate){
   reply=new Dictionary<string,object>{{"state",config.AttachmentCache.Enabled?"cache_feature_gated":"cache_disabled"}};
   if(!config.AttachmentCache.Enabled||!AttachmentCacheTransportReady)return;
   if(attachmentCache==null){
    try{
     var opened=new AttachmentCacheRuntime(config.AttachmentCache,()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds());
     try{attachmentCacheRequests=new AttachmentCacheRequests(new AttachmentCacheAdmission(accounts,opened));attachmentCache=opened;}
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
