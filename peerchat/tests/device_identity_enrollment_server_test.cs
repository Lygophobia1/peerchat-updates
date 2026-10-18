using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
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

internal static class DeviceIdentityEnrollmentServerTest {
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    static string Hash(byte[] bytes){return MailboxProtocol.Digest(bytes);}
    static string Hash(string value){return Hash(Encoding.UTF8.GetBytes(value));}
    static string Id(PortableTestKey key){return Hash(key.Export(CngKeyBlobFormat.EccPublicBlob));}
    static Dictionary<string,object> Request(string type,params string[] fields){var result=new Dictionary<string,object>{{"version",1},{"type",type}};for(int i=0;i<fields.Length;i+=2)result[fields[i]]=fields[i+1];return result;}
    static Dictionary<string,object> Call(DiscoveryServer server,Dictionary<string,object> request){return (Dictionary<string,object>)typeof(DiscoveryServer).GetMethod("Handle",BindingFlags.NonPublic|BindingFlags.Instance).Invoke(server,new object[]{request,"127.0.0.1",0});}
    static string Value(Dictionary<string,object> response,string field){Check((bool)response["ok"],"RPC "+field+" denied: "+(response.ContainsKey("error")?response["error"]:"unknown"));return (string)response[field];}
    static void Denied(Dictionary<string,object> response,string message){Check(!(bool)response["ok"],message);}
    static void Field(Stream output,byte[] bytes){uint n=(uint)bytes.Length;for(int i=3;i>=0;i--)output.WriteByte((byte)(n>>(i*8)));output.Write(bytes,0,bytes.Length);}
    static void Text(Stream output,string value){Field(output,Encoding.UTF8.GetBytes(value));}
    static void Number(Stream output,long value){for(int i=7;i>=0;i--)output.WriteByte((byte)((ulong)value>>(i*8)));}
    static byte[] Signed(string domain,byte[] payload,PortableTestKey signer){using(var memory=new MemoryStream()){var header=Encoding.ASCII.GetBytes(domain+"\0");memory.Write(header,0,header.Length);Field(memory,payload);Field(memory,signer.SignData(payload));return memory.ToArray();}}
    static byte[] Possession(string server,long account,PortableTestKey key){using(var memory=new MemoryStream()){var header=Encoding.ASCII.GetBytes("PeerChat/account-device-possession/v1\0");memory.Write(header,0,header.Length);Text(memory,server);Number(memory,account);Text(memory,Id(key));Field(memory,key.Export(CngKeyBlobFormat.EccPublicBlob));return Signed("PeerChat/account-device-possession-signed/v1",memory.ToArray(),key);}}
    static string Bind(DiscoveryServer server,string passwordToken,PortableTestKey key){string actor=Id(key);var presence=Call(server,Request("presence","sessionToken",passwordToken,"publicIdentity",actor));Check((bool)presence["ok"],"presence: "+(presence.ContainsKey("error")?presence["error"]:"unknown"));var challenge=Call(server,Request("remember_enroll_challenge","sessionToken",passwordToken,"publicIdentity",actor));var enroll=Request("remember_enroll","sessionToken",passwordToken,"publicIdentity",actor,"challengeId",Value(challenge,"challengeId"),"publicKey",Convert.ToBase64String(key.Export(CngKeyBlobFormat.EccPublicBlob)),"signature",Convert.ToBase64String(key.SignData(Convert.FromBase64String((string)challenge["signatureInput"]))));string credential=Value(Call(server,enroll),"credential");challenge=Call(server,Request("remember_challenge","credential",credential,"publicIdentity",actor,"purpose","resume"));var resume=Request("remember_resume","credential",credential,"publicIdentity",actor,"challengeId",Value(challenge,"challengeId"),"publicKey",Convert.ToBase64String(key.Export(CngKeyBlobFormat.EccPublicBlob)),"signature",Convert.ToBase64String(key.SignData(Convert.FromBase64String((string)challenge["signatureInput"]))));return Value(Call(server,resume),"sessionToken");}
    static byte[] SignChallenge(Dictionary<string,object> challenge,PortableTestKey key,string kind){return Signed(kind=="authority"?"PeerChat/account-device-authority-signed/v1":"PeerChat/account-device-endorsement-signed/v1",Convert.FromBase64String(Value(challenge,"payload")),key);}
    static byte[] ChangedAuthority(Dictionary<string,object> challenge,PortableTestKey key){byte[] payload=Convert.FromBase64String(Value(challenge,"payload"));string nonce=DeviceIdentityAuthority.Parse(SignChallenge(challenge,key,"authority")).Issuance;byte[] needle=Encoding.ASCII.GetBytes(nonce);int at=-1;for(int i=0;i<=payload.Length-needle.Length;i++){bool match=true;for(int j=0;j<needle.Length;j++)if(payload[i+j]!=needle[j]){match=false;break;}if(match){at=i;break;}}Check(at>=0,"nonce locate");payload[at]=(byte)(payload[at]=='0'?'1':'0');return Signed("PeerChat/account-device-authority-signed/v1",payload,key);}
    static Dictionary<string,object> Identity(DiscoveryServer server,string type,string token,string device,string field=null,string value=null){var request=Request(type,"sessionToken",token,"publicIdentity",device);if(field!=null)request[field]=value;return Call(server,request);}
    static void Write(Stream stream,Dictionary<string,object> request){byte[] body=Encoding.UTF8.GetBytes(new JavaScriptSerializer{MaxJsonLength=150000}.Serialize(request));byte[] header={(byte)(body.Length>>24),(byte)(body.Length>>16),(byte)(body.Length>>8),(byte)body.Length};stream.Write(header,0,4);stream.Write(body,0,body.Length);}
    static Dictionary<string,object> Read(Stream stream){byte[] header=new byte[4];for(int got=0;got<4;){int n=stream.Read(header,got,4-got);if(n<=0)throw new EndOfStreamException();got+=n;}int size=(header[0]<<24)|(header[1]<<16)|(header[2]<<8)|header[3];Check(size>0&&size<=150000,"RPC frame bound");byte[] body=new byte[size];for(int got=0;got<size;){int n=stream.Read(body,got,size-got);if(n<=0)throw new EndOfStreamException();got+=n;}return new JavaScriptSerializer{MaxJsonLength=150000}.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(body));}
    static Dictionary<string,object> IdentityRpc(int port,string pin,string type,string token,string device,string field=null,string value=null){
        using(var tcp=new TcpClient()){tcp.Connect(IPAddress.Loopback,port);tcp.ReceiveTimeout=5000;tcp.SendTimeout=5000;using(var tls=new SslStream(tcp.GetStream(),false,(sender,cert,chain,errors)=>cert!=null&&Hash(cert.GetRawCertData())==pin)){
            tls.AuthenticateAsClient("localhost",null,System.Security.Authentication.SslProtocols.Tls12,false);Write(tls,Request("rpc_open","sessionToken",token,"publicIdentity",device));Check((bool)Read(tls)["ok"],"pinned RPC open");
            var operation=Request(type,"sessionToken",token,"publicIdentity",device,"rpcRequestId",Guid.NewGuid().ToString("N"));if(field!=null)operation[field]=value;
            Write(tls,operation);var envelope=Read(tls);Check((string)envelope["type"]=="rpc_result"&&(string)envelope["operation"]==type,"RPC dispatch");return (Dictionary<string,object>)envelope["body"];
        }}
    }
    static Dictionary<string,object> IdentityOrdinary(int port,string pin,string type,string token,string device,string field=null,string value=null){
        using(var tcp=new TcpClient()){tcp.Connect(IPAddress.Loopback,port);tcp.ReceiveTimeout=5000;tcp.SendTimeout=5000;using(var tls=new SslStream(tcp.GetStream(),false,(sender,cert,chain,errors)=>cert!=null&&Hash(cert.GetRawCertData())==pin)){
            tls.AuthenticateAsClient("localhost",null,System.Security.Authentication.SslProtocols.Tls12,false);var operation=Request(type,"sessionToken",token,"publicIdentity",device);if(field!=null)operation[field]=value;Write(tls,operation);return Read(tls);
        }}
    }
    static void WaitReady(int port){var deadline=DateTime.UtcNow.AddSeconds(10);while(DateTime.UtcNow<deadline){try{using(var probe=new TcpClient()){probe.Connect(IPAddress.Loopback,port);return;}}catch(SocketException){Thread.Sleep(20);}}throw new Exception("TLS listener startup");}
    static long Account(Dictionary<string,object> response){return Convert.ToInt64(response["accountId"]);}
    public static int Main(string[] args){try{
        string root=args[0];Directory.CreateDirectory(root);string certPath=Path.Combine(root,"identity.pfx"),database=Path.Combine(root,"identity.sqlite");
        using(var key=RSA.Create(2048)){var certRequest=new CertificateRequest("CN=localhost",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);certRequest.CertificateExtensions.Add(new X509KeyUsageExtension(X509KeyUsageFlags.DigitalSignature|X509KeyUsageFlags.KeyEncipherment,true));var serverOids=new OidCollection();serverOids.Add(new Oid("1.3.6.1.5.5.7.3.1"));certRequest.CertificateExtensions.Add(new X509EnhancedKeyUsageExtension(serverOids,false));var names=new SubjectAlternativeNameBuilder();names.AddDnsName("localhost");certRequest.CertificateExtensions.Add(names.Build());using(var cert=certRequest.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddDays(1)))File.WriteAllBytes(certPath,cert.Export(X509ContentType.Pfx,"fixture"));}
        var portProbe=new TcpListener(IPAddress.Loopback,0);portProbe.Start();int port=((IPEndPoint)portProbe.LocalEndpoint).Port;portProbe.Stop();
        var config=new ServerConfiguration{DataDirectory=root,DatabasePath=database,CertificatePath=certPath,CertificatePassword="fixture",Port=port};config.Normalize();
        using(var rootKey=new PortableTestKey(256))using(var targetKey=new PortableTestKey(256))using(var outsiderKey=new PortableTestKey(256))using(var thirdKey=new PortableTestKey(256)){
            string actor=Id(rootKey),target=Id(targetKey),outsider=Id(outsiderKey),rootPassword,rootToken,targetToken,outsiderToken,serverHash;
            using(var cert=X509CertificateLoader.LoadPkcs12FromFile(certPath,"fixture",X509KeyStorageFlags.EphemeralKeySet)){Check(cert.HasPrivateKey,"fixture certificate has no private key");serverHash=Hash(cert.RawData);}
            var server=new DiscoveryServer(config);var thread=new Thread(()=>server.Run()){IsBackground=true};thread.Start();
            var serverCert=(X509Certificate2)typeof(DiscoveryServer).GetField("certificate",BindingFlags.NonPublic|BindingFlags.Instance).GetValue(server);Check(serverCert.HasPrivateKey&&Hash(serverCert.RawData)==serverHash,"server test certificate mismatch");
            WaitReady(port);
            var additional=new List<PortableTestKey>();try{var db=(AccountDatabase)typeof(DiscoveryServer).GetField("accounts",BindingFlags.NonPublic|BindingFlags.Instance).GetValue(server);
            rootPassword=Value(Call(server,Request("register","username","identity-owner","password","fixture-password","publicIdentity",actor)),"sessionToken");
            rootToken=Bind(server,rootPassword,rootKey);targetToken=Bind(server,rootPassword,targetKey);
            outsiderToken=Bind(server,Value(Call(server,Request("register","username","identity-outsider","password","fixture-password","publicIdentity",outsider)),"sessionToken"),outsiderKey);
            long account=db.Authenticate(Hash(rootToken)),other=db.Authenticate(Hash(outsiderToken));
            string own=account.ToString(),foreign=other.ToString();
            long initialRevision=db.DeviceAuthorityRevision(account);
            byte[] rootPossession=Possession(serverHash,account,rootKey),targetPossession=Possession(serverHash,account,targetKey);
            Denied(Identity(server,"device_identity_register",rootPassword,actor,"proof",Convert.ToBase64String(rootPossession)),"password session registered proof");
            var bad=(byte[])rootPossession.Clone();bad[bad.Length-1]^=1;Denied(Identity(server,"device_identity_register",rootToken,actor,"proof",Convert.ToBase64String(bad)),"bad possession signed");
            Check((bool)IdentityRpc(port,serverHash,"device_identity_register",rootToken,actor,"proof",Convert.ToBase64String(rootPossession))["ok"],"root possession register over TLS RPC");
            Check(db.DeviceAuthorityRevision(account)==initialRevision+1,"root proof did not advance revision");
            byte[] resignedRoot=Possession(serverHash,account,rootKey);Check((bool)IdentityRpc(port,serverHash,"device_identity_register",rootToken,actor,"proof",Convert.ToBase64String(resignedRoot))["ok"]&&db.DeviceAuthorityRevision(account)==initialRevision+1,"resigned identical possession advanced revision");
            Check((bool)IdentityRpc(port,serverHash,"device_identity_register",targetToken,target,"proof",Convert.ToBase64String(targetPossession))["ok"],"target possession register over TLS RPC");
            Check(db.DeviceAuthorityRevision(account)==initialRevision+2,"target proof did not advance revision");
            Check((bool)Identity(server,"device_identity_register",targetToken,target,"proof",Convert.ToBase64String(targetPossession))["ok"]&&db.DeviceAuthorityRevision(account)==initialRevision+2,"identical possession retry advanced revision");
            Denied(Identity(server,"device_identity_register",targetToken,target,"proof",Convert.ToBase64String(Possession(new string('f',64),account,targetKey))),"wrong server registered");
            Denied(Identity(server,"device_identity_snapshot",outsiderToken,outsider,"accountId",own),"nonfriend snapshot");
            Denied(Identity(server,"device_identity_authority_challenge",rootPassword,actor),"unbound authority challenge");
            var authorityChallenge=IdentityRpc(port,serverHash,"device_identity_authority_challenge",rootToken,actor);byte[] authorityWire=SignChallenge(authorityChallenge,rootKey,"authority");
            byte[] forged=SignChallenge(authorityChallenge,outsiderKey,"authority");Denied(Identity(server,"device_identity_authority_commit",rootToken,actor,"proof",Convert.ToBase64String(forged)),"foreign authority signer");
            var authorityCommit=IdentityRpc(port,serverHash,"device_identity_authority_commit",rootToken,actor,"proof",Convert.ToBase64String(authorityWire));Check((bool)authorityCommit["ok"]&&(bool)authorityCommit["authorityActive"],"authority commit over TLS RPC");
            Check(Convert.ToInt64(authorityCommit["revision"])==initialRevision+3,"authority proof did not advance revision");
            Denied(Identity(server,"device_identity_authority_commit",rootToken,actor,"proof",Convert.ToBase64String(ChangedAuthority(authorityChallenge,rootKey))),"changed signed authority replay");
            Denied(Identity(server,"device_identity_authority_challenge",targetToken,target),"authority replaced");
            var endorsementChallenge=Identity(server,"device_identity_endorse_challenge",rootToken,actor,"targetDevice",target);byte[] endorsementWire=SignChallenge(endorsementChallenge,rootKey,"endorse");
            Check(db.ReportManagedMetadata(account,actor,Hash(rootToken),"Windows","identity-fixture"),"revision bump");
            Denied(Identity(server,"device_identity_endorse_commit",rootToken,actor,"proof",Convert.ToBase64String(endorsementWire)),"stale revision admitted");
            endorsementChallenge=Identity(server,"device_identity_endorse_challenge",rootToken,actor,"targetDevice",target);endorsementWire=SignChallenge(endorsementChallenge,rootKey,"endorse");
            string nonce=DeviceIdentityEndorsement.Parse(endorsementWire).Issuance;db.Execute("UPDATE identity_challenges SET expires=0 WHERE id='"+nonce+"';");
            Denied(Identity(server,"device_identity_endorse_commit",rootToken,actor,"proof",Convert.ToBase64String(endorsementWire)),"expired challenge admitted");
            endorsementChallenge=Identity(server,"device_identity_endorse_challenge",rootToken,actor,"targetDevice",target);endorsementWire=SignChallenge(endorsementChallenge,rootKey,"endorse");
            Denied(Identity(server,"device_identity_endorse_commit",targetToken,target,"proof",Convert.ToBase64String(endorsementWire)),"member enrolled itself");
            server.RequestStop();Check(thread.Join(12000),"server restart before pending challenge commit");
            server=new DiscoveryServer(config);db=(AccountDatabase)typeof(DiscoveryServer).GetField("accounts",BindingFlags.NonPublic|BindingFlags.Instance).GetValue(server);thread=new Thread(()=>server.Run()){IsBackground=true};thread.Start();WaitReady(port);
            Check((bool)IdentityRpc(port,serverHash,"device_identity_endorse_commit",rootToken,actor,"proof",Convert.ToBase64String(endorsementWire))["ok"],"endorsement commit over TLS RPC");
            long endorsedRevision=db.DeviceAuthorityRevision(account);Check(endorsedRevision==initialRevision+5,"endorsement did not advance revision");
            Denied(Identity(server,"device_identity_endorse_challenge",rootToken,actor,"targetDevice",target),"target reenrolled");
            db.SendFriendRequest(account,other);db.RespondToRequest(other,account,true);
            var contact=Identity(server,"device_identity_snapshot",outsiderToken,outsider,"accountId",own);Check((bool)contact["ok"]&&((List<string>)contact["possessionProofs"]).Count==0&&((List<Dictionary<string,object>>)contact["endorsements"]).Count==1,"friend projection");
            Check(((List<string>)Identity(server,"device_identity_snapshot",rootToken,actor,"accountId",own)["possessionProofs"]).Count==2,"own possessions");
            db.SetBlocked(other,account,true);Denied(Identity(server,"device_identity_snapshot",outsiderToken,outsider,"accountId",own),"blocked friend snapshot");
            server.RequestStop();Check(thread.Join(12000),"server restart before exact retries");
            server=new DiscoveryServer(config);db=(AccountDatabase)typeof(DiscoveryServer).GetField("accounts",BindingFlags.NonPublic|BindingFlags.Instance).GetValue(server);thread=new Thread(()=>server.Run()){IsBackground=true};thread.Start();WaitReady(port);
            Check((bool)IdentityRpc(port,serverHash,"device_identity_authority_commit",rootToken,actor,"proof",Convert.ToBase64String(authorityWire))["ok"],"authority exact retry after restart");
            Check((bool)IdentityRpc(port,serverHash,"device_identity_endorse_commit",rootToken,actor,"proof",Convert.ToBase64String(endorsementWire))["ok"],"endorsement exact retry after restart");
            Check(db.DeviceAuthorityRevision(account)==endorsedRevision,"exact retry advanced revision");
            Check((bool)IdentityRpc(port,serverHash,"device_identity_register",rootToken,actor,"proof",Convert.ToBase64String(resignedRoot))["ok"]&&db.DeviceAuthorityRevision(account)==endorsedRevision,"resigned possession restart retry advanced revision");
            for(int i=0;i<30;i++){
                var key=new PortableTestKey(256);additional.Add(key);string device=Id(key),deviceToken=Bind(server,rootPassword,key);
                byte[] possession=Possession(serverHash,account,key);
                Check((bool)IdentityRpc(port,serverHash,"device_identity_register",deviceToken,device,"proof",Convert.ToBase64String(possession))["ok"],"bounded device registration "+i);
                var challenge=Identity(server,"device_identity_endorse_challenge",rootToken,actor,"targetDevice",device);
                Check((bool)IdentityRpc(port,serverHash,"device_identity_endorse_commit",rootToken,actor,"proof",Convert.ToBase64String(SignChallenge(challenge,rootKey,"endorse")))["ok"],"bounded endorsement "+i);
            }
            var fullRpc=IdentityRpc(port,serverHash,"device_identity_snapshot",rootToken,actor,"accountId",own);
            var fullOrdinary=IdentityOrdinary(port,serverHash,"device_identity_snapshot",rootToken,actor,"accountId",own);
            Check((bool)fullRpc["ok"]&&(bool)fullOrdinary["ok"]&&((System.Collections.ICollection)fullRpc["possessionProofs"]).Count==32&&((System.Collections.ICollection)fullRpc["endorsements"]).Count==31&&((System.Collections.ICollection)fullOrdinary["possessionProofs"]).Count==32,"32-entry RPC/ordinary snapshot");
            string thirdToken=Bind(server,rootPassword,thirdKey),third=Id(thirdKey);
            Denied(IdentityRpc(port,serverHash,"device_identity_register",thirdToken,third,"proof",Convert.ToBase64String(Possession(serverHash,account,thirdKey))),"33rd possession accepted");
            long revision=db.DeviceAuthorityRevision(account);Check(db.RevokeManagedDevice(account,actor,Hash(rootToken),target,revision,new string('a',64),new string('b',64))==null,"target revoke");
            var revoked=Identity(server,"device_identity_snapshot",rootToken,actor,"accountId",own);var revokedTarget=((List<Dictionary<string,object>>)revoked["endorsements"]).Single(row=>DeviceIdentityEndorsement.Parse(Convert.FromBase64String((string)row["proof"])).Target.Device==target);Check(!(bool)revokedTarget["active"],"revoked target active");
            Denied(Identity(server,"device_identity_endorse_commit",rootToken,actor,"proof",Convert.ToBase64String(endorsementWire)),"revoked target replay");
            Check(((List<string>)revoked["possessionProofs"]).Count==31,"revoked possession exposed as active");
            Check((bool)Identity(server,"device_identity_snapshot",outsiderToken,outsider,"accountId",foreign)["ok"],"own snapshot");
            revision=db.DeviceAuthorityRevision(account);Check(db.RevokeManagedDevice(account,third,Hash(thirdToken),actor,revision,new string('c',64),new string('d',64))==null,"root revoke");
            var rootRevoked=Identity(server,"device_identity_snapshot",thirdToken,third,"accountId",own);
            Check((bool)rootRevoked["ok"]&&!(bool)rootRevoked["authorityActive"]&&((List<Dictionary<string,object>>)rootRevoked["endorsements"]).All(row=>!(bool)row["active"]),"revoked authority retained active endorsement");
            Denied(Identity(server,"device_identity_authority_challenge",thirdToken,third),"root reselected after revocation");
            }finally{foreach(var key in additional)key.Dispose();server.RequestStop();Check(thread.Join(12000),"TLS fixture shutdown");}
            Console.WriteLine("PASS scoped identity admission: bound sessions, real signatures, immutable authority/target, durable exact retry, friend isolation, revocation.");
        }
        return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
