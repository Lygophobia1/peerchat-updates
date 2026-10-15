using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;

// Test-only process bridge. Cache operations use the shipping managed
// DiscoveryClient and actual pinned TLS; the bridge never handles file chunks,
// retries, encryption or native journal state itself.
class AttachmentApplicationHost {
 static int Main(string[] args){try{AttachmentCacheRuntimeTest.ApplicationHost(Path.GetFullPath(args[0]));return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
partial class AttachmentCacheRuntimeTest {
 static byte[] FixturePasswordHash(){using(var derive=new Rfc2898DeriveBytes("fixture-password-only",new byte[16],310000,HashAlgorithmName.SHA256))return derive.GetBytes(32);}
 // An optional packaged-server run uses its real entry point and private
 // management pipe. Only this isolated fixture's process/config is owned here.
 sealed class CandidateServer:IDisposable {
  readonly System.Diagnostics.Process process;readonly string pipe;bool disposed;
  internal CandidateServer(string path,ServerConfiguration config){
   pipe="PeerChat.AttachmentFixture."+Guid.NewGuid().ToString("N");config.ManagementPipe=pipe;
   string configuration=Path.Combine(config.DataDirectory,"candidate-fixture.json");config.Save(configuration);
   var start=new System.Diagnostics.ProcessStartInfo{FileName=path.EndsWith(".dll",StringComparison.OrdinalIgnoreCase)?"dotnet":path,UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
   if(path.EndsWith(".dll",StringComparison.OrdinalIgnoreCase))start.ArgumentList.Add(path);start.ArgumentList.Add("--config");start.ArgumentList.Add(configuration);
   process=new System.Diagnostics.Process{StartInfo=start};process.OutputDataReceived+=(s,e)=>{if(e.Data!=null)Console.Error.WriteLine(e.Data);};process.ErrorDataReceived+=(s,e)=>{if(e.Data!=null)Console.Error.WriteLine(e.Data);};
   Check(process.Start(),"Packaged fixture server launch");process.BeginOutputReadLine();process.BeginErrorReadLine();
   var until=System.Diagnostics.Stopwatch.StartNew();bool ready=false;while(until.ElapsedMilliseconds<10000&&!ready&&!process.HasExited){try{using(var probe=new System.Net.Sockets.TcpClient()){probe.Connect("localhost",config.Port);ready=true;}}catch(System.Net.Sockets.SocketException){System.Threading.Thread.Sleep(20);}}Check(ready,"Packaged fixture server startup");
  }
  public void Dispose(){if(disposed)return;if(!process.HasExited){using(var channel=new System.IO.Pipes.NamedPipeClientStream(".",pipe,System.IO.Pipes.PipeDirection.InOut)){channel.Connect(10000);using(var writer=new StreamWriter(channel,new UTF8Encoding(false),256,true)){writer.WriteLine("STOP");writer.Flush();using(var reader=new StreamReader(channel,Encoding.UTF8,true,256,true))Check(reader.ReadLine()!=null,"Packaged fixture stop reply");}}Check(process.WaitForExit(15000),"Packaged fixture server shutdown blocked");}Check(process.ExitCode==0,"Packaged fixture server exit");disposed=true;process.Dispose();}
 }
 static byte[] HostRecord(string domain,Action<BinaryWriter> action){using(var memory=new MemoryStream())using(var writer=new BinaryWriter(memory,new UTF8Encoding(false,true))){writer.Write(new byte[]{80,84,1,30});Text(writer,domain);action(writer);Check(memory.Length<=100000,"Host response bound");return memory.ToArray();}}
 static void HostValue(BinaryWriter writer,object value,int depth=0){Check(depth<=12,"Host value depth");
  if(value==null){writer.Write((byte)0);return;}if(value is bool){writer.Write((byte)((bool)value?2:1));return;}if(value is string){writer.Write((byte)4);Text(writer,(string)value);return;}
  var dictionary=value as IDictionary<string,object>;if(dictionary!=null){writer.Write((byte)6);writer.Write((ulong)dictionary.Count);foreach(var pair in dictionary){Text(writer,pair.Key);HostValue(writer,pair.Value,depth+1);}return;}
  var list=value as IList;if(list!=null){writer.Write((byte)5);writer.Write((ulong)list.Count);foreach(var item in list)HostValue(writer,item,depth+1);return;}
  var number=Convert.ToDecimal(value,CultureInfo.InvariantCulture);Check(number>=0&&number<=Int64.MaxValue&&Decimal.Truncate(number)==number,"Host unsigned integer");writer.Write((byte)3);writer.Write((ulong)number);
 }
 internal static void ApplicationHost(string root){
  var control=Console.Out;Console.SetOut(Console.Error);var json=new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=100000};
  var setup=json.Deserialize<Dictionary<string,object>>(File.ReadAllText(Path.Combine(root,"fixture.json")));var publics=((IEnumerable)setup["publics"]).Cast<object>().Select(x=>Convert.FromBase64String((string)x)).ToArray();var bindings=((IEnumerable)setup["bindings"]).Cast<object>().Select(x=>Convert.FromBase64String((string)x)).ToArray();Check(publics.Length==4&&bindings.Length==4,"Host fixture identities");
  var devices=publics.Select(MailboxProtocol.Digest).ToArray();var tokens=devices.Select(x=>Guid.NewGuid().ToString("N")).ToArray();var proof=GroupRosterProof.Parse((string)setup["creationProof"]);Check(proof.OwnerAccountId==1&&proof.OwnerIdentity==devices[0],"Host root source");string database=Path.Combine(root,"server-accounts.sqlite");var options=Options(Fixture(root,"server-cache"));options.Enabled=true;options.RetentionSeconds=3600;
  using(var db=new AccountDatabase(database)){
   for(int i=0;i<4;i++){Check(db.CreateAccountWithDevice("fixture-"+(i+1),new byte[16],FixturePasswordHash(),devices[i])==i+1,"Host accounts");ContextSession(db,i+1,devices[i],tokens[i]);}
   Check(db.RegisterGroup(1,devices[0],proof)!=null,"Host root registration");for(int i=1;i<=2;i++)Check(db.GroupInvite(proof.ServerGroupId,1,devices[0],i+1,devices[i],i*2-1)!=null&&db.GroupRespond(proof.ServerGroupId,i+1,devices[i],true,i*2)!=null,"Host initial roster");db.InitializeMailbox();for(int i=0;i<4;i++)Check(db.RegisterMailboxKey(i+1,devices[i],publics[i],bindings[i])=="accepted","Host signed recipient key registration");
  }
  string certificatePath=Path.Combine(root,"server-test.pfx"),pin;using(var key=RSA.Create(2048)){var request=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);using(var certificate=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1))){File.WriteAllBytes(certificatePath,certificate.Export(X509ContentType.Pfx,"fixture-only"));pin=MailboxProtocol.Digest(certificate.RawData);}}
  var reservation=new System.Net.Sockets.TcpListener(System.Net.IPAddress.Loopback,0);reservation.Start();int port=((System.Net.IPEndPoint)reservation.LocalEndpoint).Port;reservation.Stop();var config=new ServerConfiguration{DataDirectory=root,DatabasePath=database,LogDirectory=Path.Combine(root,"server-logs"),Port=port,CertificatePath=certificatePath,CertificatePassword="fixture-only",ManagementPipe="",AttachmentCache=options};config.Validate();
  string candidate=Environment.GetEnvironmentVariable("PEERCHAT_TEST_CACHE_CANDIDATE_SERVER");if(!String.IsNullOrEmpty(candidate))Check(Path.IsPathRooted(candidate)&&File.Exists(candidate),"Packaged fixture server path");
  IDisposable server=null;var clients=new DiscoveryClient[4];Action restart=()=>{foreach(var client in clients)if(client!=null)client.CloseRpc();if(server!=null){server.Dispose();server=null;}server=String.IsNullOrEmpty(candidate)?(IDisposable)new ContextServer(config):new CandidateServer(candidate,config);};
  Action<Dictionary<string,object>> send=value=>{control.WriteLine(Convert.ToBase64String(HostRecord("PeerChat/AttachmentTestHost/v1",w=>HostValue(w,value))));control.Flush();};
  try{
   restart();for(int i=0;i<4;i++){clients[i]=new DiscoveryClient("localhost",port,pin){AccountId=i+1,SessionToken=tokens[i],RegisteredPublicIdentity=devices[i]};clients[i].RefreshSessionInfo();}
   Dictionary<string,object> roster;using(var db=new AccountDatabase(database))roster=db.GroupRoster(proof.ServerGroupId,1,devices[0],false);send(new Dictionary<string,object>{{"pin",pin},{"roster",roster}});
   string attachmentId=null;var admitted=new HashSet<string>();var uploadedOffsets=new HashSet<long>();long uniqueUploadBytes=0,expectedCipherBytes=0;string line;while((line=Console.ReadLine())!=null){Check(line.Length<=100000,"Host request bound");var command=json.Deserialize<Dictionary<string,object>>(line);string operation=(string)command["operation"];
    if(operation=="rpc"||operation=="legacy_rpc"){
     int index=Convert.ToInt32(command["actor"])-1;Check(index>=0&&index<clients.Length,"Host actor");var request=(Dictionary<string,object>)command["request"];ulong generation=Convert.ToUInt64(request["generation"]);string requestId=(string)request["requestId"];request.Remove("generation");var client=clients[index];if(operation=="legacy_rpc"){client=new DiscoveryClient("localhost",port,pin);client.Login("fixture-"+(index+1),"fixture-password-only");client.RefreshSessionInfo();}var response=client.AttachmentCache(devices[index],request,index+1,client.SessionToken,pin);if(operation=="legacy_rpc")client.CloseRpc();if(Convert.ToString(response["state"])=="accepted"){if((string)request["type"]=="cache_admit"){var descriptor=AttachmentCacheDescriptor.Parse(Convert.FromBase64String((string)request["descriptor"]));attachmentId=descriptor.Id;admitted.Add(attachmentId);expectedCipherBytes=descriptor.CipherBytes;}if((string)request["type"]=="cache_append"&&uploadedOffsets.Add(Convert.ToInt64(request["offset"])))uniqueUploadBytes+=AttachmentCacheRequests.Decode((string)request["data"]).Length;}var wire=HostRecord("PeerChat/AttachmentResponse/v1",w=>{w.Write(generation);Text(w,requestId);HostValue(w,response);});control.WriteLine(Convert.ToBase64String(wire));control.Flush();continue;
    }
    if(operation=="project"){send(json.Deserialize<Dictionary<string,object>>((string)command["json"]));continue;}
    if(operation=="late_denied"||operation=="key_acks"||operation=="upload_once"||operation=="revoke_rejoin"){
     Func<int,string,Dictionary<string,object>> call=(actor,type)=>clients[actor].AttachmentCache(devices[actor],new Dictionary<string,object>{{"type",type},{"requestId",Guid.NewGuid().ToString("N")},{"id",attachmentId},{"after",""}},actor+1,clients[actor].SessionToken,pin);
     if(operation=="late_denied"){var pending=call(3,"cache_pending_keys");var denied=call(3,"cache_context");send(new Dictionary<string,object>{{"denied",((IEnumerable)pending["items"]).Cast<object>().Count()==0&&Convert.ToString(denied["state"])=="cache_original_membership_required"}});continue;}
     if(operation=="key_acks"){send(new Dictionary<string,object>{{"empty",new[]{1,2}.All(i=>((IEnumerable)call(i,"cache_pending_keys")["items"]).Cast<object>().Count()==0)}});continue;}
     if(operation=="upload_once"){send(new Dictionary<string,object>{{"singleObject",admitted.Count==1&&expectedCipherBytes>0&&uniqueUploadBytes==expectedCipherBytes}});continue;}
     foreach(var client in clients)client.CloseRpc();server.Dispose();server=null;using(var db=new AccountDatabase(database)){Check(db.GroupRemove(proof.ServerGroupId,1,devices[0],2,devices[1],7)!=null,"Host remove");Check(db.GroupInvite(proof.ServerGroupId,1,devices[0],2,devices[1],8)!=null&&db.GroupRespond(proof.ServerGroupId,2,devices[1],true,9)!=null,"Host rejoin");}restart();var rejected=call(1,"cache_context");send(new Dictionary<string,object>{{"denied",Convert.ToString(rejected["state"])=="cache_original_membership_required"}});continue;
    }
    if(operation=="logout"){clients[0].Logout();send(new Dictionary<string,object>{{"loggedOut",String.IsNullOrEmpty(clients[0].SessionToken)}});continue;}
    if(operation=="source_login"){foreach(var client in clients)client.CloseRpc();server.Dispose();server=null;tokens[0]=Guid.NewGuid().ToString("N");using(var db=new AccountDatabase(database))ContextSession(db,1,devices[0],tokens[0]);clients[0].SessionToken=tokens[0];restart();clients[0].RefreshSessionInfo();send(new Dictionary<string,object>{{"signedIn",true}});continue;}
    if(operation=="restart"){restart();send(new Dictionary<string,object>{{"restarted",true}});continue;}
    if(operation=="disable_cache"){config.AttachmentCache.Enabled=false;restart();send(new Dictionary<string,object>{{"disabled",true}});continue;}
    if(operation=="advance"){
     foreach(var client in clients)client.CloseRpc();server.Dispose();server=null;using(var db=new AccountDatabase(database)){Check(db.GroupInvite(proof.ServerGroupId,1,devices[0],4,devices[3],5)!=null&&db.GroupRespond(proof.ServerGroupId,4,devices[3],true,6)!=null,"Host advanced roster");roster=db.GroupRoster(proof.ServerGroupId,2,devices[1],false);}restart();send(new Dictionary<string,object>{{"roster",roster}});continue;
    }
    if(operation=="quit"){send(new Dictionary<string,object>{{"stopped",true}});break;}
    throw new InvalidOperationException("Unknown test-host command");
   }
  }finally{foreach(var client in clients)if(client!=null)client.CloseRpc();if(server!=null)server.Dispose();}
 }
}
