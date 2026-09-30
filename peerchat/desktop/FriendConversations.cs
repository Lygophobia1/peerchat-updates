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
    public long CandidateSeenUtc;
    public string Presence="Unknown",RefreshResult="Not requested";
    public string ExpectedPeer="",PresentedPeer="",Candidate="None",Source="PeerChat server relay",Route="relay-only",TransportState="Offline",TrustState="No verification pending",Failure="None",LocalSubnetDiagnostics="Not evaluated";
    public bool HasLiveSession;
    public long LiveConnectionId,LatestFailedAttemptId,SessionEpoch,SessionRevision;
    public string LiveCandidate="None",LiveSource="None",LiveRoute="None",LiveDirection="unknown",LatestFailedCandidate="None",LatestFailedSource="None",LatestFailedRoute="None",LatestFailure="None",LatestProgress="None";
}

public sealed class LocalSubnetInterface {
    public string Name="",Address="",Mask="",IgnoredReason="";
}

public sealed class LocalSubnetDecision {
    public bool SameSubnet;
    public string Diagnostics="Not evaluated";
}

// Pure subnet evaluation is kept separate from Windows interface enumeration so both
// automatic and manual connection paths use identical, directly testable semantics.
public static class LocalSubnetClassifier {
    static bool Parse(string value,out byte[] bytes){System.Net.IPAddress address;if(!System.Net.IPAddress.TryParse(value,out address)||address.AddressFamily!=System.Net.Sockets.AddressFamily.InterNetwork){bytes=null;return false;}bytes=address.GetAddressBytes();return true;}
    static int Prefix(byte[] mask){int prefix=0;bool zero=false;foreach(byte value in mask)for(int bit=7;bit>=0;bit--){bool set=(value&(1<<bit))!=0;if(set){if(zero)return -1;prefix++;}else zero=true;}return prefix;}
    public static LocalSubnetDecision Evaluate(string candidate,IEnumerable<LocalSubnetInterface> interfaces){
        var result=new LocalSubnetDecision();byte[] remote;if(!Parse(candidate,out remote)){result.Diagnostics="candidate="+(candidate??"")+"; same_subnet=no; reason=not an IPv4 address";return result;}
        var lines=new List<string>();foreach(var item in interfaces??Enumerable.Empty<LocalSubnetInterface>()){
            string label=String.IsNullOrEmpty(item.Name)?"unnamed interface":item.Name;
            if(!String.IsNullOrEmpty(item.IgnoredReason)){lines.Add("interface="+label+"; candidate="+candidate+"; same_subnet=no; ignored="+item.IgnoredReason);continue;}
            byte[] local,mask;if(!Parse(item.Address,out local)||!Parse(item.Mask,out mask)){lines.Add("interface="+label+"; local="+item.Address+"; mask="+item.Mask+"; candidate="+candidate+"; same_subnet=no; ignored=invalid IPv4 address or mask");continue;}
            int prefix=Prefix(mask);if(prefix<=0){lines.Add("interface="+label+"; local="+item.Address+"; mask="+item.Mask+"; candidate="+candidate+"; same_subnet=no; ignored=invalid/non-contiguous subnet mask");continue;}
            bool same=true;for(int index=0;index<4;index++)if((remote[index]&mask[index])!=(local[index]&mask[index])){same=false;break;}
            if(same)result.SameSubnet=true;lines.Add("interface="+label+"; local="+item.Address+"/"+prefix+"; mask="+item.Mask+"; candidate="+candidate+"; same_subnet="+(same?"yes":"no"));
        }
        result.Diagnostics=lines.Count==0?"candidate="+candidate+"; same_subnet=no; reason=no usable current IPv4 interfaces":String.Join("\n",lines);return result;
    }
}

