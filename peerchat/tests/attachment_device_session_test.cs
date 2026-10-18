using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;

class AttachmentDeviceSessionTest {
    static string enrolledChallenge;static byte[] enrolledInput;
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    static string Digest(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();}
    static byte[] SignProof(ECDsaCng key,byte[] input){
        using(var memory=new MemoryStream(input,false))using(var reader=new BinaryReader(memory,Encoding.UTF8)){
            Check(reader.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,30}),"Native proof record header");
            Func<string> text=()=>Encoding.UTF8.GetString(reader.ReadBytes(checked((int)reader.ReadUInt64())));
            Check(text()=="PeerChat/RememberedDeviceProof/v1","Native proof domain");long issued=checked((long)reader.ReadUInt64());string purpose=text();long account=checked((long)reader.ReadUInt64());string device=text(),challenge=text(),digest=text();byte[] exact=reader.ReadBytes(checked((int)reader.ReadUInt64()));
            Check(memory.Position==memory.Length&&input.Length<=1024&&DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-issued<60000&&account==1&&device==Digest(key.Key.Export(CngKeyBlobFormat.EccPublicBlob))&&challenge.Length==64&&(purpose=="enroll"||purpose=="resume")&&digest.Length==64,"Native proof binding");
            if(purpose=="enroll"){enrolledChallenge=challenge;enrolledInput=exact;}
            byte[] publicKey=key.Key.Export(CngKeyBlobFormat.EccPublicBlob),signature=key.SignData(exact);
            using(var output=new MemoryStream())using(var writer=new BinaryWriter(output)){writer.Write(new byte[]{80,84,1,4});writer.Write((ulong)publicKey.Length);writer.Write(publicKey);writer.Write((ulong)signature.Length);writer.Write(signature);return output.ToArray();}
        }
    }
    static DiscoveryClient Open(DiscoveryClient main,Func<byte[],byte[]> signer,string device,string directory,bool first){
        var method=typeof(AttachmentDeviceSession).GetMethods(BindingFlags.NonPublic|BindingFlags.Static).Single(m=>m.Name=="Open"&&m.GetParameters().Length==6);
        try{return (DiscoveryClient)method.Invoke(null,new object[]{main,signer,1L,device,directory,first});}catch(TargetInvocationException error){throw error.InnerException;}
    }
    static DiscoveryClient OpenWithGuard(DiscoveryClient main,Func<byte[],byte[]> signer,string device,string directory,Func<bool> current){
        var method=typeof(AttachmentDeviceSession).GetMethods(BindingFlags.NonPublic|BindingFlags.Static).Single(m=>m.Name=="Open"&&m.GetParameters().Length==7);
        try{return (DiscoveryClient)method.Invoke(null,new object[]{main,signer,1L,device,directory,false,current});}catch(TargetInvocationException error){throw error.InnerException;}
    }
    static void Rejected(Action action,string why){try{action();}catch(InvalidOperationException){return;}throw new Exception(why);}
    static Dictionary<string,object> PendingKeys(DiscoveryClient client,long account,string device){
        string id=Guid.NewGuid().ToString("N");
        var response=client.AttachmentCache(device,new Dictionary<string,object>{{"type","cache_pending_keys"},{"requestId",id},{"after",""}},account,client.SessionToken,client.CertificateSha256);
        if(Convert.ToString(response["state"])=="accepted")Check(Convert.ToString(response["requestId"])==id&&Convert.ToInt64(response["cacheVersion"])==1,"Cache response lost native-proof request correlation or version");
        return response;
    }
    static string Ready(CoreAdapter core,string stage){
        var elapsed=System.Diagnostics.Stopwatch.StartNew();string last="No native event yet";
        while(elapsed.ElapsedMilliseconds<15000){
            foreach(var item in core.Poll()??new CoreEvent[0]){
                if(item.kind==0){Check(item.peer!=null&&item.peer.Length==64,stage+" ready event lacked identity");return item.peer;}
                if(item.kind==8)throw new Exception(stage+" native startup error: "+item.text+" "+item.detail);
                last="kind="+item.kind+" text="+item.text;
            }
            Thread.Sleep(20);
        }
        throw new Exception(stage+" native readiness timed out: "+last);
    }
    static object IdentityClient(DiscoveryClient session,CoreAdapter core,long account,string device,long generation){
        var type=typeof(CoreAdapter).Assembly.GetType("PeerChatDesktop.DeviceIdentityDirectoryClient",true);
        var constructor=type.GetConstructors(BindingFlags.Instance|BindingFlags.NonPublic).Single();
        return constructor.Invoke(new object[]{session,account,device,generation,new Func<byte[],byte[]>(input=>core.Mailbox(14,input)),new Func<bool>(()=>true)});
    }
    static object IdentityCall(object client,string method,params object[] arguments){
        try{return client.GetType().GetMethod(method,BindingFlags.Instance|BindingFlags.NonPublic).Invoke(client,arguments);}
        catch(TargetInvocationException error){throw error.InnerException;}
    }
    static void NativeBridge(string root,int port,string pin){
        string profile=Path.Combine(root,"native-profile");Directory.CreateDirectory(profile);
        var main=new DiscoveryClient("localhost",port,pin);
        try{
            string device;
            using(var core=new CoreAdapter()){
                core.Start(profile,0,"native_device_test");
                device=Ready(core,"First core");
                Check(core.ProfilePeerId(profile)==device,"Native profile identity differed from ready event");
                Check(device!=null&&device.Length==64,"Native profile identity was not created");
                main.Register("native_device_test","fixture-only-password",device);
                Check(main.AccountId==2,"Unexpected native fixture account");
                Check(core.SetGroupAccount(2,"native_device_test"),"Native group account was not published");
                Check(core.SetAuthenticatedFriends(2,1,new FriendConversationBinding[0]),"Native authenticated account was not published");
                Check(core.Mailbox(7).Length==72,"Native proof public key was unavailable");
                string token=main.SessionToken;
                Check(Convert.ToString(PendingKeys(main,2,device)["state"])=="cache_device_session_required","Legacy messaging session reached attachment cache");
                Check(!AttachmentDeviceSession.HasCredential(main,2,device,profile),"Unexpected native credential");
                Rejected(()=>AttachmentDeviceSession.Open(main,core,2,device,profile,true,()=>false),"Stopped profile reached native enrollment");
                Check(!AttachmentDeviceSession.HasCredential(main,2,device,profile),"Stopped profile wrote a credential");
                var enrolled=AttachmentDeviceSession.Open(main,core,2,device,profile,true,()=>true);
                try{Check(enrolled.MatchesAttachmentDeviceSession(2,device)&&AttachmentDeviceSession.HasCredential(main,2,device,profile)&&main.SessionToken==token,"Native mailbox enrollment changed messaging state or failed to persist");string state=Convert.ToString(PendingKeys(enrolled,2,device)["state"]);Check(state=="accepted","Native-proof enrolled session could not access attachment cache: "+state);
                    var identities=IdentityClient(enrolled,core,2,device,1);Check(((byte[])IdentityCall(identities,"RegisterSelf")).Length>0,"Native possession proof was not registered over pinned RPC");
                    var selected=IdentityCall(identities,"ChooseAuthority",device);Check((bool)IdentityCall(identities,"Apply",selected,""),"Explicit own authority did not reconcile through native operation 14");
                    Check(main.SessionToken==token,"Identity enrollment changed the messaging token");
                }
                finally{enrolled.CloseRpc();}
            }
            using(var reopened=new CoreAdapter()){
                reopened.Start(profile,0,"native_device_test");
                Check(Ready(reopened,"Restarted core")==device&&reopened.ProfilePeerId(profile)==device,"Native identity changed after core restart");
                Check(reopened.SetGroupAccount(2,"native_device_test"),"Restarted native group account was not published");
                Check(reopened.SetAuthenticatedFriends(2,2,new FriendConversationBinding[0]),"Restarted native authenticated account was not published");
                Check(reopened.Mailbox(7).Length==72,"Restarted native proof public key was unavailable");
                string token=main.SessionToken;
                var resumed=AttachmentDeviceSession.Open(main,reopened,2,device,profile,false,()=>true);
                try{Check(resumed.MatchesAttachmentDeviceSession(2,device)&&main.SessionToken==token,"Native mailbox resume failed or changed messaging token");string state=Convert.ToString(PendingKeys(resumed,2,device)["state"]);Check(state=="accepted","Native-proof resumed session could not access attachment cache: "+state);
                    var identities=IdentityClient(resumed,reopened,2,device,2);var snapshot=IdentityCall(identities,"Snapshot",2L);Check((bool)IdentityCall(identities,"Apply",snapshot,""),"Restarted native directory did not accept fresh authenticated own snapshot");
                }
                finally{resumed.CloseRpc();}
            }
        }finally{main.CloseRpc();}
    }
    static int Main(string[] args){
        try{
            AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);
            AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
            string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);string certificatePath=args[1],password=args[2];
            string pin;using(var certificate=new X509Certificate2(certificatePath,password))pin=Digest(certificate.RawData);
            var listener=new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0);listener.Start();int port=((System.Net.IPEndPoint)listener.LocalEndpoint).Port;listener.Stop();
            string cacheDirectory=args.Length>3?Path.GetFullPath(args[3]):Path.Combine(root,"cache");Directory.CreateDirectory(cacheDirectory);File.WriteAllText(Path.Combine(cacheDirectory,AttachmentCacheStorage.Marker),Guid.NewGuid().ToString("N"));var storage=AttachmentCacheStorage.Observe(cacheDirectory);
            var cacheOptions=new AttachmentCacheConfiguration{Enabled=true,MaximumBytes=64L*1024*1024,Directory=storage.Directory,ExpectedMountPoint=storage.MountPoint,ExpectedFilesystem=storage.Filesystem,InstanceId=storage.InstanceId,RetentionSeconds=600,PartialIdleSeconds=60};
            using(var probe=new AttachmentCacheRuntime(cacheOptions,()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds())){}
            var config=new ServerConfiguration{DataDirectory=root,DatabasePath=Path.Combine(root,"accounts.sqlite"),LogDirectory=Path.Combine(root,"logs"),CertificatePath=certificatePath,CertificatePassword=password,ManagementPipe="",Port=port,AttachmentCache=cacheOptions};config.Normalize();config.Validate();
            var server=new DiscoveryServer(config);Exception serverError=null;var worker=new Thread(()=>{try{server.Run();}catch(Exception error){serverError=error;}}){IsBackground=true};worker.Start();
            try{
                var ready=System.Diagnostics.Stopwatch.StartNew();bool listening=false;while(ready.ElapsedMilliseconds<10000&&!listening){try{using(var probe=new System.Net.Sockets.TcpClient()){probe.Connect("localhost",port);listening=true;}}catch(System.Net.Sockets.SocketException){Thread.Sleep(20);}}Check(listening,"Server startup");
                using(var key=new ECDsaCng(256)){
                    string device=Digest(key.Key.Export(CngKeyBlobFormat.EccPublicBlob));Func<byte[],byte[]> signer=input=>SignProof(key,input);
                    var main=new DiscoveryClient("localhost",port,pin);main.Register("device_test","fixture-only-password",device);main.RefreshSessionInfo();string mainToken=main.SessionToken;
                    string profile=Path.Combine(root,"profile");Directory.CreateDirectory(profile);
                    Check(!AttachmentDeviceSession.HasCredential(main,main.AccountId,device,profile),"Unexpected saved credential");
                    Rejected(()=>Open(main,signer,device,profile,false),"First enrollment bypassed consent");
                    var first=Open(main,signer,device,profile,true);Check(first.MatchesAttachmentDeviceSession(1,device)&&main.SessionToken==mainToken&&AttachmentDeviceSession.HasCredential(main,1,device,profile),"Enrollment changed main session or failed to persist");
                    var remember=typeof(DiscoveryClient).GetMethod("RememberRequest",BindingFlags.Instance|BindingFlags.NonPublic);var replay=new Dictionary<string,object>{{"sessionToken",mainToken},{"publicIdentity",device},{"challengeId",enrolledChallenge},{"publicKey",Convert.ToBase64String(key.Key.Export(CngKeyBlobFormat.EccPublicBlob))},{"signature",Convert.ToBase64String(key.SignData(enrolledInput))}};
                    Rejected(()=>{try{remember.Invoke(main,new object[]{"remember_enroll",replay});}catch(TargetInvocationException error){throw error.InnerException;}},"Consumed enrollment challenge replayed");
                    string path=Directory.GetFiles(profile,"attachment-device-session-*.dat").Single();byte[] encrypted=File.ReadAllBytes(path);Check(encrypted.Length>32&&!Encoding.UTF8.GetString(encrypted).Contains("credential"),"Credential sidecar was not protected");
                    Rejected(()=>OpenWithGuard(main,signer,device,profile,()=>false),"Stopped profile resumed saved authorization");Check(File.ReadAllBytes(path).SequenceEqual(encrypted),"Stopped profile changed credential");
                    var resumed=Open(main,signer,device,profile,false);Check(resumed.MatchesAttachmentDeviceSession(1,device)&&resumed.SessionToken!=first.SessionToken&&main.SessionToken==mainToken,"Saved credential resume failed");
                    main.RefreshSessionInfo();Check(main.SessionToken==mainToken&&main.AccountId==1,"Device cache session changed main messaging authentication");
                    string copiedProfile=Path.Combine(root,"other-profile");Directory.CreateDirectory(copiedProfile);File.Copy(path,Path.Combine(copiedProfile,Path.GetFileName(path)));Rejected(()=>Open(main,signer,device,copiedProfile,true),"Copied credential crossed profile or reenrolled");
                    resumed.Logout();Rejected(()=>Open(main,signer,device,profile,true),"Revoked credential automatically reenrolled");Check(main.SessionToken==mainToken&&File.ReadAllBytes(path).SequenceEqual(encrypted),"Revocation changed main token or saved credential");
                    first.CloseRpc();resumed.CloseRpc();main.CloseRpc();
                }
                NativeBridge(root,port,pin);
            }finally{server.RequestStop();Check(worker.Join(12000),"Server shutdown");if(serverError!=null)throw serverError;}
            Console.WriteLine("PASS attachment device session: pinned enrollment/replay, native mailbox proof and cache RPC, legacy rejection, protected profile binding, restart resume, unchanged messaging token, revocation fail closed");return 0;
        }catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
