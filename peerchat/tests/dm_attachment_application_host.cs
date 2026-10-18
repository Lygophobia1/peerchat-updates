using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;

// Isolated process bridge to the actual authenticated TLS server and shipping
// Windows transport codec. Only public test keys and encrypted wire cross it.
class DirectAttachmentApplicationHost {
 static int Main(string[] args){try{Console.InputEncoding=new UTF8Encoding(false,true);Console.OutputEncoding=new UTF8Encoding(false,true);AttachmentCacheRuntimeTest.NativeReplyFailureProof();if(args.Length==1&&args[0]=="--adapter-proof")return 0;AttachmentCacheRuntimeTest.DirectApplicationHost(Path.GetFullPath(args[0]));return 0;}catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
partial class AttachmentCacheRuntimeTest {
 internal static void NativeReplyFailureProof(){
  const string id="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  byte[] command=HostRecord("PeerChat/DMCacheCommand/v1",w=>{w.Write(7UL);Text(w,id);Text(w,"cache_dm_capability");w.Write(0UL);Text(w,"");Text(w,"");w.Write(0UL);w.Write(0UL);w.Write(0UL);for(int i=0;i<3;i++)Field(w,new byte[0]);});
  int rejected=0,failed=0;string failure="";
  var execution=new DmAttachmentExecution((operation,input)=>{
   if(operation==25)return Convert.ToBase64String(command);
   if(operation==26){rejected++;throw new InvalidDataException("Injected native reply rejection before commit");}
   if(operation==27){failed++;failure=input;return "ok";}
   throw new InvalidDataException("Unexpected native operation");
  },7);
  bool threw=false;try{execution.Service(payload=>new Dictionary<string,object>{{"type","cache_result"},{"operation",payload["type"]},{"requestId",payload["requestId"]},{"state","accepted"},{"cacheVersion",2L},{"dmAttachmentVersion",1L}});}catch(InvalidDataException){threw=true;}
  Check(threw&&rejected==1&&failed==1&&execution.LastState=="native_reply_rejected","DM native reply rejection must release its borrowed request");
  var wire=Convert.FromBase64String(failure);using(var stream=new MemoryStream(wire))using(var reader=new BinaryReader(stream,new UTF8Encoding(false,true))){
   Func<string> readText=()=>{ulong count=reader.ReadUInt64();Check(count<=100000,"DM failure text bound");return new UTF8Encoding(false,true).GetString(reader.ReadBytes((int)count));};
   Check(reader.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,30})&&readText()=="PeerChat/DMCacheFailure/v1"&&reader.ReadUInt64()==7&&readText()==id&&readText()=="network_unavailable"&&stream.Position==stream.Length,"DM native failure correlation");
  }
  Console.Error.WriteLine("PASS DM managed transport releases rejected native reply with exact generation/request correlation");
 }
 internal static void DirectApplicationHost(string root){
  var control=Console.Out;Console.SetOut(Console.Error);var json=new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=100000};
  var setup=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(root,"fixture.json")));
  var publics=((IEnumerable)setup["publics"]).Cast<object>().Select(x=>Convert.FromBase64String((string)x)).ToArray();
  var bindings=((IEnumerable)setup["bindings"]).Cast<object>().Select(x=>Convert.FromBase64String((string)x)).ToArray();
  Check(publics.Length==3&&bindings.Length==3,"DM host three device fixture");
  var devices=publics.Select(MailboxProtocol.Digest).ToArray();var accountsByActor=new[]{1L,2L,2L};var tokens=devices.Select(x=>Guid.NewGuid().ToString("N")).ToArray();
  string database=Path.Combine(root,"dm-server-accounts.sqlite");var options=Options(Fixture(root,"dm-server-cache"));options.Enabled=true;options.RetentionSeconds=48*60*60;
  using(var db=new AccountDatabase(database)){
   Check(db.CreateAccountWithDevice("dm-source",new byte[16],FixturePasswordHash(),devices[0])==1&&db.CreateAccountWithDevice("dm-recipient",new byte[16],FixturePasswordHash(),devices[1])==2&&db.AddDevice(2,devices[2]),"DM host accounts/devices");
   Check(db.SendFriendRequest(1,2)=="pending"&&db.RespondToRequest(2,1,true)=="friends","DM host friendship");db.InitializeMailbox();
   for(int i=0;i<3;i++){Check(MailboxProtocol.Binding.Parse(bindings[i]).Account==accountsByActor[i],"DM fixture key account");Check(db.RegisterMailboxKey(accountsByActor[i],devices[i],publics[i],bindings[i])=="accepted","DM host recipient key");ContextSession(db,(int)accountsByActor[i],devices[i],tokens[i]);}
  }
  string certificatePath=Path.Combine(root,"dm-server-test.pfx"),pin;
  using(var key=RSA.Create(2048)){var request=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);using(var certificate=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(2))){File.WriteAllBytes(certificatePath,certificate.Export(X509ContentType.Pfx,"fixture-only"));pin=MailboxProtocol.Digest(certificate.RawData);}}
  var reservation=new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0);reservation.Start();int port=((System.Net.IPEndPoint)reservation.LocalEndpoint).Port;reservation.Stop();
  var config=new ServerConfiguration{DataDirectory=root,DatabasePath=database,LogDirectory=Path.Combine(root,"dm-server-logs"),Port=port,CertificatePath=certificatePath,CertificatePassword="fixture-only",ManagementPipe="",AttachmentCache=options};config.Validate();
  string candidate=Environment.GetEnvironmentVariable("PEERCHAT_TEST_DM_CACHE_SERVER");if(!String.IsNullOrEmpty(candidate))Check(Path.IsPathRooted(candidate)&&File.Exists(candidate),"Packaged DM fixture server");
  IDisposable server=null;var clients=new DiscoveryClient[3];var eventHints=new int[3];var eventActors=new bool[3];
  Action<int> startEvents=actor=>{clients[actor].StartEventChannel(devices[actor],notification=>{if(notification.Event=="mailbox_ready")System.Threading.Interlocked.Increment(ref eventHints[actor]);});var until=DateTime.UtcNow.AddSeconds(10);while(!clients[actor].EventChannelConnected&&DateTime.UtcNow<until)System.Threading.Thread.Sleep(5);Check(clients[actor].EventChannelConnected,"DM fixture event channel timeout");};
  Action restart=()=>{foreach(var client in clients)if(client!=null)client.StopEventChannel();if(server!=null)server.Dispose();server=String.IsNullOrEmpty(candidate)?(IDisposable)new ContextServer(config):new CandidateServer(candidate,config);for(int actor=0;actor<3;actor++)if(eventActors[actor])startEvents(actor);};
  Action<Dictionary<string,object>> send=value=>{control.WriteLine(Convert.ToBase64String(HostRecord("PeerChat/AttachmentTestHost/v1",w=>HostValue(w,value))));control.Flush();};
  var timings=new List<long>();long cachedBytes=0;int appendRequests=0,readRequests=0;var offsets=new HashSet<string>();Dictionary<string,object> lastPublication=null;int lastPublisher=0;
  var capabilityTraces=new int[3];
  try{
   restart();for(int i=0;i<3;i++){clients[i]=new DiscoveryClient("localhost",port,pin){AccountId=accountsByActor[i],SessionToken=tokens[i],RegisteredPublicIdentity=devices[i]};clients[i].RefreshSessionInfo();}
   send(new Dictionary<string,object>{{"pin",pin},{"port",port},{"accounts",accountsByActor}});
   string line;while((line=Console.ReadLine())!=null){Check(line.Length<=150000,"DM host command bound");var command=json.Deserialize<Dictionary<string,object>>(line);string operation=(string)command["operation"];
    if(operation=="native_command"){
     int actor=Convert.ToInt32(command["actor"])-1;Check(actor>=0&&actor<3,"DM host actor");var native=Convert.FromBase64String((string)command["command"]);var watch=System.Diagnostics.Stopwatch.StartNew();
     byte[] response=DmAttachmentExecution.Execute(native,payload=>{
      string type=(string)payload["type"];var result=clients[actor].AttachmentCache(devices[actor],payload,accountsByActor[actor],clients[actor].SessionToken,pin);
      if((type=="cache_dm_capability"||type=="cache_dm_register")&&capabilityTraces[actor]++<6){
       var state=Convert.ToString(result["state"]);if(!System.Text.RegularExpressions.Regex.IsMatch(state??"","^(accepted|cache_[a-z_]{1,80})$"))state="unclassified";
       Console.Error.WriteLine("DM fixture cache actor="+(actor+1)+" operation="+type+" state="+state+" cache_version="+Convert.ToString(result.ContainsKey("cacheVersion")?result["cacheVersion"]:0)+" dm_version="+Convert.ToString(result.ContainsKey("dmAttachmentVersion")?result["dmAttachmentVersion"]:0));
       if(state=="cache_storage_unavailable"){
        try{using(var diagnostic=new AttachmentCacheRuntime(options,()=>DateTimeOffset.UtcNow.ToUnixTimeSeconds())){}Console.Error.WriteLine("DM fixture cache startup diagnostic=unexpected_success");}
        catch(Exception failure){if(failure is OutOfMemoryException)throw;var code=failure.Message;Console.Error.WriteLine("DM fixture cache startup diagnostic="+(failure is InvalidOperationException&&System.Text.RegularExpressions.Regex.IsMatch(code??"","^cache_[a-z_]{1,80}$")?code:"storage_owner_or_platform_failure"));}
        throw new InvalidOperationException("DM fixture cache storage unavailable before capability registration");
       }
      }
      if(Convert.ToString(result["state"])=="accepted"){
       if(type=="cache_dm_append"){appendRequests++;string binding=Convert.ToString(payload["lease"])+":"+Convert.ToString(payload["offset"]);if(offsets.Add(binding))cachedBytes+=AttachmentCacheRequests.Decode((string)payload["data"]).Length;}
       if(type=="cache_dm_read")readRequests++;
       if(type=="cache_dm_publish"){lastPublication=new Dictionary<string,object>(payload);lastPublisher=actor;}
      }return result;
     });timings.Add(watch.ElapsedTicks*1000000/System.Diagnostics.Stopwatch.Frequency);control.WriteLine(Convert.ToBase64String(response));control.Flush();continue;
    }
    if(operation=="events_start"){int actor=Convert.ToInt32(command["actor"])-1;Check(actor>=0&&actor<3,"DM event actor");eventActors[actor]=true;startEvents(actor);send(new Dictionary<string,object>{{"connected",true},{"mailboxReady",System.Threading.Volatile.Read(ref eventHints[actor])}});continue;}
    if(operation=="event_hints"){int actor=Convert.ToInt32(command["actor"])-1;Check(actor>=0&&actor<3,"DM hint actor");send(new Dictionary<string,object>{{"mailboxReady",System.Threading.Volatile.Read(ref eventHints[actor])}});continue;}
    if(operation=="publish_rejected"){
     Check(lastPublication!=null,"DM rejection probe requires a valid stored publication");System.Threading.Thread.Sleep(500);var before=eventHints.Select(value=>value).ToArray();var invalid=new Dictionary<string,object>(lastPublication);var wire=AttachmentCacheRequests.Decode((string)invalid["envelope"]);wire[wire.Length-1]^=1;invalid["envelope"]=Convert.ToBase64String(wire);invalid["requestId"]=Guid.NewGuid().ToString("N");var result=clients[lastPublisher].AttachmentCache(devices[lastPublisher],invalid,accountsByActor[lastPublisher],clients[lastPublisher].SessionToken,pin);Check(Convert.ToString(result["state"])!="accepted","Modified publication unexpectedly accepted");System.Threading.Thread.Sleep(500);Check(before.SequenceEqual(eventHints),"Rejected publication emitted mailbox_ready");send(new Dictionary<string,object>{{"rejected",true},{"noHint",true}});continue;
    }
    if(operation=="fetch"){
     int actor=Convert.ToInt32(command["actor"])-1;Check(actor>=0&&actor<3,"DM fetch actor");var result=clients[actor].Mailbox("mailbox_fetch",devices[actor],new Dictionary<string,object>());send(result);continue;
    }
    if(operation=="mailbox_ack"){
     int actor=Convert.ToInt32(command["actor"])-1;Check(actor>=0&&actor<3,"DM ACK actor");var result=clients[actor].Mailbox("mailbox_ack",devices[actor],new Dictionary<string,object>{{"receipt",(string)command["receipt"]},{"signingPublicKey",Convert.ToBase64String(publics[actor])}});send(result);continue;
    }
    if(operation=="mailbox_store"){
     int actor=Convert.ToInt32(command["actor"])-1;Check(actor>=0&&actor<3,"DM store actor");send(clients[actor].Mailbox("mailbox_store",devices[actor],new Dictionary<string,object>{{"envelope",(string)command["receipt"]},{"signingPublicKey",Convert.ToBase64String(publics[actor])}}));continue;
    }
    if(operation=="mailbox_receipts"){
     int actor=Convert.ToInt32(command["actor"])-1;Check(actor>=0&&actor<3,"DM receipt actor");send(clients[actor].Mailbox("mailbox_receipts",devices[actor],new Dictionary<string,object>()));
     for(int observed=0;observed<clients.Length;observed++)Console.Error.WriteLine("RPC_TRACE actor="+(observed+1)+" "+clients[observed].RpcTimingDiagnostics);
     if(server is ContextServer){var flags=System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic;
      var active=(DiscoveryServer)typeof(ContextServer).GetField("server",flags).GetValue(server);
      var timing=(ServerTimingDiagnostics)typeof(DiscoveryServer).GetField("timing",flags).GetValue(active);
      foreach(var row in timing.Snapshot())Console.Error.WriteLine("SERVER_TRACE "+row);
     }continue;
    }
    if(operation=="restart"){restart();send(new Dictionary<string,object>{{"restarted",true}});continue;}
    if(operation=="logout"){int actor=command.ContainsKey("actor")?Convert.ToInt32(command["actor"])-1:0;clients[actor].Logout();send(new Dictionary<string,object>{{"loggedOut",String.IsNullOrEmpty(clients[actor].SessionToken)}});continue;}
    if(operation=="source_login"){
     foreach(var client in clients)client.CloseRpc();server.Dispose();server=null;tokens[0]=Guid.NewGuid().ToString("N");using(var db=new AccountDatabase(database))ContextSession(db,1,devices[0],tokens[0]);clients[0].SessionToken=tokens[0];restart();clients[0].RefreshSessionInfo();send(new Dictionary<string,object>{{"signedIn",true}});continue;
    }
    if(operation=="metrics"){send(new Dictionary<string,object>{{"cacheAppendRequests",appendRequests},{"cacheReadRequests",readRequests},{"appendBytesByLeaseOffset",cachedBytes},{"rpcSamples",timings.Count},{"rpcMeanUs",timings.Count==0?0:timings.Sum()/timings.Count},{"rpcMaxUs",timings.Count==0?0:timings.Max()}});continue;}
    if(operation=="project"){send(json.Deserialize<Dictionary<string,object>>((string)command["json"]));continue;}
    if(operation=="quit"){send(new Dictionary<string,object>{{"stopped",true}});break;}
    throw new InvalidOperationException("Unknown DM test-host operation");
   }
  }finally{foreach(var client in clients)if(client!=null)client.StopEventChannel();if(server!=null)server.Dispose();}
 }
}
