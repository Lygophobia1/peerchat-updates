using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
public sealed class ProfileAccountLink {
    public int Version=1;
    public long AccountId;
    public string Username="",Host="",CertificateSha256="",SessionToken="";
    public int Port;
    public bool HasSession {get{return !String.IsNullOrEmpty(SessionToken);}}
}

// One DPAPI-protected account binding belongs to one stable local profile directory.
// Clearing a session never clears the binding, so a signed-out profile cannot silently
// authenticate as a different social account.
public sealed class ProfileAccountStore {
    readonly string path;
    readonly JavaScriptSerializer json=new JavaScriptSerializer();
    readonly Func<byte[],byte[]> protect,unprotect;
    public bool Exists {get{return File.Exists(path);}}
    public ProfileAccountStore(string profileDirectory):this(profileDirectory,data=>ProtectedData.Protect(data,null,DataProtectionScope.CurrentUser),data=>ProtectedData.Unprotect(data,null,DataProtectionScope.CurrentUser)){}
    internal ProfileAccountStore(string profileDirectory,Func<byte[],byte[]> protectData,Func<byte[],byte[]> unprotectData){path=Path.Combine(profileDirectory,"account-link.dat");protect=protectData;unprotect=unprotectData;}
    public ProfileAccountLink Load(){
        if(!File.Exists(path))return null;
        try{var value=json.Deserialize<ProfileAccountLink>(Encoding.UTF8.GetString(unprotect(File.ReadAllBytes(path))));return Valid(value)?value:null;}
        catch(CryptographicException){return null;}catch(InvalidOperationException){return null;}catch(ArgumentException){return null;}catch(IOException){return null;}
    }
    public ProfileAccountLink SaveSession(long accountId,string username,string host,int port,string pin,string token,bool allowRelink=false){
        if(accountId<=0||String.IsNullOrWhiteSpace(username)||String.IsNullOrWhiteSpace(host)||port<1||port>65535||String.IsNullOrEmpty(token))throw new InvalidOperationException("The server account session is incomplete.");
        var current=Load();if(current==null&&Exists&&!allowRelink)throw new InvalidOperationException("This profile's protected account link is unavailable and cannot be replaced implicitly.");if(current!=null&&current.AccountId!=accountId&&!allowRelink)throw new InvalidOperationException("This local profile is already linked to @"+current.Username+". Switch profiles to use another account.");
        var value=new ProfileAccountLink{AccountId=accountId,Username=username.Trim(),Host=host.Trim(),Port=port,CertificateSha256=pin??"",SessionToken=token};Save(value);return value;
    }
    public ProfileAccountLink ClearSession(){var current=Load();if(current==null)return null;current.SessionToken="";Save(current);return current;}
    public void UpdateUsername(string username){var current=Load();if(current==null||String.IsNullOrWhiteSpace(username))return;current.Username=username.Trim();Save(current);}
    static bool Valid(ProfileAccountLink value){return value!=null&&value.Version==1&&value.AccountId>0&&!String.IsNullOrWhiteSpace(value.Username)&&!String.IsNullOrWhiteSpace(value.Host)&&value.Port>0&&value.Port<=65535;}
    void Save(ProfileAccountLink value){Directory.CreateDirectory(Path.GetDirectoryName(path));string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";File.WriteAllBytes(temporary,protect(Encoding.UTF8.GetBytes(json.Serialize(value))));if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);}
}
}
