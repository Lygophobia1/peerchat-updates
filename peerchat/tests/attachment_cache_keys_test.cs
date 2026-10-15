#if PEERCHAT_PORTABLE
using ECDsaCng = PortableTestKey;
#endif
using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections.Generic;
using System.Security.Cryptography;
using PeerChat.Server;

partial class AttachmentCacheRuntimeTest {
 static void KN(Stream stream,ulong value,int width){for(int n=width-1;n>=0;--n)stream.WriteByte((byte)(value>>(n*8)));}
 static void KB(Stream stream,byte[] bytes){KN(stream,(ulong)bytes.Length,4);stream.Write(bytes,0,bytes.Length);}
 static void KT(Stream stream,string value){KB(stream,Encoding.UTF8.GetBytes(value));}
 static MemoryStream KD(string domain){var stream=new MemoryStream();var bytes=Encoding.ASCII.GetBytes(domain+"\0");stream.Write(bytes,0,bytes.Length);return stream;}
 static void KS(Stream stream){KN(stream,1,2);KN(stream,0x10,2);KN(stream,1,2);KN(stream,1,2);}
 static byte[] SignKeyBytes(ECDsaCng key,byte[] bytes){using(var hash=SHA256.Create())return key.SignHash(hash.ComputeHash(bytes));}
 static byte[] RecipientBinding(ECDsaCng identity,long account,string keyId=null,long generation=1,string predecessor=""){
  var publicKey=identity.Key.Export(CngKeyBlobFormat.EccPublicBlob);using(var stream=KD("PeerChat/device-recipient-binding/v1")){
   KS(stream);KN(stream,(ulong)account,8);KT(stream,MailboxProtocol.Digest(publicKey));KT(stream,keyId??Guid.NewGuid().ToString("N"));KN(stream,(ulong)generation,8);KN(stream,1000,8);KT(stream,predecessor);KB(stream,new byte[]{4}.Concat(publicKey.Skip(8)).ToArray());stream.WriteByte(1);KB(stream,SignKeyBytes(identity,stream.ToArray()));return stream.ToArray();
  }
 }
 static byte[] KeyWire(ECDsaCng source,AttachmentCacheDescriptor descriptor,byte[] binding,string sourceMembership,string recipientMembership,long created,long expires){
  // Genuine source signature and registered on-curve recipient binding. The
  // server fixture treats HPKE bytes as opaque; actual HPKE/decrypt is exercised
  // by group_attachment_key_test.cpp and native managed-cache round trips.
  using(var header=KD("PeerChat/group-attachment-key/v1"))using(var signed=KD("PeerChat/group-attachment-key-signature/v1")){
   KS(header);KB(header,descriptor.Wire);KT(header,sourceMembership);KT(header,recipientMembership);KB(header,binding);KN(header,(ulong)created,8);KN(header,(ulong)expires,8);
   KB(signed,header.ToArray());KB(signed,MailboxProtocol.Binding.Parse(binding).Public);KB(signed,new byte[32]);KB(signed,SignKeyBytes(source,signed.ToArray()));return signed.ToArray();
  }
 }
 static void NativeKeys(string fixture,string root){Directory.CreateDirectory(root);long prior=now;now=1;try{
  var descriptor=AttachmentCacheDescriptor.Parse(File.ReadAllBytes(Path.Combine(fixture,"descriptor.bin")));var wire=File.ReadAllBytes(Path.Combine(fixture,"key-envelope.bin"));var parsed=AttachmentKeyEnvelope.Parse(wire);Check(parsed.Descriptor.Wire.SequenceEqual(descriptor.Wire),"Native key descriptor mismatch");var bytes=File.ReadAllBytes(Path.Combine(fixture,"ciphertext.bin"));var options=Options(Fixture(root,"cache"));options.RetentionSeconds=3600;
  var recipients=new List<CacheRecipient>{new CacheRecipient(descriptor.Account,descriptor.Sender,parsed.SourceMembership),new CacheRecipient(parsed.Recipient.Account,parsed.Recipient.Device,parsed.RecipientMembership)};
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){cache.Admit(descriptor,recipients);Upload(cache,descriptor,bytes);Check(cache.PublishKey(descriptor.Id,parsed),"Native envelope not committed");Check(!cache.PublishKey(descriptor.Id,parsed),"Native duplicate inflated keys");}
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){var retained=cache.ReadKey(descriptor.Id,parsed.Recipient.Device);Check(retained.Wire.SequenceEqual(wire),"Native envelope changed after restart");File.WriteAllBytes(Path.Combine(fixture,"cache-return-key.bin"),retained.Wire);File.WriteAllBytes(Path.Combine(fixture,"cache-return-ciphertext.bin"),bytes);}
  Console.WriteLine("PASS actual native signed HPKE envelope accepted by managed parser, durably cached and reopened byte-identically; authoritative group admission tested separately");
 }finally{now=prior;}}
 static void KeyAdmission(string root){KeyMetadataQuota(Path.Combine(root,"quota"));Directory.CreateDirectory(root);using(var owner=new ECDsaCng(256))using(var member=new ECDsaCng(256))using(var third=new ECDsaCng(256)){
  var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(owner,new string('1',32),1)));string actor=proof.OwnerIdentity,reader=MailboxProtocol.Digest(member.Key.Export(CngKeyBlobFormat.EccPublicBlob)),other=MailboxProtocol.Digest(third.Key.Export(CngKeyBlobFormat.EccPublicBlob));var options=Options(Fixture(root,"cache"));string dbPath=Path.Combine(root,"accounts.sqlite");var bytes=Payload(1);var descriptor=Descriptor(owner,1,bytes,group:proof.ServerGroupId,epoch:3);var binding=RecipientBinding(member,2);byte[] envelope;
  using(var db=new AccountDatabase(dbPath))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   Check(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],actor)==1&&db.CreateAccountWithDevice("member",new byte[16],new byte[32],reader)==2&&db.CreateAccountWithDevice("third",new byte[16],new byte[32],other)==3,"Key fixture accounts");Check(db.RegisterGroup(1,actor,proof)!=null&&db.GroupInvite(proof.ServerGroupId,1,actor,2,reader,1)!=null&&db.GroupRespond(proof.ServerGroupId,2,reader,true,2)!=null,"Key fixture roster");
   db.InitializeMailbox();Check(db.RegisterMailboxKey(2,reader,member.Key.Export(CngKeyBlobFormat.EccPublicBlob),binding)=="accepted","Key registration failed");var admission=new AttachmentCacheAdmission(db,cache);admission.Admit(1,actor,()=>true,descriptor.Wire);var rpc=new AttachmentCacheRequests(admission);
   var candidates=admission.RecipientKeys(1,actor,()=>true,descriptor.Id);Check(candidates.Count==2&&candidates.Single(x=>(string)x["device"]==actor)["keyState"].Equals("unavailable")&&candidates.Single(x=>(string)x["device"]==reader)["keyState"].Equals("available"),"Keys missing explicit unavailable state");
   Reject(()=>admission.RecipientKeys(2,reader,()=>true,descriptor.Id),"cache_sender_binding_mismatch");Reject(()=>admission.RecipientKeys(1,actor,()=>false,descriptor.Id),"cache_session_invalid");
   envelope=KeyWire(owner,descriptor,binding,db.AttachmentMembership(proof.ServerGroupId,1,actor),db.AttachmentMembership(proof.ServerGroupId,2,reader),now*1000,now*1000+9000);
   Reject(()=>admission.PublishKey(1,actor,()=>true,descriptor.Id,envelope),"cache_upload_incomplete");Upload(cache,descriptor,bytes);
   var corrupted=(byte[])envelope.Clone();corrupted[corrupted.Length-1]^=1;Reject(()=>admission.PublishKey(1,actor,()=>true,descriptor.Id,corrupted),"cache_key_envelope_invalid");
   Reject(()=>admission.PublishKey(2,reader,()=>true,descriptor.Id,envelope),"cache_sender_binding_mismatch");
   AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit")throw new InvalidOperationException("cache_test_rollback");};try{Reject(()=>admission.PublishKey(1,actor,()=>true,descriptor.Id,envelope),"cache_test_rollback");}finally{AttachmentCacheRuntime.TestFault=null;}Reject(()=>admission.Key(2,reader,()=>true,descriptor.Id),"cache_key_unavailable");
   Check((bool)Rpc(rpc,1,actor,"cache_publish_key","accepted","id",descriptor.Id,"envelope",AttachmentCacheRequests.Encode(envelope))["newKeyEnvelope"],"Key publication missing");Check(!admission.PublishKey(1,actor,()=>true,descriptor.Id,envelope),"Key retry not idempotent");
   var conflict=KeyWire(owner,descriptor,binding,db.AttachmentMembership(proof.ServerGroupId,1,actor),db.AttachmentMembership(proof.ServerGroupId,2,reader),now*1000,now*1000+8000);Reject(()=>admission.PublishKey(1,actor,()=>true,descriptor.Id,conflict),"cache_conflicting_key_envelope");
   Check(db.GroupInvite(proof.ServerGroupId,1,actor,3,other,3)!=null&&db.GroupRespond(proof.ServerGroupId,3,other,true,4)!=null,"Newer roster epoch fixture");Check(admission.Key(2,reader,()=>true,descriptor.Id).Wire.SequenceEqual(envelope),"Original member lost attachment after epoch rotation");Reject(()=>admission.Key(3,other,()=>true,descriptor.Id),"cache_original_membership_required");
   var shortLived=Descriptor(owner,1,bytes,group:proof.ServerGroupId,epoch:5);admission.Admit(1,actor,()=>true,shortLived.Wire);Upload(cache,shortLived,bytes);var shortKey=KeyWire(owner,shortLived,binding,db.AttachmentMembership(proof.ServerGroupId,1,actor),db.AttachmentMembership(proof.ServerGroupId,2,reader),now*1000,now*1000+1000);admission.PublishKey(1,actor,()=>true,shortLived.Id,shortKey);
   now+=2;Reject(()=>admission.Key(2,reader,()=>true,shortLived.Id),"cache_key_expired");
   Check(cache.RecipientOffset(descriptor.Id,reader)==0,"Key receipt fabricated file progress");
   string cursor;bool more;Check(admission.PendingKeys(2,reader,()=>true,"",out cursor,out more).Count==1&&!more,"Pending key missing");
   var parsedBinding=MailboxProtocol.Binding.Parse(binding);var nextBinding=RecipientBinding(member,2,generation:2,predecessor:parsedBinding.Key);Check(db.RegisterMailboxKey(2,reader,member.Key.Export(CngKeyBlobFormat.EccPublicBlob),nextBinding)=="accepted","Key rotation rejected");Check(admission.Key(2,reader,()=>true,descriptor.Id).Wire.SequenceEqual(envelope)&&!admission.PublishKey(1,actor,()=>true,descriptor.Id,envelope),"Retired exact key retry rejected");
  }
  using(var db=new AccountDatabase(dbPath))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
   var admission=new AttachmentCacheAdmission(db,cache);Check(admission.Key(2,reader,()=>true,descriptor.Id).Wire.SequenceEqual(envelope),"Key restart changed opaque envelope");var digest=MailboxProtocol.Digest(envelope);
   AttachmentCacheRuntime.TestFault=stage=>{if(stage=="before_commit")throw new InvalidOperationException("cache_test_rollback");};try{Reject(()=>admission.AcknowledgeKey(2,reader,()=>true,descriptor.Id,digest),"cache_test_rollback");}finally{AttachmentCacheRuntime.TestFault=null;}
   string cursor;bool more;Check(admission.PendingKeys(2,reader,()=>true,"",out cursor,out more).Count==1,"Rolled back key ACK consumed envelope");Check(admission.AcknowledgeKey(2,reader,()=>true,descriptor.Id,digest)&&!admission.AcknowledgeKey(2,reader,()=>true,descriptor.Id,digest),"Key ACK not idempotent");Check(cache.RecipientOffset(descriptor.Id,reader)==0,"Key ACK advanced ciphertext offset");
   // Explicitly revoke the old recipient key without modifying its identity.
   var old=MailboxProtocol.Binding.Parse(binding);using(var revoke=KD("PeerChat/device-recipient-revoke/v1")){KN(revoke,1,2);KN(revoke,2,8);KT(revoke,reader);KT(revoke,old.Key);KN(revoke,1,8);KB(revoke,SignKeyBytes(member,revoke.ToArray()));Check(db.RevokeMailboxKey(2,reader,member.Key.Export(CngKeyBlobFormat.EccPublicBlob),revoke.ToArray())=="revoked","Explicit key revocation failed");}
   Reject(()=>admission.Key(2,reader,()=>true,descriptor.Id),"cache_recipient_key_unavailable");
   Check(db.GroupRemove(proof.ServerGroupId,1,actor,2,reader,5)!=null,"Key device removal failed");Reject(()=>admission.Key(2,reader,()=>true,descriptor.Id),"cache_group_not_authorized");Check(db.GroupInvite(proof.ServerGroupId,1,actor,2,reader,6)!=null&&db.GroupRespond(proof.ServerGroupId,2,reader,true,7)!=null,"Key device rejoin failed");Reject(()=>admission.Key(2,reader,()=>true,descriptor.Id),"cache_original_membership_required");
   now+=11;cache.Cleanup(256);Reject(()=>cache.ReadKey(descriptor.Id,reader),"cache_attachment_unavailable");Check(cache.ChargedBytes<=options.MaximumBytes,"Key metadata exceeded quota");
  }
 }Console.WriteLine("PASS authoritative attachment-key group lookup, missing keys, original/current intersection, newer epoch, immutable per-device publication, rollback/restart, retired-key exact retry, revocation/rejoin and expiry cleanup; ACK is not plaintext/file completion");}
 static void KeyMetadataQuota(string root){Directory.CreateDirectory(root);using(var source=new ECDsaCng(256)){
  var options=Options(Fixture(root,"cache"));var payload=Payload(1);var descriptor=Descriptor(source,1,payload);var original=new List<CacheRecipient>{new CacheRecipient(1,descriptor.Sender,new string('a',32))};var wires=new List<byte[]>();long charge;
  for(int n=0;n<16;n++)using(var recipient=new ECDsaCng(256)){var binding=RecipientBinding(recipient,2+n);var parsed=MailboxProtocol.Binding.Parse(binding);string membership=n.ToString("x32");original.Add(new CacheRecipient(2+n,parsed.Device,membership));wires.Add(KeyWire(source,descriptor,binding,new string('a',32),membership,now*1000,now*1000+9000));}
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){cache.Admit(descriptor,original);Upload(cache,descriptor,payload);charge=cache.ChargedBytes;}
  options.MaximumBytes=charge+16384;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){Reject(()=>cache.PublishKey(descriptor.Id,AttachmentKeyEnvelope.Parse(wires[0])),"cache_quota_or_free_space_protected");Reject(()=>cache.ReadKey(descriptor.Id,original[1].Device),"cache_key_unavailable");Check(cache.Inspect(descriptor.Id).Complete&&cache.Inspect(descriptor.Id).Offset==payload.Length,"Rejected key metadata altered ciphertext");charge=cache.ChargedBytes;}
  options.MaximumBytes=charge+32768;int accepted=0,rejected=0;
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){using(var start=new System.Threading.ManualResetEventSlim(false)){
   var tasks=wires.Select(wire=>System.Threading.Tasks.Task.Run(()=>{start.Wait();try{if(cache.PublishKey(descriptor.Id,AttachmentKeyEnvelope.Parse(wire)))System.Threading.Interlocked.Increment(ref accepted);}catch(InvalidOperationException error){Check(error.Message=="cache_quota_or_free_space_protected","Unexpected concurrent key error");System.Threading.Interlocked.Increment(ref rejected);}})).ToArray();start.Set();System.Threading.Tasks.Task.WaitAll(tasks);
  }Check(accepted>0&&rejected>0&&accepted+rejected==16&&cache.ChargedBytes<=options.MaximumBytes,"Concurrent key metadata oversubscribed quota");}
  using(var cache=new AttachmentCacheRuntime(options,()=>now)){int retained=0;foreach(var recipient in original.Skip(1)){try{cache.ReadKey(descriptor.Id,recipient.Device);retained++;}catch(InvalidOperationException error){Check(error.Message=="cache_key_unavailable","Unexpected key recovery error");}}Check(retained==accepted&&cache.ChargedBytes<=options.MaximumBytes,"Concurrent key publication lost durable entries");}
 }Console.WriteLine("PASS key metadata admission before writes, 16 concurrent quota-bounded publications, clean rejection without ciphertext loss and durable restart");}
}
