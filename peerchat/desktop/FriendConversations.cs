using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
public sealed class FriendConversationBinding {
    public long AccountId;
    public string Username="",CurrentPeerId="";
    public List<string> KnownPeerIds=new List<string>();
    public bool ActiveFriend;
}

// Per-profile, protected social-to-device index. It contains only public account/device
// identifiers and display metadata; native encrypted message history remains in SQLite.
public sealed class FriendConversationIndex {
    readonly string path;
    readonly JavaScriptSerializer json=new JavaScriptSerializer();
    readonly Dictionary<long,FriendConversationBinding> records=new Dictionary<long,FriendConversationBinding>();
    readonly Func<byte[],byte[]> protect,unprotect;
    public IEnumerable<FriendConversationBinding> Records {get{return records.Values;}}
    static bool PeerId(string value){return value!=null&&value.Length==64&&value.All(c=>(c>='0'&&c<='9')||(c>='a'&&c<='f'));}
    public FriendConversationIndex(string profileDirectory):this(profileDirectory,data=>ProtectedData.Protect(data,null,DataProtectionScope.CurrentUser),data=>ProtectedData.Unprotect(data,null,DataProtectionScope.CurrentUser)){}
    public FriendConversationIndex(string profileDirectory,Func<byte[],byte[]> protectData,Func<byte[],byte[]> unprotectData){path=Path.Combine(profileDirectory,"friend-conversations.dat");protect=protectData;unprotect=unprotectData;Load();}
    void Load(){
        if(!File.Exists(path))return;
        try{var plain=unprotect(File.ReadAllBytes(path));var loaded=json.Deserialize<List<FriendConversationBinding>>(Encoding.UTF8.GetString(plain))??new List<FriendConversationBinding>();foreach(var item in loaded){if(item==null||item.AccountId<=0)continue;item.Username=item.Username??"";item.CurrentPeerId=PeerId(item.CurrentPeerId)?item.CurrentPeerId:"";item.KnownPeerIds=(item.KnownPeerIds??new List<string>()).Where(PeerId).Distinct(StringComparer.Ordinal).ToList();if(PeerId(item.CurrentPeerId)&&!item.KnownPeerIds.Contains(item.CurrentPeerId))item.KnownPeerIds.Add(item.CurrentPeerId);records[item.AccountId]=item;}}
        catch(CryptographicException){/* A mapping from another Windows user is not trusted. */}
        catch(InvalidOperationException){/* Malformed local cache is ignored; server snapshot repairs it. */}
    }
    public void Reconcile(IEnumerable<DiscoverySearchResult> friends){
        foreach(var item in records.Values)item.ActiveFriend=false;
        foreach(var friend in friends??Enumerable.Empty<DiscoverySearchResult>()){
            if(friend.AccountId<=0)continue;FriendConversationBinding item;if(!records.TryGetValue(friend.AccountId,out item)){item=new FriendConversationBinding{AccountId=friend.AccountId};records.Add(friend.AccountId,item);}item.Username=friend.Username??item.Username;item.ActiveFriend=true;if(PeerId(friend.PublicIdentity)){item.CurrentPeerId=friend.PublicIdentity;if(!item.KnownPeerIds.Contains(friend.PublicIdentity))item.KnownPeerIds.Add(friend.PublicIdentity);}
        }
        Save();
    }
    void Save(){Directory.CreateDirectory(Path.GetDirectoryName(path));string temporary=path+"."+Guid.NewGuid().ToString("N")+".tmp";var ordered=records.Values.OrderBy(item=>item.AccountId).ToList();byte[] protectedData=protect(Encoding.UTF8.GetBytes(json.Serialize(ordered)));File.WriteAllBytes(temporary,protectedData);if(File.Exists(path))File.Replace(temporary,path,null);else File.Move(temporary,path);}
}

