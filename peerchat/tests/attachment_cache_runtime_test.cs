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

partial class AttachmentCacheRuntimeTest {
 static long now=1000000;
 static void Check(bool b,string s){if(!b)throw new Exception(s);}
 static void Reject(Action a,string reason){try{a();throw new Exception("Unexpected success: "+reason);}catch(InvalidOperationException e){Check(e.Message==reason||reason=="*",e.Message+" != "+reason);}}
 static void BlobEncoding(){
  var encode=typeof(AttachmentCacheRuntime).GetMethod("Blob",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Static);
  Check(encode!=null,"SQLite blob encoder missing");
  foreach(int length in new[]{0,1,16,256,32768,65535,65536}){
   var bytes=new byte[length];for(int i=0;i<length;i++)bytes[i]=(byte)i;
   string expected="X'"+String.Concat(bytes.Select(x=>x.ToString("x2",System.Globalization.CultureInfo.InvariantCulture)))+"'";
   Check((string)encode.Invoke(null,new object[]{bytes})==expected,"SQLite blob encoding changed at length "+length);
  }
  var random=new byte[65536];new Random(233).NextBytes(random);
  string literal=(string)encode.Invoke(null,new object[]{random});
  Check(literal=="X'"+BitConverter.ToString(random).Replace("-","").ToLowerInvariant()+"'","Random maximum chunk literal changed");
  Console.WriteLine("PASS exact SQLite blob literals: empty, leading zero, every byte value, 32/64 KiB, partial final chunk and random ciphertext; bounded chunk allocations only");
 }
 static void AppendDiagnostics(string root){
  Check(CacheAppendTimings.Current==null,"Append timing leaked before request");
  using(var outer=new CacheAppendTimings()){
   bool isolated=false;var worker=new Thread(()=>{isolated=CacheAppendTimings.Current==null;});worker.Start();Check(worker.Join(5000)&&isolated,"Append metrics crossed RPC workers");
   CacheAppendTimings failed=null;
   try{using(failed=new CacheAppendTimings()){AttachmentCacheStorage.Observe("");}}catch(InvalidOperationException){}
   Check(CacheAppendTimings.Current==outer&&failed.Snapshot()[0]==1&&outer.Snapshot()[0]==0,"Failed storage inspection leaked timing scope or counters");
  }
  Check(CacheAppendTimings.Current==null,"Nested append timing was not restored");
  using(var signer=new ECDsaCng(256)){
   var options=Options(Fixture(root,"append-diagnostics"));var bytes=Payload(100000);var descriptor=Descriptor(signer,100000,bytes);string lease;
   using(var cache=new AttachmentCacheRuntime(options,()=>now)){
    cache.Admit(descriptor,Readers());lease=cache.Acquire(descriptor.Id,descriptor.Sender,true);var block=bytes.Take(65536).ToArray();long[] metrics;
    using(var capture=new CacheAppendTimings()){Check(cache.Append(lease,0,block)==65536,"Measured append durable offset");metrics=capture.Snapshot();}
    Check(metrics[0]==4&&metrics[2]==1&&metrics[4]==1&&metrics.All(x=>x>=0),"Append storage checks, blob encoding or durable transaction omitted");
    using(var capture=new CacheAppendTimings()){Check(cache.Append(lease,0,block)==65536,"Measured duplicate inflated offset");var duplicate=capture.Snapshot();Check(duplicate[0]==1&&duplicate[2]==0&&duplicate[4]==0,"Duplicate append performed a new durable transaction");}
    AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit")throw new InvalidOperationException("cache_test_timing_rollback");};
    try{using(var capture=new CacheAppendTimings()){try{cache.Append(lease,65536,bytes.Skip(65536).ToArray());throw new Exception("Append timing rollback not exercised");}catch(InvalidOperationException error){Check(error.Message=="cache_test_timing_rollback",error.Message);}Check(capture.Snapshot()[4]==1,"Failed durable transaction was not measured");}}
    finally{AttachmentCacheRuntime.TestFault=null;}
    Check(CacheAppendTimings.Current==null&&cache.Inspect(descriptor.Id).Offset==65536,"Failed append leaked metrics or advanced durable offset");
    var diagnostics=new ServerTimingDiagnostics();string id=new string('a',32);long started=Stopwatch.GetTimestamp();
    diagnostics.Record("cache_append_components","cache_dm_append",id,started,started,metrics);string row=diagnostics.Snapshot().Single();
    Check(row.Contains("request_id="+id)&&row.Contains("storage_observe_count=4")&&row.Contains("blob_encode_count=1")&&row.Contains("append_transaction_count=1")&&!row.Contains(options.Directory)&&!row.Contains(lease),"Append timing correlation/counts/privacy");
    for(int i=0;i<100;i++)diagnostics.Record("cache_append_components","cache_dm_append",id,started,started,metrics);
    Check(diagnostics.Snapshot().Length==64,"Append diagnostic history is unbounded");
   }
   using(var cache=new AttachmentCacheRuntime(options,()=>now))Check(cache.Inspect(descriptor.Id).Offset==65536,"Timing instrumentation changed crash-recovered offset");
  }
  Console.WriteLine("PASS append numeric timing correlation, bounded history, storage-check count, duplicate behavior, worker isolation, failure cleanup and FULL transaction rollback/restart");
 }
 static void Field(BinaryWriter w,byte[] value){w.Write((ulong)value.Length);w.Write(value);}
 static void Text(BinaryWriter w,string value){Field(w,Encoding.UTF8.GetBytes(value));}
 static AttachmentCacheDescriptor Descriptor(ECDsaCng key,int plainBytes,byte[] cipher,string id=null,string group=null,long epoch=1,long account=1,long? declaredCipherBytes=null){
  using(var m=new MemoryStream())using(var w=new BinaryWriter(m)){
   var publicKey=key.Key.Export(CngKeyBlobFormat.EccPublicBlob);w.Write(new byte[]{80,84,1,5});Text(w,"PeerChat/group-cache-upload/v1");Text(w,id??Guid.NewGuid().ToString("N"));Text(w,group??new string('1',32));Text(w,Guid.NewGuid().ToString("N"));Text(w,MailboxProtocol.Digest(publicKey));w.Write((ulong)account);w.Write((ulong)epoch);w.Write((ulong)plainBytes);w.Write((ulong)(declaredCipherBytes??cipher.Length));using(var sha=SHA256.Create())Field(w,sha.ComputeHash(cipher));Field(w,publicKey);using(var sha=SHA256.Create())Field(w,key.SignHash(sha.ComputeHash(m.ToArray())));return AttachmentCacheDescriptor.Parse(m.ToArray());
  }
 }
 static byte[] Payload(int plain){int size=checked(20+plain+Math.Max(1,(plain+32767)/32768)*16);var bytes=new byte[size];new Random(plain).NextBytes(bytes);return bytes;}
 static byte[] Announcement(ECDsaCng key,AttachmentCacheDescriptor descriptor,int length=1024){
  // Server-side fixture has genuine device signatures and canonical opaque
  // framing. Native tests separately prove actual group encryption/decryption.
  var wire=new byte[254+length];Action<int,string> text=(offset,value)=>Encoding.ASCII.GetBytes(value).CopyTo(wire,offset);Action<int,long> number=(offset,value)=>{for(int i=7;i>=0;i--){wire[offset+i]=(byte)value;value>>=8;}};
  text(0,"PCGE");wire[5]=1;text(6,descriptor.Group);number(38,descriptor.Epoch);number(46,descriptor.Epoch);text(54,descriptor.Message);number(86,descriptor.Account);text(94,descriptor.Sender);wire[170]=(byte)(length>>24);wire[171]=(byte)(length>>16);wire[172]=(byte)(length>>8);wire[173]=(byte)length;
  for(int n=174;n<wire.Length-64;n++)wire[n]=251;using(var hash=SHA256.Create())key.SignHash(hash.ComputeHash(wire.Take(wire.Length-64).ToArray())).CopyTo(wire,wire.Length-64);return wire;
 }
 static AttachmentCacheConfiguration Options(string directory){var evidence=AttachmentCacheStorage.Observe(directory);return new AttachmentCacheConfiguration{MaximumBytes=64L*1024*1024,Directory=evidence.Directory,ExpectedMountPoint=evidence.MountPoint,ExpectedFilesystem=evidence.Filesystem,InstanceId=evidence.InstanceId,RetentionSeconds=10,PartialIdleSeconds=30};}
 static string Fixture(string root,string name){string path=Path.Combine(root,name);Directory.CreateDirectory(path);
#if PEERCHAT_PORTABLE
  if(!OperatingSystem.IsWindows())File.SetUnixFileMode(path,UnixFileMode.UserRead|UnixFileMode.UserWrite|UnixFileMode.UserExecute);
#endif
  File.WriteAllText(Path.Combine(path,AttachmentCacheStorage.Marker),new string('a',32));return path;}
 static void Upload(AttachmentCacheRuntime cache,AttachmentCacheDescriptor d,byte[] bytes,int chunk=32768){var lease=cache.Acquire(d.Id,d.Sender,true);long offset=cache.Inspect(d.Id).Offset;while(offset<bytes.Length){int n=(int)Math.Min(chunk,bytes.Length-offset);var block=bytes.Skip((int)offset).Take(n).ToArray();offset=cache.Append(lease,offset,block);}cache.Complete(lease);cache.Complete(lease);cache.Release(lease);}
 static List<CacheRecipient> Readers(){return new List<CacheRecipient>{new CacheRecipient(2,new string('b',64),new string('4',32)),new CacheRecipient(3,new string('c',64),new string('5',32))};}
 static Dictionary<string,object> Rpc(AttachmentCacheRequests handler,long account,string device,string operation,string state,params object[] fields){
  var request=new Dictionary<string,object>{{"type",operation},{"requestId",Guid.NewGuid().ToString("N")}};for(int n=0;n<fields.Length;n+=2)request[(string)fields[n]]=fields[n+1];
  var json=new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=100000};
  var response=handler.Execute(account,device,()=>true,json.Deserialize<Dictionary<string,object>>(json.Serialize(request)));
  Check(Convert.ToString(response["state"])==state,"RPC "+operation+" state="+Convert.ToString(response["state"]));
  var wire=json.Serialize(response);Check(wire.Length<100000&&!wire.Contains("private attachment key")&&!wire.Contains("plaintext filename"),"RPC response bounds/privacy");return response;
 }
 static void CanceledUpload(string root){using(var signer=new ECDsaCng(256)){
  var options=Options(Fixture(root,"canceled-upload"));options.MaximumBytes=48L*1024*1024;options.PartialIdleSeconds=300;
  const int plain=32*1024*1024;long length=20L+plain+((plain+32767L)/32768)*16;
  var block=new byte[32768];new Random(17).NextBytes(block);var d=Descriptor(signer,plain,block,declaredCipherBytes:length);string lease;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   long empty=cache.ChargedBytes;cache.Admit(d,Readers());lease=cache.Acquire(d.Id,d.Sender,true);
   for(int i=0;i<64;++i)cache.Append(lease,i*32768L,block);
   long held=cache.ChargedBytes;cache.Release(lease,true);
   Check(cache.ChargedBytes<held-30L*1024*1024,"Canceled future reservation retained");
   Check(cache.ChargedBytes>empty,"Canceled physical chunks no longer charged");
   Reject(()=>cache.Inspect(d.Id),"cache_attachment_unavailable");Reject(()=>cache.Append(lease,64*32768L,block),"cache_lease_expired");
  }
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   Reject(()=>cache.Inspect(d.Id),"cache_attachment_unavailable");
   var replacement=Descriptor(signer,plain,block,declaredCipherBytes:length);cache.Admit(replacement,Readers());cache.Release(cache.Acquire(replacement.Id,replacement.Sender,true),true);cache.Cleanup(32);
   var bytes=Payload(70001);var complete=Descriptor(signer,70001,bytes);cache.Admit(complete,Readers());var upload=cache.Acquire(complete.Id,complete.Sender,true);
   for(long offset=0;offset<bytes.Length;){int n=(int)Math.Min(32768,bytes.Length-offset);cache.Append(upload,offset,bytes.Skip((int)offset).Take(n).ToArray());offset+=n;}
   cache.Complete(upload);cache.Release(upload,true);Check(cache.Inspect(complete.Id).Complete,"Cancel removed completed recipient file");
   var download=cache.Acquire(complete.Id,new string('b',64),false);Reject(()=>cache.Release(download,true),"cache_lease_expired");Check(cache.Read(download,0,32768).SequenceEqual(bytes.Take(32768)),"Read lease cancel attempt damaged completed file");cache.Release(download);
  }
  Console.WriteLine("PASS canceled upload: future reservation released, stored chunks charged, restart/replacement admission, read-lease rejection and completed file preserved");
 }}
 static void LargeAdmission(string root){using(var signer=new ECDsaCng(256)){
  const int limit=1536*1024*1024;const long quota=10L*1024*1024*1024;
  Func<long,long> cipherSize=n=>20+n+Math.Max(1L,(n+32767)/32768)*16;
  var opaque=new byte[36];var options=Options(Fixture(root,"large-reservations"));options.MaximumBytes=quota;
  Reject(()=>Descriptor(signer,limit+1,opaque,declaredCipherBytes:cipherSize((long)limit+1)),"Invalid signed encrypted attachment descriptor.");
  var descriptors=Enumerable.Range(0,6).Select(_=>Descriptor(signer,limit,opaque,declaredCipherBytes:cipherSize(limit))).ToArray();
  int accepted=0,rejected=0;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   System.Threading.Tasks.Parallel.ForEach(descriptors,d=>{try{cache.Admit(d,Readers());Interlocked.Increment(ref accepted);}catch(InvalidOperationException error){Check(error.Message=="cache_quota_or_free_space_protected",error.Message);Interlocked.Increment(ref rejected);}});
   Check(accepted==5&&rejected==1&&cache.ChargedBytes<=quota&&cache.ChargedBytes>9L*1024*1024*1024,"1.5 GiB reservations did not include temporary/metadata overhead");
  }
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   Check(cache.ChargedBytes>9L*1024*1024*1024&&cache.ChargedBytes<=quota,"Restart lost large reservations");
   now+=31;cache.Cleanup(64);Check(cache.ChargedBytes<8L*1024*1024,"Expired partial reservations were not released");
   Check(!cache.Admit(Descriptor(signer,limit,opaque,declaredCipherBytes:cipherSize(limit)),Readers()).Complete,"Admission reported file completion");
  }
  Console.WriteLine("PASS exact 1.5 GiB admission, limit+1 rejection, concurrent 10 GiB quota including overhead, restart reservations and expiry reclamation (metadata-only large-size fixture)");
 }}
 static void BatchQuota(string root){using(var signer=new ECDsaCng(256)){
  var options=Options(Fixture(root,"batch-quota"));options.MaximumBytes=9L*1024*1024;
  var payload=Payload(2*1024*1024);var d=Descriptor(signer,2*1024*1024,payload);
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   cache.Admit(d,Readers());string lease=cache.Acquire(d.Id,d.Sender,true);
   var chunks=new List<KeyValuePair<long,byte[]>>();for(int i=0;i<8;i++)chunks.Add(new KeyValuePair<long,byte[]>(i*65536L,payload.Skip(i*65536).Take(65536).ToArray()));
   var offsets=cache.AppendBatch(lease,chunks);Check(offsets.Length==8&&offsets[7]==524288&&cache.ChargedBytes<=options.MaximumBytes,"Batch exceeded narrow reserved quota");
  }
  using(var cache=new AttachmentCacheRuntime(options,()=>now))Check(cache.Inspect(d.Id).Offset==524288&&cache.ChargedBytes<=options.MaximumBytes,"Batch narrow-quota ledger failed restart");
  Console.WriteLine("PASS eight-chunk FULL commit under 9 MiB quota, 4 MiB journal headroom and restart ledger");
 }}

 static void BoundedCompletion(string root){using(var signer=new ECDsaCng(256)){
  var options=Options(Fixture(root,"bounded-completion"));var bytes=Payload(10*1024*1024);var d=Descriptor(signer,10*1024*1024,bytes);string lease;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   cache.Admit(d,Readers());lease=cache.Acquire(d.Id,d.Sender,true);long offset=0;
   while(offset<bytes.Length)offset=cache.Append(lease,offset,bytes.Skip((int)offset).Take(65536).ToArray());
   Check(cache.VerifyCompletionStep(lease)==null&&!cache.Inspect(d.Id).Complete,"Bounded finalization prematurely completed");
   cache.Release(lease);Reject(()=>cache.VerifyCompletionStep(lease),"cache_lease_expired");
   lease=cache.Acquire(d.Id,d.Sender,true);Check(cache.VerifyCompletionStep(lease)==null,"Replacement lease reused old hash state");
  }
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   lease=cache.Acquire(d.Id,d.Sender,true);Check(cache.VerifyCompletionStep(lease)==null,"Restart skipped ciphertext verification");
   now+=100;var evidence=cache.VerifyCompletionStep(lease);Check(evidence!=null&&!cache.Inspect(d.Id).Complete,"Hash verification published without authorization commit");
   cache.CommitCompletion(lease,evidence);Check(cache.Inspect(d.Id).Complete,"Bounded completion commit missing");
   cache.CommitCompletion(lease,cache.VerifyCompletionStep(lease));cache.Release(lease);
  }
  var bad=(byte[])bytes.Clone();bad[bad.Length-1]^=1;var corrupt=Descriptor(signer,10*1024*1024,bytes);
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   cache.Admit(corrupt,Readers());lease=cache.Acquire(corrupt.Id,corrupt.Sender,true);long offset=0;
   while(offset<bad.Length)offset=cache.Append(lease,offset,bad.Skip((int)offset).Take(65536).ToArray());
   Check(cache.VerifyCompletionStep(lease)==null,"Corrupt fixture should require a second step");
   Reject(()=>cache.VerifyCompletionStep(lease),"cache_ciphertext_digest_mismatch");Check(!cache.Inspect(corrupt.Id).Complete,"Corrupted tail accepted");cache.Release(lease);
  }
  Console.WriteLine("PASS bounded finalization: multi-step, released lease, process restart, separate commit, repeated completion and corrupt tail");
 }}

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
  string lease=mode=="upload"||mode=="batch-upload"?cache.Acquire(d.Id,d.Sender,true):null;if(mode=="cleanup")now+=11;
  AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit"){File.WriteAllText(file+".ready","ready");Thread.Sleep(Timeout.Infinite);}};
  if(mode=="upload")cache.Append(lease,32768,Payload(1024*1024).Skip(32768).Take(32768).ToArray());
  else if(mode=="batch-upload"){
   var payload=Payload(1024*1024);var chunks=new List<KeyValuePair<long,byte[]>>();long offset=32768;
   for(int i=0;i<8;i++){int length=i==0?32768:65536;chunks.Add(new KeyValuePair<long,byte[]>(offset,payload.Skip((int)offset).Take(length).ToArray()));offset+=length;}
   cache.AppendBatch(lease,chunks);
  }else cache.Cleanup(1);
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
   var announcement=Announcement(owner,descriptor);admission.Publish(1,actor,()=>true,descriptor.Id,announcement);admission.Publish(1,actor,()=>true,descriptor.Id,announcement);Check(admission.PendingAnnouncements(2,reader,()=>true,"").Items.Count==1,"Offline announcement missing");
   Reject(()=>admission.Publish(2,reader,()=>true,descriptor.Id,announcement),"cache_sender_binding_mismatch");
   var badAnnouncement=(byte[])announcement.Clone();badAnnouncement[174]^=1;Reject(()=>admission.Publish(1,actor,()=>true,descriptor.Id,badAnnouncement),"cache_announcement_binding_invalid");
   string download=admission.Acquire(2,reader,()=>true,descriptor.Id,false);Check(admission.Read(2,reader,()=>true,download,0,1024).Length==1024,"Authorized ciphertext read");
   Reject(()=>admission.Read(1,actor,()=>true,download,0,1024),"cache_lease_device_mismatch");
   Check(db.GroupRemove(group,1,actor,2,reader,3)!=null,"Member revocation");
   Reject(()=>admission.Read(2,reader,()=>true,download,0,1024),"cache_group_not_authorized");
   Check(admission.PendingAnnouncements(2,reader,()=>true,"").Items.Count==0,"Revoked member received announcement");
   Check(db.GroupInvite(group,1,actor,3,other,4)!=null&&db.GroupRespond(group,3,other,true,5)!=null,"Later member");
   Reject(()=>admission.Acquire(3,other,()=>true,descriptor.Id,false),"cache_original_membership_required");
   Check(db.GroupInvite(group,1,actor,2,reader,6)!=null&&db.GroupRespond(group,2,reader,true,7)!=null,"Rejoined member");Check(db.AttachmentMembership(group,2,reader)!=binding,"Rejoin retained old membership");
   Reject(()=>admission.Acquire(2,reader,()=>true,descriptor.Id,false),"cache_original_membership_required");
   Reject(()=>admission.Read(2,reader,()=>true,download,0,1024),"cache_original_membership_required");
   Check(admission.PendingAnnouncements(2,reader,()=>true,"").Items.Count==0,"Rejoined member regained historical announcement");
   var fresh=Descriptor(owner,65536,data,group:group,epoch:8);admission.Admit(1,actor,()=>true,fresh.Wire);Upload(cache,fresh,data);
   var lease=admission.Acquire(2,reader,()=>true,fresh.Id,false);Check(admission.Read(2,reader,()=>true,lease,0,1024).Length==1024,"New membership cannot receive new object");admission.Release(2,reader,()=>true,lease,false);
   var rpc=new AttachmentCacheRequests(admission);var rpcDescriptor=Descriptor(owner,65536,data,group:group,epoch:8);
   Rpc(rpc,1,actor,"cache_admit","accepted","descriptor",Convert.ToBase64String(rpcDescriptor.Wire));
   string rpcUpload=Convert.ToString(Rpc(rpc,1,actor,"cache_acquire","accepted","id",rpcDescriptor.Id,"upload",true)["lease"]);
   Rpc(rpc,1,actor,"cache_complete","cache_upload_incomplete","lease",rpcUpload);
   string firstChunk=AttachmentCacheRequests.Encode(data.Take(65536).ToArray());
   Check(Convert.ToInt64(Rpc(rpc,1,actor,"cache_append","accepted","lease",rpcUpload,"offset",0L,"data",firstChunk)["durableOffset"])==65536,"RPC durable prefix");
   Check(Convert.ToInt64(Rpc(rpc,1,actor,"cache_append","accepted","lease",rpcUpload,"offset",0L,"data",firstChunk)["durableOffset"])==65536,"RPC duplicate inflation");
   Rpc(rpc,1,actor,"cache_append","cache_malformed_request","lease",rpcUpload,"offset",-1L,"data",firstChunk);
   Rpc(rpc,1,actor,"cache_append","cache_malformed_request","lease",rpcUpload,"offset",65536L,"data","not base64");
   foreach(var invalid in new[]{"AA==","AB","A","++//",new string('A',87383)})Rpc(rpc,1,actor,"cache_append","cache_malformed_request","lease",rpcUpload,"offset",65536L,"data",invalid);
   Check(cache.Inspect(rpcDescriptor.Id).Offset==65536,"Malformed encoding advanced durable offset");
   Rpc(rpc,1,actor,"cache_append","accepted","lease",rpcUpload,"offset",65536L,"data",AttachmentCacheRequests.Encode(data.Skip(65536).ToArray()));
   var completed=Rpc(rpc,1,actor,"cache_complete","accepted","lease",rpcUpload);Check((bool)completed["ciphertextComplete"]&&!completed.ContainsKey("plaintextVerified"),"Ciphertext commit claimed plaintext verification");
   Rpc(rpc,1,actor,"cache_release","accepted","lease",rpcUpload,"upload",true);
   string rpcDownload=Convert.ToString(Rpc(rpc,2,reader,"cache_acquire","accepted","id",rpcDescriptor.Id,"upload",false)["lease"]);
   Rpc(rpc,1,actor,"cache_read","cache_lease_device_mismatch","lease",rpcDownload,"offset",0L,"maximum",65536L);
   Rpc(rpc,2,reader,"cache_read","cache_malformed_request","lease",rpcDownload,"offset",0L,"maximum",65537L);
   var received=Rpc(rpc,2,reader,"cache_read","accepted","lease",rpcDownload,"offset",0L,"maximum",65536L);Check(AttachmentCacheRequests.Decode((string)received["data"]).SequenceEqual(data.Take(65536)),"RPC ciphertext integrity");
   Rpc(rpc,2,reader,"cache_progress","accepted","lease",rpcDownload,"offset",65536L);
   Rpc(rpc,2,reader,"cache_release","accepted","lease",rpcDownload,"upload",false);
   Rpc(rpc,2,reader,"cache_status","accepted","id",rpcDescriptor.Id);
   Rpc(rpc,2,reader,"cache_unknown","cache_unsupported_operation");
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
 sealed class RpcClient:IDisposable {
  readonly System.Net.Sockets.TcpClient tcp=new System.Net.Sockets.TcpClient();readonly System.Net.Security.SslStream tls;readonly string token,device;
  readonly System.Web.Script.Serialization.JavaScriptSerializer json=new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=100000};
  internal RpcClient(int port,string pin,string token,string device){this.token=token;this.device=device;tcp.NoDelay=true;tcp.ReceiveTimeout=5000;tcp.SendTimeout=5000;tcp.Connect("localhost",port);tls=new System.Net.Security.SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>certificate!=null&&MailboxProtocol.Digest(certificate.GetRawCertData())==pin);try{tls.AuthenticateAsClient("localhost");Write(new Dictionary<string,object>{{"version",1},{"type","rpc_open"},{"sessionToken",token},{"publicIdentity",device}});var ready=Read();Check(ready.ContainsKey("ok")&&(bool)ready["ok"],"RPC session/device admission");}catch{Dispose();throw;}}
  void Write(Dictionary<string,object> value){var bytes=Encoding.UTF8.GetBytes(json.Serialize(value));Check(bytes.Length<=100000,"Request too large");tls.Write(new[]{(byte)(bytes.Length>>24),(byte)(bytes.Length>>16),(byte)(bytes.Length>>8),(byte)bytes.Length},0,4);tls.Write(bytes,0,bytes.Length);tls.Flush();}
  byte[] Exact(int count){var bytes=new byte[count];int offset=0;while(offset<count){int read=tls.Read(bytes,offset,count-offset);if(read==0)throw new EndOfStreamException();offset+=read;}return bytes;}
  Dictionary<string,object> Read(){var header=Exact(4);int length=(header[0]<<24)|(header[1]<<16)|(header[2]<<8)|header[3];Check(length>0&&length<=100000,"Response bound");return json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(Exact(length)));}
  internal Dictionary<string,object> Request(string operation,string expected,params object[] fields){string id=Guid.NewGuid().ToString("N");var request=new Dictionary<string,object>{{"version",1},{"type",operation},{"rpcRequestId",id},{"requestId",id},{"sessionToken",token},{"publicIdentity",device}};for(int n=0;n<fields.Length;n+=2)request[(string)fields[n]]=fields[n+1];Write(request);var response=Read();Check((string)response["type"]=="rpc_result"&&(string)response["operation"]==operation&&(string)response["rpcRequestId"]==id,"RPC correlation");var body=(Dictionary<string,object>)response["body"];if(expected!=null)Check(Convert.ToString(body["state"])==expected,"Network "+operation+" "+Convert.ToString(body["state"]));return body;}
  internal List<Dictionary<string,object>> DirectBatch(IList<Dictionary<string,object>> fields,string expected,bool partial=false){
   Check(fields.Count>=2&&fields.Count<=8,"Network batch bound");var ids=new List<string>();
   for(int i=0;i<(partial?1:fields.Count);i++){
    string id=Guid.NewGuid().ToString("N");ids.Add(id);
    var request=new Dictionary<string,object>{{"version",1},{"type","cache_dm_append"},{"rpcRequestId",id},{"requestId",id},{"sessionToken",token},{"publicIdentity",device}};
    foreach(var field in fields[i])if(field.Key!="requestId"&&field.Key!="type")request[field.Key]=field.Value;if(i==0)request["batchCount"]=fields.Count;
    Write(request);
   }
   if(partial)return new List<Dictionary<string,object>>();
   var replies=new List<Dictionary<string,object>>();for(int i=0;i<ids.Count;i++){
    var response=Read();Check((string)response["type"]=="rpc_result"&&(string)response["operation"]=="cache_dm_append"&&(string)response["rpcRequestId"]==ids[i],"Batch RPC correlation/order");
    var body=(Dictionary<string,object>)response["body"];Check((string)body["requestId"]==ids[i]&&Convert.ToString(body["state"])==expected,"Batch response state/correlation");replies.Add(body);
   }return replies;
  }
  public void Dispose(){if(tls!=null)tls.Dispose();tcp.Dispose();}
 }
 static void Network(string root,bool enabled,string shippingAssembly=null){Directory.CreateDirectory(root);using(var signer=new ECDsaCng(256))using(var readerSigner=new ECDsaCng(256)){
  var options=Options(Fixture(root,"network-cache"));options.Enabled=enabled;
  var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(signer,new string('1',32),4)));string actor=proof.OwnerIdentity,unapproved=new string('e',64);
  string token=Guid.NewGuid().ToString("N"),otherToken=Guid.NewGuid().ToString("N"),legacyToken=Guid.NewGuid().ToString("N"),readerToken=Guid.NewGuid().ToString("N"),readerDevice=MailboxProtocol.Digest(readerSigner.Key.Export(CngKeyBlobFormat.EccPublicBlob)),database=Path.Combine(root,"network-accounts.sqlite");
  var readerBinding=RecipientBinding(readerSigner,5);
  using(var db=new AccountDatabase(database)){
   for(int n=1;n<4;n++)Check(db.CreateAccountWithDevice("unused-"+n,new byte[16],new byte[32],new string((char)('a'+n),64))==n,"Network account fixture");
   Check(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],actor)==4&&db.AddDevice(4,unapproved),"Same account distinct device fixture");
   Check(db.CreateAccountWithDevice("recipient",new byte[16],new byte[32],readerDevice)==5&&db.RegisterGroup(4,actor,proof)!=null&&db.GroupInvite(proof.ServerGroupId,4,actor,5,readerDevice,1)!=null&&db.GroupRespond(proof.ServerGroupId,5,readerDevice,true,2)!=null,"Network signed roster");
   db.InitializeMailbox();Check(db.RegisterMailboxKey(5,readerDevice,readerSigner.Key.Export(CngKeyBlobFormat.EccPublicBlob),readerBinding)=="accepted","Network recipient-key registration");
   foreach(var session in new[]{new[]{token,actor,"4"},new[]{otherToken,unapproved,"4"},new[]{readerToken,readerDevice,"5"}}){long account=Int64.Parse(session[2]);string hash=MailboxProtocol.Digest(Encoding.UTF8.GetBytes(session[0])),credential=Guid.NewGuid().ToString("N");db.RememberDevice(account,session[1],credential);db.CreateDeviceSession(hash,account,session[1],DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds(),credential);Check(db.Authenticate(hash)==account&&db.SessionDevice(hash)==session[1],"Bound fixture session");}
   db.CreateSession(MailboxProtocol.Digest(Encoding.UTF8.GetBytes(legacyToken)),4,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
  }
  string certificatePath=Path.Combine(root,"test-server.pfx"),pin;
  using(var key=System.Security.Cryptography.RSA.Create(2048)){
   var request=new System.Security.Cryptography.X509Certificates.CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
   using(var certificate=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1))){File.WriteAllBytes(certificatePath,certificate.Export(System.Security.Cryptography.X509Certificates.X509ContentType.Pfx,"test-only"));pin=MailboxProtocol.Digest(certificate.RawData);}
  }
  var reservation=new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0);reservation.Start();int port=((System.Net.IPEndPoint)reservation.LocalEndpoint).Port;reservation.Stop();
  var config=new ServerConfiguration{DataDirectory=root,DatabasePath=database,LogDirectory=Path.Combine(root,"logs"),ListenAddress="127.0.0.1",Port=port,CertificatePath=certificatePath,CertificatePassword="test-only",ManagementPipe="",AttachmentCache=options};config.Validate();
  Action run,stop;
  if(shippingAssembly==null){var server=new DiscoveryServer(config);run=server.Run;stop=server.RequestStop;}
  else{
   // Load the independently built shipping assembly: its production readiness
   // constant must remain closed even when configuration explicitly opts in.
   var assembly=System.Reflection.Assembly.LoadFrom(Path.GetFullPath(shippingAssembly));var configType=assembly.GetType("PeerChat.ServerShared.ServerConfiguration",true);var shippingConfig=Activator.CreateInstance(configType);
   foreach(var field in config.GetType().GetFields()){
    var target=configType.GetField(field.Name);if(target==null)continue;var value=field.GetValue(config);
    if(field.Name=="AttachmentCache"){var nested=Activator.CreateInstance(target.FieldType);foreach(var setting in options.GetType().GetFields())target.FieldType.GetField(setting.Name).SetValue(nested,setting.GetValue(options));target.SetValue(shippingConfig,nested);}
    else if(value==null||target.FieldType.IsAssignableFrom(value.GetType()))target.SetValue(shippingConfig,value);
   }
   var serverType=assembly.GetType("PeerChat.Server.DiscoveryServer",true);var server=Activator.CreateInstance(serverType,new object[]{shippingConfig,null});
   run=()=>serverType.GetMethod("Run").Invoke(server,null);stop=()=>serverType.GetMethod("RequestStop",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).Invoke(server,null);
  }
  Exception failure=null;var worker=new Thread(()=>{try{run();}catch(Exception error){failure=error;}}){IsBackground=true};worker.Start();
  try{
   var until=Stopwatch.StartNew();bool ready=false;while(until.ElapsedMilliseconds<10000&&!ready){try{using(var probe=new System.Net.Sockets.TcpClient()){probe.Connect("localhost",port);ready=true;}}catch(System.Net.Sockets.SocketException){Thread.Sleep(20);}}Check(ready,"Network server startup");
   using(var client=new RpcClient(port,pin,token,actor)){
    if(!enabled||shippingAssembly!=null){client.Request("cache_status",enabled?"cache_feature_gated":"cache_disabled","id",new string('1',32));Check(!File.Exists(Path.Combine(options.Directory,"cache.sqlite")),"Disabled cache created storage");}
    else{
     var bytes=Payload(3*1024*1024);for(int n=0;n<65536;n++)bytes[n]=251;var descriptor=Descriptor(signer,3*1024*1024,bytes,group:proof.ServerGroupId,epoch:3,account:4);
     client.Request("cache_admit","accepted","descriptor",Convert.ToBase64String(descriptor.Wire));string lease=(string)client.Request("cache_acquire","accepted","id",descriptor.Id,"upload",true)["lease"];
     // Stop the first RPC owner mid-upload; a fresh authenticated stream must
     // recover exactly the durably committed offset, without replacing session.
     Check(Convert.ToInt64(client.Request("cache_append","accepted","lease",lease,"offset",0L,"data",AttachmentCacheRequests.Encode(bytes.Take(65536).ToArray()))["durableOffset"])==65536,"Network first offset");
     client.Dispose();using(var resumed=new RpcClient(port,pin,token,actor)){
      Check(Convert.ToInt64(resumed.Request("cache_status","accepted","id",descriptor.Id)["durableOffset"])==65536,"Network reconnect lost prefix");
      var started=Stopwatch.StartNew();long offset=65536;
      while(offset<bytes.Length){int count=(int)Math.Min(65536,bytes.Length-offset);offset=Convert.ToInt64(resumed.Request("cache_append","accepted","lease",lease,"offset",offset,"data",AttachmentCacheRequests.Encode(bytes.Skip((int)offset).Take(count).ToArray()))["durableOffset"]);}
      resumed.Request("cache_complete","accepted","lease",lease);resumed.Request("cache_release","accepted","lease",lease,"upload",true);
      Console.WriteLine("MEASURE host loopback pinned-TLS cache RPC ciphertext_bytes="+(bytes.Length-65536)+" elapsed_ms="+started.ElapsedMilliseconds+"; includes JSON/FULL SQLite, excludes source encryption and physical network");
      using(var other=new RpcClient(port,pin,otherToken,unapproved))other.Request("cache_acquire","cache_group_not_authorized","id",descriptor.Id,"upload",false);
      string download=(string)resumed.Request("cache_acquire","accepted","id",descriptor.Id,"upload",false)["lease"];
      var read=resumed.Request("cache_read","accepted","lease",download,"offset",0L,"maximum",65536L);Check(AttachmentCacheRequests.Decode((string)read["data"]).SequenceEqual(bytes.Take(65536)),"Network ciphertext changed");resumed.Request("cache_release","accepted","lease",download,"upload",false);
      resumed.Request("cache_publish","accepted","id",descriptor.Id,"envelope",AttachmentCacheRequests.Encode(Announcement(signer,descriptor)));
      var discovery=resumed.Request("cache_recipient_keys","accepted","id",descriptor.Id);Check((string)discovery["dataEncoding"]=="base64url","Key binding encoding missing");var eligible=(System.Collections.IList)discovery["recipients"];var recipientKey=eligible.Cast<Dictionary<string,object>>().Single(x=>(string)x["device"]==readerDevice);Check((string)recipientKey["keyState"]=="available"&&AttachmentCacheRequests.Decode((string)recipientKey["binding"],2048).SequenceEqual(readerBinding),"Network recipient key unavailable or changed");
      var expiry=Convert.ToInt64(resumed.Request("cache_status","accepted","id",descriptor.Id)["expiresUtc"])*1000;
      var keyWire=KeyWire(signer,descriptor,readerBinding,(string)recipientKey["sourceMembership"],(string)recipientKey["membership"],DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),expiry);
      resumed.Request("cache_publish_key","accepted","id",descriptor.Id,"envelope",AttachmentCacheRequests.Encode(keyWire));
      var signedOut=resumed.Request("logout",null);Check(signedOut.ContainsKey("ok")&&(bool)signedOut["ok"],"Source logout failed");
     }
     // Sender is signed out and every sender TCP stream is closed. A distinct
     // eligible device fetches its own pending envelope and durable ciphertext.
     string recipientLease;using(var recipient=new RpcClient(port,pin,readerToken,readerDevice)){
      var pendingKeys=(System.Collections.IList)recipient.Request("cache_pending_keys","accepted","after","")["items"];Check(pendingKeys.Count==1,"Source-offline attachment key lost");var keyItem=(Dictionary<string,object>)pendingKeys[0];Check((string)keyItem["id"]==descriptor.Id&&(string)keyItem["device"]==readerDevice,"Network key misrouted");
      var retainedKey=(Dictionary<string,object>)recipient.Request("cache_key","accepted","id",descriptor.Id)["keyEnvelope"];Check((string)retainedKey["digest"]==(string)keyItem["digest"],"Network key digest changed");
      Check((bool)recipient.Request("cache_key_ack","accepted","id",descriptor.Id,"digest",keyItem["digest"])["newKeyReceipt"]&&!(bool)recipient.Request("cache_key_ack","accepted","id",descriptor.Id,"digest",keyItem["digest"])["newKeyReceipt"],"Network key ACK not idempotent");
      var pending=recipient.Request("cache_pending","accepted","after","");var items=(System.Collections.IList)pending["items"];Check(items.Count==1,"Offline recipient lost announcement");var announcement=(Dictionary<string,object>)items[0];Check((string)announcement["id"]==descriptor.Id,"Offline announcement misrouted");
      recipient.Request("cache_announcement_ack","accepted","id",descriptor.Id,"digest",announcement["digest"]);recipientLease=(string)recipient.Request("cache_acquire","accepted","id",descriptor.Id,"upload",false)["lease"];
     }
     long received=0;string output=Path.Combine(root,"network-received.bin");using(var file=new FileStream(output,FileMode.CreateNew,FileAccess.Write,FileShare.None)){
      while(received<bytes.Length)using(var recipient=new RpcClient(port,pin,readerToken,readerDevice)){
       for(int n=0;n<24&&received<bytes.Length;n++){var response=recipient.Request("cache_read","accepted","lease",recipientLease,"offset",received,"maximum",65536L);var data=AttachmentCacheRequests.Decode((string)response["data"]);file.Write(data,0,data.Length);file.Flush(true);received+=data.Length;recipient.Request("cache_progress","accepted","lease",recipientLease,"offset",received);}
      }
     }
     using(var recipient=new RpcClient(port,pin,readerToken,readerDevice)){recipient.Request("cache_release","accepted","lease",recipientLease,"upload",false);Check(((System.Collections.IList)recipient.Request("cache_pending","accepted","after","")["items"]).Count==0,"Recipient announcement ACK lost");Check(((System.Collections.IList)recipient.Request("cache_pending_keys","accepted","after","")["items"]).Count==0,"Recipient key ACK lost");}
     Check(File.ReadAllBytes(output).SequenceEqual(bytes),"Offline recipient durable ciphertext integrity");Console.WriteLine("PASS real TLS source logout, independent recipient envelope ACK and full durable ciphertext download across RPC reconnects; not plaintext completion");
    }
   }
   using(var legacy=new RpcClient(port,pin,legacyToken,actor))legacy.Request("cache_status","cache_device_session_required","id",new string('1',32));
   bool rejected=false;try{using(var wrong=new RpcClient(port,pin,otherToken,actor)){}}catch(Exception){rejected=true;}Check(rejected,"Bound session used another same-account device");
  }finally{stop();Check(worker.Join(12000),"Network cache shutdown blocked");if(failure!=null)throw failure;File.Delete(certificatePath);}
  Console.WriteLine("PASS real pinned-TLS authenticated cache RPC enabled="+enabled+"; distinct same-account device rejection and controlled shutdown; test-only server, production gate remains closed");
 }}
 static void CompletionScheduling(string root){Directory.CreateDirectory(root);using(var owner=new ECDsaCng(256)){
  var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(owner,new string('1',32),1)));string actor=proof.OwnerIdentity;
  using(var db=new AccountDatabase(Path.Combine(root,"completion-accounts.sqlite")))using(var cache=new AttachmentCacheRuntime(Options(Fixture(root,"completion-cache")),()=>now)){
   Check(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],actor)==1&&db.RegisterGroup(1,actor,proof)!=null,"Completion account/roster");var admission=new AttachmentCacheAdmission(db,cache);
   var bytes=Payload(1048576);var descriptor=Descriptor(owner,1048576,bytes,group:proof.ServerGroupId);admission.Admit(1,actor,()=>true,descriptor.Wire);string lease=admission.Acquire(1,actor,()=>true,descriptor.Id,true);long offset=0;while(offset<bytes.Length)offset=admission.Append(1,actor,()=>true,lease,offset,bytes.Skip((int)offset).Take(65536).ToArray());
   using(var entered=new ManualResetEventSlim())using(var release=new ManualResetEventSlim())using(var responded=new ManualResetEventSlim()){
    int paused=0;Exception failed=null;bool owns=false;
    AttachmentCacheRuntime.TestFault=stage=>{if(stage=="completion_hash_chunk"&&Interlocked.CompareExchange(ref paused,1,0)==0){entered.Set();if(!release.Wait(5000))throw new Exception("Completion test barrier expired");}};
    var completion=new Thread(()=>{try{admission.Complete(1,actor,()=>true,lease);}catch(Exception error){failed=error;}});completion.Start();
    var authentication=new Thread(()=>{owns=db.OwnsDevice(1,actor);responded.Set();});
    bool responsive=false;try{Check(entered.Wait(5000),"Completion hash barrier not reached");authentication.Start();responsive=responded.Wait(500);}
    finally{release.Set();Check(completion.Join(5000),"Completion did not drain");if(authentication.ThreadState!=System.Threading.ThreadState.Unstarted)Check(authentication.Join(5000),"Authentication did not drain");AttachmentCacheRuntime.TestFault=null;}
    if(failed!=null)throw failed;Check(responsive&&owns,"Ciphertext hashing blocked unrelated authenticated account lookup");Check(cache.Inspect(descriptor.Id).Complete,"Verified completion lost");
   }
  }
 }Console.WriteLine("PASS ciphertext completion does not hold the global account/roster lock during hashing");}
 static void CompletionRecheck(string root){Directory.CreateDirectory(root);using(var owner=new ECDsaCng(256))using(var member=new ECDsaCng(256)){
  var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(owner,new string('1',32),1)));string actor=proof.OwnerIdentity,reader=MailboxProtocol.Digest(member.Key.Export(CngKeyBlobFormat.EccPublicBlob));
  using(var db=new AccountDatabase(Path.Combine(root,"recheck-accounts.sqlite")))using(var cache=new AttachmentCacheRuntime(Options(Fixture(root,"recheck-cache")),()=>now)){
   Check(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],actor)==1&&db.CreateAccountWithDevice("member",new byte[16],new byte[32],reader)==2,"Completion recheck accounts");Check(db.RegisterGroup(1,actor,proof)!=null&&db.GroupInvite(proof.ServerGroupId,1,actor,2,reader,1)!=null&&db.GroupRespond(proof.ServerGroupId,2,reader,true,2)!=null,"Completion recheck roster");
   var admission=new AttachmentCacheAdmission(db,cache);var bytes=Payload(1048576);var descriptor=Descriptor(member,1048576,bytes,group:proof.ServerGroupId,account:2,epoch:3);admission.Admit(2,reader,()=>true,descriptor.Wire);string lease=admission.Acquire(2,reader,()=>true,descriptor.Id,true);long offset=0;while(offset<bytes.Length)offset=admission.Append(2,reader,()=>true,lease,offset,bytes.Skip((int)offset).Take(65536).ToArray());
   int changed=0;AttachmentCacheRuntime.TestFault=stage=>{if(stage=="completion_hash_chunk"&&Interlocked.CompareExchange(ref changed,1,0)==0)Check(db.GroupRemove(proof.ServerGroupId,1,actor,2,reader,3)!=null,"Revocation during hashing failed");};
   try{Reject(()=>admission.Complete(2,reader,()=>true,lease),"cache_group_not_authorized");}finally{AttachmentCacheRuntime.TestFault=null;}Check(!cache.Inspect(descriptor.Id).Complete,"Revoked sender committed completion");
   var own=Descriptor(owner,1048576,bytes,group:proof.ServerGroupId,epoch:4);admission.Admit(1,actor,()=>true,own.Wire);string ownerLease=admission.Acquire(1,actor,()=>true,own.Id,true);offset=0;while(offset<bytes.Length)offset=admission.Append(1,actor,()=>true,ownerLease,offset,bytes.Skip((int)offset).Take(65536).ToArray());
   bool current=true;AttachmentCacheRuntime.TestFault=stage=>{if(stage=="completion_hash_chunk")current=false;};try{Reject(()=>admission.Complete(1,actor,()=>current,ownerLease),"cache_session_invalid");}finally{AttachmentCacheRuntime.TestFault=null;}Check(!cache.Inspect(own.Id).Complete,"Superseded session committed completion");
   var verified=cache.VerifyCompletion(ownerLease);cache.Release(ownerLease);string replacement=cache.Acquire(own.Id,actor,true);Reject(()=>cache.CommitCompletion(replacement,verified),"cache_completion_evidence_mismatch");
   Check(!cache.Inspect(own.Id).Complete,"Stale lease evidence committed completion");admission.Complete(1,actor,()=>true,replacement);Check(cache.Inspect(own.Id).Complete,"Valid retry could not complete");
  }
 }Console.WriteLine("PASS completion rechecks real revocation, session invalidation and lease ownership after hashing; valid retry recovers without resetting bytes");}
 static void Announcements(string root){Directory.CreateDirectory(root);using(var owner=new ECDsaCng(256))using(var member=new ECDsaCng(256))using(var third=new ECDsaCng(256)){
  var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(owner,new string('1',32),1)));string actor=proof.OwnerIdentity,reader=MailboxProtocol.Digest(member.Key.Export(CngKeyBlobFormat.EccPublicBlob)),other=MailboxProtocol.Digest(third.Key.Export(CngKeyBlobFormat.EccPublicBlob));var options=Options(Fixture(root,"announcements-cache"));string database=Path.Combine(root,"announcements-accounts.sqlite"),acknowledged=null;var ids=new HashSet<string>();
  using(var db=new AccountDatabase(database))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   Check(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],actor)==1&&db.CreateAccountWithDevice("member",new byte[16],new byte[32],reader)==2&&db.CreateAccountWithDevice("third",new byte[16],new byte[32],other)==3,"Announcement accounts");Check(db.RegisterGroup(1,actor,proof)!=null&&db.GroupInvite(proof.ServerGroupId,1,actor,2,reader,1)!=null&&db.GroupRespond(proof.ServerGroupId,2,reader,true,2)!=null&&db.GroupInvite(proof.ServerGroupId,1,actor,3,other,3)!=null&&db.GroupRespond(proof.ServerGroupId,3,other,true,4)!=null,"Announcement roster");
   var admission=new AttachmentCacheAdmission(db,cache);var rpc=new AttachmentCacheRequests(admission);var bytes=Payload(1);
   for(int n=0;n<9;n++){var descriptor=Descriptor(owner,1,bytes,group:proof.ServerGroupId,epoch:5);ids.Add(descriptor.Id);admission.Admit(1,actor,()=>true,descriptor.Wire);var wire=Announcement(owner,descriptor,65536);Reject(()=>admission.Publish(1,actor,()=>true,descriptor.Id,wire),"cache_upload_incomplete");Upload(cache,descriptor,bytes);Rpc(rpc,1,actor,"cache_publish","accepted","id",descriptor.Id,"envelope",AttachmentCacheRequests.Encode(wire));Rpc(rpc,1,actor,"cache_publish","accepted","id",descriptor.Id,"envelope",AttachmentCacheRequests.Encode(wire));Reject(()=>admission.Publish(1,actor,()=>true,descriptor.Id,Announcement(owner,descriptor,1024)),"cache_conflicting_announcement");}
   string cursor="";var seen=new HashSet<string>();bool more;do{var page=Rpc(rpc,2,reader,"cache_pending","accepted","after",cursor);var items=(System.Collections.IList)page["items"];Check(items.Count==1,"Worst-case envelopes exceeded one bounded page");foreach(Dictionary<string,object> item in items){Check(seen.Add((string)item["id"]),"Announcement pagination repeated object");Check(AttachmentCacheRequests.Decode((string)item["envelope"],65790).Length==65790,"Announcement envelope truncated");}string next=(string)page["cursor"];Check(next!=cursor,"Announcement cursor stalled");cursor=next;more=(bool)page["more"];}while(more);Check(seen.SetEquals(ids),"Announcement pagination lost object");
   var first=admission.PendingAnnouncements(2,reader,()=>true,"").Items[0];acknowledged=first.Id;Rpc(rpc,2,reader,"cache_announcement_ack","cache_announcement_ack_mismatch","id",first.Id,"digest",new string('0',64));Check((bool)Rpc(rpc,2,reader,"cache_announcement_ack","accepted","id",first.Id,"digest",first.Digest)["newAnnouncementReceipt"],"First announcement receipt missing");Check(!(bool)Rpc(rpc,2,reader,"cache_announcement_ack","accepted","id",first.Id,"digest",first.Digest)["newAnnouncementReceipt"],"Duplicate announcement receipt inflated state");
   Check(cache.PendingAnnouncements(2,reader,"").Count==8&&cache.PendingAnnouncements(3,other,"").Count==9&&cache.RecipientOffset(first.Id,reader)==0,"Manifest ACK consumed another device or claimed file progress");
   AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit")throw new InvalidOperationException("cache_test_rollback");};try{Reject(()=>admission.AcknowledgeAnnouncement(3,other,()=>true,first.Id,first.Digest),"cache_test_rollback");}finally{AttachmentCacheRuntime.TestFault=null;}Check(cache.PendingAnnouncements(3,other,"").Count==9,"Uncommitted announcement ACK consumed data");
   Check(db.GroupRemove(proof.ServerGroupId,1,actor,2,reader,5)!=null,"Announcement revocation");Check(admission.PendingAnnouncements(2,reader,()=>true,"").Items.Count==0,"Revoked member received durable announcement");Check(db.GroupInvite(proof.ServerGroupId,1,actor,2,reader,6)!=null&&db.GroupRespond(proof.ServerGroupId,2,reader,true,7)!=null,"Announcement rejoin");Check(admission.PendingAnnouncements(2,reader,()=>true,"").Items.Count==0,"Rejoined member regained historical announcements");Check(cache.ChargedBytes<=options.MaximumBytes,"Announcement metadata escaped quota");
  }
  using(var db=new AccountDatabase(database))using(var cache=new AttachmentCacheRuntime(options,()=>now)){var admission=new AttachmentCacheAdmission(db,cache);Check(!cache.PendingAnnouncements(2,reader,"").Contains(acknowledged)&&cache.PendingAnnouncements(3,other,"").Count==9,"Restart lost independent announcement ACKs");Check(admission.Announcement(3,other,()=>true,acknowledged).Wire.Length==65790,"Eligible offline device lost encrypted announcement after restart");now+=11;Check(cache.PendingAnnouncements(3,other,"").Count==0,"Expired announcement remained downloadable");Reject(()=>admission.Announcement(3,other,()=>true,acknowledged),"cache_attachment_expired");cache.Cleanup(256);Reject(()=>cache.ReadAnnouncement(acknowledged),"cache_attachment_unavailable");Check(cache.ChargedBytes<=options.MaximumBytes,"Announcement cleanup quota mismatch");}
 }Console.WriteLine("PASS signed encrypted announcement binding, maximum-frame pagination, independent/idempotent durable ACKs, rollback/restart, revocation/rejoin and expiration; announcement ACK is not file completion");}
 static long CacheSql(string path,string mutation,string query){
  Check(Path.IsPathRooted(path)&&Path.GetFileName(path)=="cache.sqlite","Isolated owned cache path required");IntPtr db=IntPtr.Zero,statement=IntPtr.Zero;
  try{Check(Sqlite.sqlite3_open16(path,out db)==0,"Fixture cache open");Check(SqliteNative.prepare(db,"PRAGMA application_id",out statement)==0&&SqliteNative.step(statement)==100&&SqliteNative.columnLong(statement,0)==1346585409,"Fixture database is not a cache");SqliteNative.finalize(statement);statement=IntPtr.Zero;
   if(mutation!=null){IntPtr error;int result=Sqlite.sqlite3_exec(db,mutation,IntPtr.Zero,IntPtr.Zero,out error);if(error!=IntPtr.Zero)Sqlite.sqlite3_free(error);Check(result==0,"Fixture cache mutation failed (SQLite "+result+")");}
   Check(SqliteNative.prepare(db,query,out statement)==0&&SqliteNative.step(statement)==100,"Fixture cache query failed");return SqliteNative.columnLong(statement,0);
  }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);if(db!=IntPtr.Zero)Sqlite.sqlite3_close(db);}
 }
 static void LedgerRecovery(string root){using(var signer=new ECDsaCng(256)){
  var options=Options(Fixture(root,"ledger-cache"));var data=Payload(65536);var descriptor=Descriptor(signer,65536,data);string path=Path.Combine(options.Directory,"cache.sqlite");
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){cache.Admit(descriptor,Readers());string lease=cache.Acquire(descriptor.Id,descriptor.Sender,true);cache.Append(lease,0,data.Take(32768).ToArray());}
  // Simulate an owned pre-release cache without the new derived totals table.
  CacheSql(path,"DROP TABLE reservation_total;","SELECT count(*) FROM objects");
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){Check(cache.Inspect(descriptor.Id).Offset==32768,"Derived ledger reconstruction reset durable prefix");Upload(cache,descriptor,data);cache.Publish(descriptor.Id,Announcement(signer,descriptor));var value=cache.ReadAnnouncement(descriptor.Id);Check(cache.AcknowledgeAnnouncement(descriptor.Id,new string('b',64),value.Digest),"Ledger fixture receipt");Check(cache.ChargedBytes<=options.MaximumBytes,"Reconstructed quota escaped budget");}
  long expected=CacheSql(path,null,"SELECT amount FROM reservation_total WHERE id=1");Check(expected==8192,"Pending recipient metadata reservation was released prematurely");
  CacheSql(path,"UPDATE reservation_total SET amount=amount+1 WHERE id=1;","SELECT amount FROM reservation_total WHERE id=1");Reject(()=>{using(var cache=new AttachmentCacheRuntime(options,()=>now)){}} ,"cache_reservation_ledger_mismatch");
  Check(CacheSql(path,null,"SELECT amount FROM reservation_total WHERE id=1")==expected+1&&CacheSql(path,null,"SELECT durable_offset FROM objects WHERE id='"+descriptor.Id+"'")==descriptor.CipherBytes&&CacheSql(path,null,"SELECT count(*) FROM announcement_acks")==1,"Corrupt ledger rejection reset durable data/receipt");
 }Console.WriteLine("PASS additive derived-ledger reconstruction preserves partial upload; metadata remains reserved until recipient ACK; inconsistent existing totals fail closed without resetting ciphertext/receipts");}
 static void OwnershipBeforeSchema(string root){
  var options=Options(Fixture(root,"ownership-cache"));string path=Path.Combine(options.Directory,"cache.sqlite");using(var cache=new AttachmentCacheRuntime(options,()=>now)){}
  CacheSql(path,"DROP TABLE announcement_acks;DROP TABLE announcements;DROP TABLE reservation_total;","SELECT count(*) FROM objects");
  options.InstanceId=new string('b',32);File.WriteAllText(Path.Combine(options.Directory,AttachmentCacheStorage.Marker),options.InstanceId);var original=File.ReadAllBytes(path);
  Reject(()=>{using(var cache=new AttachmentCacheRuntime(options,()=>now)){}} ,"cache_instance_or_schema_mismatch");Check(File.ReadAllBytes(path).SequenceEqual(original),"Mismatched cache instance was modified before ownership validation");
  Console.WriteLine("PASS mismatched cache instance rejected before schema changes; original index bytes preserved");
 }
 static void NativeObject(string source,string target){
  var wire=File.ReadAllBytes(Path.Combine(source,"descriptor.bin"));var descriptor=AttachmentCacheDescriptor.Parse(wire);
  var cipher=File.ReadAllBytes(Path.Combine(source,"ciphertext",descriptor.Id+"-encrypted.bin"));Check(cipher.LongLength==descriptor.CipherBytes&&MailboxProtocol.Digest(cipher)==descriptor.CipherDigest,"Native descriptor ciphertext binding");
  var tampered=(byte[])wire.Clone();tampered[50]^=1;bool rejected=false;try{AttachmentCacheDescriptor.Parse(tampered);}catch(InvalidOperationException){rejected=true;}Check(rejected,"Tampered native descriptor accepted");
  var options=Options(Fixture(target,"native-cache"));var recipients=new List<CacheRecipient>{new CacheRecipient(descriptor.Account,descriptor.Sender,new string('1',32))};
  byte[] announcement=File.Exists(Path.Combine(source,"announcement.bin"))?File.ReadAllBytes(Path.Combine(source,"announcement.bin")):null;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){cache.Admit(descriptor,recipients);Upload(cache,descriptor,cipher,65536);if(announcement!=null)cache.Publish(descriptor.Id,announcement);}
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){if(announcement!=null)Check(cache.ReadAnnouncement(descriptor.Id).Wire.SequenceEqual(announcement),"Native group signature/announcement restart binding");var lease=cache.Acquire(descriptor.Id,descriptor.Sender,false);using(var output=File.Create(Path.Combine(source,"cache-return.bin"))){long offset=0;while(offset<descriptor.CipherBytes){var chunk=cache.Read(lease,offset,65536);output.Write(chunk,0,chunk.Length);offset+=chunk.Length;cache.RecipientProgress(lease,offset,false);}output.Flush(true);}cache.Release(lease);}
  Console.WriteLine("PASS native signed descriptor, ciphertext integrity, managed FULL cache upload/restart/read and signature tamper rejection; separate roster/TLS tests cover admission");
 }
 static int Main(string[] args){try{if(args.Length==2&&args[0]=="--append-diagnostics"){BlobEncoding();AppendDiagnostics(Path.GetFullPath(args[1]));return 0;}if(args.Length==2&&args[0]=="--keys"){KeyAdmission(args[1]);return 0;}if(args.Length==3&&args[0]=="--native-keys"){NativeKeys(args[1],args[2]);return 0;}if(args.Length==2&&args[0]=="--ownership"){OwnershipBeforeSchema(args[1]);return 0;}if(args.Length==2&&args[0]=="--ledger"){LedgerRecovery(args[1]);return 0;}if(args.Length==2&&args[0]=="--announcements"){Announcements(args[1]);return 0;}if(args.Length==2&&args[0]=="--completion"){CompletionScheduling(Path.Combine(args[1],"scheduling"));CompletionRecheck(Path.Combine(args[1],"recheck"));return 0;}if(args.Length>0&&args[0]=="--crash-child")return Child(args);if(args.Length==3&&args[0]=="--native-object"){NativeObject(args[1],args[2]);return 0;}if(args.Length==3&&args[0]=="--production-gate"){Network(args[1],true,args[2]);return 0;}if(args.Length==2&&args[0]=="--pressure"){Pressure(Path.GetFullPath(args[1]));return 0;}if(args.Length==2&&args[0]=="--admission"){Admission(Path.GetFullPath(args[1]));ConcurrentReservations(Path.GetFullPath(args[1]));return 0;}if(args.Length==2&&args[0]=="--network"){Network(Path.Combine(args[1],"disabled"),false);Network(Path.Combine(args[1],"enabled"),true);return 0;}string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);BlobEncoding();AppendDiagnostics(root);RoundTrips(root);CanceledUpload(root);LargeAdmission(root);BoundedCompletion(root);CorruptionAndExpiry(root);Pressure(root);Admission(root);ConcurrentReservations(root);Announcements(Path.Combine(root,"announcements"));LedgerRecovery(Path.Combine(root,"ledger"));OwnershipBeforeSchema(Path.Combine(root,"ownership"));CompletionScheduling(Path.Combine(root,"scheduling"));CompletionRecheck(Path.Combine(root,"recheck"));KeyAdmission(Path.Combine(root,"keys"));Crash(root,"upload");Crash(root,"batch-upload");Crash(root,"cleanup");Console.WriteLine("PASS cache runtime and authoritative admission; client integration remains gated");return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
