using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using PeerChat.Server;
using PeerChat.ServerShared;

internal static class FriendRelayRevocationServerTest {
    sealed class Relay : IDisposable {
        internal TcpClient Tcp;
        internal SslStream Tls;
        internal string Session;
        public void Dispose(){if(Tls!=null)Tls.Dispose();if(Tcp!=null)Tcp.Close();}
    }
    static readonly JavaScriptSerializer Json=new JavaScriptSerializer();
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static string Hash(string token){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(token))).Replace("-","").ToLowerInvariant();}
    static string Device(int n){return n.ToString("x64");}
    static void Write(Stream stream,byte[] bytes){var header=new byte[]{(byte)(bytes.Length>>24),(byte)(bytes.Length>>16),(byte)(bytes.Length>>8),(byte)bytes.Length};stream.Write(header,0,4);stream.Write(bytes,0,bytes.Length);stream.Flush();}
    static byte[] Read(Stream stream){byte[] header=Exact(stream,4);if(header==null)return null;int size=header[0]<<24|header[1]<<16|header[2]<<8|header[3];if(size<1||size>70000)throw new InvalidDataException("Relay frame bound failed.");return Exact(stream,size);}
    static byte[] Exact(Stream stream,int size){var bytes=new byte[size];for(int done=0;done<size;){int n=stream.Read(bytes,done,size-done);if(n==0)return null;done+=n;}return bytes;}
    static Relay Open(int port,string pin,string token,string local,long target,string remote){
        var tcp=new TcpClient();tcp.Connect(IPAddress.Loopback,port);tcp.ReceiveTimeout=4000;tcp.SendTimeout=4000;
        var tls=new SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>{
            if(certificate==null)return false;
            using(var sha=SHA256.Create())return String.Equals(BitConverter.ToString(sha.ComputeHash(certificate.GetRawCertData())).Replace("-",""),pin,StringComparison.Ordinal);
        });
        try{
            tls.AuthenticateAsClient("localhost");tls.ReadTimeout=4000;tls.WriteTimeout=4000;
            Write(tls,Encoding.UTF8.GetBytes(Json.Serialize(new Dictionary<string,object>{{"version",1},{"type","relay_attach"},{"sessionToken",token},
                {"publicIdentity",local},{"targetAccountId",target.ToString()},{"targetPublicIdentity",remote},{"purpose","friend"}})));
            var result=Json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(Read(tls)));
            Check(result.ContainsKey("ok")&&(bool)result["ok"]&&(string)result["type"]=="relay_ready","Paired friend relay was not ready.");
            return new Relay{Tcp=tcp,Tls=tls,Session=(string)result["sessionId"]};
        }catch{tls.Dispose();tcp.Close();throw;}
    }
    static void Send(Relay relay,string text){Write(relay.Tls,Encoding.ASCII.GetBytes(relay.Session+text));}
    static void Case(string root,string certificate,string pin,string name,Action<AccountDatabase,string,string,string> revoke,bool boundRecipient=false,bool pending=false){
        string folder=Path.Combine(root,name);Directory.CreateDirectory(folder);
        string a=Device(71),b=Device(82),senderToken=Guid.NewGuid().ToString("N"),recipientToken=Guid.NewGuid().ToString("N");
        string senderHash=Hash(senderToken),recipientHash=Hash(recipientToken);
        var portProbe=new TcpListener(IPAddress.Loopback,0);portProbe.Start();int port=((IPEndPoint)portProbe.LocalEndpoint).Port;portProbe.Stop();
        string database=Path.Combine(folder,"relay.db");
        using(var db=new AccountDatabase(database)){
            db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'sender',X'00',X'00',1),(8,'recipient',X'00',X'00',1);"+
                "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+a+"',1),(8,'"+b+"',1);"+
                (pending?"INSERT INTO friend_requests(sender_id,recipient_id,state,created_utc,updated_utc) VALUES(7,8,'pending',1,1);":"INSERT INTO friendships(account_low,account_high,created_utc) VALUES(7,8,1);"));
            long expiry=DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds();db.CreateSession(senderHash,7,expiry);
            if(boundRecipient){db.RememberDevice(8,b,"old-test-credential");db.CreateDeviceSession(recipientHash,8,b,expiry,"old-test-credential");}
            else db.CreateSession(recipientHash,8,expiry);
            var config=new ServerConfiguration{DataDirectory=folder,DatabasePath=database,LogDirectory=Path.Combine(folder,"logs"),Port=port,
                CertificatePath=certificate,CertificatePassword="test-only",ManagementPipe=""};
            var server=new DiscoveryServer(config);Exception serverFailure=null;
            var worker=new Thread(()=>{try{server.Run();}catch(Exception e){serverFailure=e;}}){IsBackground=true};worker.Start();
            try{
                bool ready=false;DateTime deadline=DateTime.UtcNow.AddSeconds(8);
                while(DateTime.UtcNow<deadline&&!ready){try{using(var tcp=new TcpClient()){tcp.Connect(IPAddress.Loopback,port);ready=true;}}catch(SocketException){Thread.Sleep(20);}}
                Check(ready,"Local test server did not start.");
                var leftTask=Task.Run(()=>Open(port,pin,senderToken,a,8,b));
                var rightTask=Task.Run(()=>Open(port,pin,recipientToken,b,7,a));
                Check(Task.WaitAll(new Task[]{leftTask,rightTask},10000),"Relay pair timed out.");
                using(var left=leftTask.Result)using(var right=rightTask.Result){
                    Check(left.Session==right.Session&&left.Session.Length==43,"Relay pair did not share one session.");
                    Send(left,"before-revocation");
                    Check(Encoding.ASCII.GetString(Read(right.Tls))==left.Session+"before-revocation","Healthy friend relay did not forward.");
                    revoke(db,a,b,recipientHash);
                    try{Send(left,"after-revocation");}catch(IOException){}
                    bool forwarded=false;
                    try{var payload=Read(right.Tls);forwarded=payload!=null&&Encoding.ASCII.GetString(payload)==right.Session+"after-revocation";}
                    catch(IOException){}catch(System.Security.Authentication.AuthenticationException){}
                    Check(!forwarded,"Established friend relay forwarded after "+name+".");
                }
            }finally{server.RequestStop();Check(worker.Join(8000),"Local test server did not stop.");if(serverFailure!=null)throw serverFailure;}
        }
    }
    public static int Main(string[] args){try{
        Check(args.Length==1,"Expected isolated temporary directory.");Directory.CreateDirectory(args[0]);
        string certificate=Path.Combine(args[0],"relay-test.pfx");
        string pin;using(var key=RSA.Create(2048)){
            var request=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
            using(var cert=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1))){
                using(var sha=SHA256.Create())pin=BitConverter.ToString(sha.ComputeHash(cert.RawData)).Replace("-","");
                File.WriteAllBytes(certificate,cert.Export(X509ContentType.Pfx,"test-only"));
            }
        }
        Case(args[0],certificate,pin,"session-revoked",(db,a,b,hash)=>db.RevokeSession(hash));
        Case(args[0],certificate,pin,"friend-removed",(db,a,b,hash)=>db.Execute("DELETE FROM friendships WHERE account_low=7 AND account_high=8;"));
        Case(args[0],certificate,pin,"blocked",(db,a,b,hash)=>db.Execute("INSERT INTO account_blocks(blocker_id,blocked_id,created_utc) VALUES(8,7,1);"));
        Case(args[0],certificate,pin,"device-removed",(db,a,b,hash)=>db.Execute("DELETE FROM devices WHERE account_id=8 AND public_identity='"+b+"';"));
        Case(args[0],certificate,pin,"credential-superseded",(db,a,b,hash)=>db.RememberDevice(8,b,"replacement-test-credential"),true);
        Case(args[0],certificate,pin,"pending-request-cancelled",(db,a,b,hash)=>db.Execute("DELETE FROM friend_requests WHERE sender_id=7 AND recipient_id=8;"),false,true);
        Console.WriteLine("PASS: genuine paired friend relay forwards for friends and legacy pending requests, then refuses frames after session revocation, relationship removal/block, device removal, remembered-credential supersession, or pending-request cancellation.");return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