// Connection facts are keyed by the expected cryptographic peer identity. An event for one
// friend can therefore never borrow candidate/trust/failure state from another friend.
public sealed class ConnectionAttemptTracker {
    readonly Dictionary<string,ConnectionAttemptDetails> attempts=new Dictionary<string,ConnectionAttemptDetails>(StringComparer.Ordinal);
    static string Key(CoreEvent e,ConversationModel conversation){if(e!=null&&!String.IsNullOrEmpty(e.expected))return e.expected;if(conversation!=null&&conversation.Contact!=null&&!String.IsNullOrEmpty(conversation.Contact.peer))return conversation.Contact.peer;if(e!=null&&!String.IsNullOrEmpty(e.peer))return e.peer;return conversation==null?"":conversation.ConversationId;}
    public ConnectionAttemptDetails For(ConversationModel conversation){string key=Key(null,conversation);ConnectionAttemptDetails value;return !String.IsNullOrEmpty(key)&&attempts.TryGetValue(key,out value)?value:new ConnectionAttemptDetails();}
    static void Project(ConnectionAttemptDetails value){if(value.HasLiveSession){value.AttemptId=value.LiveConnectionId;value.Candidate=value.LiveCandidate;value.Source=value.LiveSource;value.Route=value.LiveRoute;value.TransportState="Ready";value.TrustState="Verified";value.Failure="None";}}
    public ConnectionAttemptDetails Update(CoreEvent e,ConversationModel conversation,string transport=null,string trust=null,string failure=null){
        string key=Key(e,conversation);ConnectionAttemptDetails value;if(!attempts.TryGetValue(key,out value)){value=new ConnectionAttemptDetails();attempts[key]=value;}bool acceptState=true;
        if(e!=null){
            if(!String.IsNullOrEmpty(e.expected))value.ExpectedPeer=e.expected;if(!String.IsNullOrEmpty(e.peer)&&(e.peer!=e.expected||e.kind==3))value.PresentedPeer=e.peer;
            if(e.kind==17){bool newer=e.sessionEpoch>value.SessionEpoch||(e.sessionEpoch==value.SessionEpoch&&e.sessionRevision>value.SessionRevision);acceptState=newer;if(newer){value.SessionEpoch=e.sessionEpoch;value.SessionRevision=e.sessionRevision;value.HasLiveSession=e.connected;value.LiveConnectionId=e.connected?e.attempt:0;if(e.connected){value.LiveCandidate=String.IsNullOrEmpty(e.candidate)?"Unknown":e.candidate;value.LiveSource=String.IsNullOrEmpty(e.source)?"Direct":e.source;value.LiveRoute=String.IsNullOrEmpty(e.route)?"authenticated":e.route;value.LiveDirection=String.IsNullOrEmpty(e.detail)?"unknown":e.detail;}}}
            else if(e.kind==3&&value.SessionRevision==0){value.HasLiveSession=true;value.LiveConnectionId=e.attempt;value.LiveCandidate=String.IsNullOrEmpty(e.candidate)?"Unknown":e.candidate;value.LiveSource=String.IsNullOrEmpty(e.source)?"Direct":e.source;value.LiveRoute=String.IsNullOrEmpty(e.route)?"authenticated":e.route;value.LiveDirection=String.IsNullOrEmpty(e.detail)?"unknown":e.detail;}
            else if(e.kind==4&&value.SessionRevision==0&&value.HasLiveSession&&(e.attempt==0||e.attempt==value.LiveConnectionId)){value.HasLiveSession=false;}
            if(e.kind==16)value.LatestProgress=String.IsNullOrEmpty(e.text)?"Connection lifecycle transition":e.text;
            if(e.kind==8){value.LatestFailedAttemptId=e.attempt;value.LatestFailedCandidate=String.IsNullOrEmpty(e.candidate)?"Unknown":e.candidate;value.LatestFailedSource=String.IsNullOrEmpty(e.source)?"Unknown":e.source;value.LatestFailedRoute=String.IsNullOrEmpty(e.route)?"Unknown":e.route;value.LatestFailure=String.IsNullOrEmpty(failure)?(e.text??"Connection attempt failed"):failure;}
            if(!value.HasLiveSession){if(e.attempt>0)value.AttemptId=e.attempt;if(e.kind==14)value.PresentedPeer="";if(!String.IsNullOrEmpty(e.candidate))value.Candidate=e.candidate;if(!String.IsNullOrEmpty(e.source))value.Source=e.source;if(!String.IsNullOrEmpty(e.route))value.Route=e.route;}
        }
        if(acceptState&&!value.HasLiveSession){if(!String.IsNullOrEmpty(transport))value.TransportState=transport;if(!String.IsNullOrEmpty(trust))value.TrustState=trust;if(!String.IsNullOrEmpty(failure))value.Failure=failure;}Project(value);return value;
    }
}
}
