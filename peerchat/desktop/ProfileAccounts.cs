using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
public static class DiscoveryRestoreRetryPolicy {
    public static bool SessionRejected(Exception error){
        var response=error as DiscoveryRequestException;
        if(response!=null)return response.ErrorCode=="session_invalid"||response.ErrorCode=="session_superseded";
        string message=(error.Message??"").ToLowerInvariant();
        return message.Contains("different server account")||message.Contains("already linked");
    }
    public static bool Transient(Exception error){
        // Protocol, certificate, identity and authorization rejection must never
        // be relabeled as a temporary outage or trigger automatic re-enrollment.
        return error is System.Net.Sockets.SocketException||error is TimeoutException||
            error is IOException&&!(error is InvalidDataException);
    }
    public static int DelayMilliseconds(int failures){return failures<=1?2000:failures==2?4000:failures==3?8000:failures==4?16000:30000;}
    public static bool ScopeCurrent(bool closing,ulong profile,ulong currentProfile,ulong session,ulong currentSession,bool hasDiscovery,long attempt=0,long currentAttempt=0){
        return !closing&&!hasDiscovery&&profile==currentProfile&&session==currentSession&&attempt==currentAttempt;
    }
}
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

// These IDs are only creation attempts made by this profile. They are not a
// source of group authorization; the authenticated server roster is. Keeping
// the marker across restarts prevents a failed new group from silently being
// presented as an old, intentionally local-only group.
public sealed class PendingGroupRegistrationStore {
    readonly string path;
    public PendingGroupRegistrationStore(string profileDirectory){path=Path.Combine(profileDirectory,"pending-group-registrations.txt");}
    static bool Id(string value,int length){return value!=null&&value.Length==length&&System.Linq.Enumerable.All(value,c=>(c>='0'&&c<='9')||(c>='a'&&c<='f'));}
    public string[] Load(long accountId,string peerId){
        if(!File.Exists(path))return new string[0];
        var ids=new System.Collections.Generic.HashSet<string>(StringComparer.Ordinal);
        foreach(string line in File.ReadAllLines(path,Encoding.UTF8)){
            var fields=line.Split('|');
            if(fields.Length==3&&fields[0]==accountId.ToString()&&fields[1]==peerId&&Id(fields[2],32))ids.Add(fields[2]);
        }
        return System.Linq.Enumerable.ToArray(ids);
    }
    public string[] AllIds(){
        if(!File.Exists(path))return new string[0];
        var ids=new System.Collections.Generic.HashSet<string>(StringComparer.Ordinal);
        foreach(string line in File.ReadAllLines(path,Encoding.UTF8)){
            var fields=line.Split('|');if(fields.Length==3&&Id(fields[2],32))ids.Add(fields[2]);
        }
        return System.Linq.Enumerable.ToArray(ids);
    }
    public void Add(long accountId,string peerId,string groupId){
        if(accountId<=0||!Id(peerId,64)||!Id(groupId,32))throw new InvalidOperationException("The new group identity is incomplete.");
        var lines=File.Exists(path)?new System.Collections.Generic.List<string>(File.ReadAllLines(path,Encoding.UTF8)):new System.Collections.Generic.List<string>();
        string record=accountId+"|"+peerId+"|"+groupId;if(!lines.Contains(record))lines.Add(record);Save(lines);
    }
    public void Remove(long accountId,string peerId,string groupId){
        if(!File.Exists(path))return;
        string record=accountId+"|"+peerId+"|"+groupId;
        var lines=new System.Collections.Generic.List<string>(File.ReadAllLines(path,Encoding.UTF8));lines.RemoveAll(value=>value==record);Save(lines);
    }
    void Save(System.Collections.Generic.List<string> lines){
        Directory.CreateDirectory(Path.GetDirectoryName(path));string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";
        try{File.WriteAllLines(temporary,lines.ToArray(),new UTF8Encoding(false));if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);}
        finally{if(File.Exists(temporary))File.Delete(temporary);}
    }
}

public static class GroupRegistrationPolicy {
    // An event is only a demand hint. Both endpoints must still match the
    // current authenticated roster; native admission and peer TLS remain final.
    public static bool IncomingControlReady(ServerGroupRoster roster,DiscoveryEvent demand,long accountId,string peerId){
        if(roster==null||demand==null||demand.Purpose!="group_key"||demand.TargetPublicIdentity!=peerId||
           roster.GroupId!=demand.GroupId||roster.RosterGeneration<1||roster.RosterGeneration!=demand.RosterGeneration||
           accountId<=0||demand.AccountId<=0||demand.AccountId==accountId||demand.PublicIdentity==peerId||roster.Members==null)return false;
        bool local=false,remote=false;
        foreach(var member in roster.Members){
            if(member.State!="member")continue;
            local|=member.AccountId==accountId&&member.PeerId==peerId;
            remote|=member.AccountId==demand.AccountId&&member.PeerId==demand.PublicIdentity;
        }
        return local&&remote;
    }
    public static bool OwnerRosterReady(ServerGroupRoster roster,string localGroupId,long accountId,string peerId){
        return roster!=null&&roster.LegacyGroupId==localGroupId&&roster.GroupId!=null&&roster.GroupId.Length==32&&
            roster.RosterGeneration==1&&roster.Members!=null&&roster.Members.Count==1&&
            roster.Members[0].AccountId==accountId&&roster.Members[0].PeerId==peerId&&
            roster.Members[0].Role=="owner"&&roster.Members[0].State=="member";
    }
    public static bool CanInvite(ServerGroupRoster roster,long accountId,string peerId){
        if(roster==null||roster.RosterGeneration<1||roster.Members==null)return false;
        foreach(var member in roster.Members)
            if(member.AccountId==accountId&&member.PeerId==peerId&&member.State=="member"&&
               (member.Role=="owner"||member.Role=="admin"))return true;
        return false;
    }
}
}
