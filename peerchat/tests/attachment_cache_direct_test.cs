#if PEERCHAT_PORTABLE
using ECDsaCng = PortableTestKey;
#endif
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using PeerChat.Server;

class DirectCacheTest {
 static int Main(string[] args){try{if(args.Length==3&&args[0]=="--legacy-runtime"){string root=Path.GetFullPath(args[2]);AttachmentCacheRuntimeTest.DirectChecks(root);AttachmentCacheRuntimeTest.DirectLegacyRollback(Path.GetFullPath(args[1]),Path.Combine(root,"encrypted-cache"));}else AttachmentCacheRuntimeTest.DirectChecks(Path.GetFullPath(args[0]));return 0;}catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
partial class AttachmentCacheRuntimeTest {
 internal static void DirectLegacyRollback(string assemblyPath,string directory){
  // Exercise the previous shipping assembly on an isolated cache created by
  // the new DM implementation. No live cache or older database is restored.
  var assembly=System.Reflection.Assembly.LoadFrom(assemblyPath);
  var optionsType=assembly.GetType("PeerChat.ServerShared.AttachmentCacheConfiguration",true);
  var oldOptions=Activator.CreateInstance(optionsType);var options=Options(directory);options.RetentionSeconds=48*60*60;
  foreach(var field in options.GetType().GetFields())optionsType.GetField(field.Name).SetValue(oldOptions,field.GetValue(options));
  var runtime=assembly.GetType("PeerChat.Server.AttachmentCacheRuntime",true);var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.Public|System.Reflection.BindingFlags.NonPublic;
  var constructor=runtime.GetConstructor(flags,null,new[]{optionsType,typeof(Func<long>)},null);Check(constructor!=null,"Previous cache constructor missing");
  long clock=DateTimeOffset.UtcNow.ToUnixTimeSeconds();
  using(var old=(IDisposable)constructor.Invoke(new object[]{oldOptions,new Func<long>(()=>clock)})){
   var rows=runtime.GetMethod("Rows",flags);Check(rows!=null,"Previous cache bounded query missing");
   var before=(List<string[]>)rows.Invoke(old,new object[]{"SELECT id,durable_offset,digest FROM objects ORDER BY id",128,3});Check(before.Count>=3,"Rollback fixture missing new DM objects");
   runtime.GetMethod("Cleanup",flags).Invoke(old,new object[]{1});
   var retained=(List<string[]>)rows.Invoke(old,new object[]{"SELECT id,durable_offset,digest FROM objects ORDER BY id",128,3});Check(before.Select(x=>String.Join("|",x)).SequenceEqual(retained.Select(x=>String.Join("|",x))),"Previous runtime altered unexpired DM bytes");
   clock+=8*24*60*60;runtime.GetMethod("Cleanup",flags).Invoke(old,new object[]{256});
   Check(((List<string[]>)rows.Invoke(old,new object[]{"SELECT id FROM objects",128,1})).Count==0,"Previous cleanup could not expire new DM objects");
  }
  using(var reopened=new AttachmentCacheRuntime(options,()=>clock)){Check(reopened.ChargedBytes<=options.MaximumBytes,"New runtime rejected safely cleaned rollback cache");}
  Console.WriteLine("PASS previous shipping cache assembly opens new DM storage, preserves unexpired offsets/digests, expires objects with legacy cleanup and reopens under new runtime; isolated cache only");
 }
 static string DId(ECDsaCng signer){return MailboxProtocol.Digest(signer.Key.Export(CngKeyBlobFormat.EccPublicBlob));}
 static AttachmentCacheDescriptor DirectDescriptor(ECDsaCng key,long sender,long recipient,DirectCacheAudience audience,byte[] ciphertext){
  using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream)){
   writer.Write(new byte[]{80,84,1,30});Text(writer,"PeerChat/dm-cache-upload/v1");Text(writer,Guid.NewGuid().ToString("N"));Text(writer,Guid.NewGuid().ToString("N"));Text(writer,DId(key));writer.Write((ulong)sender);writer.Write((ulong)recipient);Text(writer,audience.Relationship);Text(writer,audience.Digest);writer.Write(1UL);writer.Write((ulong)ciphertext.Length);using(var sha=SHA256.Create())Field(writer,sha.ComputeHash(ciphertext));Field(writer,key.Key.Export(CngKeyBlobFormat.EccPublicBlob));Field(writer,SignKeyBytes(key,stream.ToArray()));return AttachmentCacheDescriptor.Parse(stream.ToArray());
  }
 }
 static void DFriend(AccountDatabase db){Check(db.SendFriendRequest(1,2)=="pending"&&db.RespondToRequest(2,1,true)=="friends","DM friendship");}
 static void DRegister(AccountDatabase db,ECDsaCng key,long account,byte[] binding){Check(db.RegisterMailboxKey(account,DId(key),key.Key.Export(CngKeyBlobFormat.EccPublicBlob),binding)=="accepted","DM verified key registration");}
 static byte[] DCapability(ECDsaCng key,long account,byte[] wire){var binding=MailboxProtocol.Binding.Parse(wire);using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream)){
  writer.Write(new byte[]{80,84,1,30});Text(writer,"PeerChat/dm-cache-device/v1");writer.Write((ulong)account);Text(writer,DId(key));Text(writer,binding.Key);writer.Write((ulong)binding.Generation);writer.Write(1UL);writer.Write(1UL);Field(writer,key.Key.Export(CngKeyBlobFormat.EccPublicBlob));Field(writer,SignKeyBytes(key,stream.ToArray()));return stream.ToArray();
 }}
 static void DRevoke(AccountDatabase db,ECDsaCng key,long account,byte[] wire){var b=MailboxProtocol.Binding.Parse(wire);using(var stream=KD("PeerChat/device-recipient-revoke/v1")){KN(stream,1,2);KN(stream,(ulong)account,8);KT(stream,b.Device);KT(stream,b.Key);KN(stream,(ulong)b.Generation,8);KB(stream,SignKeyBytes(key,stream.ToArray()));Check(db.RevokeMailboxKey(account,b.Device,key.Key.Export(CngKeyBlobFormat.EccPublicBlob),stream.ToArray())=="revoked","DM key revocation");}}
 static byte[] DNotice(ECDsaCng source,AttachmentCacheDescriptor descriptor,byte[] wire,string message=null,byte marker=17){
  // Server-only opaque ciphertext/signature fixture. Native/Application suites
  // independently prove actual HPKE private-body opening and file verification.
  var binding=MailboxProtocol.Binding.Parse(wire);using(var header=KD("PeerChat/mailbox-envelope/v1"))using(var signed=KD("PeerChat/mailbox-device-signature/v1")){
   KS(header);KN(header,(ulong)descriptor.Account,8);KT(header,descriptor.Sender);KN(header,(ulong)binding.Account,8);KT(header,binding.Device);KT(header,binding.Key);KN(header,(ulong)binding.Generation,8);KT(header,message??descriptor.Message);KN(header,(ulong)(now*1000),8);KN(header,(ulong)(now*1000+3600000),8);KN(header,1,8);header.WriteByte(1);
   KB(signed,header.ToArray());KB(signed,binding.Public);KB(signed,Enumerable.Repeat(marker,32).ToArray());KB(signed,SignKeyBytes(source,signed.ToArray()));return signed.ToArray();
  }
 }
 static byte[] DVerified(ECDsaCng key,AttachmentCacheDescriptor descriptor,byte[] bindingWire){var binding=MailboxProtocol.Binding.Parse(bindingWire);using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream)){
  writer.Write(new byte[]{80,84,1,30});Text(writer,"PeerChat/dm-attachment-verified-file/v1");writer.Write((ulong)descriptor.Account);Text(writer,descriptor.Sender);writer.Write((ulong)binding.Account);Text(writer,binding.Device);Text(writer,binding.Key);writer.Write((ulong)binding.Generation);Text(writer,descriptor.Message);Text(writer,descriptor.Id);Text(writer,MailboxProtocol.Digest(descriptor.Wire));Text(writer,descriptor.AudienceDigest);Field(writer,key.Key.Export(CngKeyBlobFormat.EccPublicBlob));writer.Write((ulong)(now*1000));Field(writer,SignKeyBytes(key,stream.ToArray()));return stream.ToArray();
 }}
 internal static void DirectChecks(string root){
  Directory.CreateDirectory(root);long previous=now;now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();
  try{using(var sender=new ECDsaCng(256))using(var first=new ECDsaCng(256))using(var second=new ECDsaCng(256))using(var outsider=new ECDsaCng(256)){
   string database=Path.Combine(root,"accounts.db"),source=DId(sender),reader=DId(first),sibling=DId(second),other=DId(outsider);
   var options=Options(Fixture(root,"encrypted-cache"));options.RetentionSeconds=48*60*60;
   var sourceKey=RecipientBinding(sender,1);var firstKey=RecipientBinding(first,2);var secondKey=RecipientBinding(second,2);
   AttachmentCacheDescriptor descriptor;DirectCacheAudience audience;byte[] payload=Payload(1024*1024+37);string oldLease;
   using(var db=new AccountDatabase(database))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
    Check(db.CreateAccountWithDevice("sender",new byte[16],new byte[32],source)==1&&db.CreateAccountWithDevice("recipient",new byte[16],new byte[32],reader)==2&&db.CreateAccountWithDevice("outsider",new byte[16],new byte[32],other)==3,"DM accounts");Check(db.AddDevice(2,sibling),"DM sibling owned device");db.InitializeMailbox();DFriend(db);DRegister(db,sender,1,sourceKey);DRegister(db,first,2,firstKey);DRegister(db,second,2,secondKey);
    var admission=new AttachmentCacheAdmission(db,cache);var gated=new AttachmentCacheRequests(admission);
    Rpc(gated,1,source,"cache_dm_capability","cache_dm_feature_gated");var rpc=new AttachmentCacheRequests(admission,true);
    Rpc(rpc,1,source,"cache_dm_capability","accepted");
    Reject(()=>admission.PrepareDirect(1,source,()=>true,2),"cache_dm_source_upgrade_required");
    var capability=DCapability(sender,1,sourceKey);Check(admission.RegisterDirect(1,source,()=>true,capability)&&admission.RegisterDirect(1,source,()=>true,capability),"Idempotent source capability");
    Reject(()=>admission.PrepareDirect(1,source,()=>true,2),"cache_dm_recipient_upgrade_required");
    var forged=(byte[])capability.Clone();forged[forged.Length-1]^=1;Reject(()=>admission.RegisterDirect(1,source,()=>true,forged),"cache_dm_capability_binding_invalid");
    Reject(()=>admission.RegisterDirect(2,reader,()=>true,capability),"cache_dm_capability_binding_invalid");
    admission.RegisterDirect(2,reader,()=>true,DCapability(first,2,firstKey));admission.RegisterDirect(2,sibling,()=>true,DCapability(second,2,secondKey));
    var prepared=Rpc(rpc,1,source,"cache_dm_prepare","accepted","recipientAccountId",2L);
    audience=DirectCacheAudience.Parse(AttachmentCacheRequests.Decode((string)prepared["audience"],32768),1,source,2);Check(audience.Recipients.Count==2,"Offline devices omitted from frozen audience");descriptor=DirectDescriptor(sender,1,2,audience,payload);
    Reject(()=>admission.Admit(1,source,()=>true,descriptor.Wire),"cache_scope_mismatch");Reject(()=>admission.AdmitDirect(1,source,()=>false,descriptor.Wire,audience.Wire),"cache_dm_authority_stale");
    var tampered=(byte[])descriptor.Wire.Clone();tampered[tampered.Length-1]^=1;Reject(()=>AttachmentCacheDescriptor.Parse(tampered),"*");
    var changed=(byte[])audience.Wire.Clone();changed[changed.Length-1]^=1;Reject(()=>admission.AdmitDirect(1,source,()=>true,descriptor.Wire,changed),"*");
    Check(!admission.AdmitDirect(1,source,()=>true,descriptor.Wire,audience.Wire).Complete,"Admission claimed plaintext/file completion");admission.AdmitDirect(1,source,()=>true,descriptor.Wire,audience.Wire);
    Reject(()=>admission.Inspect(2,reader,()=>true,descriptor.Id),"cache_scope_mismatch");Reject(()=>admission.InspectDirect(3,other,()=>true,descriptor.Id),"cache_dm_original_recipient_required");Reject(()=>admission.AcquireDirect(2,reader,()=>true,descriptor.Id,true),"cache_sender_binding_mismatch");
    oldLease=admission.AcquireDirect(1,source,()=>true,descriptor.Id,true);var block=payload.Take(65536).ToArray();Check(admission.AppendDirect(1,source,()=>true,oldLease,0,block)==65536,"DM prefix");Check(admission.AppendDirect(1,source,()=>true,oldLease,0,block)==65536,"DM duplicate inflated progress");
    var corrupt=(byte[])block.Clone();corrupt[0]^=1;Reject(()=>admission.AppendDirect(1,source,()=>true,oldLease,0,corrupt),"cache_conflicting_chunk");Reject(()=>admission.CompleteDirect(1,source,()=>true,oldLease),"cache_upload_incomplete");
   }
   using(var db=new AccountDatabase(database))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
    db.InitializeMailbox();var admission=new AttachmentCacheAdmission(db,cache);var rpc=new AttachmentCacheRequests(admission,true);
    Reject(()=>admission.AppendDirect(1,source,()=>true,oldLease,65536,new byte[32768]),"cache_lease_expired");Check(admission.InspectDirect(1,source,()=>true,descriptor.Id).Offset==65536,"Restart lost durable upload prefix");
    string upload=admission.AcquireDirect(1,source,()=>true,descriptor.Id,true);for(long offset=65536;offset<payload.Length;){int count=(int)Math.Min(65536,payload.Length-offset);offset=admission.AppendDirect(1,source,()=>true,upload,offset,payload.Skip((int)offset).Take(count).ToArray());}
    Check(admission.CompleteDirect(1,source,()=>true,upload)&&admission.CompleteDirect(1,source,()=>true,upload),"DM completion idempotence");admission.ReleaseDirect(1,source,()=>true,upload,true);
    var notice=DNotice(sender,descriptor,firstKey);var siblingNotice=DNotice(sender,descriptor,secondKey);
    Reject(()=>admission.PublishDirect(2,reader,()=>true,descriptor.Id,notice),"cache_sender_binding_mismatch");
    Reject(()=>admission.PublishDirect(1,source,()=>true,descriptor.Id,DNotice(sender,descriptor,firstKey,Guid.NewGuid().ToString("N"))),"cache_dm_notice_binding_invalid");
    var broken=(byte[])notice.Clone();broken[broken.Length-1]^=1;Reject(()=>admission.PublishDirect(1,source,()=>true,descriptor.Id,broken),"cache_dm_notice_not_authorized");
    Check(admission.PublishDirect(1,source,()=>true,descriptor.Id,notice)&&admission.PublishDirect(1,source,()=>true,descriptor.Id,notice)&&admission.PublishDirect(1,source,()=>true,descriptor.Id,siblingNotice),"DM exact notice publication");
    Reject(()=>admission.PublishDirect(1,source,()=>true,descriptor.Id,DNotice(sender,descriptor,firstKey,marker:18)),"cache_dm_notice_conflicting_message");
    Check(db.FetchMailbox(2,reader).Count==1&&db.FetchMailbox(2,sibling).Count==1,"Independent recipient mailbox notices missing");
    string download=admission.AcquireDirect(2,reader,()=>true,descriptor.Id,false);Check(admission.ReadDirect(2,reader,()=>true,download,0,65536).SequenceEqual(payload.Take(65536)),"DM durable read");admission.ProgressDirect(2,reader,()=>true,download,65536);admission.ProgressDirect(2,reader,()=>true,download,65536);
    Check(cache.RecipientOffset(descriptor.Id,reader)==65536&&cache.RecipientOffset(descriptor.Id,sibling)==0,"One recipient consumed another's offset");
    var receipt=DVerified(first,descriptor,firstKey);Reject(()=>admission.VerifyDirectReceipt(2,reader,()=>true,descriptor.Id,receipt),"cache_recipient_ciphertext_incomplete");
    for(long offset=65536;offset<payload.Length;){var block=admission.ReadDirect(2,reader,()=>true,download,offset,65536);offset+=block.Length;admission.ProgressDirect(2,reader,()=>true,download,offset);}
    Check(admission.VerifyDirectReceipt(2,reader,()=>true,descriptor.Id,receipt)&&!admission.VerifyDirectReceipt(2,reader,()=>true,descriptor.Id,receipt),"DM duplicate verification inflated receipt");
    var verifiedRows=(List<Dictionary<string,object>>)admission.DirectReceipts(1,source,()=>true,descriptor.Id,"")["items"];
    Check((bool)verifiedRows.Single(x=>(string)x["device"]==reader)["plaintextVerified"]&&!(bool)verifiedRows.Single(x=>(string)x["device"]==sibling)["plaintextVerified"],"One receipt completed another device");
    var badReceipt=(byte[])receipt.Clone();badReceipt[badReceipt.Length-1]^=1;Reject(()=>admission.VerifyDirectReceipt(2,reader,()=>true,descriptor.Id,badReceipt),"cache_verification_receipt_invalid");
    // Rotation preserves an explicitly frozen, unrevoked retired key for an
    // existing attachment, but new admission requires the new capability.
    var rotated=RecipientBinding(first,2,generation:2,predecessor:MailboxProtocol.Binding.Parse(firstKey).Key);
    DRegister(db,first,2,rotated);Check(admission.InspectDirect(2,reader,()=>true,descriptor.Id).Complete,"Retired original key lost authorized attachment");
    Reject(()=>admission.PrepareDirect(1,source,()=>true,2),"cache_dm_recipient_upgrade_required");
    admission.RegisterDirect(2,reader,()=>true,DCapability(first,2,rotated));
    Reject(()=>admission.AdmitDirect(1,source,()=>true,descriptor.Wire,audience.Wire),"cache_dm_audience_changed");
    long beforeExpiry=now;now=cache.Inspect(descriptor.Id).Expires-1;
    string expiryLease=admission.AcquireDirect(2,sibling,()=>true,descriptor.Id,false);
    Check(admission.ReadDirect(2,sibling,()=>true,expiryLease,0,65536).Length==65536,"Pre-expiry lease read failed");now+=2;
    Reject(()=>admission.AcquireDirect(2,sibling,()=>true,descriptor.Id,false),"cache_attachment_expired");
    Reject(()=>admission.ReadDirect(2,sibling,()=>true,expiryLease,0,65536),"cache_attachment_expired");
    Reject(()=>admission.ProgressDirect(2,sibling,()=>true,expiryLease,65536),"cache_attachment_expired");
    Reject(()=>admission.InspectDirect(2,sibling,()=>true,descriptor.Id),"cache_attachment_expired");
    Check(admission.ReleaseDirect(2,sibling,()=>true,expiryLease,false),"Expired attachment lease could not be released");now=beforeExpiry;
    // The expiry clock advance deliberately retired old leases. Establish a
    // fresh authorized retired-key lease before testing explicit revocation.
    download=admission.AcquireDirect(2,reader,()=>true,descriptor.Id,false);
    Check(admission.ReadDirect(2,reader,()=>true,download,0,65536).Length==65536,"Retired-key lease was not authorized before revocation");
    DRevoke(db,first,2,firstKey);Reject(()=>admission.ReadDirect(2,reader,()=>true,download,65536,65536),"cache_dm_original_recipient_required");Check(admission.InspectDirect(2,sibling,()=>true,descriptor.Id).Complete,"Revoked sibling blocked eligible recipient");
    // A newly verified recipient is not silently granted access to an old blob.
    Check(db.AddDevice(2,other)==false,"Foreign identity moved account");using(var late=new ECDsaCng(256)){Check(db.AddDevice(2,DId(late)),"Late fixture device");var lateBinding=RecipientBinding(late,2);DRegister(db,late,2,lateBinding);admission.RegisterDirect(2,DId(late),()=>true,DCapability(late,2,lateBinding));Reject(()=>admission.InspectDirect(2,DId(late),()=>true,descriptor.Id),"cache_dm_original_recipient_required");}
    db.RemoveFriend(1,2);Reject(()=>admission.InspectDirect(2,sibling,()=>true,descriptor.Id),"cache_dm_authority_stale");DFriend(db);Reject(()=>admission.InspectDirect(2,sibling,()=>true,descriptor.Id),"cache_dm_authority_stale");
    var newAudience=DirectCacheAudience.Parse(admission.PrepareDirect(1,source,()=>true,2),1,source,2);Check(newAudience.Relationship!=audience.Relationship,"Re-friend reused historical authority");
    var current=DirectDescriptor(sender,1,2,newAudience,Payload(1));admission.AdmitDirect(1,source,()=>true,current.Wire,newAudience.Wire);Upload(cache,current,Payload(1));db.SetBlocked(2,1,true);Reject(()=>admission.InspectDirect(1,source,()=>true,current.Id),"cache_dm_authority_stale");
    db.SetBlocked(2,1,false);DFriend(db);
    var beforeSourceRevoke=DirectCacheAudience.Parse(admission.PrepareDirect(1,source,()=>true,2),1,source,2);
    var revokedSourceObject=DirectDescriptor(sender,1,2,beforeSourceRevoke,Payload(1));admission.AdmitDirect(1,source,()=>true,revokedSourceObject.Wire,beforeSourceRevoke.Wire);Upload(cache,revokedSourceObject,Payload(1));
    string liveReader=admission.AcquireDirect(2,sibling,()=>true,revokedSourceObject.Id,false);Check(admission.InspectDirect(2,sibling,()=>true,revokedSourceObject.Id).Complete,"Source revocation fixture was not authorized");
    DRevoke(db,sender,1,sourceKey);Reject(()=>admission.ReadDirect(2,sibling,()=>true,liveReader,0,65536),"cache_dm_authority_stale");Reject(()=>admission.InspectDirect(1,source,()=>true,revokedSourceObject.Id),"cache_dm_authority_stale");
    Check(cache.ChargedBytes<=options.MaximumBytes,"DM storage exceeded quota");
   }
  }}finally{now=previous;}
  Console.WriteLine("PASS typed DM scope, signed immutable offline audience, duplicate durable chunks, upload restart, independent recipient offsets, revocation, late-device/cross-account denial, unfriend/re-add and block invalidation; no plaintext completion claim");
 }
}
