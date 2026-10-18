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

static class DeviceManagerAdapterTest {
    static int checks;
    static void Check(bool ok,string reason){checks++;if(!ok)throw new Exception(reason);}
    static string Hash(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();}
    static byte[] Sign(ECDsaCng key,byte[] record){
        using(var stream=new MemoryStream(record))using(var reader=new BinaryReader(stream)){
            Check(reader.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,30}),"Typed native record");Func<string> text=()=>Encoding.UTF8.GetString(reader.ReadBytes(checked((int)reader.ReadUInt64())));string domain=text();long localIssued=checked((long)reader.ReadUInt64());Check(Math.Abs(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-localIssued)<60000,"Fresh local issue time");string canonical,actor;
            if(domain=="PeerChat/RememberedDeviceProof/v1"){
                string purpose=text();long account=checked((long)reader.ReadUInt64());actor=text();string nonce=text(),binding=text();Check(purpose=="enroll"||purpose=="resume","Remembered purpose");canonical="PeerChat remembered device v1|"+purpose+"|"+account+"|"+actor+"|"+nonce+"|"+binding;
            }else{
                Check(domain=="PeerChat/DeviceManagementProof/v1","Scoped revoke domain");long account=checked((long)reader.ReadUInt64());actor=text();string target=text();long revision=checked((long)reader.ReadUInt64());string pin=text(),epoch=text();long issued=checked((long)reader.ReadUInt64()),expires=checked((long)reader.ReadUInt64());string nonce=text(),binding=text();Check(actor!=target&&expires-issued==60,"Revocation binding");canonical="PeerChat device management v1|revoke|"+account+"|"+actor+"|"+target+"|"+revision+"|"+pin+"|"+epoch+"|"+issued+"|"+expires+"|"+nonce+"|"+binding;
            }
            byte[] input=reader.ReadBytes(checked((int)reader.ReadUInt64()));Check(stream.Position==stream.Length&&input.SequenceEqual(Encoding.ASCII.GetBytes(canonical)),"Canonical original-message proof");byte[] pub=key.Key.Export(CngKeyBlobFormat.EccPublicBlob);Check(actor==Hash(pub),"Actor is signing fixture device");byte[] sig=key.SignData(input);
            using(var output=new MemoryStream())using(var writer=new BinaryWriter(output)){writer.Write(new byte[]{80,84,1,4});writer.Write((ulong)pub.Length);writer.Write(pub);writer.Write((ulong)sig.Length);writer.Write(sig);return output.ToArray();}
        }
    }
    static DiscoveryClient Open(DiscoveryClient main,ECDsaCng key,string peer,string path,bool first){var method=typeof(AttachmentDeviceSession).GetMethods(BindingFlags.NonPublic|BindingFlags.Static).Single(m=>m.Name=="Open"&&m.GetParameters().Length==6);try{return (DiscoveryClient)method.Invoke(null,new object[]{main,new Func<byte[],byte[]>(input=>Sign(key,input)),main.AccountId,peer,path,first});}catch(TargetInvocationException error){throw error.InnerException;}}
    static void Denied(Action action,string reason){bool rejected=false;try{action();}catch(Exception error){if(error is DiscoveryRequestException||error is InvalidDataException||error is InvalidOperationException||error is IOException)rejected=true;else throw;}Check(rejected,reason);}
    static DiscoveryServer Start(ServerConfiguration config,out Thread thread){var server=new DiscoveryServer(config);thread=new Thread(()=>server.Run()){IsBackground=true};thread.Start();var deadline=DateTime.UtcNow.AddSeconds(10);while(DateTime.UtcNow<deadline){try{using(var socket=new System.Net.Sockets.TcpClient()){socket.Connect("localhost",config.Port);return server;}}catch(System.Net.Sockets.SocketException){Thread.Sleep(20);}}throw new Exception("Server startup timed out");}
    static void Stop(DiscoveryServer server,Thread thread){server.RequestStop();Check(thread.Join(12000),"Server shutdown");}
    static int Main(string[] args){try{
        AppContext.SetSwitch("Switch.System.IO.UseLegacyPathHandling",false);AppContext.SetSwitch("Switch.System.IO.BlockLongPaths",false);
        string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);string pin;using(var cert=new X509Certificate2(args[1],args[2]))pin=Hash(cert.RawData);
        var listener=new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0);listener.Start();int port=((System.Net.IPEndPoint)listener.LocalEndpoint).Port;listener.Stop();
        var config=new ServerConfiguration{DataDirectory=root,DatabasePath=Path.Combine(root,"accounts.sqlite"),LogDirectory=Path.Combine(root,"logs"),CertificatePath=args[1],CertificatePassword=args[2],ManagementPipe="",Port=port};config.Normalize();config.Validate();Thread thread;var server=Start(config,out thread);
        var clients=new List<DiscoveryClient>();
        try{using(var actorKey=new ECDsaCng(256))using(var targetKey=new ECDsaCng(256))using(var otherKey=new ECDsaCng(256)){
            string actor=Hash(actorKey.Key.Export(CngKeyBlobFormat.EccPublicBlob)),target=Hash(targetKey.Key.Export(CngKeyBlobFormat.EccPublicBlob)),other=Hash(otherKey.Key.Export(CngKeyBlobFormat.EccPublicBlob));
            var main=new DiscoveryClient("localhost",port,pin);clients.Add(main);main.Register("manager_test","fixture-only-password",actor);main.ReportPresence(actor);
            var second=new DiscoveryClient("localhost",port,pin);clients.Add(second);second.Login("manager_test","fixture-only-password");second.ReportPresence(target);
            var foreign=new DiscoveryClient("localhost",port,pin);clients.Add(foreign);foreign.Register("other_account","fixture-only-password",other);
            string actorProfile=Path.Combine(root,"actor-profile"),targetProfile=Path.Combine(root,"target-profile");Directory.CreateDirectory(actorProfile);Directory.CreateDirectory(targetProfile);
            var bound=Open(main,actorKey,actor,actorProfile,true);clients.Add(bound);var targetBound=Open(second,targetKey,target,targetProfile,true);clients.Add(targetBound);
            Func<byte[],byte[]> signer=input=>Sign(actorKey,input);var manager=new DeviceManagementClient(bound,main.AccountId,actor,signer,()=>true);
            var first=manager.PublishMetadata("Windows","fixture.1");Check(first.Devices.Length==2&&first.Devices.All(d=>d.Identity!=other),"Own-account inventory isolation");Check(first.Devices.Single(d=>d.Identity==actor).Current,"Current device exact match");Check(first.Devices.Single(d=>d.Identity==actor).Platform=="Windows"&&first.Devices.Single(d=>d.Identity==actor).Version=="fixture.1","Authenticated self-reported metadata");Check(manager.PublishMetadata("Windows","fixture.1").Revision==first.Revision,"Unchanged metadata does not bump authority revision");
            var named=manager.Rename(first,target,"Phone Café");Check(named.Revision>first.Revision&&named.Devices.Single(d=>d.Identity==target).Name=="Phone Café","Rename CAS");
            foreign.SendFriendRequest(main.AccountId);main.RespondToFriendRequest(foreign.AccountId,true);var friendState=foreign.GetFriendState();Check(friendState.DeviceNames.ContainsKey(main.AccountId)&&friendState.DeviceNames[main.AccountId][target]=="Phone Café","Production opt-in contact name refresh");
            var friendIndex=new FriendConversationIndex(Path.Combine(root,"foreign-profile"));friendIndex.Reconcile(friendState.Friends,friendState.DeviceNames);Check(new FriendConversationIndex(Path.Combine(root,"foreign-profile")).Records.Single().DeviceNames[target]=="Phone Café","Production contact names durable across client restart");
            Denied(()=>manager.Rename(first,target,"stale"),"Stale rename rejected");
            var targetManager=new DeviceManagementClient(targetBound,main.AccountId,target,input=>Sign(targetKey,input),()=>true);Check(targetManager.Inventory().Devices.Single(d=>d.Identity==target).Name=="Phone Café","Other-device rename consumption");
            Denied(()=>manager.Revoke(named,actor,"fixture-only-password"),"Self revoke rejected client boundary");
            Denied(()=>manager.Revoke(named,target,"wrong-password"),"Password step-up rejected");Check(manager.Inventory().Devices.Single(d=>d.Identity==target).State=="active","Wrong password no mutation");
            Thread.Sleep(2100); // Honor the server's intentional password-attempt rate limit.
            var revoked=manager.Revoke(named,target,"fixture-only-password");Check(revoked.Devices.Single(d=>d.Identity==target).State=="revoked","Revoke production TLS RPC committed");Check(revoked.Devices.Single(d=>d.Identity==actor).State=="active","Actor retained");
            Denied(()=>targetManager.Inventory(),"Revoked bound session denied");Denied(()=>main.RefreshSessionInfo(),"Legacy account session invalidated");
            Check(Directory.GetFiles(targetProfile,"attachment-device-session-*.dat").Length==1,"Revoked credential preserved locally");
            second.Login("manager_test","fixture-only-password");Denied(()=>second.ReportPresence(target),"Password cannot resurrect tombstone");Denied(()=>Open(second,targetKey,target,targetProfile,false),"Revoked credential cannot resume");
            foreach(var client in clients)client.CloseRpc();Stop(server,thread);server=null;
            server=Start(config,out thread);main.Login("manager_test","fixture-only-password");main.ReportPresence(actor);var resumed=Open(main,actorKey,actor,actorProfile,false);clients.Add(resumed);var after=new DeviceManagementClient(resumed,main.AccountId,actor,signer,()=>true).Inventory();
            Check(after.Revision==revoked.Revision&&after.Epoch!=revoked.Epoch,"Revision durable while process epoch changes");Check(after.Devices.Single(d=>d.Identity==target).State=="revoked"&&after.Devices.Single(d=>d.Identity==target).Name=="Phone Café","Restart preserves name/tombstone");
            foreign.Login("other_account","fixture-only-password");var afterNames=foreign.GetFriendState();Check(afterNames.DeviceNames[main.AccountId][target]=="Phone Café","Server restart retains authorized historical name");Check(afterNames.RevokedDevices[main.AccountId].Contains(target),"Cosmetic revoked label never removes revocation");
            Denied(()=>new DeviceManagementClient(resumed,main.AccountId,actor,signer,()=>false).Inventory(),"Late profile callback rejected");
        }}finally{foreach(var client in clients)client.CloseRpc();if(server!=null)Stop(server,thread);}
        Console.WriteLine("PASS Device Manager production TLS + real managed adapter: credential DPAPI enrollment/resume, own-account isolation, rename conflict, cross-device name, step-up rejection, revoke, legacy invalidation, resurrection rejection and server restart. "+checks+" assertions; fixture keys only.");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);var rejected=error as DiscoveryRequestException;if(rejected!=null)Console.Error.WriteLine("Server rejection: "+rejected.ErrorCode);return 1;}}
}
