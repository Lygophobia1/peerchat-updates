using System;
using System.Collections.Generic;
using System.IO;
using PeerChat.Server;
using PeerChat.ServerShared;
class CachePolicyTest {
 static void Check(bool b,string s){if(!b)throw new Exception(s);}
 static CacheBudgetSnapshot Snapshot(){return new CacheBudgetSnapshot{StorageAvailable=true,FilesystemBytes=100L<<30,AvailableBytes=80L<<30};}
 static int Main(string[] args){try{
  var o=new AttachmentCacheOptions();var s=Snapshot();var p=AttachmentCacheBudget.Plan(o,s,1L<<30,100);Check(p.Allowed&&p.EffectiveQuotaBytes==10L<<30,"Default capacity");
  o.MaximumBytes=100L<<30;s.FilesystemBytes=1000L<<30;s.AvailableBytes=800L<<30;p=AttachmentCacheBudget.Plan(o,s,40L<<30,100);Check(p.Allowed&&p.EffectiveQuotaBytes==100L<<30,"64-bit large capacity");
  o.MaximumBytes=10L<<30;s=Snapshot();s.Objects=new List<CacheBudgetObject>{new CacheBudgetObject{Id="leased",AllocatedBytes=4L<<30,Complete=true,CompletedAt=1,ExpiresAt=2,ProtectedLease=true},new CacheBudgetObject{Id="newer-expired",AllocatedBytes=2L<<30,Complete=true,CompletedAt=20,ExpiresAt=30},new CacheBudgetObject{Id="old-unexpired",AllocatedBytes=2L<<30,Complete=true,CompletedAt=10,ExpiresAt=200},new CacheBudgetObject{Id="partial",AllocatedBytes=1L<<30,ReservedBytes=1L<<30,ExpiresAt=200}};
  p=AttachmentCacheBudget.Plan(o,s,3L<<30,100);Check(p.Allowed&&p.Evict.Count==2&&p.Evict[0]=="newer-expired"&&p.Evict[1]=="old-unexpired","Expiry and oldest ordering / leases");
  o.MaximumBytes=3L<<30;p=AttachmentCacheBudget.Plan(o,s,0,100);Check(!p.Allowed&&!p.Evict.Contains("leased")&&!p.Evict.Contains("partial"),"Quota reduction revoked protected work");
  s=Snapshot();s.AvailableBytes=5L<<30;p=AttachmentCacheBudget.Plan(o,s,1,100);Check(!p.Allowed&&p.EffectiveQuotaBytes==0,"Free reserve");
  s.StorageAvailable=false;Check(!AttachmentCacheBudget.Plan(o,s,1,100).Allowed,"Missing mount admitted");
  s=Snapshot();s.OtherAllocatedBytes=long.MaxValue;s.OtherReservedBytes=1;bool rejected=false;try{AttachmentCacheBudget.Plan(o,s,1,100);}catch(InvalidOperationException){rejected=true;}Check(rejected,"Overflow admitted");
  o=new AttachmentCacheOptions{MaximumBytes=8L<<40};s=Snapshot();s.FilesystemBytes=100L<<40;s.AvailableBytes=80L<<40;s.OtherAllocatedBytes=3L<<40;Check(AttachmentCacheBudget.Plan(o,s,4L<<40,100).Allowed,"Multi-terabyte capacity narrowed to 32 bits");Check(!AttachmentCacheBudget.Plan(o,s,6L<<40,100).Allowed,"Unpaged multi-terabyte charges ignored");
  o=new AttachmentCacheOptions();s=Snapshot();s.Objects.Add(new CacheBudgetObject{Id="expired",AllocatedBytes=1024,Complete=true,CompletedAt=1,ExpiresAt=2});s.Objects.Add(new CacheBudgetObject{Id="retained",AllocatedBytes=2048,Complete=true,CompletedAt=1,ExpiresAt=200});
  p=AttachmentCacheBudget.Plan(o,s,0,100,true);Check(p.Allowed&&p.Evict.Count==1&&p.Evict[0]=="expired","Periodic expiry when under quota");
  s=Snapshot();s.OtherReservedBytes=9L<<30;Check(!AttachmentCacheBudget.Plan(o,s,2L<<30,100).Allowed,"Concurrent reservations oversubscribed quota");
  string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);string configPath=Path.Combine(root,"config.json"),cachePath=Path.Combine(root,"cache-not-created");
  var config=new ServerConfiguration{DataDirectory=Path.Combine(root,"data"),AttachmentCache=new AttachmentCacheConfiguration{MaximumBytes=100L<<30,RetentionSeconds=90L*24*60*60,MinimumFreeBytes=3L<<30,MinimumFreePercent=25,Directory=cachePath,ExpectedFilesystem="test-volume-identity",ExpectedMountPoint=Path.GetPathRoot(cachePath),InstanceId=new string('a',32)}};
  config.Save(configPath);var loaded=ServerConfiguration.Load(configPath);Check(loaded.AttachmentCache.MaximumBytes==100L<<30&&loaded.AttachmentCache.RetentionSeconds==90L*24*60*60&&loaded.AttachmentCache.MinimumFreePercent==25&&loaded.AttachmentCache.Directory==cachePath,"Configuration round trip");
  loaded.AttachmentCache.MaximumBytes=1L<<30;loaded.Save(configPath);Check(ServerConfiguration.Load(configPath).AttachmentCache.MaximumBytes==1L<<30&&!Directory.Exists(cachePath),"Quota change performed storage mutation");
  File.WriteAllText(configPath,"{\"DataDirectory\":\""+root.Replace("\\","\\\\")+"\"}");Check(ServerConfiguration.Load(configPath).AttachmentCache.MaximumBytes==10L<<30,"Legacy config default");
  File.WriteAllText(configPath,"{\"AttachmentCache\":null}");rejected=false;try{ServerConfiguration.Load(configPath);}catch(InvalidOperationException){rejected=true;}Check(rejected,"Null configuration silently reset");
  CacheStorageEvidence evidence;string reason;Check(!AttachmentCacheStorage.TryInspect(config.AttachmentCache,out evidence,out reason)&&!Directory.Exists(cachePath),"Storage probe created fallback directory");
  Directory.CreateDirectory(cachePath);Check(!AttachmentCacheStorage.TryInspect(config.AttachmentCache,out evidence,out reason),"Uninitialized cache admitted");
  File.WriteAllText(Path.Combine(cachePath,AttachmentCacheStorage.Marker),new string('a',32));evidence=AttachmentCacheStorage.Observe(cachePath);
  var bound=new AttachmentCacheConfiguration{Directory=evidence.Directory,ExpectedMountPoint=evidence.MountPoint,ExpectedFilesystem=evidence.Filesystem,InstanceId=evidence.InstanceId};
  Check(AttachmentCacheStorage.TryInspect(bound,out evidence,out reason)&&evidence.FilesystemBytes>0,"Actual Windows volume probe failed");
  string actualMount=bound.ExpectedMountPoint;bound.ExpectedMountPoint=cachePath;Check(!AttachmentCacheStorage.TryInspect(bound,out evidence,out reason)&&reason=="cache_mount_mismatch","Unmounted mountpoint fell back to parent volume");bound.ExpectedMountPoint=actualMount;
  bound.ExpectedFilesystem="different-mounted-storage";Check(!AttachmentCacheStorage.TryInspect(bound,out evidence,out reason)&&reason=="cache_filesystem_mismatch","Different storage admitted");bound.ExpectedFilesystem=evidence.Filesystem;
  bound.InstanceId=new string('b',32);Check(!AttachmentCacheStorage.TryInspect(bound,out evidence,out reason)&&reason=="cache_instance_mismatch","Different cache instance admitted");
  var mount=AttachmentCacheStorage.LinuxMount("/mnt/usb/cache","1 0 8:1 / / rw - ext4 /dev/root rw\n2 1 8:2 / /mnt/usb rw - ext4 /dev/sda1 rw\n3 1 8:3 / /mnt/usbcache rw - ext4 /dev/sdb1 rw");Check(mount[0]=="/mnt/usb"&&mount[1]=="/dev/sda1","Longest correct mount selection");
  mount=AttachmentCacheStorage.LinuxMount("/mnt/usb/cache","1 0 8:1 / / rw - ext4 /dev/root rw");Check(mount[0]=="/","Unmount was not distinguishable from configured mount");
  mount=AttachmentCacheStorage.LinuxMount("/mnt/usb drive/cache","1 0 8:1 / / rw - ext4 /dev/root rw\n2 1 8:2 / /mnt/usb\\040drive rw - ext4 /dev/sda1 rw");Check(mount[0]=="/mnt/usb drive","Escaped mount path");
  foreach(var path in new[]{"relative-cache","C:drive-relative-cache","\\root-relative-cache"}){rejected=false;try{new AttachmentCacheConfiguration{Directory=path}.Validate();}catch(InvalidOperationException){rejected=true;}Check(rejected,"Nonabsolute cache configuration admitted");}
  Console.WriteLine("PASS cache policy/configuration/storage: configurable defaults, 100-GiB and multi-terabyte 64-bit accounting, lease/quota reduction, expiry/reservations, restart round trip, read-only actual volume binding, missing mount/marker, changed identity, mount parsing and no automatic creation/migration");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
