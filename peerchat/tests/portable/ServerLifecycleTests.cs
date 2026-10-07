using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Net.Security;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;

static class ServerLifecycleTests {
    sealed class HeldPush:IPushProvider {
        internal readonly ManualResetEvent Entered=new ManualResetEvent(false),Release=new ManualResetEvent(false);
        public PushResult Wake(string token){Entered.Set();Release.WaitOne(5000);return PushResult.Sent;}
    }
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static string Scalar(AccountDatabase db,string sql){
        var handle=(IntPtr)typeof(AccountDatabase).GetField("database",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(db);
        IntPtr statement=IntPtr.Zero;
        try{Check(SqliteNative.prepare(handle,sql,out statement)==0,"prepare");Check(SqliteNative.step(statement)==100,"step");return SqliteNative.columnText(statement,0);}
        finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
    }
    internal static void Run(string root){
        string database=Path.Combine(root,"portable-\u03b4-history.db");
        for(int i=0;i<50;i++)using(var db=new AccountDatabase(database)){
            Check(Scalar(db,"PRAGMA journal_mode")=="wal","WAL changed");
            Check(Scalar(db,"PRAGMA synchronous")=="2","FULL durability changed");
            Check(Scalar(db,"PRAGMA busy_timeout")=="5000","busy timeout");
            db.Execute("CREATE TABLE IF NOT EXISTS portable_fixture(value TEXT UNIQUE);INSERT OR IGNORE INTO portable_fixture VALUES('Jonah \u03b4 \ud83d\ude80');");
            Check(Scalar(db,"SELECT value FROM portable_fixture")=="Jonah \u03b4 \ud83d\ude80","SQLite UTF-8 changed");
        }
        using(var first=new AccountDatabase(database))using(var second=new AccountDatabase(database)){
            first.Execute("BEGIN IMMEDIATE;");Exception failure=null;
            var writer=new Thread(()=>{try{second.Execute("INSERT OR IGNORE INTO portable_fixture VALUES('busy recovered');");}catch(Exception e){failure=e;}});
            writer.Start();Thread.Sleep(100);first.Execute("COMMIT;");Check(writer.Join(6000)&&failure==null,"busy writer failed");
        }
        string corrupt=Path.Combine(root,"corrupt.db");File.WriteAllText(corrupt,"not a sqlite database");bool rejected=false;
        try{using(var ignored=new AccountDatabase(corrupt)){}}catch(InvalidOperationException){rejected=true;}
        Check(rejected,"corrupt DB did not fail closed");
        using(var db=new AccountDatabase(Path.Combine(root,"push-drain.db"))){
            string device=new string('e',64);long account=db.CreateAccountWithDevice("push-drain",new byte[16],new byte[32],device);
            db.RememberDevice(account,device,"test-only-remembered-hash");Check(db.RegisterPush(account,device,"test-only-token"),"push fixture");
            var provider=new HeldPush();var dispatcher=new PushWakeDispatcher(db,provider);
            Check(dispatcher.Hint(account,device)&&provider.Entered.WaitOne(5000),"push worker start");
            dispatcher.Stop();Check(!dispatcher.Drain(10),"push drain must wait for active work");
            provider.Release.Set();Check(dispatcher.Drain(5000)&&!dispatcher.Hint(account,device),"push stop/drain");
            provider.Entered.Dispose();provider.Release.Dispose();
        }
        // Test-only certificate; no production stores/identities are used.
        string pfx=Path.Combine(root,"lifecycle-test.pfx");
        using(var key=RSA.Create(2048)){
            var request=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
            using(var cert=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1)))File.WriteAllBytes(pfx,cert.Export(X509ContentType.Pfx,"test-only"));
        }
        if(!OperatingSystem.IsWindows())File.SetUnixFileMode(pfx,UnixFileMode.UserRead|UnixFileMode.UserWrite);
        for(int cycle=0;cycle<5;cycle++){
            var reservation=new TcpListener(IPAddress.Loopback,0);reservation.Start();int port=((IPEndPoint)reservation.LocalEndpoint).Port;reservation.Stop();
            var config=new ServerConfiguration{DataDirectory=root,DatabasePath=database,LogDirectory=Path.Combine(root,"logs"),Port=port,CertificatePath=pfx,CertificatePassword="test-only",ManagementPipe=""};config.Validate();
            var server=new DiscoveryServer(config);Exception error=null;
            var runner=new Thread(()=>{try{server.Run();}catch(Exception e){error=e;}}){IsBackground=true};runner.Start();
            using(var idle=new TcpClient()){
                var deadline=DateTime.UtcNow.AddSeconds(10);while(true){try{idle.Connect(IPAddress.Loopback,port);break;}catch(SocketException){if(DateTime.UtcNow>=deadline)throw;Thread.Sleep(20);}}
                using(var partial=new TcpClient()){
                    partial.Connect(IPAddress.Loopback,port);
                    using(var expected=X509CertificateLoader.LoadPkcs12FromFile(pfx,"test-only"))
                    using(var tls=new SslStream(partial.GetStream(),false,(sender,cert,chain,errors)=>cert!=null&&cert.GetCertHashString()==expected.GetCertHashString())){
                        tls.AuthenticateAsClient("localhost");tls.Write(new byte[]{0,0},0,2);tls.Flush();
                        server.RequestStop();Check(runner.Join(12000),"server stop hung with active TLS/partial reads");Check(error==null,"shutdown failed: "+error);
                    }
                }
            }
            using(var db=new AccountDatabase(database))Check(Scalar(db,"SELECT count(*) FROM portable_fixture")=="2","restart lost committed records");
            var reopened=new TcpListener(IPAddress.Loopback,port);reopened.Start();reopened.Stop();
        }
        Console.WriteLine("PASS: SQLite Unicode/FULL/WAL/busy/reopen/corruption; five active TLS/partial-read shutdown/drain/restart cycles.");
    }
}
