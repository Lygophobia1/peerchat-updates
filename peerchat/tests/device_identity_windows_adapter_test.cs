using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using PeerChatDesktop;

namespace PeerChatDesktop {
public sealed class DiscoveryRequestException : Exception {public string ErrorCode;public DiscoveryRequestException(string code){ErrorCode=code;}}
public sealed partial class DiscoveryClient {
    internal bool supportsRpc=true;
    public long AccountId=7;
    public string SessionToken="session",CertificateSha256=new string('a',64);
    internal Func<string,Dictionary<string,object>,Dictionary<string,object>> Answer;
    public bool MatchesAttachmentDeviceSession(long account,string device){return account==7&&device==new string('b',64);}
    Dictionary<string,object> Rpc(string type,string device,Dictionary<string,object> fields,string token=null,string pin=null,long account=0){return Answer(type,fields);}
}
}

internal static class DeviceIdentityWindowsAdapterTest {
    static void Check(bool condition,string message){if(!condition)throw new Exception(message);}
    static long U64(byte[] wire,ref int at){Check(wire.Length-at>=8,"truncated command");ulong value=0;for(int n=0;n<8;n++)value|=(ulong)wire[at++]<<(8*n);return checked((long)value);}
    static byte[] Bytes(byte[] wire,ref int at){long length=U64(wire,ref at);Check(length>=0&&length<=wire.Length-at,"bad command field");var result=new byte[length];Buffer.BlockCopy(wire,at,result,0,(int)length);at+=(int)length;return result;}
    static string Text(byte[] wire,ref int at){return Encoding.UTF8.GetString(Bytes(wire,ref at));}
    static void Big32(Stream stream,int value){for(int shift=24;shift>=0;shift-=8)stream.WriteByte((byte)(value>>shift));}
    static void Big64(Stream stream,long value){for(int shift=56;shift>=0;shift-=8)stream.WriteByte((byte)(value>>shift));}
    static void Field(Stream stream,byte[] value){Big32(stream,value.Length);stream.Write(value,0,value.Length);}
    static byte[] Evidence(string domain,Action<Stream> fields){using(var stream=new MemoryStream()){var prefix=Encoding.ASCII.GetBytes(domain+"\0");stream.Write(prefix,0,prefix.Length);fields(stream);return stream.ToArray();}}
    static Dictionary<string,object> Snapshot(long account,string pin,string authority="",object[] possessions=null){return new Dictionary<string,object>{{"deviceIdentityVersion",1},{"serverFingerprint",pin},{"accountId",account},{"revision",1},{"authorityProof",authority},{"authorityActive",authority.Length>0},{"endorsements",new object[0]},{"possessionProofs",possessions??new object[0]}};}
    static void Reject(Action action,string title){try{action();}catch(InvalidDataException){return;}throw new Exception("accepted "+title);}
    public static int Main(){
        var pin=new string('a',64);var actor=new string('b',64);var target=new string('c',64);
        var connection=new DiscoveryClient();string sent=null;
        connection.Answer=(operation,fields)=>{sent=operation;Check(operation=="device_identity_register","wrong register RPC");Check((string)fields["proof"]==Convert.ToBase64String(new byte[]{8,9}),"wrong possession proof");return new Dictionary<string,object>{{"ok",true}};};
        int nativeCalls=0;
        var client=new DeviceIdentityDirectoryClient(connection,7,actor,19,input=>{
            nativeCalls++;Check(input.Take(4).SequenceEqual(new byte[]{80,84,1,30}),"wrong PT header");int at=4;
            Check(Text(input,ref at)=="PeerChat/IdentityDirectoryCommand/v1","wrong command domain");Check(U64(input,ref at)==0,"wrong native operation");Check(U64(input,ref at)==7,"wrong account");Check(U64(input,ref at)==19,"wrong native friend generation");Check(Text(input,ref at)==pin,"wrong server pin");Check(U64(input,ref at)>0&&at==input.Length,"wrong command tail");return new byte[]{8,9};
        },()=>true);
        Check(client.RegisterSelf().SequenceEqual(new byte[]{8,9})&&sent=="device_identity_register"&&nativeCalls==1,"self registration failed");
        connection.CertificateSha256=pin.ToUpperInvariant();
        var upperPinClient=new DeviceIdentityDirectoryClient(connection,7,actor,19,input=>{
            int at=4;Check(Text(input,ref at)=="PeerChat/IdentityDirectoryCommand/v1","upper-pin command domain");
            Check(U64(input,ref at)==0&&U64(input,ref at)==7&&U64(input,ref at)==19&&Text(input,ref at)==pin,"TLS pin was not canonicalized for signed evidence");return new byte[]{8,9};
        },()=>true);
        Check(upperPinClient.RegisterSelf().Length==2,"uppercase transport pin blocked canonical proof");
        connection.CertificateSha256=pin;
        Check(DeviceIdentitySnapshot.Parse(Snapshot(7,pin),7,pin,true).AuthorityProof.Length==0,"empty authority not accepted");
        Reject(()=>DeviceIdentitySnapshot.Parse(Snapshot(8,pin,"",new object[]{Convert.ToBase64String(new byte[]{1})}),8,pin,false),"contact possession disclosure");
        Reject(()=>DeviceIdentitySnapshot.Parse(Snapshot(8,pin),9,pin,false),"cross-account snapshot");
        Reject(()=>DeviceIdentitySnapshot.Parse(Snapshot(8,pin),8,new string('f',64),false),"cross-server snapshot");
        byte[] possessionPayload=Evidence("PeerChat/account-device-possession/v1",output=>{Field(output,Encoding.ASCII.GetBytes(pin));Big64(output,8);Field(output,Encoding.ASCII.GetBytes(target));Field(output,new byte[72]);});
        byte[] possession=Evidence("PeerChat/account-device-possession-signed/v1",output=>{Field(output,possessionPayload);Field(output,new byte[64]);});
        byte[] authorityPayload=Evidence("PeerChat/account-device-authority/v1",output=>{Field(output,possession);Big64(output,1);Big64(output,1);Field(output,Encoding.ASCII.GetBytes(new string('d',64)));Field(output,Encoding.ASCII.GetBytes(new string('e',64)));Big64(output,1);Big64(output,2);});
        byte[] authorityProof=Evidence("PeerChat/account-device-authority-signed/v1",output=>{Field(output,authorityPayload);Field(output,new byte[64]);});
        var snapshot=DeviceIdentitySnapshot.Parse(Snapshot(8,pin,Convert.ToBase64String(authorityProof)),8,pin,false);
        Check(snapshot.AuthorityFingerprint()==target,"authority fingerprint extraction failed");
        snapshot.Server=new string('f',64);Reject(()=>{snapshot.AuthorityFingerprint();},"authority server substitution");
        long before=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        connection.Answer=(operation,fields)=>{Check(operation=="device_identity_snapshot","wrong snapshot RPC");return new Dictionary<string,object>(Snapshot(8,pin,Convert.ToBase64String(authorityProof))){{"ok",true}};};
        var applyClient=new DeviceIdentityDirectoryClient(connection,7,actor,19,input=>{
            int at=4;Check(Text(input,ref at)=="PeerChat/IdentityDirectoryCommand/v1","wrong apply command");
            Check(U64(input,ref at)==3&&U64(input,ref at)==7&&U64(input,ref at)==19,"wrong apply context");Check(Text(input,ref at)==pin,"wrong apply server");
            long started=U64(input,ref at);Check(started>=before&&started<=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),"snapshot start not forwarded");
            Check(U64(input,ref at)==8&&U64(input,ref at)==1,"wrong remote account or revision");
            Check(Bytes(input,ref at).SequenceEqual(authorityProof),"wrong authority proof");
            Check(U64(input,ref at)==1&&U64(input,ref at)==0&&Text(input,ref at)==target&&at==input.Length,"wrong apply tail");
            return new byte[]{1};
        },()=>true);
        var fresh=applyClient.Snapshot(8);Check(applyClient.Apply(fresh,target),"valid apply failed");
        var revoked=DeviceIdentitySnapshot.Parse(Snapshot(8,pin,Convert.ToBase64String(authorityProof)),8,pin,false);revoked.AuthorityActive=false;revoked.RequestedUtcMilliseconds=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
        var revocationClient=new DeviceIdentityDirectoryClient(connection,7,actor,19,input=>{
            int at=4;Text(input,ref at);Check(U64(input,ref at)==3,"revocation did not reach native apply");U64(input,ref at);U64(input,ref at);Text(input,ref at);U64(input,ref at);U64(input,ref at);U64(input,ref at);Bytes(input,ref at);
            Check(U64(input,ref at)==0&&U64(input,ref at)==0&&Text(input,ref at)==""&&at==input.Length,"inactive authority was not forwarded as a revocation");return new byte[]{1};
        },()=>true);
        Check(revocationClient.Apply(revoked),"revocation apply failed");
        var nativeRejected=new DeviceIdentityDirectoryClient(connection,7,actor,19,input=>new byte[0],()=>true);
        Reject(()=>nativeRejected.Apply(fresh,target),"native rejection presented as success");
        fresh.RequestedUtcMilliseconds=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-31000;
        Reject(()=>applyClient.Apply(fresh,target),"delayed snapshot lease");
        Console.WriteLine("Windows identity adapter: PASS");return 0;
    }
}
