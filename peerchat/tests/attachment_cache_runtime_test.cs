#if PEERCHAT_PORTABLE
using ECDsaCng = PortableTestKey;
#endif
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;

class AttachmentCacheRuntimeTest {
 static long now=1000000;
 static void Check(bool b,string s){if(!b)throw new Exception(s);}
 static void Reject(Action a,string reason){try{a();throw new Exception("Unexpected success: "+reason);}catch(InvalidOperationException e){Check(e.Message==reason||reason=="*",e.Message+" != "+reason);}}
 static void Field(BinaryWriter w,byte[] value){w.Write((ulong)value.Length);w.Write(value);}
 static void Text(BinaryWriter w,string value){Field(w,Encoding.UTF8.GetBytes(value));}
 static AttachmentCacheDescriptor Descriptor(ECDsaCng key,int plainBytes,byte[] cipher,string id=null,string group=null,long epoch=1,long account=1){
  using(var m=new MemoryStream())using(var w=new BinaryWriter(m)){
   var publicKey=key.Key.Export(CngKeyBlobFormat.EccPublicBlob);w.Write(new byte[]{80,84,1,5});Text(w,"PeerChat/group-cache-upload/v1");Text(w,id??Guid.NewGuid().ToString("N"));Text(w,group??new string('1',32));Text(w,Guid.NewGuid().ToString("N"));Text(w,MailboxProtocol.Digest(publicKey));w.Write((ulong)account);w.Write((ulong)epoch);w.Write((ulong)plainBytes);w.Write((ulong)cipher.Length);using(var sha=SHA256.Create())Field(w,sha.ComputeHash(cipher));Field(w,publicKey);using(var sha=SHA256.Create())Field(w,key.SignHash(sha.ComputeHash(m.ToArray())));return AttachmentCacheDescriptor.Parse(m.ToArray());
  }
 }
 static byte[] Payload(int plain){int size=checked(20+plain+Math.Max(1,(plain+32767)/32768)*16);var bytes=new byte[size];new Random(plain).NextBytes(bytes);return bytes;}
 static AttachmentCacheConfiguration Options(string directory){var evidence=AttachmentCacheStorage.Observe(directory);return new AttachmentCacheConfiguration{MaximumBytes=64L*1024*1024,Directory=evidence.Directory,ExpectedMountPoint=evidence.MountPoint,ExpectedFilesystem=evidence.Filesystem,InstanceId=evidence.InstanceId,RetentionSeconds=10,PartialIdleSeconds=30};}
 static string Fixture(string root,string name){string path=Path.Combine(root,name);Directory.CreateDirectory(path);
#if PEERCHAT_PORTABLE
  if(!OperatingSystem.IsWindows())File.SetUnixFileMode(path,UnixFileMode.UserRead|UnixFileMode.UserWrite|UnixFileMode.UserExecute);
#endif
  File.WriteAllText(Path.Combine(path,AttachmentCacheStorage.Marker),new string('a',32));return path;}
 static void Upload(AttachmentCacheRuntime cache,AttachmentCacheDescriptor d,byte[] bytes,int chunk=32768){var lease=cache.Acquire(d.Id,d.Sender,true);long offset=cache.Inspect(d.Id).Offset;while(offset<bytes.Length){int n=(int)Math.Min(chunk,bytes.Length-offset);var block=bytes.Skip((int)offset).Take(n).ToArray();offset=cache.Append(lease,offset,block);}cache.Complete(lease);cache.Complete(lease);cache.Release(lease);}
 static List<CacheRecipient> Readers(){return new List<CacheRecipient>{new CacheRecipient(2,new string('b',64),new string('4',32)),new CacheRecipient(3,new string('c',64),new string('5',32))};}
 static void RoundTrips(string root){using(var signer=new ECDsaCng(256)){
  var path=Fixture(root,"roundtrips");var options=Options(path);
  foreach(int size in new[]{0,1,32768,1024*1024,3*1024*1024,10*1024*1024}){
   var bytes=Payload(size);var d=Descriptor(signer,size,bytes);string oldLease;
   using(var cache=new AttachmentCacheRuntime(options,()=>now)){
    Check(!cache.Admit(d,Readers()).Complete,"Admission claimed completion");oldLease=cache.Acquire(d.Id,d.Sender,true);
    int first=Math.Min(32768,bytes.Length);var block=bytes.Take(first).ToArray();Check(cache.Append(oldLease,0,block)==first,"First durable offset");Check(cache.Append(oldLease,0,block)==first,"Duplicate inflated progress");
    var changed=(byte[])block.Clone();changed[0]^=1;Reject(()=>cache.Append(oldLease,0,changed),"cache_conflicting_chunk");
    if(first<bytes.Length)Reject(()=>cache.Complete(oldLease),"cache_upload_incomplete");
    Reject(()=>cache.Acquire(d.Id,new string('d',64),false),"cache_original_recipient_required");
   }
   using(var cache=new AttachmentCacheRuntime(options,()=>now)){
    Reject(()=>cache.Append(oldLease,0,bytes.Take(Math.Min(32768,bytes.Length)).ToArray()),"cache_lease_expired");Upload(cache,d,bytes,65536);var state=cache.Inspect(d.Id);Check(state.Complete&&state.Offset==bytes.Length&&state.Expires==now+10,"Committed completion/restart");
    var first=cache.Acquire(d.Id,new string('b',64),false);var second=cache.Acquire(d.Id,new string('c',64),false);
    var result=new MemoryStream();long offset=0;while(offset<bytes.Length){var data=cache.Read(first,offset,65536);result.Write(data,0,data.Length);offset+=data.Length;cache.RecipientProgress(first,offset,false);}Check(result.ToArray().SequenceEqual(bytes),"Ciphertext read integrity");
    cache.RecipientProgress(first,bytes.Length,true);Check(cache.RecipientOffset(d.Id,new string('b',64))==bytes.Length&&cache.RecipientOffset(d.Id,new string('c',64))==0,"Independent recipients");
    cache.RecipientProgress(second,Math.Min(32768,bytes.Length),false);cache.Release(first);cache.Release(second);Check(cache.ChargedBytes<=options.MaximumBytes,"Quota oversubscribed");
   }
   using(var cache=new AttachmentCacheRuntime(options,()=>now)){Check(cache.Inspect(d.Id).Complete&&cache.RecipientOffset(d.Id,new string('b',64))==bytes.Length,"Completed restart persistence");}
   Console.WriteLine("PASS durable cache ciphertext size="+bytes.Length+" offsets/duplicate/restart/independent recipients/integrity");
  }
 }}
 static void CorruptionAndExpiry(string root){using(var signer=new ECDsaCng(256)){
  string path=Fixture(root,"expiry");var options=Options(path);var bytes=Payload(1024*1024);var d=Descriptor(signer,1024*1024,bytes);var other=Descriptor(signer,1024*1024,bytes);
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   cache.Admit(d,Readers());var corrupt=(byte[])bytes.Clone();corrupt[2]^=1;var lease=cache.Acquire(d.Id,d.Sender,true);long offset=0;while(offset<corrupt.Length){int n=(int)Math.Min(32768,corrupt.Length-offset);offset=cache.Append(lease,offset,corrupt.Skip((int)offset).Take(n).ToArray());}
   Reject(()=>cache.Complete(lease),"cache_ciphertext_digest_mismatch");Check(!cache.Inspect(d.Id).Complete,"Corrupt upload reported complete");cache.Release(lease);
   cache.Admit(other,Readers());Upload(cache,other,bytes);var protectedLease=cache.Acquire(other.Id,new string('b',64),false);now+=11;cache.Cleanup(64);Check(cache.Read(protectedLease,0,1024).Length==1024,"Active lease evicted");Reject(()=>cache.Acquire(other.Id,new string('c',64),false),"cache_attachment_expired");cache.Release(protectedLease);cache.Cleanup(128);Reject(()=>cache.Inspect(other.Id),"cache_attachment_unavailable");
   now+=40;cache.Cleanup(128);Reject(()=>cache.Inspect(d.Id),"cache_attachment_unavailable");
  }
  options.MaximumBytes=6L*1024*1024;using(var cache=new AttachmentCacheRuntime(options,()=>now)){Check(cache.ChargedBytes<=options.MaximumBytes,"Expired storage not reclaimed");}
  Console.WriteLine("PASS corrupt/incomplete rejection, protected lease, expired access, bounded expiration and reclamation");
 }}
 static void Crash(string root,string mode){using(var signer=new ECDsaCng(256)){
  string path=Fixture(root,"crash-"+mode);var options=Options(path);var bytes=Payload(1024*1024);var descriptor=Descriptor(signer,1024*1024,bytes);string info=Path.Combine(root,"descriptor-"+mode+".bin");File.WriteAllBytes(info,descriptor.Wire);
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){cache.Admit(descriptor,Readers());if(mode=="cleanup")Upload(cache,descriptor,bytes);else{var lease=cache.Acquire(descriptor.Id,descriptor.Sender,true);cache.Append(lease,0,bytes.Take(32768).ToArray());}}
  string executable=System.Reflection.Assembly.GetExecutingAssembly().Location,prefix="";
#if PEERCHAT_PORTABLE
  executable=Environment.ProcessPath;if(Path.GetFileNameWithoutExtension(executable)=="dotnet")prefix="\""+System.Reflection.Assembly.GetExecutingAssembly().Location+"\" ";
#endif
  var start=new ProcessStartInfo(executable,prefix+"--crash-child \""+path+"\" \""+info+"\" "+mode+" "+now){UseShellExecute=false,CreateNoWindow=true};
  string ready=info+".ready";using(var child=Process.Start(start)){var wait=Stopwatch.StartNew();while(!File.Exists(ready)&&!child.HasExited&&wait.ElapsedMilliseconds<20000)Thread.Sleep(10);Check(File.Exists(ready)&&!child.HasExited,"Crash boundary not reached");child.Kill();Check(child.WaitForExit(5000),"Crash child did not terminate");}
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){if(mode=="cleanup"){Reject(()=>cache.Inspect(descriptor.Id),"cache_attachment_unavailable");cache.Cleanup(128);}else{Check(cache.Inspect(descriptor.Id).Offset==32768,"Uncommitted prefix acknowledged after crash");Upload(cache,descriptor,bytes);Check(cache.Inspect(descriptor.Id).Complete,"Recovery completion");}}
  Console.WriteLine("PASS actual process kill before SQLite commit during "+mode+"; durable restart recovery");
 }}
 static int Child(string[] args){string path=args[1],file=args[2],mode=args[3];now=Int64.Parse(args[4]);var d=AttachmentCacheDescriptor.Parse(File.ReadAllBytes(file));using(var cache=new AttachmentCacheRuntime(Options(path),()=>now)){
  string lease=mode=="upload"?cache.Acquire(d.Id,d.Sender,true):null;if(mode=="cleanup")now+=11;
  AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit"){File.WriteAllText(file+".ready","ready");Thread.Sleep(Timeout.Infinite);}};
  if(mode=="upload")cache.Append(lease,32768,Payload(1024*1024).Skip(32768).Take(32768).ToArray());else cache.Cleanup(1);
 }return 2;}
 static void Pressure(string root){using(var signer=new ECDsaCng(256)){
  var path=Fixture(root,"pressure");var options=Options(path);var bytes=Payload(3*1024*1024);var retained=new List<AttachmentCacheDescriptor>();
  using(var cache=new AttachmentCacheRuntime(options,()=>now))for(int n=0;n<3;n++){
   var d=Descriptor(signer,3*1024*1024,bytes);retained.Add(d);cache.Admit(d,Readers());Upload(cache,d,bytes);now++;
  }
  // Existing completed data fits after a safe quota reduction, but the next
  // upload needs actual reclamation, not merely Space(0) succeeding.
  options.MaximumBytes=20L*1024*1024;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   var protectedLease=cache.Acquire(retained[2].Id,new string('b',64),false);
   var incoming=Payload(6*1024*1024);var descriptor=Descriptor(signer,6*1024*1024,incoming);
   cache.Admit(descriptor,Readers());Check(cache.Inspect(descriptor.Id).Offset==0,"Admission progress fabricated");
   Reject(()=>cache.Inspect(retained[0].Id),"cache_attachment_unavailable");
   Check(cache.Inspect(retained[2].Id).Complete&&cache.Read(protectedLease,0,1024).Length==1024,"Protected newest object evicted");
   Upload(cache,descriptor,incoming);Check(cache.ChargedBytes<=options.MaximumBytes,"Pressure upload exceeded quota");cache.Release(protectedLease);
  }
  Console.WriteLine("PASS admission reclaims the required incoming budget, oldest eligible first, preserving active leases");
 }}
 static void Admission(string root){Directory.CreateDirectory(root);using(var owner=new ECDsaCng(256))using(var member=new ECDsaCng(256))using(var outsider=new ECDsaCng(256)){
  var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(owner,new string('1',32),1)));
  string group=proof.ServerGroupId,actor=proof.OwnerIdentity,reader=MailboxProtocol.Digest(member.Key.Export(CngKeyBlobFormat.EccPublicBlob)),other=MailboxProtocol.Digest(outsider.Key.Export(CngKeyBlobFormat.EccPublicBlob));
  var options=Options(Fixture(root,"admission"));var data=Payload(65536);var descriptor=Descriptor(owner,65536,data,group:group,epoch:3);
  using(var db=new AccountDatabase(Path.Combine(root,"admission-accounts.sqlite")))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   Check(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],actor)==1&&db.CreateAccountWithDevice("reader",new byte[16],new byte[32],reader)==2&&db.CreateAccountWithDevice("other",new byte[16],new byte[32],other)==3,"Admission accounts");
   Check(db.RegisterGroup(1,actor,proof)!=null&&db.GroupInvite(group,1,actor,2,reader,1)!=null&&db.GroupRespond(group,2,reader,true,2)!=null,"Admission roster");
   var admission=new AttachmentCacheAdmission(db,cache);string binding=db.AttachmentMembership(group,2,reader);Check(!String.IsNullOrEmpty(binding),"Membership backfill");
   db.Execute("BEGIN IMMEDIATE;DELETE FROM group_roster_devices WHERE public_identity='"+reader+"';ROLLBACK;");Check(db.AttachmentMembership(group,2,reader)==binding,"Rolled-back membership changed");
   Reject(()=>admission.Admit(2,reader,()=>true,descriptor.Wire),"cache_sender_binding_mismatch");
   Reject(()=>admission.Admit(1,actor,()=>false,descriptor.Wire),"cache_session_or_epoch_stale");
   Reject(()=>admission.Admit(1,actor,()=>true,Descriptor(owner,65536,data,group:group,epoch:2).Wire),"cache_session_or_epoch_stale");
   admission.Admit(1,actor,()=>true,descriptor.Wire);
   Reject(()=>admission.Acquire(2,reader,()=>true,descriptor.Id,false),"cache_upload_incomplete");
   Reject(()=>admission.Acquire(3,other,()=>true,descriptor.Id,false),"cache_group_not_authorized");
   string upload=admission.Acquire(1,actor,()=>true,descriptor.Id,true);
   Reject(()=>admission.Append(1,actor,()=>false,upload,0,data.Take(32768).ToArray()),"cache_session_invalid");Check(cache.Inspect(descriptor.Id).Offset==0,"Rejected session advanced offset");
   long offset=0;while(offset<data.Length)offset=admission.Append(1,actor,()=>true,upload,offset,data.Skip((int)offset).Take(32768).ToArray());
   admission.Complete(1,actor,()=>true,upload);admission.Release(1,actor,()=>true,upload,true);
   string download=admission.Acquire(2,reader,()=>true,descriptor.Id,false);Check(admission.Read(2,reader,()=>true,download,0,1024).Length==1024,"Authorized ciphertext read");
   Reject(()=>admission.Read(1,actor,()=>true,download,0,1024),"cache_lease_device_mismatch");
   Check(db.GroupRemove(group,1,actor,2,reader,3)!=null,"Member revocation");
   Reject(()=>admission.Read(2,reader,()=>true,download,0,1024),"cache_group_not_authorized");
   Check(db.GroupInvite(group,1,actor,3,other,4)!=null&&db.GroupRespond(group,3,other,true,5)!=null,"Later member");
   Reject(()=>admission.Acquire(3,other,()=>true,descriptor.Id,false),"cache_original_membership_required");
   Check(db.GroupInvite(group,1,actor,2,reader,6)!=null&&db.GroupRespond(group,2,reader,true,7)!=null,"Rejoined member");Check(db.AttachmentMembership(group,2,reader)!=binding,"Rejoin retained old membership");
   Reject(()=>admission.Acquire(2,reader,()=>true,descriptor.Id,false),"cache_original_membership_required");
   Reject(()=>admission.Read(2,reader,()=>true,download,0,1024),"cache_original_membership_required");
   var fresh=Descriptor(owner,65536,data,group:group,epoch:8);admission.Admit(1,actor,()=>true,fresh.Wire);Upload(cache,fresh,data);
   var lease=admission.Acquire(2,reader,()=>true,fresh.Id,false);Check(admission.Read(2,reader,()=>true,lease,0,1024).Length==1024,"New membership cannot receive new object");admission.Release(2,reader,()=>true,lease,false);
  }
  using(var db=new AccountDatabase(Path.Combine(root,"admission-accounts.sqlite")))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   var admission=new AttachmentCacheAdmission(db,cache);Reject(()=>admission.Acquire(2,reader,()=>true,descriptor.Id,false),"cache_original_membership_required");
   var lease=admission.Acquire(1,actor,()=>true,descriptor.Id,false);Check(admission.Read(1,actor,()=>true,lease,0,1024).Length==1024,"Owner restart access lost");
  }
  Console.WriteLine("PASS authoritative roster/session admission, wrong account/device/lease, revoke/rejoin, later-member historical exclusion, transaction rollback and restart");
 }}
 static void ConcurrentReservations(string root){using(var signer=new ECDsaCng(256)){
  var options=Options(Fixture(root,"concurrent"));options.MaximumBytes=20L*1024*1024;var bytes=Payload(2*1024*1024);
  var descriptors=Enumerable.Range(0,16).Select(n=>Descriptor(signer,2*1024*1024,bytes)).ToArray();int accepted=0,rejected=0;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   System.Threading.Tasks.Parallel.ForEach(descriptors,d=>{try{cache.Admit(d,Readers());Interlocked.Increment(ref accepted);}catch(InvalidOperationException e){Check(e.Message=="cache_quota_or_free_space_protected",e.Message);Interlocked.Increment(ref rejected);}});
   Check(accepted>0&&rejected>0&&accepted+rejected==16&&cache.ChargedBytes<=options.MaximumBytes,"Concurrent reservations oversubscribed quota");
   foreach(var d in descriptors){try{Check(cache.Inspect(d.Id).Offset==0,"Offered bytes claimed durable");}catch(InvalidOperationException e){Check(e.Message=="cache_attachment_unavailable",e.Message);}}
  }
  using(var cache=new AttachmentCacheRuntime(options,()=>now))Check(cache.ChargedBytes<=options.MaximumBytes,"Restart lost reservations");
  Console.WriteLine("PASS 16 concurrent admissions preserve a single bounded reservation ledger across restart; accepted="+accepted+" rejected="+rejected);
 }}
 static int Main(string[] args){try{if(args.Length>0&&args[0]=="--crash-child")return Child(args);if(args.Length==2&&args[0]=="--pressure"){Pressure(Path.GetFullPath(args[1]));return 0;}if(args.Length==2&&args[0]=="--admission"){Admission(Path.GetFullPath(args[1]));ConcurrentReservations(Path.GetFullPath(args[1]));return 0;}string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);RoundTrips(root);CorruptionAndExpiry(root);Pressure(root);Admission(root);ConcurrentReservations(root);Crash(root,"upload");Crash(root,"cleanup");Console.WriteLine("PASS cache runtime and authoritative admission; network endpoint/client integration remain gated");return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
