using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;

// Isolated pinned TLS fixture, not production trust or a network speed claim.
using var key=RSA.Create(2048);
var request=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
using var generated=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddHours(1));
using var cert=X509CertificateLoader.LoadPkcs12(generated.Export(X509ContentType.Pfx),null,X509KeyStorageFlags.UserKeySet);
var expected=cert.GetCertHashString(HashAlgorithmName.SHA256);
foreach(var mode in new[]{(noDelay:false,joined:false),(noDelay:true,joined:false),(noDelay:false,joined:true)}){
    var listener=new TcpListener(IPAddress.Loopback,0);listener.Start();
    var server=Task.Run(async()=>{
        using var tcp=await listener.AcceptTcpClientAsync();tcp.NoDelay=mode.noDelay;tcp.ReceiveTimeout=15000;tcp.SendTimeout=15000;
        using var tls=new SslStream(tcp.GetStream());
        await tls.AuthenticateAsServerAsync(cert,false,SslProtocols.Tls12,false);
        for(int n=0;n<25;n++){
            var frame=ReadFrame(tls);if(frame.Length!=32768)throw new Exception("Payload framing changed");
            WriteFrame(tls,new byte[64],mode.joined);
        }
    });
    using var peer=new TcpClient{NoDelay=true,ReceiveTimeout=15000,SendTimeout=15000};await peer.ConnectAsync(IPAddress.Loopback,((IPEndPoint)listener.LocalEndpoint).Port);
    using var connection=new SslStream(peer.GetStream(),false,(_,presented,_,_)=>presented!=null&&presented.GetCertHashString(HashAlgorithmName.SHA256)==expected);
    try{await connection.AuthenticateAsClientAsync("localhost",null,SslProtocols.Tls12,false);}catch{await server;throw;}
    var costs=new List<double>();
    for(int n=0;n<25;n++){
        var started=Stopwatch.GetTimestamp();WriteFrame(connection,new byte[32768],true);
        if(ReadFrame(connection).Length!=64)throw new Exception("ACK framing changed");
        if(n>=5)costs.Add(Stopwatch.GetElapsedTime(started).TotalMilliseconds);
    }
    await server.WaitAsync(TimeSpan.FromSeconds(15));listener.Stop();
    Console.WriteLine($"HOST_TLS_FRAMING server_no_delay={mode.noDelay} joined_frame={mode.joined} samples={costs.Count} mean_round_ms={costs.Average():F3} max_round_ms={costs.Max():F3}");
}
static byte[] ReadFrame(Stream stream){
    var header=new byte[4];stream.ReadExactly(header);int size=(header[0]<<24)|(header[1]<<16)|(header[2]<<8)|header[3];
    if(size<1||size>65536)throw new Exception("Frame bound");var bytes=new byte[size];stream.ReadExactly(bytes);return bytes;
}
static void WriteFrame(Stream stream,byte[] bytes,bool joined){
    var header=new[]{(byte)(bytes.Length>>24),(byte)(bytes.Length>>16),(byte)(bytes.Length>>8),(byte)bytes.Length};
    if(joined){var frame=new byte[bytes.Length+4];header.CopyTo(frame,0);bytes.CopyTo(frame,4);stream.Write(frame);}
    else{stream.Write(header);stream.Write(bytes);}
}
