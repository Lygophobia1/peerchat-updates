#if PEERCHAT_PORTABLE
using ECDsaCng = PortableTestKey;
#endif
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;

class AttachmentCacheContextTest {
 static int Main(string[] args){try{AttachmentCacheRuntimeTest.ContextChecks(Path.GetFullPath(args[0]));return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
partial class AttachmentCacheRuntimeTest {
 sealed class ContextServer:IDisposable {
  readonly DiscoveryServer server;readonly Thread worker;Exception failure;
  internal ContextServer(ServerConfiguration config){server=new DiscoveryServer(config);worker=new Thread(()=>{try{server.Run();}catch(Exception e){failure=e;}}){IsBackground=true};worker.Start();
   var until=Stopwatch.StartNew();bool ready=false;while(until.ElapsedMilliseconds<10000&&!ready){try{using(var probe=new System.Net.Sockets.TcpClient()){probe.Connect("localhost",config.Port);ready=true;}}catch(System.Net.Sockets.SocketException){Thread.Sleep(20);}}Check(ready,"Context TLS server startup");
  }
  public void Dispose(){server.RequestStop();Check(worker.Join(12000),"Context TLS shutdown blocked");if(failure!=null)throw failure;}
 }
 static string ContextIdentity(ECDsaCng key){return MailboxProtocol.Digest(key.Key.Export(CngKeyBlobFormat.EccPublicBlob));}
 static byte[] VerificationWire(ECDsaCng signer,AttachmentCacheDescriptor descriptor,long account,long verifiedUtc){using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream)){
  writer.Write(new byte[]{80,84,1,30});Text(writer,"PeerChat/group-attachment-verified/v1");Field(writer,descriptor.Wire);writer.Write((ulong)account);Text(writer,ContextIdentity(signer));Field(writer,signer.Key.Export(CngKeyBlobFormat.EccPublicBlob));writer.Write((ulong)verifiedUtc);Field(writer,SignKeyBytes(signer,stream.ToArray()));return stream.ToArray();
 }}
 static void ContextSession(AccountDatabase db,int account,string device,string token){string credential=Guid.NewGuid().ToString("N");db.RememberDevice(account,device,credential);db.CreateDeviceSession(MailboxProtocol.Digest(Encoding.UTF8.GetBytes(token)),account,device,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds(),credential);}
 static long ContextGeneration(Dictionary<string,object> context,string field){return Convert.ToInt64(((Dictionary<string,object>)context[field])["rosterGeneration"]);}
 static void ContextCheck(Dictionary<string,object> context,AttachmentCacheDescriptor descriptor,string device,long original,long latest,long expires){
  Check((string)context["id"]==descriptor.Id&&AttachmentCacheRequests.Decode((string)context["descriptor"],2048).SequenceEqual(descriptor.Wire),"Context descriptor binding");
  Check(ContextGeneration(context,"originalRoster")==original&&ContextGeneration(context,"currentRoster")==latest&&Convert.ToInt64(context["expiresUtc"])==expires,"Context epoch/immutable expiry");
  Check((string)context["recipientMembership"]==(string)context["currentRecipientMembership"]&&((string)context["sourceMembership"]).Length==32,"Context incarnation");
  foreach(string name in new[]{"originalRoster","currentRoster"}){var roster=(Dictionary<string,object>)context[name];Check(((IEnumerable)roster["members"]).Cast<Dictionary<string,object>>().Any(x=>(string)x["publicIdentity"]==device&&(string)x["state"]=="member"),"Explicit device missing");Check((string)roster["creationProof"]!="","Signed root missing");}
 }
 internal static void ContextChecks(string root){Directory.CreateDirectory(root);ContextStorage(Path.Combine(root,"storage"));ContextPaging(Path.Combine(root,"paging"));ContextNetwork(Path.Combine(root,"tls"));Console.WriteLine("PASS cache context production-adapter TLS/storage checks; shipping gate remains FALSE; no plaintext completion claim");}
 static void ContextStorage(string root){Directory.CreateDirectory(root);long prior=now;now=1000000;try{using(var source=new ECDsaCng(256)){
  var options=Options(Fixture(root,"cache"));var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(source,new string('1',32),1)));string actor=proof.OwnerIdentity;
  using(var db=new AccountDatabase(Path.Combine(root,"accounts.sqlite")))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   Check(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],actor)==1&&db.RegisterGroup(1,actor,proof)!=null,"Context storage roster");var admission=new AttachmentCacheAdmission(db,cache);var bytes=Payload(1);var descriptor=Descriptor(source,1,bytes,group:proof.ServerGroupId);
   AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit")throw new InvalidOperationException("cache_test_rollback");};try{Reject(()=>admission.Admit(1,actor,()=>true,descriptor.Wire),"cache_test_rollback");}finally{AttachmentCacheRuntime.TestFault=null;}
   Reject(()=>cache.Describe(descriptor.Id),"cache_attachment_unavailable");admission.Admit(1,actor,()=>true,descriptor.Wire);Check(admission.Context(1,actor,()=>true,descriptor.Id)!=null,"Early context missing");Reject(()=>admission.Acquire(1,actor,()=>true,descriptor.Id,false),"cache_upload_incomplete");Upload(cache,descriptor,bytes);
   var captured=admission.Context(1,actor,()=>true,descriptor.Id);long expires=Convert.ToInt64(captured["expiresUtc"]);Reject(()=>admission.Context(1,actor,()=>false,descriptor.Id),"cache_session_invalid");
   var receipt=VerificationWire(source,descriptor,1,now*1000);var rpc=new AttachmentCacheRequests(admission);Rpc(rpc,1,actor,"cache_verified","cache_recipient_ciphertext_incomplete","id",descriptor.Id,"receipt",AttachmentCacheRequests.Encode(receipt));
   var initial=(List<Dictionary<string,object>>)admission.VerificationReceipts(1,actor,()=>true,descriptor.Id,"")["items"];Check(initial.Count==1&&!(bool)initial[0]["plaintextVerified"]&&(string)initial[0]["receipt"]=="","Ciphertext complete fabricated plaintext verification");
   string downloaded=cache.Acquire(descriptor.Id,actor,false);cache.RecipientProgress(downloaded,bytes.Length,false);cache.Release(downloaded);
   var malformed=(byte[])receipt.Clone();malformed[malformed.Length-1]^=1;Reject(()=>admission.VerifyReceipt(1,actor,()=>true,descriptor.Id,malformed),"cache_verification_receipt_invalid");Reject(()=>admission.VerifyReceipt(1,actor,()=>true,descriptor.Id,VerificationWire(source,descriptor,2,now*1000)),"cache_verification_device_mismatch");Reject(()=>admission.VerifyReceipt(1,actor,()=>true,descriptor.Id,VerificationWire(source,descriptor,1,now*1000+300001)),"cache_verification_time_invalid");
   AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit")throw new InvalidOperationException("cache_test_rollback");};try{Reject(()=>admission.VerifyReceipt(1,actor,()=>true,descriptor.Id,receipt),"cache_test_rollback");}finally{AttachmentCacheRuntime.TestFault=null;}
   Check(!(bool)((List<Dictionary<string,object>>)admission.VerificationReceipts(1,actor,()=>true,descriptor.Id,"")["items"])[0]["plaintextVerified"],"Rolled-back receipt survived");
   Check((bool)Rpc(rpc,1,actor,"cache_verified","accepted","id",descriptor.Id,"receipt",AttachmentCacheRequests.Encode(receipt))["newVerificationReceipt"]&&!(bool)Rpc(rpc,1,actor,"cache_verified","accepted","id",descriptor.Id,"receipt",AttachmentCacheRequests.Encode(receipt))["newVerificationReceipt"],"Receipt retry not idempotent");
   Reject(()=>admission.VerifyReceipt(1,actor,()=>true,descriptor.Id,VerificationWire(source,descriptor,1,now*1000+1)),"cache_conflicting_verification_receipt");var signedRows=(IEnumerable)Rpc(rpc,1,actor,"cache_receipts","accepted","id",descriptor.Id,"after","")["items"];var signedRow=signedRows.Cast<Dictionary<string,object>>().Single();Check((bool)signedRow["plaintextVerified"]&&AttachmentCacheRequests.Decode((string)signedRow["receipt"],4096).SequenceEqual(receipt),"Signed receipt missing");
   // An older admitted object may exist, but retrying admission cannot invent
   // its historical snapshot from the present-day authoritative roster.
   var legacy=Descriptor(source,1,bytes,group:proof.ServerGroupId);cache.Admit(legacy,db.AttachmentRecipients(proof.ServerGroupId));Upload(cache,legacy,bytes);admission.Admit(1,actor,()=>true,legacy.Wire);Reject(()=>admission.Context(1,actor,()=>true,legacy.Id),"cache_context_unavailable");
   // Receipt expiry is not a deletion or rollback of already durable bytes.
   string retainedCopy=Path.Combine(root,"retained-downloaded-ciphertext.bin");File.WriteAllBytes(retainedCopy,bytes);now=expires;Reject(()=>admission.Context(1,actor,()=>true,descriptor.Id),"cache_attachment_expired");Reject(()=>admission.VerifyReceipt(1,actor,()=>true,descriptor.Id,receipt),"cache_attachment_expired");Check(cache.RecipientOffset(descriptor.Id,actor)==bytes.Length&&File.ReadAllBytes(retainedCopy).SequenceEqual(bytes),"Expired receipt discarded completed durable copy");now=prior;
   var roster=db.GroupRoster(proof.ServerGroupId,1,actor,false);var rows=(List<Dictionary<string,object>>)roster["members"];for(int i=0;i<256;i++)rows.Add(new Dictionary<string,object>(rows[0]));Reject(()=>AttachmentCacheContext.Snapshot(roster),"cache_context_too_large");
  }
 }}finally{now=prior;}
 Console.WriteLine("PASS atomic context/receipt rollback, malformed signature/device/time/conflict rejection, no verification from byte/key ACK, exact receipt retry, expired receipt preserves durable copy, missing evidence/session/bounds rejection");}
 static void ContextPaging(string root){Directory.CreateDirectory(root);var keys=Enumerable.Range(0,10).Select(x=>new ECDsaCng(256)).ToArray();try{
  var options=Options(Fixture(root,"cache"));var devices=keys.Select(ContextIdentity).ToArray();var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(keys[0],new string('1',32),1)));var bytes=Payload(1);var descriptor=Descriptor(keys[0],1,bytes,group:proof.ServerGroupId,epoch:19);
  using(var db=new AccountDatabase(Path.Combine(root,"accounts.sqlite")))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   for(int i=0;i<keys.Length;i++)Check(db.CreateAccountWithDevice("account-"+i,new byte[16],new byte[32],devices[i])==i+1,"Paging account");Check(db.RegisterGroup(1,devices[0],proof)!=null,"Paging root");
   for(int i=1;i<keys.Length;i++)Check(db.GroupInvite(proof.ServerGroupId,1,devices[0],i+1,devices[i],i*2-1)!=null&&db.GroupRespond(proof.ServerGroupId,i+1,devices[i],true,i*2)!=null,"Paging member");
   var admission=new AttachmentCacheAdmission(db,cache);admission.Admit(1,devices[0],()=>true,descriptor.Wire);Upload(cache,descriptor,bytes);
   for(int i=1;i<keys.Length;i++){string lease=cache.Acquire(descriptor.Id,devices[i],false);cache.RecipientProgress(lease,bytes.Length,false);cache.Release(lease);admission.VerifyReceipt(i+1,devices[i],()=>true,descriptor.Id,VerificationWire(keys[i],descriptor,i+1,now*1000));}
   var rpc=new AttachmentCacheRequests(admission);var first=Rpc(rpc,1,devices[0],"cache_receipts","accepted","id",descriptor.Id,"after","");var firstRows=((IEnumerable)first["items"]).Cast<Dictionary<string,object>>().ToList();Check(firstRows.Count==8&&(bool)first["more"],"Receipt first page bound");var second=Rpc(rpc,1,devices[0],"cache_receipts","accepted","id",descriptor.Id,"after",first["cursor"]);var secondRows=((IEnumerable)second["items"]).Cast<Dictionary<string,object>>().ToList();Check(secondRows.Count==2&&!(bool)second["more"]&&firstRows.Concat(secondRows).Select(x=>(string)x["device"]).Distinct().Count()==10,"Receipt page lost/duplicated recipient");
   Check(firstRows.Concat(secondRows).Count(x=>(bool)x["plaintextVerified"])==9,"Unsigned sender counted verified");Rpc(rpc,1,devices[0],"cache_receipts","cache_verification_cursor_invalid","id",descriptor.Id,"after","bad");
   Check(db.GroupRemove(proof.ServerGroupId,1,devices[0],2,devices[1],19)!=null,"Paging revoke");var remaining=new List<string>();string cursor="";bool more;do{var page=Rpc(rpc,1,devices[0],"cache_receipts","accepted","id",descriptor.Id,"after",cursor);remaining.AddRange(((IEnumerable)page["items"]).Cast<Dictionary<string,object>>().Select(x=>(string)x["device"]));cursor=(string)page["cursor"];more=(bool)page["more"];}while(more);Check(remaining.Count==9&&!remaining.Contains(devices[1]),"Receipt page leaked revoked recipient");
  }
 }finally{foreach(var key in keys)key.Dispose();}Console.WriteLine("PASS signed receipt bounded pagination, cursor validation, no duplicates, sender-only access and current-membership filtering");}
 static void ContextNetwork(string root){Directory.CreateDirectory(root);using(var source=new ECDsaCng(256))using(var first=new ECDsaCng(256))using(var second=new ECDsaCng(256))using(var late=new ECDsaCng(256)){
  var keys=new[]{source,first,second,late};var devices=keys.Select(ContextIdentity).ToArray();var tokens=keys.Select(x=>Guid.NewGuid().ToString("N")).ToArray();var bindings=new[]{RecipientBinding(first,2),RecipientBinding(second,3)};
  var options=Options(Fixture(root,"cache"));options.Enabled=true;options.RetentionSeconds=1800;string database=Path.Combine(root,"accounts.sqlite");var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(source,new string('1',32),1)));
  using(var db=new AccountDatabase(database)){
   for(int i=0;i<keys.Length;i++){Check(db.CreateAccountWithDevice("account-"+i,new byte[16],new byte[32],devices[i])==i+1,"Context account");ContextSession(db,i+1,devices[i],tokens[i]);}
   Check(db.RegisterGroup(1,devices[0],proof)!=null,"Context registration");
   for(int i=1;i<=2;i++)Check(db.GroupInvite(proof.ServerGroupId,1,devices[0],i+1,devices[i],i*2-1)!=null&&db.GroupRespond(proof.ServerGroupId,i+1,devices[i],true,i*2)!=null,"Context member");
   db.InitializeMailbox();for(int i=1;i<=2;i++)Check(db.RegisterMailboxKey(i+1,devices[i],keys[i].Key.Export(CngKeyBlobFormat.EccPublicBlob),bindings[i-1])=="accepted","Context recipient key");
  }
  string certificatePath=Path.Combine(root,"fixture.pfx"),pin;using(var key=RSA.Create(2048)){var request=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);using(var certificate=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1))){File.WriteAllBytes(certificatePath,certificate.Export(X509ContentType.Pfx,"fixture-only"));pin=MailboxProtocol.Digest(certificate.RawData);}}
  var reservation=new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0);reservation.Start();int port=((System.Net.IPEndPoint)reservation.LocalEndpoint).Port;reservation.Stop();
  var config=new ServerConfiguration{DataDirectory=root,DatabasePath=database,LogDirectory=Path.Combine(root,"logs"),Port=port,CertificatePath=certificatePath,CertificatePassword="fixture-only",ManagementPipe="",AttachmentCache=options};config.Validate();
  var bytes=Payload(300000);var descriptor=Descriptor(source,300000,bytes,group:proof.ServerGroupId,epoch:5);long expires=0;string staleLease=null;var keyDigests=new string[2];var verificationWires=new byte[2][];
  using(var server=new ContextServer(config)){
   using(var sender=new RpcClient(port,pin,tokens[0],devices[0])){
    sender.Request("cache_admit","accepted","descriptor",Convert.ToBase64String(descriptor.Wire));var earlyContext=sender.Request("cache_context","accepted","id",descriptor.Id);expires=Convert.ToInt64(earlyContext["expiresUtc"]);string lease=(string)sender.Request("cache_acquire","accepted","id",descriptor.Id,"upload",true)["lease"];
    var recipients=((IEnumerable)sender.Request("cache_recipient_keys","accepted","id",descriptor.Id)["recipients"]).Cast<Dictionary<string,object>>().ToList();
    for(int i=1;i<=2;i++){var recipient=recipients.Single(x=>(string)x["device"]==devices[i]);var wire=KeyWire(source,descriptor,bindings[i-1],(string)recipient["sourceMembership"],(string)recipient["membership"],DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),expires*1000);keyDigests[i-1]=MailboxProtocol.Digest(wire);sender.Request("cache_publish_key","accepted","id",descriptor.Id,"envelope",AttachmentCacheRequests.Encode(wire));}
    using(var earlyRecipient=new RpcClient(port,pin,tokens[1],devices[1])){
     var pending=earlyRecipient.Request("cache_pending_keys","accepted","after","");Check(((IEnumerable)pending["items"]).Cast<object>().Count()==1,"Early encrypted group offer missing");
     var receivingContext=earlyRecipient.Request("cache_context","accepted","id",descriptor.Id);long earlyHorizon=Convert.ToInt64(receivingContext["expiresUtc"]);Check(earlyHorizon>=expires,"Early context shortened signed offer lifetime");ContextCheck(receivingContext,descriptor,devices[1],5,5,earlyHorizon);
     earlyRecipient.Request("cache_acquire","cache_upload_incomplete","id",descriptor.Id,"upload",false);
    }
    for(int offset=0;offset<bytes.Length;){var block=bytes.Skip(offset).Take(65536).ToArray();sender.Request("cache_append","accepted","lease",lease,"offset",(long)offset,"data",AttachmentCacheRequests.Encode(block));offset+=block.Length;}
    sender.Request("cache_complete","accepted","lease",lease);sender.Request("cache_release","accepted","lease",lease,"upload",true);var context=sender.Request("cache_context","accepted","id",descriptor.Id);expires=Convert.ToInt64(context["expiresUtc"]);ContextCheck(context,descriptor,devices[0],5,5,expires);
    sender.Request("logout",null);
   }
   using(var recipient=new RpcClient(port,pin,tokens[1],devices[1])){
    ContextCheck(recipient.Request("cache_context","accepted","id",descriptor.Id),descriptor,devices[1],5,5,expires);recipient.Request("cache_key_ack","accepted","id",descriptor.Id,"digest",keyDigests[0]);recipient.Request("cache_verified","cache_recipient_ciphertext_incomplete","id",descriptor.Id,"receipt",AttachmentCacheRequests.Encode(VerificationWire(first,descriptor,2,DateTimeOffset.UtcNow.ToUnixTimeMilliseconds())));staleLease=(string)recipient.Request("cache_acquire","accepted","id",descriptor.Id,"upload",false)["lease"];
    var block=AttachmentCacheRequests.Decode((string)recipient.Request("cache_read","accepted","lease",staleLease,"offset",0L,"maximum",32768L)["data"]);Check(block.SequenceEqual(bytes.Take(32768)),"Context first recipient data");recipient.Request("cache_progress","accepted","lease",staleLease,"offset",32768L);
   }
  }
  // Change current epoch only while stopped; source stays logged out. The
  // frozen original snapshot and completed expiration must survive restart.
  using(var db=new AccountDatabase(database)){Check(db.Authenticate(MailboxProtocol.Digest(Encoding.UTF8.GetBytes(tokens[0])))==0,"Source logout did not persist");Check(db.GroupInvite(proof.ServerGroupId,1,devices[0],4,devices[3],5)!=null&&db.GroupRespond(proof.ServerGroupId,4,devices[3],true,6)!=null,"Context advance epoch");}
  using(var cache=new AttachmentCacheRuntime(options,()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds()))Check(cache.RecipientOffset(descriptor.Id,devices[1])==32768&&cache.RecipientOffset(descriptor.Id,devices[2])==0,"Independent durable recipient cursors");
  options.RetentionSeconds=1;
  using(var server=new ContextServer(config)){
   for(int i=1;i<=2;i++)using(var recipient=new RpcClient(port,pin,tokens[i],devices[i])){
    ContextCheck(recipient.Request("cache_context","accepted","id",descriptor.Id),descriptor,devices[i],5,7,expires);var pending=(IEnumerable)recipient.Request("cache_pending_keys","accepted","after","")["items"];Check(pending.Cast<object>().Count()==(i==1?0:1),"One recipient ACK consumed another's key");
    recipient.Request("cache_key","accepted","id",descriptor.Id);if(i==1)recipient.Request("cache_read","cache_lease_expired","lease",staleLease,"offset",32768L,"maximum",32768L);
    string lease=(string)recipient.Request("cache_acquire","accepted","id",descriptor.Id,"upload",false)["lease"];using(var received=new MemoryStream()){
     for(long offset=0;offset<bytes.Length;){var block=AttachmentCacheRequests.Decode((string)recipient.Request("cache_read","accepted","lease",lease,"offset",offset,"maximum",65536L)["data"]);received.Write(block,0,block.Length);offset+=block.Length;recipient.Request("cache_progress","accepted","lease",lease,"offset",offset);}Check(received.ToArray().SequenceEqual(bytes),"Two-recipient source-offline restart ciphertext integrity");
    }recipient.Request("cache_release","accepted","lease",lease,"upload",false);recipient.Request("cache_key_ack","accepted","id",descriptor.Id,"digest",keyDigests[i-1]);
    verificationWires[i-1]=VerificationWire(keys[i],descriptor,i+1,DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());Check((bool)recipient.Request("cache_verified","accepted","id",descriptor.Id,"receipt",AttachmentCacheRequests.Encode(verificationWires[i-1]))["newVerificationReceipt"]&&!(bool)recipient.Request("cache_verified","accepted","id",descriptor.Id,"receipt",AttachmentCacheRequests.Encode(verificationWires[i-1]))["newVerificationReceipt"],"TLS signed receipt retry");recipient.Request("cache_receipts","cache_sender_binding_mismatch","id",descriptor.Id,"after","");
   }
   using(var newcomer=new RpcClient(port,pin,tokens[3],devices[3]))newcomer.Request("cache_context","cache_original_membership_required","id",descriptor.Id);
  }
  using(var db=new AccountDatabase(database)){
   using(var cache=new AttachmentCacheRuntime(options,()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds())){var admission=new AttachmentCacheAdmission(db,cache);var rows=(List<Dictionary<string,object>>)admission.VerificationReceipts(1,devices[0],()=>true,descriptor.Id,"")["items"];Check(rows.Count==3&&!(bool)rows.Single(x=>(string)x["device"]==devices[0])["plaintextVerified"],"Source fabricated a receipt");for(int i=1;i<=2;i++)Check(AttachmentCacheRequests.Decode((string)rows.Single(x=>(string)x["device"]==devices[i])["receipt"],4096).SequenceEqual(verificationWires[i-1]),"Signed receipt lost at restart");}
   Check(db.GroupRemove(proof.ServerGroupId,1,devices[0],3,devices[2],7)!=null&&db.GroupInvite(proof.ServerGroupId,1,devices[0],3,devices[2],8)!=null&&db.GroupRespond(proof.ServerGroupId,3,devices[2],true,9)!=null,"Context removed/rejoined fixture");
  }
  using(var server=new ContextServer(config))using(var rejoined=new RpcClient(port,pin,tokens[2],devices[2])){rejoined.Request("cache_context","cache_original_membership_required","id",descriptor.Id);rejoined.Request("cache_key","cache_original_membership_required","id",descriptor.Id);rejoined.Request("cache_verified","cache_original_membership_required","id",descriptor.Id,"receipt",AttachmentCacheRequests.Encode(verificationWires[1]));}
  Console.WriteLine("PASS pinned TLS: two recipients, source logout, actual server stop/reopen, original/current roster evidence, immutable expiration under retention change, separate durable key receipts/byte cursors, exact ciphertext downloads, stale lease/new-member/rejoin denial");
 }}
}