// A small per-profile, metadata-only diagnostic log. Callers supply structured
// connection facts; message contents and authentication secrets never enter it.
public sealed class ConnectionDiagnosticLog {
    const long MaximumBytes=131072;
    const int RetainedLines=300;
    readonly string path;
    readonly object gate=new object();
    public ConnectionDiagnosticLog(string profileDirectory){path=Path.Combine(profileDirectory,"connection.log");}
    static string Safe(string value){if(String.IsNullOrEmpty(value))return "-";return value.Replace('\r',' ').Replace('\n',' ').Trim().Substring(0,Math.Min(300,value.Trim().Length));}
    public void Record(string stage,long accountId,string conversationId,string expectedPeer,string currentPeer,string candidate,string source,string route,string result){
        string line=DateTime.UtcNow.ToString("O")+" stage="+Safe(stage)+" account="+(accountId>0?accountId.ToString():"-")+" conversation="+Safe(conversationId)+" expected_peer="+Safe(expectedPeer)+" current_peer="+Safe(currentPeer)+" candidate="+Safe(candidate)+" source="+Safe(source)+" route="+Safe(route)+" result="+Safe(result);
        lock(gate){Directory.CreateDirectory(Path.GetDirectoryName(path));File.AppendAllText(path,line+Environment.NewLine,new UTF8Encoding(false));var info=new FileInfo(path);if(info.Length>MaximumBytes){var kept=File.ReadAllLines(path).Reverse().Take(RetainedLines).Reverse().ToArray();File.WriteAllLines(path,kept,new UTF8Encoding(false));}}
    }
    public string Read(){lock(gate){try{return File.Exists(path)?File.ReadAllText(path):"No connection attempts have been recorded for this profile.";}catch(IOException){return "The connection log is temporarily unavailable.";}}}
}

public sealed class ConnectionAttemptDetails {
    public long AttemptId;
    public string ExpectedPeer="",PresentedPeer="",Candidate="None",Source="local/direct",Route="None",TransportState="Offline",TrustState="No verification pending",Failure="None";
}

// Connection facts are keyed by the expected cryptographic peer identity. An event for one
// friend can therefore never borrow candidate/trust/failure state from another friend.
public sealed class ConnectionAttemptTracker {
    readonly Dictionary<string,ConnectionAttemptDetails> attempts=new Dictionary<string,ConnectionAttemptDetails>(StringComparer.Ordinal);
    static string Key(CoreEvent e,ConversationModel conversation){if(e!=null&&!String.IsNullOrEmpty(e.expected))return e.expected;if(conversation!=null&&conversation.Contact!=null&&!String.IsNullOrEmpty(conversation.Contact.peer))return conversation.Contact.peer;if(e!=null&&!String.IsNullOrEmpty(e.peer))return e.peer;return conversation==null?"":conversation.ConversationId;}
    public ConnectionAttemptDetails For(ConversationModel conversation){string key=Key(null,conversation);ConnectionAttemptDetails value;return !String.IsNullOrEmpty(key)&&attempts.TryGetValue(key,out value)?value:new ConnectionAttemptDetails();}
    public ConnectionAttemptDetails Update(CoreEvent e,ConversationModel conversation,string transport=null,string trust=null,string failure=null){string key=Key(e,conversation);ConnectionAttemptDetails value;if(!attempts.TryGetValue(key,out value)){value=new ConnectionAttemptDetails();attempts[key]=value;}if(e!=null){if(e.kind==14){value.PresentedPeer="";value.Failure="None";}if(e.attempt>0)value.AttemptId=e.attempt;if(!String.IsNullOrEmpty(e.expected))value.ExpectedPeer=e.expected;if(!String.IsNullOrEmpty(e.peer)&&(e.peer!=e.expected||e.kind==3))value.PresentedPeer=e.peer;if(!String.IsNullOrEmpty(e.candidate))value.Candidate=e.candidate;if(!String.IsNullOrEmpty(e.source))value.Source=e.source;if(!String.IsNullOrEmpty(e.route))value.Route=e.route;}if(!String.IsNullOrEmpty(transport))value.TransportState=transport;if(!String.IsNullOrEmpty(trust))value.TrustState=trust;if(!String.IsNullOrEmpty(failure))value.Failure=failure;return value;}
}
}
