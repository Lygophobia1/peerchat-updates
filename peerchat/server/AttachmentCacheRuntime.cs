using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using PeerChat.ServerShared;

namespace PeerChat.Server {
// Bounded ciphertext storage endpoint. The client TransferManager remains the
// ACK/retry/window owner. No sockets, client plaintext or recipient keys here.
// Callers must additionally hold authoritative account/roster admission through
// each operation; a storage lease is never a replacement for authorization.
internal sealed partial class AttachmentCacheRuntime : IDisposable {
 internal const long OperationHeadroom=4L*1024*1024;
 readonly object gate=new object();readonly AttachmentCacheConfiguration options;
 readonly AttachmentCacheDirectory directory;readonly Func<long> clock;
 IntPtr database;bool disposed;
#if PEERCHAT_CACHE_TESTS
 internal static Action<string> TestFault;
#endif
 readonly Dictionary<string,AttachmentCacheDescriptor> verifiedDescriptors=new Dictionary<string,AttachmentCacheDescriptor>(StringComparer.Ordinal);
 internal sealed class Status {internal long Offset,Size,Expires;internal bool Complete;}
 static void Require(bool value,string reason){if(!value)throw new InvalidOperationException(reason);}
 static string Quote(string value){return "'"+value.Replace("'","''")+"'";}
 static string Blob(byte[] value){
  var timing=CacheAppendTimings.Current;long started=timing==null?0:System.Diagnostics.Stopwatch.GetTimestamp();
  try{
   // Only the supplied bounded field/chunk is encoded. Avoid one temporary
   // string per byte while preserving the exact lowercase SQLite literal.
   const string digits="0123456789abcdef";var text=new char[checked(value.Length*2+3)];text[0]='X';text[1]='\'';
   for(int i=0;i<value.Length;i++){text[2+i*2]=digits[value[i]>>4];text[3+i*2]=digits[value[i]&15];}
   text[text.Length-1]='\'';return new string(text);
  }finally{if(timing!=null)timing.RecordEncode(started);}
 }
 static long Number(string value){long n;Require(Int64.TryParse(value,NumberStyles.Integer,CultureInfo.InvariantCulture,out n),"cache_index_invalid");return n;}
 void Execute(string sql){IntPtr error;if(Sqlite.sqlite3_exec(database,sql,IntPtr.Zero,IntPtr.Zero,out error)!=0){if(error!=IntPtr.Zero)Sqlite.sqlite3_free(error);throw new InvalidOperationException("cache_transaction_failed");}}
 List<string[]> Rows(string sql,int limit,int columns=1){var result=new List<string[]>();IntPtr statement=IntPtr.Zero;try{
  Require(SqliteNative.prepare(database,sql,out statement)==0,"cache_index_read_failed");int code;
  while((code=SqliteNative.step(statement))==100){Require(result.Count<limit,"cache_index_page_exceeded");var row=new string[columns];for(int i=0;i<columns;i++)row[i]=SqliteNative.columnText(statement,i);result.Add(row);}Require(code==101,"cache_index_read_failed");return result;
 }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}}
 long Scalar(string sql){var rows=Rows(sql,1);Require(rows.Count==1,"cache_index_scalar_missing");return Number(rows[0][0]);}
 byte[] Binary(string sql){IntPtr statement=IntPtr.Zero;try{Require(SqliteNative.prepare(database,sql,out statement)==0,"cache_index_read_failed");if(SqliteNative.step(statement)!=100)return null;return SqliteNative.columnBlob(statement,0);}finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}}
 void Transaction(Action action){Execute("BEGIN IMMEDIATE;");try{action();
#if PEERCHAT_CACHE_TESTS
  if(TestFault!=null)TestFault("before_commit");
#endif
  Execute("COMMIT;");
#if PEERCHAT_CACHE_TESTS
  if(TestFault!=null)TestFault("after_commit");
#endif
 }catch{try{Execute("ROLLBACK;");}catch{}throw;}}
 long Now(){long now=clock();Require(now>0&&now<=253402300799L,"cache_clock_invalid");return now;}
 void Check(){Require(!disposed,"cache_closed");directory.Check();}
 const string ReservationExpression="CASE WHEN complete=0 THEN MAX(0,budget-durable_offset-(durable_offset/32768)*8192) ELSE CASE WHEN NOT EXISTS(SELECT 1 FROM announcements a WHERE a.object_id=o.id) THEN 262144 ELSE 0 END+(SELECT count(*) FROM recipients r WHERE r.object_id=o.id AND NOT EXISTS(SELECT 1 FROM announcement_acks a WHERE a.object_id=r.object_id AND a.device=r.device))*8192 END";
 long CalculatedReservations(){return Scalar("SELECT COALESCE(SUM("+ReservationExpression+"),0) FROM objects o");}
 long ObjectReservation(string id){return Scalar("SELECT COALESCE((SELECT "+ReservationExpression+" FROM objects o WHERE id="+Quote(id)+"),0)");}
 long Reserved(){return Scalar("SELECT amount FROM reservation_total WHERE id=1");}
 void ValidateInstance(){Require(Scalar("PRAGMA application_id")==1346585409&&Scalar("PRAGMA page_size")==4096&&Scalar("SELECT count(*) FROM settings WHERE version=1 AND instance="+Quote(options.InstanceId))==1&&Scalar("SELECT count(*) FROM settings")==1,"cache_instance_or_schema_mismatch");}
 void InitializeReservationLedger(){
  Execute("CREATE TABLE IF NOT EXISTS reservation_total(id INTEGER PRIMARY KEY CHECK(id=1),amount INTEGER NOT NULL CHECK(typeof(amount)='integer' AND amount>=0));");
  // Additive initialization for an owned pre-release cache. Reconstruct only a
  // missing derived ledger, never identities, recipients or durable offsets.
  if(Scalar("SELECT count(*) FROM reservation_total")==0)Transaction(()=>Execute("INSERT INTO reservation_total VALUES(1,"+CalculatedReservations()+");"));
  Require(Scalar("SELECT count(*) FROM reservation_total")==1&&Reserved()>=0,"cache_reservation_ledger_invalid");
 }
 void ReservedMutation(string id,Action action){
  long previous=ObjectReservation(id);
  Transaction(()=>{action();long amount=checked(Reserved()+checked(ObjectReservation(id)-previous));Require(amount>=0,"cache_reservation_ledger_invalid");Execute("UPDATE reservation_total SET amount="+amount+" WHERE id=1;");});
 }
 void Space(long extra){var storage=directory.Evidence();long actual=directory.AllocatedBytes();long reserved=Reserved();long percent=checked(storage.FilesystemBytes/100*options.MinimumFreePercent+(storage.FilesystemBytes%100*options.MinimumFreePercent+99)/100);long reserve=Math.Max(options.MinimumFreeBytes,percent);
  long needed=checked(reserved+OperationHeadroom+extra);Require(actual<=options.MaximumBytes&&needed<=options.MaximumBytes-actual&&storage.AvailableBytes>=reserve&&needed<=storage.AvailableBytes-reserve,"cache_quota_or_free_space_protected");
 }
 internal AttachmentCacheRuntime(AttachmentCacheConfiguration configuration,Func<long> time){
  configuration.Validate();options=new AttachmentCacheConfiguration{MaximumBytes=configuration.MaximumBytes,RetentionSeconds=configuration.RetentionSeconds,PartialIdleSeconds=configuration.PartialIdleSeconds,MinimumFreeBytes=configuration.MinimumFreeBytes,MinimumFreePercent=configuration.MinimumFreePercent,Directory=configuration.Directory,ExpectedFilesystem=configuration.ExpectedFilesystem,ExpectedMountPoint=configuration.ExpectedMountPoint,InstanceId=configuration.InstanceId};clock=time;directory=new AttachmentCacheDirectory(options);
  try{
   string path=Path.Combine(directory.Path,"cache.sqlite");bool existing=File.Exists(path);
   if(existing){var header=new byte[100];using(var file=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.ReadWrite)){Require(file.Read(header,0,header.Length)==header.Length,"cache_index_header_invalid");}Require(System.Text.Encoding.ASCII.GetString(header,0,16)=="SQLite format 3\0"&&header[68]==80&&header[69]==67&&header[70]==67&&header[71]==65,"cache_index_not_owned");}
   long allocated=directory.AllocatedBytes();var storage=directory.Evidence();long percent=checked(storage.FilesystemBytes/100*options.MinimumFreePercent+(storage.FilesystemBytes%100*options.MinimumFreePercent+99)/100);Require((existing||allocated<=options.MaximumBytes&&OperationHeadroom<=options.MaximumBytes-allocated)&&storage.AvailableBytes>=Math.Max(options.MinimumFreeBytes,percent)+OperationHeadroom,"cache_startup_space_protected");
   Require(Sqlite.sqlite3_open16(path,out database)==0,"cache_index_open_failed");
   // Validate an existing owner's immutable binding before PRAGMA/schema
   // updates. A matching filesystem marker alone cannot authorize another
   // cache index's additive initialization.
   if(existing)ValidateInstance();
   Execute("PRAGMA busy_timeout=5000;PRAGMA foreign_keys=ON;PRAGMA temp_store=MEMORY;PRAGMA cache_size=-2048;PRAGMA journal_mode=TRUNCATE;PRAGMA synchronous=FULL;PRAGMA locking_mode=EXCLUSIVE;");
   if(!existing)Execute("PRAGMA page_size=4096;PRAGMA auto_vacuum=INCREMENTAL;PRAGMA application_id=1346585409;");
   if(existing){var checkedRows=Rows("PRAGMA quick_check",1);Require(checkedRows.Count==1&&checkedRows[0][0]=="ok","cache_index_corrupt");}
   Execute("CREATE TABLE IF NOT EXISTS settings(version INTEGER NOT NULL,instance TEXT NOT NULL);"+
    "CREATE TABLE IF NOT EXISTS objects(id TEXT PRIMARY KEY,descriptor BLOB NOT NULL,digest TEXT NOT NULL,group_id TEXT NOT NULL,account INTEGER NOT NULL,sender TEXT NOT NULL,epoch INTEGER NOT NULL,size INTEGER NOT NULL,durable_offset INTEGER NOT NULL DEFAULT 0,budget INTEGER NOT NULL,complete INTEGER NOT NULL DEFAULT 0,completed INTEGER NOT NULL DEFAULT 0,expires INTEGER NOT NULL,deleting INTEGER NOT NULL DEFAULT 0);"+
    "CREATE INDEX IF NOT EXISTS objects_cleanup ON objects(deleting,expires,completed);"+
    "CREATE TABLE IF NOT EXISTS recipients(object_id TEXT NOT NULL REFERENCES objects(id),account INTEGER NOT NULL,device TEXT NOT NULL,membership TEXT NOT NULL,durable_offset INTEGER NOT NULL DEFAULT 0,verified INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(object_id,device));"+
    "CREATE TABLE IF NOT EXISTS chunks(object_id TEXT NOT NULL REFERENCES objects(id),offset INTEGER NOT NULL,data BLOB NOT NULL CHECK(length(data)>0 AND length(data)<=65536),digest TEXT NOT NULL CHECK(length(digest)=64),PRIMARY KEY(object_id,offset));"+
    "CREATE TABLE IF NOT EXISTS leases(id TEXT PRIMARY KEY,object_id TEXT NOT NULL REFERENCES objects(id),device TEXT NOT NULL,upload INTEGER NOT NULL,expires INTEGER NOT NULL);"+
    "CREATE TABLE IF NOT EXISTS announcements(object_id TEXT PRIMARY KEY REFERENCES objects(id),wire BLOB NOT NULL,digest TEXT NOT NULL);"+
    "CREATE TABLE IF NOT EXISTS announcement_acks(object_id TEXT NOT NULL REFERENCES objects(id),device TEXT NOT NULL,digest TEXT NOT NULL,PRIMARY KEY(object_id,device));");
   if(!existing)Execute("INSERT INTO settings VALUES(1,"+Quote(options.InstanceId)+");");
   ValidateInstance();InitializeKeyEnvelopes();InitializeContexts();InitializeVerificationReceipts();
   // Exclusive owner has been acquired. Every old process lease is now stale;
   // durable byte offsets/reservations remain authoritative and are NOT reset.
   InitializeReservationLedger();Transaction(()=>Execute("DELETE FROM leases;"));Reconcile();
  }catch{if(database!=IntPtr.Zero){Sqlite.sqlite3_close(database);database=IntPtr.Zero;}directory.Dispose();throw;}
 }
 void Reconcile(){ReconcileKeyEnvelopes();Require(Scalar("SELECT count(*) FROM objects WHERE size<36 OR durable_offset<0 OR durable_offset>size OR (deleting=0 AND budget<size) OR complete NOT IN (0,1) OR deleting NOT IN (0,1) OR (complete=1 AND durable_offset<>size)")==0,"cache_index_bounds_invalid");
  Require(Reserved()==CalculatedReservations(),"cache_reservation_ledger_mismatch");
  Require(Scalar("SELECT count(*) FROM announcements a JOIN objects o ON o.id=a.object_id WHERE o.complete<>1 OR length(a.wire)<254 OR length(a.wire)>65790 OR length(a.digest)<>64")==0,"cache_announcement_index_invalid");
  Require(Scalar("SELECT count(*) FROM announcement_acks a WHERE NOT EXISTS(SELECT 1 FROM announcements n WHERE n.object_id=a.object_id AND n.digest=a.digest) OR NOT EXISTS(SELECT 1 FROM recipients r WHERE r.object_id=a.object_id AND r.device=a.device)")==0,"cache_announcement_receipt_invalid");
  Require(Scalar("SELECT count(*) FROM objects o WHERE durable_offset<>COALESCE((SELECT SUM(length(data)) FROM chunks c WHERE c.object_id=o.id),0) AND deleting=0")==0,"cache_chunk_prefix_invalid");
  string after="";for(;;){var objects=Rows("SELECT id,durable_offset FROM objects WHERE deleting=0 AND id>"+Quote(after)+" ORDER BY id LIMIT 128",128,2);if(objects.Count==0)break;
   foreach(var row in objects){long expected=0;for(;;){var chunks=Rows("SELECT offset,length(data) FROM chunks WHERE object_id="+Quote(row[0])+" AND offset>="+expected+" ORDER BY offset LIMIT 128",128,2);if(chunks.Count==0)break;foreach(var chunk in chunks){Require(Number(chunk[0])==expected&&Number(chunk[1])>0&&Number(chunk[1])<=65536,"cache_chunk_order_invalid");expected=checked(expected+Number(chunk[1]));}}Require(expected==Number(row[1]),"cache_chunk_prefix_invalid");}after=objects[objects.Count-1][0];
  }
 }
 AttachmentCacheDescriptor Descriptor(string id){Require(MailboxProtocol.Id(id,32),"cache_id_invalid");AttachmentCacheDescriptor found;if(verifiedDescriptors.TryGetValue(id,out found)){Require(Scalar("SELECT count(*) FROM objects WHERE id="+Quote(id)+" AND deleting=0")==1,"cache_attachment_unavailable");return found;}var bytes=Binary("SELECT descriptor FROM objects WHERE id="+Quote(id)+" AND deleting=0");Require(bytes!=null,"cache_attachment_unavailable");found=AttachmentCacheDescriptor.Parse(bytes);if(verifiedDescriptors.Count>=128)verifiedDescriptors.Remove(verifiedDescriptors.Keys.First());verifiedDescriptors[id]=found;return found;}
 internal AttachmentCacheDescriptor Describe(string id){lock(gate){Check();return Descriptor(id);}}
 internal string OriginalMembership(string id,long account,string device){lock(gate){Check();Descriptor(id);Require(account>0&&MailboxProtocol.Id(device,64),"cache_recipient_invalid");var row=Rows("SELECT membership FROM recipients WHERE object_id="+Quote(id)+" AND account="+account+" AND device="+Quote(device),1);return row.Count==0?null:row[0][0];}}
 internal string LeaseObject(string lease,string device,bool upload){lock(gate){Check();string id=Lease(lease,upload);Require(MailboxProtocol.Id(device,64)&&Scalar("SELECT count(*) FROM leases WHERE id="+Quote(lease)+" AND device="+Quote(device))==1,"cache_lease_device_mismatch");return id;}}
 internal Status Inspect(string id){lock(gate){Check();var d=Descriptor(id);return new Status{Size=d.CipherBytes,Offset=Scalar("SELECT durable_offset FROM objects WHERE id="+Quote(id)),Complete=Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1,Expires=Scalar("SELECT expires FROM objects WHERE id="+Quote(id))};}}
 internal Status Admit(AttachmentCacheDescriptor descriptor,IList<CacheRecipient> recipients,byte[] context=null){lock(gate){Check();Require(descriptor!=null&&recipients!=null&&recipients.Count>0&&recipients.Count<=64&&recipients.Select(x=>x.Device).Distinct(StringComparer.Ordinal).Count()==recipients.Count,"cache_recipient_set_invalid");
  var old=Binary("SELECT descriptor FROM objects WHERE id="+Quote(descriptor.Id));if(old!=null){Require(old.SequenceEqual(descriptor.Wire),"cache_conflicting_descriptor");Require(Scalar("SELECT deleting FROM objects WHERE id="+Quote(descriptor.Id))==0,"cache_attachment_unavailable");return Inspect(descriptor.Id);}
  long now=Now(),budget=checked(descriptor.CipherBytes+((descriptor.CipherBytes+32767)/32768)*8192+1024*1024+recipients.Count*4096);CleanupLocked(8,false);
  try{Space(budget);}catch(InvalidOperationException){CleanupLocked(64,true,budget);Space(budget);}
  verifiedDescriptors.Remove(descriptor.Id);ReservedMutation(descriptor.Id,()=>{Execute("INSERT INTO objects(id,descriptor,digest,group_id,account,sender,epoch,size,budget,expires) VALUES("+Quote(descriptor.Id)+","+Blob(descriptor.Wire)+","+Quote(descriptor.CipherDigest)+","+Quote(descriptor.Group)+","+descriptor.Account+","+Quote(descriptor.Sender)+","+descriptor.Epoch+","+descriptor.CipherBytes+","+budget+","+checked(now+options.PartialIdleSeconds)+");");foreach(var r in recipients)Execute("INSERT INTO recipients(object_id,account,device,membership) VALUES("+Quote(descriptor.Id)+","+r.Account+","+Quote(r.Device)+","+Quote(r.Membership)+");");InsertContext(descriptor.Id,context);});return Inspect(descriptor.Id);
 }}
 internal string Acquire(string id,string device,bool upload,long seconds=120){lock(gate){Check();Require(MailboxProtocol.Id(device,64)&&seconds>0&&seconds<=300,"cache_lease_invalid");var d=Descriptor(id);long now=Now();Require(Scalar("SELECT expires FROM objects WHERE id="+Quote(id))>now,"cache_attachment_expired");
  Require(upload?d.Sender==device:Scalar("SELECT count(*) FROM recipients WHERE object_id="+Quote(id)+" AND device="+Quote(device))==1,"cache_original_recipient_required");
  Require(upload||Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1,"cache_upload_incomplete");
  Execute("DELETE FROM leases WHERE expires<="+now+";");Require(Scalar("SELECT count(*) FROM leases")<256,"cache_lease_capacity");
  if(upload)Require(Scalar("SELECT count(*) FROM leases WHERE object_id="+Quote(id)+" AND upload=1")==0,"cache_upload_already_owned");
  string lease=Guid.NewGuid().ToString("N");Space(0);Execute("INSERT INTO leases VALUES("+Quote(lease)+","+Quote(id)+","+Quote(device)+","+(upload?1:0)+","+checked(now+seconds)+");");return lease;
 }}
 string Lease(string lease,bool upload){Require(MailboxProtocol.Id(lease,32),"cache_lease_invalid");var rows=Rows("SELECT object_id FROM leases WHERE id="+Quote(lease)+" AND upload="+(upload?1:0)+" AND expires>"+Now(),1);Require(rows.Count==1,"cache_lease_expired");string id=rows[0][0];Descriptor(id);return id;}
 internal void Release(string lease,bool abandonUpload=false){lock(gate){Check();Require(MailboxProtocol.Id(lease,32),"cache_lease_invalid");
  if(abandonUpload){string id=Lease(lease,true);
   // Completed immutable objects can already have recipients. Canceling the
   // sender's local action must not revoke another device's retained file.
   if(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==0){
    var held=Rows("SELECT id FROM leases WHERE object_id="+Quote(id),256);
    // Release only future reservation. Existing SQLite pages, WAL and chunks
    // remain physically charged until bounded cleanup actually reclaims them.
    ReservedMutation(id,()=>Execute("DELETE FROM leases WHERE object_id="+Quote(id)+";UPDATE objects SET deleting=1,budget=durable_offset+(durable_offset/32768)*8192 WHERE id="+Quote(id)+";"));
    foreach(var row in held)ForgetCompletion(row[0]);CleanupLocked(1,false);return;
   }
  }
  Execute("DELETE FROM leases WHERE id="+Quote(lease));ForgetCompletion(lease);
 }}
 internal long Append(string lease,long offset,byte[] bytes){lock(gate){Check();Require(bytes!=null&&bytes.Length>0&&bytes.Length<=65536&&offset>=0,"cache_chunk_invalid");string id=Lease(lease,true);var d=Descriptor(id);long durable=Scalar("SELECT durable_offset FROM objects WHERE id="+Quote(id));
  Require(offset<=durable&&offset<=d.CipherBytes&&bytes.Length<=d.CipherBytes-offset&&offset%32768==0&&(bytes.Length==32768||bytes.Length==65536||offset+bytes.Length==d.CipherBytes),"cache_chunk_out_of_order");
  if(offset<durable){var old=Binary("SELECT data FROM chunks WHERE object_id="+Quote(id)+" AND offset="+offset);Require(old!=null&&old.SequenceEqual(bytes),"cache_conflicting_chunk");return durable;}
  Require(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==0,"cache_immutable_object");Space(0);long next=checked(offset+bytes.Length),now=Now();
  var timing=CacheAppendTimings.Current;long started=timing==null?0:System.Diagnostics.Stopwatch.GetTimestamp();
  try{ReservedMutation(id,()=>{Execute("INSERT INTO chunks VALUES("+Quote(id)+","+offset+","+Blob(bytes)+","+Quote(MailboxProtocol.Digest(bytes))+");");Execute("UPDATE objects SET durable_offset="+next+",expires="+checked(now+options.PartialIdleSeconds)+" WHERE id="+Quote(id)+";UPDATE leases SET expires="+checked(now+120)+" WHERE id="+Quote(lease)+";");});}
  finally{if(timing!=null)timing.RecordTransaction(started);}return next;
 }}
 // A bounded RPC window is one durable acknowledgement unit. Keep individual
 // chunk rows so legacy reads, completion verification and recovery are unchanged.
 internal long[] AppendBatch(string lease,IList<KeyValuePair<long,byte[]>> chunks){lock(gate){
  Check();Require(chunks!=null&&chunks.Count>=2&&chunks.Count<=8,"cache_chunk_invalid");
  // The existing admission reserves the entire ciphertext plus per-chunk
  // overhead. Retain at least four times the maximum window for the rollback
  // journal, SQLite page rounding and the final ledger/object updates.
  Require(OperationHeadroom>=8L*65536*4,"cache_quota_or_free_space_protected");
  string id=Lease(lease,true);var d=Descriptor(id);
  long durable=Scalar("SELECT durable_offset FROM objects WHERE id="+Quote(id));
  long cursor=chunks[0].Key,original=durable;var offsets=new long[chunks.Count];
  Require(cursor>=0&&cursor<=durable,"cache_chunk_out_of_order");
  for(int i=0;i<chunks.Count;i++){
   long offset=chunks[i].Key;byte[] bytes=chunks[i].Value;
   Require(bytes!=null&&bytes.Length>0&&bytes.Length<=65536,"cache_chunk_invalid");
   Require(offset==cursor&&offset<=d.CipherBytes&&bytes.Length<=d.CipherBytes-offset&&offset%32768==0&&
    (bytes.Length==32768||bytes.Length==65536||offset+bytes.Length==d.CipherBytes),"cache_chunk_out_of_order");
   long next=checked(offset+bytes.Length);offsets[i]=next;cursor=next;
   if(offset<original){Require(next<=original,"cache_conflicting_chunk");var old=Binary("SELECT data FROM chunks WHERE object_id="+Quote(id)+" AND offset="+offset);Require(old!=null&&old.SequenceEqual(bytes),"cache_conflicting_chunk");}
  }
  if(cursor<=original)return offsets;
  Require(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==0,"cache_immutable_object");
  Space(0);long now=Now();var timing=CacheAppendTimings.Current;long started=timing==null?0:System.Diagnostics.Stopwatch.GetTimestamp();
  try{ReservedMutation(id,()=>{
   foreach(var chunk in chunks)if(chunk.Key>=original)Execute("INSERT INTO chunks VALUES("+Quote(id)+","+chunk.Key+","+Blob(chunk.Value)+","+Quote(MailboxProtocol.Digest(chunk.Value))+");");
   Execute("UPDATE objects SET durable_offset="+cursor+",expires="+checked(now+options.PartialIdleSeconds)+" WHERE id="+Quote(id)+";UPDATE leases SET expires="+checked(now+120)+" WHERE id="+Quote(lease)+";");
  });}finally{if(timing!=null)timing.RecordTransaction(started);}return offsets;
 }}
 internal sealed class CompletionEvidence {
  internal readonly string Lease,Id,Digest;internal readonly long Size;
  internal CompletionEvidence(string lease,AttachmentCacheDescriptor descriptor){Lease=lease;Id=descriptor.Id;Digest=descriptor.CipherDigest;Size=descriptor.CipherBytes;}
 }
 sealed class CompletionProgress {internal string Id,Digest;internal long Size,Offset;internal bool Busy;internal SHA256 Hash=SHA256.Create();}
 readonly Dictionary<string,CompletionProgress> completionProgress=new Dictionary<string,CompletionProgress>();
 void ForgetCompletion(string lease){CompletionProgress item;if(completionProgress.TryGetValue(lease,out item)){completionProgress.Remove(lease);if(!item.Busy)item.Hash.Dispose();}}
 // Optional bounded finalization for updated clients. State is advisory only:
 // restart/lease replacement rehashes the immutable ciphertext from zero.
 internal CompletionEvidence VerifyCompletionStep(string lease){
  AttachmentCacheDescriptor descriptor;CompletionProgress progress;
  lock(gate){Check();string id=Lease(lease,true);descriptor=Descriptor(id);Require(Scalar("SELECT durable_offset FROM objects WHERE id="+Quote(id))==descriptor.CipherBytes,"cache_upload_incomplete");
   if(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1)return new CompletionEvidence(lease,descriptor);
   foreach(var old in completionProgress.Keys.ToArray())if(Scalar("SELECT count(*) FROM leases WHERE id="+Quote(old)+" AND expires>"+Now())==0)ForgetCompletion(old);
   if(!completionProgress.TryGetValue(lease,out progress)){Require(completionProgress.Count<16,"cache_completion_capacity");progress=new CompletionProgress{Id=id,Digest=descriptor.CipherDigest,Size=descriptor.CipherBytes};completionProgress.Add(lease,progress);}
   Require(!progress.Busy&&progress.Id==id&&progress.Digest==descriptor.CipherDigest&&progress.Size==descriptor.CipherBytes,"cache_completion_evidence_mismatch");progress.Busy=true;
  }
  try{long end=Math.Min(descriptor.CipherBytes,checked(progress.Offset+8L*1024*1024));
   while(progress.Offset<end){byte[] chunk;lock(gate){Check();Require(Lease(lease,true)==descriptor.Id,"cache_completion_evidence_mismatch");chunk=ReadChunk(descriptor.Id,progress.Offset);}
    progress.Hash.TransformBlock(chunk,0,chunk.Length,null,0);progress.Offset=checked(progress.Offset+chunk.Length);
   }
   lock(gate){Check();Require(Lease(lease,true)==descriptor.Id,"cache_completion_evidence_mismatch");Execute("UPDATE leases SET expires="+checked(Now()+120)+" WHERE id="+Quote(lease));}
   if(progress.Offset<descriptor.CipherBytes)return null;
   progress.Hash.TransformFinalBlock(new byte[0],0,0);Require(String.Concat(progress.Hash.Hash.Select(x=>x.ToString("x2")))==descriptor.CipherDigest,"cache_ciphertext_digest_mismatch");
   lock(gate){ForgetCompletion(lease);}return new CompletionEvidence(lease,descriptor);
  }catch{lock(gate){ForgetCompletion(lease);}throw;}
  finally{lock(gate){progress.Busy=false;if(!completionProgress.ContainsKey(lease))progress.Hash.Dispose();}}
 }
 internal CompletionEvidence VerifyCompletion(string lease){
  AttachmentCacheDescriptor descriptor;
  lock(gate){Check();string id=Lease(lease,true);descriptor=Descriptor(id);Require(Scalar("SELECT durable_offset FROM objects WHERE id="+Quote(id))==descriptor.CipherBytes,"cache_upload_incomplete");if(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1)return new CompletionEvidence(lease,descriptor);}
  using(var sha=SHA256.Create()){long offset=0;while(offset<descriptor.CipherBytes){byte[] chunk;
   // Each bounded read owns the cache lock; hashing owns neither the global
   // roster lock nor the cache lock. Full-prefix chunks cannot be overwritten.
   lock(gate){Check();Require(Lease(lease,true)==descriptor.Id,"cache_completion_evidence_mismatch");chunk=ReadChunk(descriptor.Id,offset);}
#if PEERCHAT_CACHE_TESTS
   if(TestFault!=null)TestFault("completion_hash_chunk");
#endif
   sha.TransformBlock(chunk,0,chunk.Length,null,0);offset=checked(offset+chunk.Length);}sha.TransformFinalBlock(new byte[0],0,0);Require(String.Concat(sha.Hash.Select(x=>x.ToString("x2")))==descriptor.CipherDigest,"cache_ciphertext_digest_mismatch");}
  return new CompletionEvidence(lease,descriptor);
 }
 internal void CommitCompletion(string lease,CompletionEvidence evidence){lock(gate){Check();string id=Lease(lease,true);var d=Descriptor(id);
  Require(evidence!=null&&evidence.Lease==lease&&evidence.Id==id&&evidence.Digest==d.CipherDigest&&evidence.Size==d.CipherBytes,"cache_completion_evidence_mismatch");
  Require(Scalar("SELECT durable_offset FROM objects WHERE id="+Quote(id))==d.CipherBytes,"cache_upload_incomplete");if(Scalar("SELECT complete FROM objects WHERE id="+Quote(id))==1)return;
  Space(0);long now=Now();ReservedMutation(id,()=>Execute("UPDATE objects SET complete=1,completed="+now+",expires="+checked(now+options.RetentionSeconds)+" WHERE id="+Quote(id)+";UPDATE leases SET expires="+checked(now+120)+" WHERE id="+Quote(lease)+";"));
 }}
 internal void Complete(string lease){CommitCompletion(lease,VerifyCompletion(lease));}
 internal byte[] Read(string lease,long offset,int maximum){lock(gate){Check();Require(offset>=0&&maximum>0&&maximum<=65536,"cache_range_invalid");string id=Lease(lease,false);var d=Descriptor(id);Require(offset<=d.CipherBytes,"cache_range_invalid");int count=(int)Math.Min(maximum,d.CipherBytes-offset);var output=new byte[count];int used=0;
  while(used<count){long current=checked(offset+used);var starts=Rows("SELECT offset FROM chunks WHERE object_id="+Quote(id)+" AND offset<="+current+" ORDER BY offset DESC LIMIT 1",1);Require(starts.Count==1,"cache_chunk_missing");long start=Number(starts[0][0]);var chunk=ReadChunk(id,start);int at=checked((int)(current-start));Require(at>=0&&at<chunk.Length,"cache_chunk_missing");int n=Math.Min(count-used,chunk.Length-at);Buffer.BlockCopy(chunk,at,output,used,n);used+=n;}
  Execute("UPDATE leases SET expires="+checked(Now()+120)+" WHERE id="+Quote(lease));return output;
 }}
 byte[] ReadChunk(string id,long offset){IntPtr statement=IntPtr.Zero;try{Require(SqliteNative.prepare(database,"SELECT data,digest FROM chunks WHERE object_id="+Quote(id)+" AND offset="+offset,out statement)==0&&SqliteNative.step(statement)==100,"cache_chunk_missing");var bytes=SqliteNative.columnBlob(statement,0);Require(bytes.Length>0&&bytes.Length<=65536&&MailboxProtocol.Digest(bytes)==SqliteNative.columnText(statement,1),"cache_ciphertext_corrupt");return bytes;}finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}}
 internal void RecipientProgress(string lease,long offset,bool verified){lock(gate){Check();string id=Lease(lease,false);var d=Descriptor(id);Require(offset>=0&&offset<=d.CipherBytes&&(!verified||offset==d.CipherBytes),"cache_receipt_invalid");
  Execute("UPDATE recipients SET durable_offset=MAX(durable_offset,"+offset+"),verified=MAX(verified,"+(verified?1:0)+") WHERE object_id="+Quote(id)+" AND device=(SELECT device FROM leases WHERE id="+Quote(lease)+");");
 }}
 internal long RecipientOffset(string id,string device){lock(gate){Check();Descriptor(id);Require(MailboxProtocol.Id(device,64),"cache_recipient_invalid");return Scalar("SELECT durable_offset FROM recipients WHERE object_id="+Quote(id)+" AND device="+Quote(device));}}
 void CleanupLocked(int operations,bool pressure,long incomingBudget=0){long now=Now();Execute("DELETE FROM leases WHERE expires<="+now+";");
  for(int n=0;n<operations;n++){
   var pending=Rows("SELECT object_id FROM (SELECT id AS object_id FROM objects WHERE deleting=1 ORDER BY id LIMIT 1)",1);
   if(pending.Count==0){var candidates=Rows("SELECT object_id FROM (SELECT o.id AS object_id FROM objects o WHERE deleting=0 AND NOT EXISTS(SELECT 1 FROM leases l WHERE l.object_id=o.id) AND (expires<="+now+(pressure?" OR complete=1":"")+") ORDER BY CASE WHEN expires<="+now+" THEN 0 ELSE 1 END,CASE WHEN complete=1 THEN completed ELSE expires END,id LIMIT 1)",1);if(candidates.Count==0)break;string candidate=candidates[0][0];Execute("UPDATE objects SET deleting=1 WHERE id="+Quote(candidate));pending=candidates;}
   string id=pending[0][0];var offsets=Rows("SELECT offset FROM chunks WHERE object_id="+Quote(id)+" ORDER BY offset LIMIT 8",8);
   if(offsets.Count>0)Transaction(()=>Execute("DELETE FROM chunks WHERE object_id="+Quote(id)+" AND offset IN ("+String.Join(",",offsets.Select(x=>Number(x[0]).ToString(CultureInfo.InvariantCulture)))+");"));
   else ReservedMutation(id,()=>Execute("DELETE FROM attachment_key_envelopes WHERE object_id="+Quote(id)+";DELETE FROM announcement_acks WHERE object_id="+Quote(id)+";DELETE FROM announcements WHERE object_id="+Quote(id)+";DELETE FROM recipients WHERE object_id="+Quote(id)+";DELETE FROM objects WHERE id="+Quote(id)+";"));
   Execute("PRAGMA incremental_vacuum(128);");
   // Admission must fit the reserved incoming object too. Space(0) can already
   // pass before reclamation begins and would stop after only one tiny batch.
   if(pressure){try{Space(incomingBudget);break;}catch(InvalidOperationException){}}
  }
 }
 internal void Cleanup(int operations=16){lock(gate){Check();Require(operations>0&&operations<=256,"cache_cleanup_bound");CleanupLocked(operations,false);}}
 internal long ChargedBytes {get{lock(gate){Check();return checked(directory.AllocatedBytes()+Reserved()+OperationHeadroom);}}}
 public void Dispose(){lock(gate){if(disposed)return;disposed=true;foreach(var lease in completionProgress.Keys.ToArray())ForgetCompletion(lease);if(database!=IntPtr.Zero){Sqlite.sqlite3_close(database);database=IntPtr.Zero;}directory.Dispose();}}
}
}
