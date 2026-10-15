using System;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using PeerChat.ServerShared;

namespace PeerChat.Server {
// Pins this cache's storage, not merely its pathname. No automatic initialization
// or migration. Linux uses an open directory + proc-fd path; Windows uses the
// volume GUID path and denies directory deletion/rename while it is owned.
internal sealed class AttachmentCacheDirectory : IDisposable {
 readonly AttachmentCacheConfiguration options;
 readonly CacheStorageEvidence original;
 IntPtr unixDirectory=IntPtr.Zero;
 SafeFileHandle windowsDirectory;
 FileStream ownerFile;
 internal readonly string Path;
 internal readonly long AllocationUnit;
 [DllImport("libc",SetLastError=true)] static extern IntPtr opendir(string name);
 [DllImport("libc",SetLastError=true)] static extern int dirfd(IntPtr directory);
 [DllImport("libc",SetLastError=true)] static extern int closedir(IntPtr directory);
 [DllImport("libc",SetLastError=true)] static extern int flock(int fd,int operation);
 [DllImport("libc",SetLastError=true)] static extern int fstatvfs64(int fd,IntPtr buffer);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr attributes,uint disposition,uint flags,IntPtr template);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetDiskFreeSpace(string root,out uint sectors,out uint bytes,out uint free,out uint total);
 static void Require(bool value){if(!value)throw new InvalidOperationException("Encrypted cache storage ownership unavailable.");}
 static string MountId(int fd){var lines=File.ReadAllLines("/proc/self/fdinfo/"+fd.ToString(CultureInfo.InvariantCulture));Require(lines.Length<128);var entry=lines.SingleOrDefault(x=>x.StartsWith("mnt_id:",StringComparison.Ordinal));Require(entry!=null);return entry.Substring(7).Trim();}
 internal AttachmentCacheDirectory(AttachmentCacheConfiguration configuration){
  options=configuration;string reason;CacheStorageEvidence evidence;
  Require(AttachmentCacheStorage.TryInspect(options,out evidence,out reason));original=evidence;
  try{
#if PEERCHAT_PORTABLE
   if(!OperatingSystem.IsWindows()){
    var mode=File.GetUnixFileMode(options.Directory);Require((mode&(UnixFileMode.GroupWrite|UnixFileMode.OtherWrite|UnixFileMode.OtherRead|UnixFileMode.OtherExecute))==0);
    unixDirectory=opendir(options.Directory);Require(unixDirectory!=IntPtr.Zero);int fd=dirfd(unixDirectory);Require(fd>=0&&flock(fd,6)==0); // LOCK_EX | LOCK_NB
    Require(MountId(fd)==original.MountGeneration.Split(':')[0]);
    Path="/proc/self/fd/"+fd.ToString(CultureInfo.InvariantCulture);
    var target=File.ResolveLinkTarget(Path,true);Require(target!=null&&String.Equals(target.FullName,original.Directory,StringComparison.Ordinal));
    var buffer=Marshal.AllocHGlobal(1024);try{Require(fstatvfs64(fd,buffer)==0);AllocationUnit=IntPtr.Size==8?Marshal.ReadInt64(buffer,8):(long)(uint)Marshal.ReadInt32(buffer,4);}finally{Marshal.FreeHGlobal(buffer);}
   }else
#endif
   {
    windowsDirectory=CreateFile(original.Directory,0,3,IntPtr.Zero,3,0x02000000,IntPtr.Zero);Require(windowsDirectory!=null&&!windowsDirectory.IsInvalid);
    Require(original.Filesystem.StartsWith("\\\\?\\Volume{",StringComparison.Ordinal));
    Path=original.Filesystem+original.Directory.Substring(original.MountPoint.Length).TrimStart('\\','/');
    string owner=System.IO.Path.Combine(Path,"cache.owner");Require(!File.Exists(owner)||(File.GetAttributes(owner)&FileAttributes.ReparsePoint)==0);ownerFile=new FileStream(owner,FileMode.OpenOrCreate,FileAccess.ReadWrite,FileShare.None);
    uint sectors,bytes,free,total;Require(GetDiskFreeSpace(original.Filesystem,out sectors,out bytes,out free,out total));AllocationUnit=checked((long)sectors*bytes);
   }
   Require(AllocationUnit>0&&AllocationUnit<=16L*1024*1024);Check();
  }catch{Dispose();throw;}
 }
 internal void Check(){
  CacheStorageEvidence evidence;string reason;Require(AttachmentCacheStorage.TryInspect(options,out evidence,out reason));
  Require(evidence.MountGeneration==original.MountGeneration&&evidence.Filesystem==original.Filesystem);
  var marker=System.IO.Path.Combine(Path,AttachmentCacheStorage.Marker);Require((File.GetAttributes(marker)&FileAttributes.ReparsePoint)==0&&new FileInfo(marker).Length<=64&&File.ReadAllText(marker).Trim()==options.InstanceId);
  if(unixDirectory!=IntPtr.Zero)Require(MountId(dirfd(unixDirectory))==original.MountGeneration.Split(':')[0]);
 }
 internal CacheStorageEvidence Evidence(){Check();return AttachmentCacheStorage.Observe(options.Directory);}
 internal long AllocatedBytes(){
  Check();long total=checked(AllocationUnit*4);int count=0;
  foreach(var file in Directory.EnumerateFileSystemEntries(Path)){
   Require(++count<=8);string name=System.IO.Path.GetFileName(file);Require(name==AttachmentCacheStorage.Marker||name=="cache.sqlite"||name=="cache.sqlite-journal"||name=="cache.owner");
   Require((File.GetAttributes(file)&(FileAttributes.ReparsePoint|FileAttributes.Directory))==0);
   long size=new FileInfo(file).Length;total=checked(total+checked((size/AllocationUnit+(size%AllocationUnit==0?0:1))*AllocationUnit));
  }return total;
 }
 public void Dispose(){if(ownerFile!=null){ownerFile.Dispose();ownerFile=null;}if(windowsDirectory!=null){windowsDirectory.Dispose();windowsDirectory=null;}if(unixDirectory!=IntPtr.Zero){closedir(unixDirectory);unixDirectory=IntPtr.Zero;}}
}
}
