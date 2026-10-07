using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using PeerChat.Server;
using PeerChat.ServerShared;
class PortableServerTests {
    public static int Main(string[] args){try{
        string root=args[0];var json=new JavaScriptSerializer();
        string fixture=File.ReadAllText(Path.Combine(root,"framework.json"));ServerJsonFixtures.Validate(fixture);
        var value=json.Deserialize<System.Collections.Generic.Dictionary<string,object>>(fixture);
        string modern=json.Serialize(value);for(int i=0;i<1000;i++)ServerJsonFixtures.Check(json.Serialize(value)==modern,"nondeterministic JSON");
        File.WriteAllText(Path.Combine(root,"modern.json"),modern,new UTF8Encoding(false));
        byte[] identity=File.ReadAllBytes(Path.Combine(root,"identity.bin")),input=File.ReadAllBytes(Path.Combine(root,"signature-input.bin")),signature=File.ReadAllBytes(Path.Combine(root,"signature.bin"));
        string peer=MailboxProtocol.Digest(identity);
        ServerJsonFixtures.Check(MailboxProtocol.Verify(input,signature,identity,peer),"CNG -> portable P1363");
        signature[0]^=1;ServerJsonFixtures.Check(!MailboxProtocol.Verify(input,signature,identity,peer),"tamper");
        var proof=GroupRosterProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,"group-proof.bin"))));
        ServerJsonFixtures.Check(proof.OwnerIdentity==peer,"group proof/Peer ID changed");
        bool rejected=false;try{PortablePlatform.ValidateAgreementPublic(new byte[72]);}catch(CryptographicException){rejected=true;}
        ServerJsonFixtures.Check(rejected,"malformed public key accepted");
        if(OperatingSystem.IsWindows()){
            var secret=Encoding.UTF8.GetBytes("test-only DPAPI compatibility");
            ServerJsonFixtures.Check(Encoding.UTF8.GetString(PortablePlatform.Dpapi(PortablePlatform.Dpapi(secret,true),false))=="test-only DPAPI compatibility","DPAPI");
            using(var rsa=RSA.Create(2048)){var bytes=Encoding.ASCII.GetBytes("test-only JWT");ServerJsonFixtures.Check(rsa.VerifyData(bytes,PortablePlatform.RsaSign(rsa.ExportPkcs8PrivateKey(),bytes),HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1),"portable FCM RSA");}
            ServerJsonFixtures.Check(GroupRosterProofTest.Main()==0,"group signature regressions");
            ServerJsonFixtures.Check(GroupRosterDatabaseTest.Main(new[]{Path.Combine(root,"portable-roster.db")})==0,"group roster DB");
            ServerJsonFixtures.Check(FcmProviderTest.Main()==0,"FCM");
        }
        var config=new ServerConfiguration();config.SetTurnRestSharedSecret(OperatingSystem.IsWindows()?"test-only-storage-secret":"");
        ServerJsonFixtures.Check(!json.Serialize(config).Contains("\"TurnRestSharedSecret\":"),"secret serialized");
        ServerJsonFixtures.Check(ServerPresenceTest.Main(new[]{Path.Combine(root,"presence")})==0,"presence");
        if(args.Length>1)ServerJsonFixtures.Check(EncryptedMailboxServerTest.Main(new[]{args[1]})==0,"mailbox");
        Console.WriteLine("PASS: modern shared server JSON, identity/signatures, protected configuration, presence and durable storage compatibility. Host result, not Linux runtime acceptance.");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
