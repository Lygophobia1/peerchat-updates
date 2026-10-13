#if PEERCHAT_PORTABLE
using ECDsaCng = PortableTestKey;
#endif
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Net.Security;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;

internal static class GroupCiphertextFanoutTest {
    static void Require(bool value,string reason){if(!value)throw new Exception(reason);}
    static string Identity(ECDsaCng key){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(key.Key.Export(CngKeyBlobFormat.EccPublicBlob))).Replace("-","").ToLowerInvariant();}
    static void Text(byte[] bytes,int offset,string value){Encoding.ASCII.GetBytes(value).CopyTo(bytes,offset);}
    static void Number(byte[] bytes,int offset,long value){for(int i=7;i>=0;i--){bytes[offset+i]=(byte)value;value>>=8;}}
    // Routing-only fixture: server deliberately cannot authenticate/decrypt ciphertext.
    // Real signed AES-GCM roundtrip, history and replay rejection are tested natively.
    static byte[] Envelope(string group,long generation,long account,string device,string message,int size=20000){var wire=new byte[254+size];Text(wire,0,"PCGE");wire[5]=1;Text(wire,6,group);Number(wire,38,generation);Number(wire,46,generation);Text(wire,54,message);Number(wire,86,account);Text(wire,94,device);wire[170]=(byte)(size>>24);wire[171]=(byte)(size>>16);wire[172]=(byte)(size>>8);wire[173]=(byte)size;for(int i=174;i<wire.Length;i++)wire[i]=(byte)(i*31);return wire;}
    static string State(Dictionary<string,object> result){return Convert.ToString(result["state"]);}
    static void Wait(Func<bool> done,string reason){var end=DateTime.UtcNow.AddSeconds(12);while(DateTime.UtcNow<end){if(done())return;Thread.Sleep(20);}throw new Exception(reason);}
    static int RelayCount(DiscoveryServer server){
        var type=typeof(DiscoveryServer);var gate=type.GetField("relayGate",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server);
        lock(gate)return ((System.Collections.ICollection)type.GetField("relayEndpoints",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server)).Count;
    }
    static void CancelledPairing(DiscoveryServer server,DiscoveryClient client,string local,string target,string group,long generation){
        TcpClient tcp;using(var relay=client.OpenAuthenticatedStream(out tcp,true))using(tcp){
            var json=new System.Web.Script.Serialization.JavaScriptSerializer();
            var request=new Dictionary<string,object>{{"version",1},{"type","relay_attach"},{"sessionToken",client.SessionToken},{"publicIdentity",local},{"targetAccountId","2"},{"targetPublicIdentity",target},{"purpose","group_key"},{"groupId",group},{"rosterGeneration",generation.ToString()}};
            DiscoveryClient.WriteFrame(relay,Encoding.UTF8.GetBytes(json.Serialize(request)));
            Wait(()=>RelayCount(server)==1,"Authenticated pending relay did not register.");
            tcp.Close();
            var deadline=DateTime.UtcNow.AddSeconds(3);while(DateTime.UtcNow<deadline&&RelayCount(server)!=0)Thread.Sleep(20);
            Require(RelayCount(server)==0,"Disconnected pending attachment retained relay ownership until pair timeout.");
        }
        Console.WriteLine("PASS: authenticated pending pairing releases ownership promptly on disconnect, before retry.");
    }
    static void Network(string directory,string certificate,string password,string owner,string member,string third,string group,long generation,byte[] wire,AccountDatabase database){
        var probe=new TcpListener(IPAddress.Loopback,0);probe.Start();int port=((IPEndPoint)probe.LocalEndpoint).Port;probe.Stop();
        var config=new ServerConfiguration{DataDirectory=directory,DatabasePath=Path.Combine(directory,"server.db"),LogDirectory=Path.Combine(directory,"logs"),Port=port,CertificatePath=certificate,CertificatePassword=password};
        var server=new DiscoveryServer(config);Exception serverError=null;var worker=new Thread(()=>{try{server.Run();}catch(Exception e){serverError=e;}}){IsBackground=true};worker.Start();
        var clients=new List<DiscoveryClient>();var events=new List<DiscoveryEvent>[3];object gate=new object();
        string pin;using(var cert=new X509Certificate2(certificate,password))using(var sha=SHA256.Create())pin=BitConverter.ToString(sha.ComputeHash(cert.RawData)).Replace("-","");
        string[] names={"owner","member","third"},devices={owner,member,third};
        try{
            for(int i=0;i<3;i++){
                var client=new DiscoveryClient("localhost",port,pin);clients.Add(client);int index=i;events[i]=new List<DiscoveryEvent>();
                // Test DB sessions are authenticated by the production event subscription.
                string token=Guid.NewGuid().ToString("N");string hash;using(var sha=SHA256.Create())hash=BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(token))).Replace("-","").ToLowerInvariant();
                database.CreateSession(hash,i+1,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());client.SessionToken=token;client.AccountId=i+1;
                client.StartEventChannel(devices[i],e=>{lock(gate)events[index].Add(e);if(e.Event=="group_message")client.QueueGroupAck(e.GroupId,e.MessageId,"accepted");});
            }
            Wait(()=>clients.All(c=>c.EventChannelConnected),"Three authenticated existing event channels did not connect.");
            CancelledPairing(server,clients[0],owner,member,group,generation);
            // Exercise the production pinned-TLS request and typed projection,
            // not reflection-only dispatch. Ordinary membership epochs have no
            // signed device-transition proof: recovery must expose the gap.
            var current=clients[0].GetGroupDeviceRecovery(group,generation,owner);
            Require(current.Complete&&current.Reason=="none"&&current.Steps.Count==0&&current.NextGeneration==generation,"Current authenticated recovery cursor failed.");
            var gap=clients[0].GetGroupDeviceRecovery(group,1,owner);
            Require(!gap.Complete&&gap.Reason=="signed_ledger_gap"&&gap.Steps.Count==0&&gap.NextGeneration==1&&gap.CurrentGeneration==generation,"Unsigned membership epochs were fabricated as device recovery evidence.");
            bool wrongDevice=false;try{clients[0].GetGroupDeviceRecovery(group,generation,member);}catch(DiscoveryRequestException){wrongDevice=true;}
            Require(wrongDevice,"An account session retrieved evidence under another account's device.");
            bool future=false;try{clients[0].GetGroupDeviceRecovery(group,generation+1,owner);}catch(DiscoveryRequestException){future=true;}
            Require(future,"A future recovery cursor was accepted.");
            bool wrongGroup=false;try{clients[0].GetGroupDeviceRecovery(new string('9',32),generation,owner);}catch(DiscoveryRequestException){wrongGroup=true;}
            Require(wrongGroup,"A non-member recovered unrelated group evidence.");
            Console.WriteLine("PASS: actual pinned-TLS recovery, current cursor, unsigned epoch gap, wrong device, future cursor and unrelated group rejection.");
            string message=Encoding.ASCII.GetString(wire,54,32);
            Require(clients[0].QueueGroupMessage(group,message,generation,Convert.ToBase64String(wire)),"Existing event channel rejected send.");
            try{Wait(()=>{lock(gate)return events[1].Any(e=>e.Event=="group_message")&&events[2].Any(e=>e.Event=="group_message")&&events[0].Any(e=>e.State=="recipient_acknowledged"&&e.AcceptedCount==2);},"Three-client online fanout / recipient receipts did not complete.");}
            catch{lock(gate)for(int i=0;i<3;i++)Console.Error.WriteLine("fanout_test client="+i+" connected="+clients[i].EventChannelConnected+" events="+String.Join(",",events[i].Select(e=>e.Event+":"+e.State+":"+e.AcceptedCount)));throw;}
            lock(gate){for(int i=1;i<3;i++)Require(Convert.FromBase64String(events[i].First(e=>e.Event=="group_message").Envelope).SequenceEqual(wire),"Server modified opaque ciphertext.");}
            Require(clients[0].QueueGroupMessage(group,message,generation,Convert.ToBase64String(wire)),"Duplicate request not queued.");
            Wait(()=>{lock(gate)return events[0].Any(e=>e.State=="duplicate_server_request");},"Duplicate server request was not identified.");
            lock(gate)Require(events[1].Count(e=>e.Event=="group_message")==1&&events[2].Count(e=>e.Event=="group_message")==1,"Duplicate server send repeated recipient delivery.");
            clients[2].StopEventChannel();Thread.Sleep(200);
            var next=Envelope(group,generation,1,owner,new string('e',32));
            Require(clients[0].QueueGroupMessage(group,new string('e',32),generation,Convert.ToBase64String(next)),"Online-only send failed after recipient left.");
            Wait(()=>{lock(gate)return events[0].Any(e=>e.MessageId==new string('e',32)&&e.State=="server_accepted_online_only"&&e.SessionCount==1);},"Offline recipient was treated as online delivery.");
            clients[2].StartEventChannel(third,e=>{lock(gate)events[2].Add(e);});Wait(()=>clients[2].EventChannelConnected,"Existing channel reconnect failed.");Thread.Sleep(350);
            lock(gate)Require(!events[2].Any(e=>e.MessageId==new string('e',32)),"Online-only fanout fabricated offline retry.");
            Require(clients[0].QueueGroupMessage(group,new string('f',32),generation,Convert.ToBase64String(Envelope(group,generation,1,owner,new string('f',32)))),"New message after reconnect failed.");
            Wait(()=>{lock(gate)return events[2].Any(e=>e.MessageId==new string('f',32)&&e.Event=="group_message");},"Reconnected authorized session did not receive subsequent ciphertext.");
            clients[0].StopEventChannel();
            TcpClient rawTcp;using(var raw=clients[0].OpenAuthenticatedStream(out rawTcp))using(rawTcp){
                var json=new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=100000};
                DiscoveryClient.WriteFrame(raw,Encoding.UTF8.GetBytes(json.Serialize(new Dictionary<string,object>{{"version",1},{"type","events"},{"sessionToken",clients[0].SessionToken},{"publicIdentity",owner},{"clientHeartbeats",true}})));
                var ready=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(DiscoveryClient.ReadFrame(raw,100000)));Require(Convert.ToBoolean(ready["ok"]),"Raw authenticated protocol test subscription failed.");
                DiscoveryClient.WriteFrame(raw,Encoding.UTF8.GetBytes(json.Serialize(new Dictionary<string,object>{{"type","group_send"},{"groupId",group},{"messageId",message},{"rosterGeneration",generation.ToString()},{"envelope",Convert.ToBase64String(wire)},{"recipients",new[]{999L}}})));
                Dictionary<string,object> response;do{response=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(DiscoveryClient.ReadFrame(raw,100000)));}while(Convert.ToString(response["type"])!="group_delivery");
                Require(State(response)=="malformed_request","Sender-controlled recipient list was not rejected by the actual protocol handler.");
            }
            Console.WriteLine("PASS: three non-friend authenticated event channels fan out byte-identical opaque envelopes, recipient receipts, duplicates, offline exclusion and subsequent reconnect delivery; no peer mesh.");
        }finally{foreach(var client in clients)client.StopEventChannel();typeof(DiscoveryServer).GetMethod("RequestStop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,null);Require(worker.Join(6000),"Isolated test server did not stop.");if(serverError!=null)throw serverError;}
    }
    public static int Main(string[] args){try{
        Require(args.Length==3,"Temporary directory / certificate / test certificate password required.");Directory.CreateDirectory(args[0]);
        using(var a=new ECDsaCng(256))using(var b=new ECDsaCng(256))using(var c=new ECDsaCng(256))using(var d=new ECDsaCng(256))using(var database=new AccountDatabase(Path.Combine(args[0],"server.db"))){
            string owner=Identity(a),member=Identity(b),third=Identity(c),outsider=Identity(d);
            string[] ids={owner,member,third,outsider},names={"owner","member","third","outsider"};for(int i=0;i<4;i++)Require(database.CreateAccountWithDevice(names[i],new byte[16],new byte[32],ids[i])==i+1,"Account fixture failed.");
            var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(a,new string('a',32),1)));string group=proof.ServerGroupId;
            Require(database.RegisterGroup(1,owner,proof)!=null,"Owner roster failed.");Require(database.GroupInvite(group,1,owner,2,member,1)!=null&&database.GroupRespond(group,2,member,true,2)!=null,"Member acceptance failed.");Require(database.GroupInvite(group,1,owner,3,third,3)!=null&&database.GroupRespond(group,3,third,true,4)!=null,"Third acceptance failed.");
            Require(!database.Friends(1,2)&&!database.Friends(1,3),"Fixture created friendship.");
            var service=new GroupCiphertextFanout(database);var captured=new List<Dictionary<string,object>>();
            var origin=new GroupFanoutSession{Account=1,Device=owner,Binding="owner",Current=()=>true,Send=x=>{}};
            Func<int,GroupFanoutSession> session=i=>new GroupFanoutSession{Account=i+1,Device=ids[i],Binding="session"+i,Current=()=>true,Send=x=>captured.Add(x)};
            var recipients=new List<GroupFanoutSession>{origin,session(1),session(2),session(3)};
            var wire=Envelope(group,5,1,owner,new string('b',32));var result=service.Send(origin,group,5,wire,recipients);
            Require(State(result)=="server_accepted_online_only"&&Convert.ToInt32(result["sessionCount"])==2&&captured.Count==2,"Fanout leaked to non-member or wrong session count.");
            Require(State(service.Send(origin,group,5,wire,recipients))=="duplicate_server_request"&&captured.Count==2,"Duplicate fanout repeated.");
            Require(State(service.Ack(recipients[1],group,new string('b',32),"accepted"))=="recipient_acknowledged","Recipient ACK failed.");
            Require(Convert.ToInt32(service.Ack(recipients[1],group,new string('b',32),"rejected")["acceptedCount"])==1,"Repeated ACK downgraded receipt.");
            var wrong=session(3);Require(State(service.Send(wrong,group,5,Envelope(group,5,4,outsider,new string('c',32)),recipients))=="not_authorized_or_stale","Non-member sender accepted.");
            wrong=session(1);wrong.Device=outsider;Require(State(service.Send(wrong,group,5,Envelope(group,5,2,outsider,new string('c',32)),recipients))=="not_authorized_or_stale","Wrong device accepted.");
            Require(State(service.Send(origin,group,4,Envelope(group,4,1,owner,new string('c',32)),recipients))=="not_authorized_or_stale","Stale generation accepted.");
            Require(State(service.Send(origin,group,5,new byte[65791],recipients))=="malformed_request","Oversized envelope accepted.");
            var altered=(byte[])wire.Clone();altered[0]^=1;Require(State(service.Send(origin,group,5,altered,recipients))=="malformed_request","Malformed envelope accepted.");
            altered=(byte[])wire.Clone();altered[200]^=1;Require(State(service.Send(origin,group,5,altered,recipients))=="conflicting_message","Conflicting message ID accepted.");
            Network(args[0],args[1],args[2],owner,member,third,group,5,wire,database);
            Require(database.GroupRemove(group,1,owner,3,third,5)!=null,"Removal failed.");captured.Clear();result=service.Send(origin,group,6,Envelope(group,6,1,owner,new string('d',32)),recipients);Require(Convert.ToInt32(result["sessionCount"])==1&&captured.Count==1,"Removed member received new generation.");
            Require(State(service.Ack(recipients[2],group,new string('b',32),"accepted"))=="stale_or_unsubmitted_ack","Removed member kept stale ACK authority.");
            Require(State(service.Send(recipients[2],group,6,Envelope(group,6,3,third,new string('8',32)),recipients))=="not_authorized_or_stale","Removed member sent future traffic.");
            Require(State(service.Send(origin,new string('9',32),6,Envelope(new string('9',32),6,1,owner,new string('8',32)),recipients))=="not_authorized_or_stale","Unrelated group obtained fanout.");
            // Mutation between recipient writes: the second write must recheck the epoch.
            var mutating=session(1);mutating.Send=x=>{Require(database.GroupInvite(group,1,owner,3,third,6)!=null,"In-flight roster mutation fixture failed.");};
            captured.Clear();result=service.Send(origin,group,6,Envelope(group,6,1,owner,new string('7',32)),new List<GroupFanoutSession>{mutating,session(1)});
            Require(Convert.ToInt32(result["sessionCount"])==1&&captured.Count==0,"Fanout continued writing old-generation envelopes after roster mutation.");
        }
        Console.WriteLine("PASS: roster/device authorization, removal, generations, bounded opaque routing, malformed/oversized rejection and ACK idempotency.");return 0;
    }catch(Exception e){Console.Error.WriteLine("FAIL: "+e.GetType().Name+": "+e.Message);return 1;}}
}
