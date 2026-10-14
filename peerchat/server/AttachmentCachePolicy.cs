using System;
using System.Collections.Generic;
using System.Linq;
using PeerChat.ServerShared;
namespace PeerChat.Server {
// Pure admission/eviction policy, not a transport, filesystem or transfer owner.
// No production endpoint is enabled by this class.
internal sealed class AttachmentCacheOptions : AttachmentCacheConfiguration {}
internal sealed class CacheBudgetObject {
 public string Id;
 public long AllocatedBytes,ReservedBytes,CompletedAt,ExpiresAt;
 public bool Complete,ProtectedLease;
}
internal sealed class CacheBudgetSnapshot {
 public bool StorageAvailable;
 // Objects is a bounded eviction-candidate page, not the complete object index.
 // Other* MUST include every charge outside this page (metadata and reservations
 // included). The transactional ledger advances globally ordered pages under
 // its admission gate; this bound must never become a total cache-object limit.
 public long FilesystemBytes,AvailableBytes,OtherAllocatedBytes,OtherReservedBytes;
 public IList<CacheBudgetObject> Objects=new List<CacheBudgetObject>();
}
internal sealed class CacheAdmissionPlan {
 public bool Allowed;public string Reason;
 public long EffectiveQuotaBytes,ChargedBytes,ReserveBytes;
 public readonly List<string> Evict=new List<string>();
}
internal static class AttachmentCacheBudget {
 static long Add(long a,long b){if(a<0||b<0||a>long.MaxValue-b)throw new InvalidOperationException("Cache accounting overflow.");return a+b;}
 static long Sum(IEnumerable<long> values){long result=0;foreach(var value in values)result=Add(result,value);return result;}
 // Caller must hold the SAME reservation/commit gate while applying this plan
 // and persisting the new reservation. A plan alone never authorizes an upload.
 public static CacheAdmissionPlan Plan(AttachmentCacheConfiguration options,CacheBudgetSnapshot snapshot,long newReservation,long now,bool cleanupExpired=false){
  options.Validate();var plan=new CacheAdmissionPlan{Reason="storage_unavailable"};
  if(!snapshot.StorageAvailable)return plan;
  if(newReservation<0||now<0||snapshot.FilesystemBytes<=0||snapshot.AvailableBytes<0||snapshot.AvailableBytes>snapshot.FilesystemBytes||snapshot.Objects.Count>4096)throw new InvalidOperationException("Invalid cache accounting snapshot.");
  var ids=new HashSet<string>(StringComparer.Ordinal);foreach(var item in snapshot.Objects)if(String.IsNullOrEmpty(item.Id)||!ids.Add(item.Id)||item.AllocatedBytes<0||item.ReservedBytes<0||item.CompletedAt<0||item.ExpiresAt<0)throw new InvalidOperationException("Invalid cache object accounting.");
  long allocated=Add(snapshot.OtherAllocatedBytes,Sum(snapshot.Objects.Select(x=>x.AllocatedBytes)));
  if(allocated>snapshot.FilesystemBytes-snapshot.AvailableBytes)throw new InvalidOperationException("Cache allocated space exceeds filesystem usage.");
  long reserved=Add(snapshot.OtherReservedBytes,Sum(snapshot.Objects.Select(x=>x.ReservedBytes)));
  long percent=snapshot.FilesystemBytes/100*options.MinimumFreePercent+(snapshot.FilesystemBytes%100*options.MinimumFreePercent+99)/100;
  plan.ReserveBytes=Math.Max(options.MinimumFreeBytes,percent);
  long headroom=Math.Max(0,snapshot.AvailableBytes-plan.ReserveBytes);
  plan.EffectiveQuotaBytes=Math.Min(options.MaximumBytes,Add(allocated,headroom));
  long charged=Add(allocated,reserved),free=snapshot.AvailableBytes;
  Func<bool> fits=()=>charged<=plan.EffectiveQuotaBytes&&newReservation<=plan.EffectiveQuotaBytes-charged&&free>=plan.ReserveBytes&&reserved<=free-plan.ReserveBytes&&newReservation<=free-plan.ReserveBytes-reserved;
  // Expired first, then oldest eligible completed; no active lease eviction.
  foreach(var item in snapshot.Objects.Where(x=>!x.ProtectedLease&&(x.ExpiresAt<=now||x.Complete)).OrderBy(x=>x.ExpiresAt<=now?0:1).ThenBy(x=>x.Complete?x.CompletedAt:x.ExpiresAt).ThenBy(x=>x.Id,StringComparer.Ordinal)){
   if(fits()&&(!cleanupExpired||item.ExpiresAt>now))break;charged-=Add(item.AllocatedBytes,item.ReservedBytes);reserved-=item.ReservedBytes;free=Add(free,item.AllocatedBytes);plan.Evict.Add(item.Id);
  }
  plan.ChargedBytes=charged;
  if(fits()){plan.Allowed=true;plan.Reason="admitted";}else{plan.Reason="quota_or_free_space_protected";}
  return plan;
 }
}
}
