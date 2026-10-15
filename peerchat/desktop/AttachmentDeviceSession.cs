using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
// This credential authorizes only the separate attachment-cache RPC client.
// It never replaces the application's account-password messaging session.
public static class AttachmentDeviceSession {
    sealed class Binding {
        public readonly DiscoveryClient Main;public readonly string Host,Pin,Token,Device,Directory;public readonly int Port;public readonly long Account;readonly Func<bool> stillCurrent;
        public Binding(DiscoveryClient main,long account,string device,string directory,Func<bool> stillCurrent=null){
            if(main==null||account<=0||main.AccountId!=account||!Id(device,64)||String.IsNullOrEmpty(main.SessionToken)||!Pin(main.CertificateSha256)||String.IsNullOrWhiteSpace(main.Host)||main.Port<1||main.Port>65535||String.IsNullOrWhiteSpace(directory))throw new InvalidOperationException("Attachment device verification requires a pinned current account and local profile.");
            Main=main;Account=account;Device=device;Host=main.Host;Port=main.Port;Pin=main.CertificateSha256;Token=main.SessionToken;Directory=Path.GetFullPath(directory);this.stillCurrent=stillCurrent;
            CheckCurrent();
        }
        public void CheckCurrent(){if(stillCurrent!=null&&!stillCurrent()||Main.AccountId!=Account||Main.SessionToken!=Token||Main.Host!=Host||Main.Port!=Port||Main.CertificateSha256!=Pin)throw new InvalidOperationException("Attachment authenticated account, profile, or server context changed.");}
    }
    sealed class Credential {
        public int Version;public string Host,Pin,Device,Secret;public int Port;public long Account;
    }
    sealed class Store {
        readonly string path;readonly byte[] entropy;readonly JavaScriptSerializer json=new JavaScriptSerializer{MaxJsonLength=4096};
        public bool Exists{get{return File.Exists(path);}}
        public Store(Binding binding){string root=binding.Directory;string scope=binding.Host.ToLowerInvariant()+"|"+binding.Port.ToString(CultureInfo.InvariantCulture)+"|"+binding.Pin.ToLowerInvariant()+"|"+binding.Account.ToString(CultureInfo.InvariantCulture)+"|"+binding.Device;using(var hash=SHA256.Create()){string name=BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(scope))).Replace("-","").ToLowerInvariant();path=Path.Combine(root,"attachment-device-session-"+name+".dat");entropy=hash.ComputeHash(Encoding.UTF8.GetBytes("PeerChat attachment device session v1|"+root.TrimEnd(Path.DirectorySeparatorChar).ToUpperInvariant()+"|"+scope));}}
        public Credential Load(){
            if(!Exists)return null;
            try{var encrypted=File.ReadAllBytes(path);if(encrypted.Length<1||encrypted.Length>8192)throw new InvalidDataException();var plain=ProtectedData.Unprotect(encrypted,entropy,DataProtectionScope.CurrentUser);try{if(plain.Length<1||plain.Length>4096)throw new InvalidDataException();var value=json.Deserialize<Credential>(new UTF8Encoding(false,true).GetString(plain));if(!Valid(value))throw new InvalidDataException();return value;}finally{Array.Clear(plain,0,plain.Length);}}
            catch(Exception error)when(error is CryptographicException||error is IOException||error is InvalidDataException||error is ArgumentException||error is InvalidOperationException){throw new InvalidOperationException("Saved attachment device authorization is unavailable; it will not be replaced automatically.");}
        }
        public void Save(Credential value){
            if(!Valid(value))throw new InvalidOperationException("Attachment device authorization is incomplete.");
            Directory.CreateDirectory(Path.GetDirectoryName(path));string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
            byte[] plain=Encoding.UTF8.GetBytes(json.Serialize(value));try{var encrypted=ProtectedData.Protect(plain,entropy,DataProtectionScope.CurrentUser);File.WriteAllBytes(temporary,encrypted);if(File.Exists(path))throw new InvalidOperationException("Saved attachment device authorization changed during enrollment.");File.Move(temporary,path);}finally{Array.Clear(plain,0,plain.Length);if(File.Exists(temporary))File.Delete(temporary);}
        }
    }
    static bool Id(string value,int length){return value!=null&&value.Length==length&&value.All(c=>(c>='0'&&c<='9')||(c>='a'&&c<='f'));}
    static bool Pin(string value){return value!=null&&value.Length==64&&value.All(Uri.IsHexDigit);}
    static bool Valid(Credential value){return value!=null&&value.Version==1&&value.Account>0&&!String.IsNullOrWhiteSpace(value.Host)&&value.Port>0&&value.Port<=65535&&Pin(value.Pin)&&Id(value.Device,64)&&CredentialFormat(value.Secret);}
    static bool CredentialFormat(string value){return value!=null&&Regex.IsMatch(value,"^[A-Za-z0-9_-]{43}$",RegexOptions.CultureInvariant);}
    static string Hash(string value){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(value))).Replace("-","").ToLowerInvariant();}
    static void Require(bool valid){if(!valid)throw new InvalidOperationException("Attachment device proof was rejected.");}
    static bool Fresh(long issued){long age=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-issued;return age>=0&&age<=60000;}
    public static bool HasCredential(DiscoveryClient main,long account,string device,string profileDirectory){var binding=new Binding(main,account,device,profileDirectory);binding.CheckCurrent();return new Store(binding).Exists;}
    public static string SafeFailure(Exception error){
        var rejected=error as DiscoveryRequestException;
        if(rejected!=null){
            switch(rejected.ErrorCode){
                case "remember_revoked":case "remember_invalid":return "Saved attachment device authorization was revoked. It will not be replaced automatically.";
                case "remember_proof_invalid":case "remember_device_mismatch":return "Attachment device proof was rejected by the server.";
                case "remember_rate_limited":return "Attachment device verification is temporarily rate limited. Retry later.";
                case "session_invalid":case "device_session_mismatch":return "Attachment account session changed. Sign in again before retrying.";
                default:return "Attachment device verification was rejected by the server.";
            }
        }
        if(error is IOException||error is System.Net.Sockets.SocketException||error is System.Security.Authentication.AuthenticationException)return "Attachment device verification could not reach the pinned server. Retry when connected.";
        return "Attachment device verification failed. Any saved authorization was kept; retry from this profile.";
    }
    public static DiscoveryClient Open(DiscoveryClient main,CoreAdapter core,long account,string device,string profileDirectory,bool allowFirstEnrollment){
        return Open(main,core,account,device,profileDirectory,allowFirstEnrollment,null);
    }
    public static DiscoveryClient Open(DiscoveryClient main,CoreAdapter core,long account,string device,string profileDirectory,bool allowFirstEnrollment,Func<bool> stillCurrent){
        if(core==null)throw new ArgumentNullException("core");
        return Open(main,input=>core.Mailbox(9,input),account,device,profileDirectory,allowFirstEnrollment,stillCurrent);
    }
    internal static DiscoveryClient Open(DiscoveryClient main,Func<byte[],byte[]> nativeProof,long account,string device,string profileDirectory,bool allowFirstEnrollment){
        return Open(main,nativeProof,account,device,profileDirectory,allowFirstEnrollment,null);
    }
    internal static DiscoveryClient Open(DiscoveryClient main,Func<byte[],byte[]> nativeProof,long account,string device,string profileDirectory,bool allowFirstEnrollment,Func<bool> stillCurrent){
        if(nativeProof==null)throw new ArgumentNullException("nativeProof");
        var binding=new Binding(main,account,device,profileDirectory,stillCurrent);var store=new Store(binding);var saved=store.Load();binding.CheckCurrent();
        if(saved==null){
            if(!allowFirstEnrollment)throw new InvalidOperationException("Attachment device verification needs your approval before first enrollment.");
            long issued=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();binding.CheckCurrent();var challenge=main.RememberRequest("remember_enroll_challenge",new Dictionary<string,object>{{"sessionToken",binding.Token},{"publicIdentity",device}});binding.CheckCurrent();
            var proof=Proof(nativeProof,challenge,"enroll",binding.Token,binding.Account,binding.Device,issued,binding);
            var request=new Dictionary<string,object>(proof){{"sessionToken",binding.Token}};binding.CheckCurrent();Require(Fresh(issued));
            var enrolled=main.RememberRequest("remember_enroll",request);binding.CheckCurrent();
            string credential=DiscoveryClient.RememberText(enrolled,"credential");
            Require(DiscoveryClient.RememberNumber(enrolled,"accountId")==account&&DiscoveryClient.RememberText(enrolled,"publicIdentity")==device&&CredentialFormat(credential));
            saved=new Credential{Version=1,Host=binding.Host,Port=binding.Port,Pin=binding.Pin.ToLowerInvariant(),Account=account,Device=device,Secret=credential};binding.CheckCurrent();store.Save(saved);
        }
        if(!String.Equals(saved.Host,binding.Host,StringComparison.OrdinalIgnoreCase)||saved.Port!=binding.Port||!String.Equals(saved.Pin,binding.Pin,StringComparison.OrdinalIgnoreCase)||saved.Account!=account||saved.Device!=device)throw new InvalidOperationException("Saved attachment device authorization belongs to a different server, account, or device.");
        binding.CheckCurrent();var cache=new DiscoveryClient(binding.Host,binding.Port,binding.Pin);
        try{
            long issued=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();binding.CheckCurrent();var challenge=cache.RememberRequest("remember_challenge",new Dictionary<string,object>{{"publicIdentity",device},{"credential",saved.Secret},{"purpose","resume"}});binding.CheckCurrent();
            var proof=Proof(nativeProof,challenge,"resume",saved.Secret,account,device,issued,binding);var request=new Dictionary<string,object>(proof){{"credential",saved.Secret}};binding.CheckCurrent();Require(Fresh(issued));
            var response=cache.RememberRequest("remember_resume",request);binding.CheckCurrent();
            cache.AcceptRememberedDeviceSession(response,account,device);binding.CheckCurrent();return cache;
        }catch{cache.CloseRpc();throw;}
    }
    static Dictionary<string,object> Proof(Func<byte[],byte[]> nativeProof,Dictionary<string,object> challenge,string purpose,string binding,long account,string device,long issued,Binding current){
        string id=DiscoveryClient.RememberText(challenge,"challengeId"),claimedDevice=DiscoveryClient.RememberText(challenge,"publicIdentity"),claimedPurpose=DiscoveryClient.RememberText(challenge,"purpose"),encoded=DiscoveryClient.RememberText(challenge,"signatureInput");
        Require(Id(id,64)&&claimedDevice==device&&claimedPurpose==purpose&&DiscoveryClient.RememberNumber(challenge,"accountId")==account&&encoded!=null&&encoded.Length<=512);
        string digest=Hash(binding);byte[] expected=Encoding.ASCII.GetBytes("PeerChat remembered device v1|"+purpose+"|"+account.ToString(CultureInfo.InvariantCulture)+"|"+device+"|"+id+"|"+digest);
        byte[] supplied;try{supplied=Convert.FromBase64String(encoded);}catch(FormatException){throw new InvalidOperationException("Attachment device proof was rejected.");}
        Require(supplied.SequenceEqual(expected)&&Fresh(issued));
        // Native signs the exact challenge once, after independent account and device checks.
        current.CheckCurrent();byte[] publicKey,signature;using(var result=new MailboxExecution.Record(nativeProof(NativeProofInput(issued,purpose,account,device,id,digest,expected)),4)){publicKey=result.Bytes();signature=result.Bytes();result.End();}current.CheckCurrent();
        Require(publicKey.Length==72&&signature.Length==64&&HashBytes(publicKey)==device&&Fresh(issued));
        return new Dictionary<string,object>{{"publicIdentity",device},{"challengeId",id},{"publicKey",Convert.ToBase64String(publicKey)},{"signature",Convert.ToBase64String(signature)}};
    }
    static string HashBytes(byte[] value){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(value)).Replace("-","").ToLowerInvariant();}
    static void WriteBytes(BinaryWriter writer,byte[] value){writer.Write((ulong)value.Length);writer.Write(value);}
    static byte[] NativeProofInput(long issued,string purpose,long account,string device,string id,string digest,byte[] expected){
        using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream,Encoding.UTF8)){
            writer.Write(new byte[]{80,84,1,30});WriteBytes(writer,Encoding.UTF8.GetBytes("PeerChat/RememberedDeviceProof/v1"));writer.Write(checked((ulong)issued));WriteBytes(writer,Encoding.UTF8.GetBytes(purpose));writer.Write(checked((ulong)account));WriteBytes(writer,Encoding.UTF8.GetBytes(device));WriteBytes(writer,Encoding.UTF8.GetBytes(id));WriteBytes(writer,Encoding.UTF8.GetBytes(digest));WriteBytes(writer,expected);Require(stream.Length<=1024);return stream.ToArray();
        }
    }
}
public sealed partial class DiscoveryClient {
    string boundCacheToken,boundCacheDevice,boundCachePin,boundCacheHost;int boundCachePort;long boundCacheAccount;
    internal Dictionary<string,object> RememberRequest(string operation,Dictionary<string,object> fields){if(!new[]{"remember_enroll_challenge","remember_enroll","remember_challenge","remember_resume"}.Contains(operation))throw new ArgumentException("Unsupported remembered device request.");if(CertificateSha256==null||CertificateSha256.Length!=64||!CertificateSha256.All(Uri.IsHexDigit))throw new InvalidOperationException("Pinned server identity required.");return Request(operation,fields);}
    internal static string RememberText(Dictionary<string,object> data,string key){object value;return data!=null&&data.TryGetValue(key,out value)?value as string:null;}
    internal static long RememberNumber(Dictionary<string,object> data,string key){object value;if(data==null||!data.TryGetValue(key,out value))return 0;try{return Convert.ToInt64(value,CultureInfo.InvariantCulture);}catch{return 0;}}
    internal void AcceptRememberedDeviceSession(Dictionary<string,object> response,long account,string device){
        if(RememberNumber(response,"accountId")!=account||RememberNumber(response,"rpcVersion")!=1||!Regex.IsMatch(RememberText(response,"sessionToken")??"","^[A-Za-z0-9_-]{43}$")||!Boolean.Equals(response.ContainsKey("relayOnlyPresence")?response["relayOnlyPresence"]:null,true))throw new InvalidOperationException("Attachment device session response was invalid.");
        SaveSession(RememberText(response,"username"),response);RegisteredPublicIdentity=device;rpcDevice=device;
        boundCacheAccount=account;boundCacheDevice=device;boundCacheHost=Host;boundCachePort=Port;boundCachePin=CertificateSha256;boundCacheToken=SessionToken;
    }
    public bool MatchesAttachmentDeviceSession(long account,string device){return boundCacheAccount==account&&boundCacheAccount>0&&boundCacheDevice==device&&boundCacheToken==SessionToken&&!String.IsNullOrEmpty(boundCacheToken)&&boundCacheHost==Host&&boundCachePort==Port&&boundCachePin==CertificateSha256&&AccountId==account&&RegisteredPublicIdentity==device;}
}
}
