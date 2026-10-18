#if PEERCHAT_PORTABLE
using ECDsaCng=PortableTestKey;
#endif
using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using PeerChat.Server;

internal static class DeviceManagementCacheTest {
 public static int Main(string[] args){try{AttachmentCacheRuntimeTest.ManagedSourceRevocation(Path.GetFullPath(args[0]));Console.WriteLine("PASS device revoke/cache boundary: retained signed source, only existing original recipient downloads, revoked requester/key and new admission denied");return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
partial class AttachmentCacheRuntimeTest {
 internal static void ManagedSourceRevocation(string root){
  Directory.CreateDirectory(root);long previous=now;now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();
  try{using(var sourceKey=new ECDsaCng(256))using(var recipientKey=new ECDsaCng(256))using(var managerKey=new ECDsaCng(256))using(var recipientManagerKey=new ECDsaCng(256)){
   string source=DId(sourceKey),recipient=DId(recipientKey),manager=DId(managerKey),token=new string('b',64),credential=new string('c',64);
   var sourceBinding=RecipientBinding(sourceKey,1);var recipientBinding=RecipientBinding(recipientKey,2);var options=Options(Fixture(root,"ciphertext"));options.RetentionSeconds=3600;
   using(var db=new AccountDatabase(Path.Combine(root,"accounts.sqlite")))using(var cache=new AttachmentCacheRuntime(options,()=>now)){
    Check(db.CreateAccountWithDevice("source",new byte[16],new byte[32],source)==1&&db.CreateAccountWithDevice("recipient",new byte[16],new byte[32],recipient)==2,"Managed source fixture accounts");db.AddDevice(1,manager);db.InitializeMailbox();DFriend(db);DRegister(db,sourceKey,1,sourceBinding);DRegister(db,recipientKey,2,recipientBinding);
    var admission=new AttachmentCacheAdmission(db,cache);admission.EnableDirect();admission.RegisterDirect(1,source,()=>true,DCapability(sourceKey,1,sourceBinding));admission.RegisterDirect(2,recipient,()=>true,DCapability(recipientKey,2,recipientBinding));
    var audience=DirectCacheAudience.Parse(admission.PrepareDirect(1,source,()=>true,2),1,source,2);var payload=Payload(32768);var descriptor=DirectDescriptor(sourceKey,1,2,audience,payload);
    admission.AdmitDirect(1,source,()=>true,descriptor.Wire,audience.Wire);Upload(cache,descriptor,payload);
    var groupProof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(sourceKey,new string('1',32),1)));string group=groupProof.ServerGroupId;
    Check(db.RegisterGroup(1,source,groupProof)!=null&&db.GroupInvite(group,1,source,2,recipient,1)!=null&&db.GroupRespond(group,2,recipient,true,2)!=null,"Group revoke fixture roster");
    var groupDescriptor=Descriptor(sourceKey,32768,payload,group:group,epoch:3);admission.Admit(1,source,()=>true,groupDescriptor.Wire);Upload(cache,groupDescriptor,payload);
    string download=admission.AcquireDirect(2,recipient,()=>true,descriptor.Id,false);Check(admission.ReadDirect(2,recipient,()=>true,download,0,1024).SequenceEqual(payload.Take(1024)),"Original recipient initial read");
    db.RememberDevice(1,manager,credential);db.CreateDeviceSession(token,1,manager,now+3600,credential);Check(db.RevokeManagedDevice(1,manager,token,source,db.DeviceAuthorityRevision(1),new string('d',64),new string('e',64))==null,"Managed source revoke");
    // This is already committed ciphertext. Requester is the still-authorized ORIGINAL recipient,
    // not the revoked uploader; the original signed source/key/provenance must remain intact.
    Check(admission.ReadDirect(2,recipient,()=>true,download,1024,1024).SequenceEqual(payload.Skip(1024).Take(1024)),"Revoking uploader erased original recipient download authority");
    Check(!db.GroupMessageAuthorized(group,3,1,source)&&db.GroupMessageAuthorized(group,3,2,recipient),"Group current participation ignored tombstone");Check(db.AttachmentRecipients(group).Count==1&&db.AttachmentRecipients(group)[0].Device==recipient,"New group cache audience retained revoked participant");
    string groupRead=admission.Acquire(2,recipient,()=>true,groupDescriptor.Id,false);Check(admission.Read(2,recipient,()=>true,groupRead,0,1024).SequenceEqual(payload.Take(1024)),"Group original recipient lost retained ciphertext");Reject(()=>admission.Acquire(1,source,()=>true,groupDescriptor.Id,false),"cache_group_not_authorized");
    Reject(()=>admission.ReadDirect(2,recipient,()=>false,download,0,1024),"cache_dm_authority_stale");
    Reject(()=>admission.InspectDirect(1,source,()=>true,descriptor.Id),"cache_dm_authority_stale");
    Reject(()=>admission.AdmitDirect(1,source,()=>true,descriptor.Wire,audience.Wire),"cache_dm_authority_stale");
    Reject(()=>admission.AcquireDirect(1,source,()=>true,descriptor.Id,true),"cache_dm_authority_stale");
    Reject(()=>admission.AcquireDirect(2,recipient,()=>true,descriptor.Id,true),"cache_sender_binding_mismatch");
    // Test-only transactional fault injections never modify production records.
    db.Execute("BEGIN IMMEDIATE;UPDATE mailbox_recipient_keys SET proof=X'00' WHERE device='"+source+"';");try{Reject(()=>admission.ReadDirect(2,recipient,()=>true,download,0,1024),"cache_dm_authority_stale");}finally{db.Execute("ROLLBACK;");}
    db.Execute("BEGIN IMMEDIATE;UPDATE mailbox_recipient_keys SET state='revoked' WHERE device='"+source+"';");try{Reject(()=>admission.ReadDirect(2,recipient,()=>true,download,0,1024),"cache_dm_authority_stale");}finally{db.Execute("ROLLBACK;");}
    string recipientManager=DId(recipientManagerKey),recipientToken=new string('f',64);Check(db.AddDevice(2,recipientManager),"Recipient manager fixture enrollment");db.RememberDevice(2,recipientManager,new string('9',64));db.CreateDeviceSession(recipientToken,2,recipientManager,now+3600,new string('9',64));
    Reject(()=>admission.ReadDirect(2,recipientManager,()=>true,download,0,1024),"cache_lease_device_mismatch");
    Check(db.RevokeManagedDevice(2,recipientManager,recipientToken,recipient,db.DeviceAuthorityRevision(2),new string('7',64),new string('8',64))==null,"Recipient managed revoke");Reject(()=>admission.ReadDirect(2,recipient,()=>true,download,0,1024),"cache_dm_authority_stale");Reject(()=>admission.Read(2,recipient,()=>true,groupRead,0,1024),"cache_group_not_authorized");
    Check(cache.Inspect(descriptor.Id).Complete,"Verified encrypted object was deleted");
   }
  }}finally{now=previous;}
 }
}
