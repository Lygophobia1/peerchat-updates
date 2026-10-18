using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using PeerChat.Server;
using PeerChat.ServerShared;

internal static class LegacySessionCompatibilityTest {
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    static string Hash(string token){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(token))).Replace("-","").ToLowerInvariant();}
    static Dictionary<string,object> Request(string type,params string[] fields){var value=new Dictionary<string,object>{{"version",1},{"type",type}};for(int i=0;i<fields.Length;i+=2)value[fields[i]]=fields[i+1];return value;}
    static Dictionary<string,object> Call(DiscoveryServer server,Dictionary<string,object> request){return (Dictionary<string,object>)typeof(DiscoveryServer).GetMethod("Handle",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,new object[]{request,"127.0.0.1",0});}
    static string Accepted(Dictionary<string,object> response,string field){Check((bool)response["ok"],"Expected accepted "+field);return Convert.ToString(response[field]);}
    static void Rejected(Dictionary<string,object> response,string error){Check(!(bool)response["ok"]&&Convert.ToString(response["error"])==error,"Expected "+error);}
    static void WriteFrame(Stream stream,byte[] bytes){var header=new byte[]{(byte)(bytes.Length>>24),(byte)(bytes.Length>>16),(byte)(bytes.Length>>8),(byte)bytes.Length};stream.Write(header,0,4);stream.Write(bytes,0,bytes.Length);stream.Flush();}
    static byte[] ReadFrame(Stream stream){var header=new byte[4];ReadExact(stream,header);int length=header[0]<<24|header[1]<<16|header[2]<<8|header[3];Check(length>0&&length<=100000,"Event response bound");var bytes=new byte[length];ReadExact(stream,bytes);return bytes;}
    static void ReadExact(Stream stream,byte[] bytes){int done=0;while(done<bytes.Length){int count=stream.Read(bytes,done,bytes.Length-done);if(count<=0)throw new EndOfStreamException();done+=count;}}
    static Dictionary<string,object> EventReply(int port,string pin,string token,string device){
        using(var tcp=new TcpClient()){tcp.Connect(IPAddress.Loopback,port);tcp.ReceiveTimeout=5000;tcp.SendTimeout=5000;
            using(var tls=new SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>{
                if(certificate==null)return false;
                using(var sha=SHA256.Create())return String.Equals(BitConverter.ToString(sha.ComputeHash(certificate.GetRawCertData())).Replace("-",""),pin,StringComparison.Ordinal);
            })){
                tls.AuthenticateAsClient("localhost");
                WriteFrame(tls,Encoding.UTF8.GetBytes(Json.Serialize(Request("events","sessionToken",token,"publicIdentity",device,"clientHeartbeats","true"))));
                return Json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls)));
            }
        }
    }
    static long AuthorityRows(AccountDatabase db,string device){
        var field=typeof(AccountDatabase).GetField("database",BindingFlags.Instance|BindingFlags.NonPublic);
        IntPtr statement;Check(SqliteNative.prepare((IntPtr)field.GetValue(db),"SELECT count(*) FROM device_authority WHERE public_identity='"+device+"'",out statement)==0,"Authority fixture query");
        try{Check(SqliteNative.step(statement)==100,"Authority fixture row");return SqliteNative.columnLong(statement,0);}finally{SqliteNative.finalize(statement);}
    }
    public static int Main(string[] args){
        DiscoveryServer server=null;Thread worker=null;Exception serverFailure=null;
        try{
            Check(args.Length==1,"Expected an isolated output directory");Directory.CreateDirectory(args[0]);
            string pfx=Path.Combine(args[0],"server.pfx"),database=Path.Combine(args[0],"accounts.db"),pin;
            using(var rsa=RSA.Create(2048)){
                var request=new CertificateRequest("CN=localhost",rsa,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
                using(var certificate=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1))){
                    using(var sha=SHA256.Create())pin=BitConverter.ToString(sha.ComputeHash(certificate.RawData)).Replace("-","");
                    File.WriteAllBytes(pfx,certificate.Export(X509ContentType.Pfx,"test-only"));
                }
            }
            var probe=new TcpListener(IPAddress.Loopback,0);probe.Start();int port=((IPEndPoint)probe.LocalEndpoint).Port;probe.Stop();
            var config=new ServerConfiguration{DataDirectory=args[0],DatabasePath=database,LogDirectory=Path.Combine(args[0],"logs"),Port=port,
                CertificatePath=pfx,CertificatePassword="test-only",ManagementPipe=""};
            server=new DiscoveryServer(config);
            var db=(AccountDatabase)typeof(DiscoveryServer).GetField("accounts",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server);
            string oldDevice=new string('a',64),newDevice=new string('b',64);
            string oldRegistered=Accepted(Call(server,Request("register","username","legacy_owner","password","fixture-secret","publicIdentity",oldDevice)),"sessionToken");
            string legacy=Accepted(Call(server,Request("login","username","legacy_owner","password","fixture-secret")),"sessionToken");
            long oldAccount=db.Authenticate(Hash(legacy));
            Check(oldAccount>0&&db.SessionDevice(Hash(legacy))==""&&AuthorityRows(db,oldDevice)==0&&db.ActiveDevice(oldAccount,oldDevice),
                "Owned legacy device without an authority row was not active under an unbound login");
            string newRegistered=Accepted(Call(server,Request("register","username","bound_owner","password","fixture-secret","publicIdentity",newDevice)),"sessionToken");
            long newAccount=db.Authenticate(Hash(newRegistered));
            db.RememberDevice(newAccount,newDevice,new string('c',64));
            string bound=Guid.NewGuid().ToString("N");db.CreateDeviceSession(Hash(bound),newAccount,newDevice,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds(),new string('c',64));
            Check(db.Authenticate(Hash(bound))==newAccount&&db.SessionDevice(Hash(bound))==newDevice&&db.SessionAllowsDevice(Hash(bound),newDevice),
                "Valid remembered device session was not accepted");
            Check(db.SendFriendRequest(oldAccount,newAccount)=="pending"&&db.RespondToRequest(newAccount,oldAccount,true)=="friends","Fixture friendship failed");
            Check(Convert.ToInt64(Accepted(Call(server,Request("session_info","sessionToken",legacy)),"accountId"))==oldAccount,"Legacy session_info failed");
            Check(Accepted(Call(server,Request("presence","sessionToken",legacy,"publicIdentity",oldDevice,"directCandidates","false")),"registeredPublicIdentity")==oldDevice,
                "Legacy unbound presence failed");
            Check(Accepted(Call(server,Request("presence","sessionToken",bound,"publicIdentity",newDevice,"directCandidates","false")),"registeredPublicIdentity")==newDevice,
                "Bound presence failed");
            Check((bool)Call(server,Request("friend_state","sessionToken",legacy))["ok"]&&db.IsPresent(oldAccount)&&db.IsPresent(newAccount)&&
                db.CurrentFriendRelayAuthorized(oldAccount,oldDevice,Hash(legacy),newAccount,newDevice,Hash(bound)),
                "Legacy and bound sessions did not retain friend/presence/relay eligibility");
            worker=new Thread(()=>{try{server.Run();}catch(Exception error){serverFailure=error;}}){IsBackground=true};worker.Start();
            bool listening=false;var deadline=DateTime.UtcNow.AddSeconds(8);
            while(DateTime.UtcNow<deadline&&!listening){try{using(var tcp=new TcpClient()){tcp.Connect(IPAddress.Loopback,port);listening=true;}}catch(SocketException){Thread.Sleep(20);}}
            Check(listening,"Local event server did not start");
            Check((bool)EventReply(port,pin,legacy,oldDevice)["ok"],"Legacy unbound event subscription was rejected");
            Check((bool)EventReply(port,pin,bound,newDevice)["ok"],"Bound event subscription was rejected");
            Rejected(Call(server,Request("presence","sessionToken",bound,"publicIdentity",oldDevice,"directCandidates","false")),"device_session_mismatch");
            Check(!(bool)EventReply(port,pin,bound,oldDevice)["ok"],"Bound event session accepted a different device");
            db.Execute("INSERT INTO device_authority(public_identity,revoked,revoked_utc) VALUES('"+oldDevice+"',1,1),('"+newDevice+"',1,1);");
            Check(!db.ActiveDevice(oldAccount,oldDevice)&&!db.ActiveDevice(newAccount,newDevice)&&db.Authenticate(Hash(bound))==0,
                "Revocation did not remove current device authority");
            Rejected(Call(server,Request("presence","sessionToken",legacy,"publicIdentity",oldDevice,"directCandidates","false")),"device_revoked");
            Rejected(Call(server,Request("presence","sessionToken",bound,"publicIdentity",newDevice,"directCandidates","false")),"session_invalid");
            Check(!(bool)EventReply(port,pin,legacy,oldDevice)["ok"]&&!(bool)EventReply(port,pin,bound,newDevice)["ok"],
                "Revoked device reopened an event subscription");
            Check(!db.CurrentFriendRelayAuthorized(oldAccount,oldDevice,Hash(legacy),newAccount,newDevice,Hash(bound)),
                "Revoked device retained friend relay authority");
            db.RevokeSession(Hash(legacy));Check(db.Authenticate(Hash(legacy))==0,"Revoked legacy token remained authenticated");
            Console.WriteLine("PASS: legacy unbound login, owned device without authority row, presence, friends, event subscription and relay eligibility; valid bound session; mismatch and revocation rejection.");
            return 0;
        }catch(Exception error){Console.Error.WriteLine(error);return 1;}
        finally{if(server!=null)server.RequestStop();if(worker!=null){if(!worker.Join(8000))Console.Error.WriteLine("Event server did not stop");if(serverFailure!=null)Console.Error.WriteLine(serverFailure);}}
    }
}
