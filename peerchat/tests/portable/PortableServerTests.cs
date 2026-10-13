using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using PeerChat.Server;
using PeerChat.ServerShared;
class PortableServerTests {
    public static int Main(string[] args){try{
        if(args.Length>0&&args[0]=="--discovery")return PeerChat.ServerTests.DiscoveryIntegrationTest.Main(args[1..]);
        if(args.Length==2&&args[0]=="--fcm-live-config")return LiveFcmConfiguration(args[1]);
        if(args.Length==2&&args[0]=="--group-device")return GroupDeviceServerTest.Main(new[]{args[1]});
        if(args.Length==4&&args[0]=="--archive-relay")return ArchiveRelayLifecycleTest.Main(new[]{args[1],args[2],args[3]});
        if(args.Length==4&&args[0]=="--group-fanout")return GroupCiphertextFanoutTest.Main(new[]{args[1],args[2],args[3]});
        string root=args[0];ServerLifecycleTests.Run(root);var json=new JavaScriptSerializer();
        string fixture=File.ReadAllText(Path.Combine(root,"framework.json"));ServerJsonFixtures.Validate(fixture);
        var value=json.Deserialize<System.Collections.Generic.Dictionary<string,object>>(fixture);
        string modern=json.Serialize(value);for(int i=0;i<1000;i++)ServerJsonFixtures.Check(json.Serialize(value)==modern,"nondeterministic JSON");
        File.WriteAllText(Path.Combine(root,"modern.json"),modern,new UTF8Encoding(false));
        byte[] identity=File.ReadAllBytes(Path.Combine(root,"identity.bin")),input=File.ReadAllBytes(Path.Combine(root,"signature-input.bin")),signature=File.ReadAllBytes(Path.Combine(root,"signature.bin"));
        string peer=MailboxProtocol.Digest(identity);
        ServerJsonFixtures.Check(MailboxProtocol.Verify(input,signature,identity,peer),"CNG -> portable P1363");
        using(var signer=new PortableTestKey(256)){
            File.WriteAllBytes(Path.Combine(root,"portable-identity.bin"),signer.Key.Export(CngKeyBlobFormat.EccPublicBlob));
            File.WriteAllBytes(Path.Combine(root,"portable-signature.bin"),signer.SignData(input));
        }
        signature[0]^=1;ServerJsonFixtures.Check(!MailboxProtocol.Verify(input,signature,identity,peer),"tamper");
        var proof=GroupRosterProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,"group-proof.bin"))));
        ServerJsonFixtures.Check(proof.OwnerIdentity==peer,"group proof/Peer ID changed");
        bool rejected=false;try{PortablePlatform.ValidateAgreementPublic(new byte[72]);}catch(CryptographicException){rejected=true;}
        ServerJsonFixtures.Check(rejected,"malformed public key accepted");
        if(OperatingSystem.IsWindows()){
            var secret=Encoding.UTF8.GetBytes("test-only DPAPI compatibility");
            ServerJsonFixtures.Check(Encoding.UTF8.GetString(PortablePlatform.Dpapi(PortablePlatform.Dpapi(secret,true),false))=="test-only DPAPI compatibility","DPAPI");
        }
            using(var rsa=RSA.Create(2048)){var bytes=Encoding.ASCII.GetBytes("test-only JWT");ServerJsonFixtures.Check(rsa.VerifyData(bytes,PortablePlatform.RsaSign(rsa.ExportPkcs8PrivateKey(),bytes),HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1),"portable FCM RSA");}
            ServerJsonFixtures.Check(GroupRosterProofTest.Main()==0,"group signature regressions");
            ServerJsonFixtures.Check(GroupRosterDatabaseTest.Main(new[]{Path.Combine(root,"portable-roster.db")})==0,"group roster DB");
            ServerJsonFixtures.Check(FcmProviderTest.Main()==0,"FCM");
            string pfx=Path.Combine(root,"lifecycle-test.pfx");
            ServerJsonFixtures.Check(GroupCiphertextFanoutTest.Main(new[]{Path.Combine(root,"fanout"),pfx,"test-only"})==0,"modern three-client fanout");
            ServerJsonFixtures.Check(GroupMediaRoomsTest.Main(new[]{Path.Combine(root,"rooms"),pfx,"test-only"})==0,"modern room authorization");
        var config=new ServerConfiguration();config.SetTurnRestSharedSecret(OperatingSystem.IsWindows()?"test-only-storage-secret":"");
        ServerJsonFixtures.Check(!json.Serialize(config).Contains("\"TurnRestSharedSecret\":"),"secret serialized");
        ServerJsonFixtures.Check(ServerPresenceTest.Main(new[]{Path.Combine(root,"presence")})==0,"presence");
        if(args.Length>1)ServerJsonFixtures.Check(EncryptedMailboxServerTest.Main(new[]{args[1],Path.Combine(root,"lifecycle-test.pfx"),"test-only"})==0,"mailbox");
        Console.WriteLine("PASS: shared server JSON, identity/signatures, configuration, presence, groups and durable mailbox compatibility on "+System.Runtime.InteropServices.RuntimeInformation.RuntimeIdentifier);return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
    // Explicit operator-invoked validation only. Never included in server/client
    // packages. No device push is sent, and no credential/token is printed.
    static int LiveFcmConfiguration(string path){
        try{
            var config=ServerConfiguration.Load(path);
            if(!config.FcmEnabled)throw new InvalidOperationException();
            var provider=FcmPushProvider.Create(config);
            var method=typeof(FcmPushProvider).GetMethod("AccessToken",
                System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance);
            var token=method.Invoke(provider,null) as string;
            if(String.IsNullOrEmpty(token))throw new InvalidOperationException();
            if(!String.Equals(token,method.Invoke(provider,null) as string,StringComparison.Ordinal))throw new InvalidOperationException();
            Console.WriteLine("PASS: production Linux FCM credential parsing, platform RSA/JWT, real Google OAuth access-token acquisition and cache reuse; no push/secret output.");
            return 0;
        }catch{
            Console.Error.WriteLine("FAIL: live FCM configuration/OAuth validation; credential and response contents withheld.");
            return 1;
        }
    }
}
