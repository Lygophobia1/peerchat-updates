using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using PeerChat.Server;
using PeerChat.ServerShared;

// Actual production server, SQLite, signed CNG fixtures and pinned TLS sockets.
// Outer relay lifecycle proof; native archive/verified import is tested separately.
internal static class ArchiveRelayLifecycleTest {
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
    static string Hash(byte[] bytes){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-","").ToLowerInvariant();}
    static byte[] ReadExact(Stream stream,int length){var bytes=new byte[length];int offset=0;while(offset<length){int n=stream.Read(bytes,offset,length-offset);if(n==0)throw new EndOfStreamException();offset+=n;}return bytes;}
    static void Write(Stream stream,byte[] bytes){stream.Write(new[]{(byte)(bytes.Length>>24),(byte)(bytes.Length>>16),(byte)(bytes.Length>>8),(byte)bytes.Length},0,4);stream.Write(bytes,0,bytes.Length);}
    static byte[] Read(Stream stream){var h=ReadExact(stream,4);int n=h[0]<<24|h[1]<<16|h[2]<<8|h[3];Check(n>0&&n<100000,"Frame bound");return ReadExact(stream,n);}
    sealed class Socket : IDisposable {
        internal readonly TcpClient Tcp;internal readonly SslStream Tls;
        internal Socket(int port,string pin){Tcp=new TcpClient();Tcp.NoDelay=true;Tcp.Connect(IPAddress.Loopback,port);Tls=new SslStream(Tcp.GetStream(),false,(s,c,chain,e)=>c!=null&&Hash(c.GetRawCertData())==pin);Tls.AuthenticateAsClient("localhost",null,SslProtocols.Tls12,false);Tls.ReadTimeout=4000;Tls.WriteTimeout=4000;}
        internal void Send(Dictionary<string,object> request){Write(Tls,Encoding.UTF8.GetBytes(Json.Serialize(request)));}
        internal Dictionary<string,object> Result(){return Json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(Read(Tls)));}
        public void Dispose(){Tcp.Close();Tls.Dispose();}
    }
    static Dictionary<string,object> Request(string type,string token,string device){return new Dictionary<string,object>{{"version",1},{"type",type},{"sessionToken",token},{"publicIdentity",device}};}
    static Socket Attach(int port,string pin,string token,string device,string target,ArchiveRelayAuthorization scope){var socket=new Socket(port,pin);var request=Request("relay_attach",token,device);request["targetAccountId"]="8";request["targetPublicIdentity"]=target;request["purpose"]="archive_history";scope.AddFields(request);request["archiveSourceGeneration"]=scope.SourceGeneration.ToString();request["archiveReaderGeneration"]=scope.ReaderGeneration.ToString();socket.Send(request);return socket;}
    static int Count(DiscoveryServer server){var t=typeof(DiscoveryServer);var gate=t.GetField("relayGate",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server);lock(gate)return ((ICollection)t.GetField("relayEndpoints",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server)).Count;}
    static void Until(Func<bool> done,string reason){var end=DateTime.UtcNow.AddSeconds(3);while(!done()&&DateTime.UtcNow<end)Thread.Sleep(10);Check(done(),reason);}
    public static int Main(string[] args){try{
        string root=args[0];var issuer=ArchiveAuthorizationProof.Descriptor.Parse(File.ReadAllBytes(Path.Combine(root,"issuer-descriptor.bin")));var reader=ArchiveAuthorizationProof.Descriptor.Parse(File.ReadAllBytes(Path.Combine(root,"reader-descriptor.bin")));
        var serve=File.ReadAllBytes(Path.Combine(root,"serve-both.bin"));var receive=File.ReadAllBytes(Path.Combine(root,"read-both.bin"));
        string path=Path.Combine(root,"relay-lifecycle.sqlite");
        using(var db=new AccountDatabase(path)){
            db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(8,'isolated-archive',X'00',X'00',1);");
            foreach(var descriptor in new[]{issuer,reader}){Check(db.AddDevice(8,descriptor.Device),"Add fixture device");Check(db.RegisterArchiveDescriptor(8,descriptor.Device,descriptor.Wire)=="accepted","Signed descriptor");}
            Check(db.RegisterArchiveGrant(8,issuer.Device,serve)=="accepted"&&db.RegisterArchiveGrant(8,issuer.Device,receive)=="accepted","Signed grants");
            string a=Guid.NewGuid().ToString("N"),b=Guid.NewGuid().ToString("N");db.CreateSession(Hash(Encoding.UTF8.GetBytes(a)),8,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());db.CreateSession(Hash(Encoding.UTF8.GetBytes(b)),8,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
            var probe=new TcpListener(IPAddress.Loopback,0);probe.Start();int port=((IPEndPoint)probe.LocalEndpoint).Port;probe.Stop();
            string pin;using(var cert=new X509Certificate2(args[1],args[2]))pin=Hash(cert.RawData);
            var server=new DiscoveryServer(new ServerConfiguration{DataDirectory=root,DatabasePath=path,LogDirectory=Path.Combine(root,"relay-logs"),Port=port,CertificatePath=args[1],CertificatePassword=args[2]});
            var thread=new Thread(server.Run){IsBackground=true};thread.Start();
            Until(()=>{try{using(var ready=new TcpClient()){ready.Connect(IPAddress.Loopback,port);return true;}}catch(SocketException){return false;}},"Isolated TLS listener startup");
            try{using(var ae=new Socket(port,pin))using(var be=new Socket(port,pin)){
                ae.Send(Request("events",a,issuer.Device));Check(Convert.ToBoolean(ae.Result()["ok"]),"Source event authorization");
                be.Send(Request("events",b,reader.Device));Check(Convert.ToBoolean(be.Result()["ok"]),"Reader event authorization");
                var sg=ArchiveAuthorizationProof.Grant.Parse(serve,issuer.Public);var rg=ArchiveAuthorizationProof.Grant.Parse(receive,issuer.Public);
                for(int direction=0;direction<2;direction++){
                    var scope=new ArchiveRelayAuthorization{Issuer=issuer.Device,Source=direction==0?issuer.Device:reader.Device,Reader=direction==0?reader.Device:issuer.Device,SourceGeneration=1,ReaderGeneration=1,SourceGrantId=direction==0?sg.IssuanceId:rg.IssuanceId,ReaderGrantId=direction==0?rg.IssuanceId:sg.IssuanceId};
                    string rt=direction==0?b:a,st=direction==0?a:b;
                    for(int cycle=0;cycle<3;cycle++){
                        using(var stale=Attach(port,pin,rt,scope.Reader,scope.Source,scope)){Until(()=>Count(server)==1,"Pending archive claim");stale.Tcp.Close();Until(()=>Count(server)==0,"Cancelled archive pairing retained ownership");}
                        using(var left=Attach(port,pin,rt,scope.Reader,scope.Source,scope)){
                            Until(()=>Count(server)==1,"Fresh archive claim");
                            using(var duplicate=Attach(port,pin,rt,scope.Reader,scope.Source,scope))Check(Convert.ToString(duplicate.Result()["error"])=="relay_duplicate_attachment","Pending duplicate not rejected");
                            using(var right=Attach(port,pin,st,scope.Source,scope.Reader,scope)){
                                var lr=left.Result();var rr=right.Result();Check(Convert.ToBoolean(lr["ok"])&&Convert.ToBoolean(rr["ok"])&&Convert.ToString(lr["sessionId"])==Convert.ToString(rr["sessionId"])&&Convert.ToBoolean(lr["incoming"])!=Convert.ToBoolean(rr["incoming"]),"Paired archive roles");
                                using(var duplicate=Attach(port,pin,rt,scope.Reader,scope.Source,scope))Check(Convert.ToString(duplicate.Result()["error"])=="relay_duplicate_attachment","Active winner replaced");
                                byte[] payload=Encoding.ASCII.GetBytes(Convert.ToString(lr["sessionId"])+"opaque-fixture");Write(left.Tls,payload);Check(Convert.ToBase64String(Read(right.Tls))==Convert.ToBase64String(payload),"Winner lost after duplicate");Write(right.Tls,payload);Check(Convert.ToBase64String(Read(left.Tls))==Convert.ToBase64String(payload),"Reverse forwarding");
                            }
                        }
                        Until(()=>Count(server)==0,"Paired cleanup");Check(db.Authenticate(Hash(Encoding.UTF8.GetBytes(a)))==8&&db.Authenticate(Hash(Encoding.UTF8.GetBytes(b)))==8,"Relay retries invalidated account session");
                    }
                }
                // A genuinely newer authenticated event session for THIS device
                // must still supersede the old one; archive errors did not do so.
                string next=Guid.NewGuid().ToString("N");db.CreateSession(Hash(Encoding.UTF8.GetBytes(next)),8,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
                using(var newer=new Socket(port,pin)){newer.Send(Request("events",next,reader.Device));Check(Convert.ToBoolean(newer.Result()["ok"]),"New authenticated session");using(var obsolete=new Socket(port,pin)){obsolete.Send(Request("events",b,reader.Device));Check(Convert.ToString(obsolete.Result()["error"])=="event_session_superseded","Real supersession suppressed");}}
            }}finally{typeof(DiscoveryServer).GetMethod("RequestStop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,null);Check(thread.Join(6000),"Server shutdown");}
        }
        Console.WriteLine("PASS: signed archive/pinned-TLS relay both directions, cancelled pairing, repeated retry, duplicate rejection without winner replacement, exact opaque forwarding, auth retained and real supersession rejected.");return 0;
    }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.GetType().Name+": "+error.Message);return 1;}}
}
