using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;

static class ServerWorkerBurstTests {
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    public static int Run(string root,bool baseline){
        Directory.CreateDirectory(root);
        int oldWorker,oldIo;ThreadPool.GetMinThreads(out oldWorker,out oldIo);
        Check(ThreadPool.SetMinThreads(1,oldIo),"test pool minimum");
        string pfx=Path.Combine(root,"test.pfx");
        using(var key=RSA.Create(2048)) {
            var request=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
            using(var certificate=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1)))File.WriteAllBytes(pfx,certificate.Export(X509ContentType.Pfx,"test-only"));
        }
        if(!OperatingSystem.IsWindows())File.SetUnixFileMode(pfx,UnixFileMode.UserRead|UnixFileMode.UserWrite);
        var listener=new TcpListener(IPAddress.Loopback,0);listener.Start();int port=((IPEndPoint)listener.LocalEndpoint).Port;listener.Stop();
        var config=new ServerConfiguration{DataDirectory=root,DatabasePath=Path.Combine(root,"burst.sqlite"),LogDirectory=Path.Combine(root,"logs"),Port=port,CertificatePath=pfx,CertificatePassword="test-only",ManagementPipe=""};config.Validate();
        var server=new DiscoveryServer(config);
        var db=(AccountDatabase)typeof(DiscoveryServer).GetField("accounts",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server);
        string device=new string('a',64),token="isolated-burst-token";
        long account=db.CreateAccountWithDevice("burst",new byte[16],new byte[32],device);
        db.CreateSession(MailboxProtocol.Digest(Encoding.UTF8.GetBytes(token)),account,DateTimeOffset.UtcNow.ToUnixTimeSeconds()+600);
        Exception failure=null;var runner=new Thread(()=>{try{server.Run();}catch(Exception e){failure=e;}}){IsBackground=true};
        var held=new List<TcpClient>();runner.Start();
        try {
            var deadline=Stopwatch.StartNew();
            for(int i=0;i<12;i++) {
                var client=new TcpClient();held.Add(client);
                while(true){try{client.Connect(IPAddress.Loopback,port);break;}catch(SocketException){Check(deadline.ElapsedMilliseconds<10000,"listener readiness");Thread.Sleep(20);}}
            }
            var watch=Stopwatch.StartNew();
            using(var expected=X509CertificateLoader.LoadPkcs12FromFile(pfx,"test-only"))
            using(var client=new TcpClient()) {
                client.Connect(IPAddress.Loopback,port);
                using(var tls=new SslStream(client.GetStream(),false,(s,c,chain,errors)=>c!=null&&c.GetCertHashString()==expected.GetCertHashString())) {
                    tls.ReadTimeout=20000;tls.WriteTimeout=20000;tls.AuthenticateAsClient("localhost");
                    string request="{\"version\":1,\"type\":\"friend_state\",\"sessionToken\":\""+token+"\"}";
                    DiscoveryClient.WriteFrame(tls,Encoding.UTF8.GetBytes(request));
                    string reply=Encoding.UTF8.GetString(DiscoveryClient.ReadFrame(tls));
                    Check(reply.Contains("\"ok\":true"),"authenticated request failed");
                }
            }
            Console.WriteLine("WORKER_BURST partial_tls_clients=12 authenticated_request_ms="+watch.ElapsedMilliseconds+" scope=isolated_loopback_no_physical_network baseline="+baseline);
            if(!baseline){int workers,io;ThreadPool.GetMinThreads(out workers,out io);Check(workers>=32&&io==oldIo,"worker reserve or I/O minimum changed incorrectly");Check(watch.ElapsedMilliseconds<1500,"healthy request queued behind partial TLS clients");}
        } finally {
            foreach(var client in held)client.Dispose();server.RequestStop();Check(runner.Join(25000),"burst shutdown");ThreadPool.SetMinThreads(oldWorker,oldIo);
        }
        Check(failure==null,"server runner failed: "+failure);return 0;
    }
}
