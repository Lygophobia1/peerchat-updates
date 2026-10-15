using System;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using PeerChat.ServerShared;

namespace PeerChat.Server {
// Read-only storage binding probe. This is NOT permission to write through a
// pathname: the eventual cache adapter must anchor and revalidate open handles.
internal sealed class CacheStorageEvidence {
 public string Directory,MountPoint,Filesystem,InstanceId,MountGeneration;
 public long FilesystemBytes,AvailableBytes;
}
internal static class AttachmentCacheStorage {
 internal const string Marker=".peerchat-attachment-cache-instance";
 static readonly StringComparison PathComparison=Path.DirectorySeparatorChar=='\\'?StringComparison.OrdinalIgnoreCase:StringComparison.Ordinal;
 static string Canonical(string path){var full=Path.GetFullPath(path);var root=Path.GetPathRoot(full);return full.Length>root.Length?full.TrimEnd(Path.DirectorySeparatorChar,Path.AltDirectorySeparatorChar):root;}
 static bool Within(string directory,string mount){return String.Equals(directory,mount,PathComparison)||directory.StartsWith(mount.EndsWith(Path.DirectorySeparatorChar.ToString())?mount:mount+Path.DirectorySeparatorChar,PathComparison);}
 static string ReadBounded(string path,int maximum){using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite|FileShare.Delete)){
  using(var contents=new MemoryStream()){var buffer=new byte[Math.Min(4096,maximum+1)];int count;while((count=file.Read(buffer,0,buffer.Length))>0){if(count>maximum-contents.Length)throw new InvalidOperationException("Cache storage evidence exceeds bound.");contents.Write(buffer,0,count);}return new UTF8Encoding(false,true).GetString(contents.ToArray());}
 }}
 static void NoLinks(string directory){for(var path=directory;!String.IsNullOrEmpty(path);path=Path.GetDirectoryName(path))if((File.GetAttributes(path)&FileAttributes.ReparsePoint)!=0)throw new InvalidOperationException("Cache storage path contains a link.");}
 static string UnescapeMount(string value){return value.Replace("\\040"," ").Replace("\\011","\t").Replace("\\012","\n").Replace("\\134","\\");}
 // Linux mountinfo is kernel evidence, never a guessed drive/card capacity.
 internal static string[] LinuxMount(string directory,string mountInfo){
  string[] best=null;foreach(var line in mountInfo.Split('\n')){
   var halves=line.Split(new[]{" - "},StringSplitOptions.None);if(halves.Length!=2)continue;
   var left=halves[0].Split(' ');var right=halves[1].Split(' ');if(left.Length<6||right.Length<2)continue;
   var mount=UnescapeMount(left[4]);if(!mount.StartsWith("/",StringComparison.Ordinal)||!(directory==mount||directory.StartsWith(mount=="/"?mount:mount+"/",StringComparison.Ordinal)))continue;
   if(best==null||mount.Length>best[0].Length)best=new[]{mount,UnescapeMount(right[1]),left[0]+":"+left[2]+":"+left[3],right[0]};
  }
  if(best==null)throw new InvalidOperationException("Cache mount is unavailable.");return best;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetVolumePathName(string path,StringBuilder mount,uint length);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetVolumeNameForVolumeMountPoint(string mount,StringBuilder name,uint length);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetDiskFreeSpaceEx(string path,out ulong available,out ulong total,out ulong free);
 internal static CacheStorageEvidence Observe(string directory){
  if(String.IsNullOrEmpty(directory)||!Path.IsPathRooted(directory))throw new InvalidOperationException("Cache directory is not configured.");
  directory=Canonical(directory);if(!System.IO.Directory.Exists(directory))throw new InvalidOperationException("Cache directory is unavailable.");NoLinks(directory);
  var marker=Path.Combine(directory,Marker);if((File.GetAttributes(marker)&FileAttributes.ReparsePoint)!=0)throw new InvalidOperationException("Cache marker is a link.");
  string instance=ReadBounded(marker,64).Trim();if(instance.Length!=32||instance.Any(c=>!((c>='0'&&c<='9')||(c>='a'&&c<='f'))))throw new InvalidOperationException("Cache instance marker is invalid.");
  var result=new CacheStorageEvidence{Directory=directory,InstanceId=instance};
#if PEERCHAT_PORTABLE
  if(!OperatingSystem.IsWindows()){
   var mount=LinuxMount(directory,ReadBounded("/proc/self/mountinfo",4*1024*1024));string uuid=null,source=mount[1];
   if(File.Exists(source)){var sourceLink=File.ResolveLinkTarget(source,true);if(sourceLink!=null)source=sourceLink.FullName;}
   if(System.IO.Directory.Exists("/dev/disk/by-uuid"))foreach(var link in System.IO.Directory.EnumerateFiles("/dev/disk/by-uuid").Take(4097)){
    var target=File.ResolveLinkTarget(link,true);if(target!=null&&String.Equals(target.FullName,source,StringComparison.Ordinal)){uuid=Path.GetFileName(link);break;}
   }
   if(String.IsNullOrEmpty(uuid))throw new InvalidOperationException("Cache filesystem identity is unavailable.");
   var drive=DriveInfo.GetDrives().SingleOrDefault(x=>String.Equals(Canonical(x.Name),Canonical(mount[0]),StringComparison.Ordinal));if(drive==null||!drive.IsReady)throw new InvalidOperationException("Cache filesystem statistics are unavailable.");
   result.MountPoint=Canonical(mount[0]);result.Filesystem="UUID="+uuid;result.MountGeneration=mount[2];result.FilesystemBytes=drive.TotalSize;result.AvailableBytes=drive.AvailableFreeSpace;return result;
  }
#endif
  var volume=new StringBuilder(1024);var identity=new StringBuilder(1024);ulong available,total,free;
  if(!GetVolumePathName(directory,volume,1024)||!GetVolumeNameForVolumeMountPoint(volume.ToString(),identity,1024)||!GetDiskFreeSpaceEx(directory,out available,out total,out free)||available>(ulong)Int64.MaxValue||total>(ulong)Int64.MaxValue)throw new InvalidOperationException("Cache filesystem statistics are unavailable.");
  result.MountPoint=Canonical(volume.ToString());result.Filesystem=identity.ToString();result.MountGeneration=result.Filesystem;result.FilesystemBytes=(long)total;result.AvailableBytes=(long)available;return result;
 }
 internal static string Rejection(AttachmentCacheConfiguration options,CacheStorageEvidence observed){
  options.Validate();if(observed==null)return "cache_storage_unavailable";
  if(String.IsNullOrEmpty(options.Directory)||String.IsNullOrEmpty(options.ExpectedMountPoint)||String.IsNullOrEmpty(options.ExpectedFilesystem)||String.IsNullOrEmpty(options.InstanceId))return "cache_storage_not_bound";
  if(!String.Equals(Canonical(options.Directory),observed.Directory,PathComparison)||!String.Equals(Canonical(options.ExpectedMountPoint),observed.MountPoint,PathComparison)||!Within(observed.Directory,observed.MountPoint))return "cache_mount_mismatch";
  if(!String.Equals(options.ExpectedFilesystem,observed.Filesystem,StringComparison.Ordinal))return "cache_filesystem_mismatch";
  if(!String.Equals(options.InstanceId,observed.InstanceId,StringComparison.Ordinal))return "cache_instance_mismatch";
  if(observed.FilesystemBytes<=0||observed.AvailableBytes<0||observed.AvailableBytes>observed.FilesystemBytes)return "cache_filesystem_statistics_invalid";
  return "";
 }
 internal static bool TryInspect(AttachmentCacheConfiguration options,out CacheStorageEvidence observed,out string reason){
  observed=null;try{options.Validate();observed=Observe(options.Directory);reason=Rejection(options,observed);return reason.Length==0;}catch{reason="cache_storage_unavailable";return false;}
 }
}
}
