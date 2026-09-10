// Managed account/discovery client for Milestone 1.
// Purpose: talk to the discovery service without exposing the native chat core to account passwords
// or session tokens. Inputs are account UI values plus the existing public peer fingerprint.
// Outputs are authenticated account/session and public discovery results. The TLS certificate is
// explicitly pinned because discovery is a separate trust boundary from peer-to-peer TLS.
using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Web.Script.Serialization;
using System.Threading;

namespace PeerChatDesktop {
public sealed class DiscoveryClient {
    readonly JavaScriptSerializer json=new JavaScriptSerializer { MaxJsonLength=16*1024 };
    public string Host, CertificateSha256, SessionToken, Username;
    public long AccountId;
    public int Port;
    // Called only for an unpinned development server or a server identity replacement. The caller
    // must present an explicit UI confirmation; this client never silently trusts first contact.
    readonly Func<string,int,string,bool,bool> certificateTrustPrompt;
    readonly object eventGate=new object();
    Thread eventThread;
    volatile bool eventStop;
    // Read by the UI refresh scheduler without taking the event-channel lock.  A healthy
    // push channel lets the client use a very slow safety snapshot; a dropped channel uses
    // a bounded retry cadence while it reconnects.
    public volatile bool EventChannelConnected;
    readonly ManualResetEvent eventStopSignal=new ManualResetEvent(false);
    Action<DiscoveryEvent> eventHandler;
    int eventReconnectDelayMs=1000;
    DateTime eventConnectedUtc=DateTime.MinValue;
    public DiscoveryClient(string host,int port,string certificateSha256,Func<string,int,string,bool,bool> trustPrompt=null){Host=host;Port=port;CertificateSha256=NormalizePin(certificateSha256);certificateTrustPrompt=trustPrompt;}
    static string NormalizePin(string value){return (value??"").Replace(" ","").Replace(":","").ToUpperInvariant();}
    static string CertificateFingerprint(X509Certificate certificate){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(certificate.GetRawCertData())).Replace("-","");}
    static byte[] ReadExact(Stream stream,int length){var buffer=new byte[length];int offset=0;while(offset<length){int read=stream.Read(buffer,offset,length-offset);if(read==0)throw new IOException("Discovery server closed the connection.");offset+=read;}return buffer;}
    static void WriteFrame(Stream stream,byte[] data){var header=new[]{(byte)(data.Length>>24),(byte)(data.Length>>16),(byte)(data.Length>>8),(byte)data.Length};stream.Write(header,0,4);stream.Write(data,0,data.Length);}
    static byte[] ReadFrame(Stream stream){var h=ReadExact(stream,4);int length=(h[0]<<24)|(h[1]<<16)|(h[2]<<8)|h[3];if(length<1||length>16*1024)throw new InvalidDataException("Invalid discovery response size.");return ReadExact(stream,length);}
    Dictionary<string,object> Request(string type,Dictionary<string,object> fields) {
        if(String.IsNullOrWhiteSpace(Host)||Port<1||Port>65535)throw new InvalidOperationException("The configured PeerChat server endpoint is invalid.");
        fields["version"]=1;fields["type"]=type;
        using(var tcp=new TcpClient()) { tcp.ReceiveTimeout=10000;tcp.SendTimeout=10000;tcp.Connect(Host,Port); using(var tls=new SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>{
            if(certificate==null)return false;string observed=CertificateFingerprint(certificate);string trusted=CertificateSha256;
            if(trusted.Length==64&&String.Equals(observed,trusted,StringComparison.OrdinalIgnoreCase))return true;
            bool replacement=trusted.Length==64;bool accepted=certificateTrustPrompt!=null&&certificateTrustPrompt(Host,Port,observed,replacement);
            if(accepted)CertificateSha256=observed;return accepted;
        })) { tls.AuthenticateAsClient(Host,null,SslProtocols.Tls12|SslProtocols.Tls13,false);var data=Encoding.UTF8.GetBytes(json.Serialize(fields));if(data.Length>4096)throw new InvalidDataException("Discovery request is too large.");WriteFrame(tls,data);var response=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls)));object ok;if(!response.TryGetValue("ok",out ok)||!(bool)ok){object error;response.TryGetValue("error",out error);throw new InvalidOperationException("Discovery: "+(error??"request_failed"));}return response; } }
    }
    public void Register(string username,string password,string publicIdentity){var r=Request("register",new Dictionary<string,object>{{"username",username},{"password",password},{"publicIdentity",publicIdentity}});SaveSession(username,r);}
    public void Login(string username,string password){var r=Request("login",new Dictionary<string,object>{{"username",username},{"password",password}});SaveSession(username,r);}
    void SaveSession(string username,Dictionary<string,object> result){object token,accountId,returnedUsername;if(!result.TryGetValue("sessionToken",out token)||String.IsNullOrEmpty(token as string))throw new InvalidDataException("Discovery server returned no session.");Username=result.TryGetValue("username",out returnedUsername)?returnedUsername as string:username;SessionToken=(string)token;if(result.TryGetValue("accountId",out accountId))AccountId=Convert.ToInt64(accountId);}
    // A device reports its currently reachable listener candidate with presence. The server gives
    // that candidate only to an authenticated friend; public username search remains endpoint-free.
    public PresenceResult ReportPresence(string publicIdentity,string endpointHost="",int endpointPort=0,string publicEndpointHost="",int publicEndpointPort=0){var r=Request("presence",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",publicIdentity},{"endpointHost",endpointHost??""},{"endpointPort",endpointPort.ToString()},{"publicEndpointHost",publicEndpointHost??""},{"publicEndpointPort",publicEndpointPort.ToString()}});object value;if(r.TryGetValue("accountId",out value))AccountId=Convert.ToInt64(value);if(r.TryGetValue("username",out value)&&value is string)Username=(string)value;return new PresenceResult{AccountId=AccountId,Username=Username??"",RegisteredPublicIdentity=r.ContainsKey("registeredPublicIdentity")?r["registeredPublicIdentity"] as string:"",EndpointHost=r.ContainsKey("endpointHost")?r["endpointHost"] as string:"",EndpointPort=r.ContainsKey("endpointPort")?Convert.ToInt32(r["endpointPort"]):0,PublicEndpointHost=r.ContainsKey("publicEndpointHost")?r["publicEndpointHost"] as string:"",PublicEndpointPort=r.ContainsKey("publicEndpointPort")?Convert.ToInt32(r["publicEndpointPort"]):0,PublicCandidateSource=r.ContainsKey("publicCandidateSource")?r["publicCandidateSource"] as string:"",ObservedAddressClassification=r.ContainsKey("observedAddressClassification")?r["observedAddressClassification"] as string:""};}
    public void RefreshSessionInfo(){var r=Request("session_info",new Dictionary<string,object>{{"sessionToken",SessionToken??""}});AccountId=Convert.ToInt64(r["accountId"]);Username=(string)r["username"];}
    public void Logout(){if(String.IsNullOrEmpty(SessionToken))return;try{Request("logout",new Dictionary<string,object>{{"sessionToken",SessionToken}});}finally{SessionToken="";EventChannelConnected=false;}}
    public DiscoverySearchResult Search(string username){var r=Request("search",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"username",username}});return new DiscoverySearchResult{AccountId=Convert.ToInt64(r["accountId"]),Username=(string)r["username"],PublicIdentity=(string)r["publicIdentity"],Online=Convert.ToBoolean(r["online"])};}
    // Only public discovery fields cross this boundary. Session tokens and passwords are never
    // included in diagnostics; the messages below are deliberately limited to query/result shape.
    public List<DiscoverySearchResult> Suggest(string prefix){
        System.Diagnostics.Debug.WriteLine("PeerChat discovery suggest request started: '"+prefix+"'.");
        var r=Request("suggest",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"username",prefix}});
        var results=new List<DiscoverySearchResult>(); object rawValues;
        if(r.TryGetValue("suggestions",out rawValues)){
            // JavaScriptSerializer has returned both ArrayList and object[] across framework
            // versions. Accept the common IEnumerable shape rather than silently showing no UI.
            var values=rawValues as System.Collections.IEnumerable;
            if(values!=null)foreach(object raw in values){
                var item=raw as Dictionary<string,object>;if(item==null)continue;
                results.Add(new DiscoverySearchResult{AccountId=Convert.ToInt64(item["accountId"]),Username=(string)item["username"],PublicIdentity=(string)item["publicIdentity"],Online=Convert.ToBoolean(item["online"])});
            }
        }
        System.Diagnostics.Debug.WriteLine("PeerChat discovery suggest response: "+results.Count+" public result(s): "+String.Join(",",results.Select(x=>x.Username).ToArray()));
        return results;
    }
    public string SendFriendRequest(long accountId){return State(Request("friend_request",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    public string RespondToFriendRequest(long accountId,bool accept){return State(Request("friend_respond",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()},{"action",accept?"accept":"reject"}}));}
    public string CancelFriendRequest(long accountId){return State(Request("friend_cancel",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    public string RemoveFriend(long accountId){return State(Request("friend_remove",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    public string SetBlocked(long accountId,bool blocked){return State(Request(blocked?"friend_block":"friend_unblock",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    static string State(Dictionary<string,object> response){object state;return response.TryGetValue("state",out state)?state as string:"";}
    static List<DiscoverySearchResult> Profiles(Dictionary<string,object> response,string key){var results=new List<DiscoverySearchResult>();object raw;if(!response.TryGetValue(key,out raw))return results;var values=raw as IEnumerable;if(values==null)return results;foreach(object value in values){var item=value as Dictionary<string,object>;if(item==null)continue;object host,port,publicHost,publicPort,seen,source,classification;item.TryGetValue("endpointHost",out host);item.TryGetValue("endpointPort",out port);item.TryGetValue("publicEndpointHost",out publicHost);item.TryGetValue("publicEndpointPort",out publicPort);item.TryGetValue("candidateSeenUtc",out seen);item.TryGetValue("publicCandidateSource",out source);item.TryGetValue("observedAddressClassification",out classification);results.Add(new DiscoverySearchResult{AccountId=Convert.ToInt64(item["accountId"]),Username=(string)item["username"],PublicIdentity=(string)item["publicIdentity"],Online=Convert.ToBoolean(item["online"]),EndpointHost=host as string??"",EndpointPort=port==null?0:Convert.ToInt32(port),PublicEndpointHost=publicHost as string??"",PublicEndpointPort=publicPort==null?0:Convert.ToInt32(publicPort),CandidateSeenUtc=seen==null?0:Convert.ToInt64(seen),PublicCandidateSource=source as string??"",ObservedAddressClassification=classification as string??""});}return results;}
    public FriendState GetFriendState(){var response=Request("friend_state",new Dictionary<string,object>{{"sessionToken",SessionToken??""}});return new FriendState{Incoming=Profiles(response,"incoming"),Outgoing=Profiles(response,"outgoing"),Friends=Profiles(response,"friends"),Blocked=Profiles(response,"blocked")};}
    // The push channel is deliberately only a notification stream. Each event causes the UI to
    // request a fresh friend snapshot, so reconnects or missed frames cannot make client state
    // authoritative. It uses the same TLS pin/explicit development trust prompt as Request.
    public void StartEventChannel(Action<DiscoveryEvent> handler){if(handler==null||String.IsNullOrEmpty(SessionToken))return;lock(eventGate){eventHandler=handler;if(eventThread!=null&&eventThread.IsAlive)return;eventStop=false;eventStopSignal.Reset();eventThread=new Thread(EventLoop){IsBackground=true,Name="PeerChat discovery events"};eventThread.Start();}}
    public void StopEventChannel(){lock(eventGate){eventStop=true;eventStopSignal.Set();} }
    void EventLoop(){bool everConnected=false;while(!eventStop){TcpClient tcp=null;SslStream tls=null;try{tcp=new TcpClient();tcp.ReceiveTimeout=1000;tcp.SendTimeout=10000;tcp.Connect(Host,Port);tls=new SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>{if(certificate==null)return false;string observed=CertificateFingerprint(certificate);string trusted=CertificateSha256;if(trusted.Length==64&&String.Equals(observed,trusted,StringComparison.OrdinalIgnoreCase))return true;bool replacement=trusted.Length==64;bool accepted=certificateTrustPrompt!=null&&certificateTrustPrompt(Host,Port,observed,replacement);if(accepted)CertificateSha256=observed;return accepted;});tls.AuthenticateAsClient(Host,null,SslProtocols.Tls12|SslProtocols.Tls13,false);var request=new Dictionary<string,object>{{"version",1},{"type","events"},{"sessionToken",SessionToken??""}};var data=Encoding.UTF8.GetBytes(json.Serialize(request));WriteFrame(tls,data);var ready=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls)));object ok;if(!ready.TryGetValue("ok",out ok)||!(bool)ok)throw new InvalidOperationException("Discovery event subscription rejected.");EventChannelConnected=true;eventConnectedUtc=DateTime.UtcNow;var connectedCallback=eventHandler;if(everConnected){try{if(connectedCallback!=null)connectedCallback(new DiscoveryEvent{Event="channel_connected"});}catch{}}everConnected=true;while(!eventStop){if(tcp.Client.Poll(1000000,SelectMode.SelectRead)){if(tcp.Available==0)throw new IOException("Discovery event channel closed.");var response=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls)));object type;if(response.TryGetValue("type",out type)&&String.Equals(type as string,"friend_event",StringComparison.Ordinal)){var notification=new DiscoveryEvent{Event=response.ContainsKey("event")?response["event"] as string:"",AccountId=response.ContainsKey("accountId")?Convert.ToInt64(response["accountId"]):0,PublicIdentity=response.ContainsKey("publicIdentity")?response["publicIdentity"] as string:"",EndpointHost=response.ContainsKey("endpointHost")?response["endpointHost"] as string:"",EndpointPort=response.ContainsKey("endpointPort")?Convert.ToInt32(response["endpointPort"]):0,PublicEndpointHost=response.ContainsKey("publicEndpointHost")?response["publicEndpointHost"] as string:"",PublicEndpointPort=response.ContainsKey("publicEndpointPort")?Convert.ToInt32(response["publicEndpointPort"]):0};var callback=eventHandler;try{if(callback!=null)callback(notification);}catch{/* UI callbacks must not terminate the reconnect loop. */}}}}}
            catch(Exception error){if(!eventStop){System.Diagnostics.Debug.WriteLine("PeerChat discovery event channel reconnect in "+eventReconnectDelayMs+" ms: "+error.GetType().Name);eventReconnectDelayMs=Math.Min(30000,eventReconnectDelayMs*2);}}
            finally{if(EventChannelConnected&&DateTime.UtcNow-eventConnectedUtc>TimeSpan.FromSeconds(15))eventReconnectDelayMs=1000;EventChannelConnected=false;try{if(tls!=null)tls.Dispose();}catch{}try{if(tcp!=null)tcp.Close();}catch{}}
            if(!eventStop)eventStopSignal.WaitOne(eventReconnectDelayMs);
        }}
}
public sealed class DiscoverySearchResult { public long AccountId,CandidateSeenUtc; public string Username,PublicIdentity,EndpointHost,PublicEndpointHost,PublicCandidateSource,ObservedAddressClassification; public int EndpointPort,PublicEndpointPort; public bool Online; }
public sealed class FriendState { public List<DiscoverySearchResult> Incoming=new List<DiscoverySearchResult>(),Outgoing=new List<DiscoverySearchResult>(),Friends=new List<DiscoverySearchResult>(),Blocked=new List<DiscoverySearchResult>(); }
public sealed class DiscoveryEvent { public string Event,PublicIdentity,EndpointHost,PublicEndpointHost; public long AccountId; public int EndpointPort,PublicEndpointPort; public bool Online; }
public sealed class PresenceResult { public long AccountId; public string Username,RegisteredPublicIdentity,EndpointHost,PublicEndpointHost,PublicCandidateSource,ObservedAddressClassification; public int EndpointPort,PublicEndpointPort; }
}
