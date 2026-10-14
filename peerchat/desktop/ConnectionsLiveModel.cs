using System;
using System.Linq;
using System.Globalization;
using System.Collections.Generic;
using System.Text.RegularExpressions;
namespace PeerChatDesktop {
// Copied existing observations only. No transport callbacks, retention or measurement owner.
public sealed class ConnectionsRouteObservation {
    public string Peer,Target,Conversation,Route,Candidate,Source,Direction,Transport,Trust,Progress;
    public long Epoch,Revision,WinnerId,AttemptId;public bool Connected,Selected;
}
public sealed class ConnectionsLiveInput {
    public string Profile,Context,ConnectionText,RetainedLog,Unavailable,SelectedSessionId;
    public bool AccountContextAvailable,DiscoveryConnected;
    public ConnectionsRouteObservation[] Routes=new ConnectionsRouteObservation[0];
    public FailureSnapshot[] Failures=new FailureSnapshot[0];
}
public static class ConnectionsSafeText {
    static readonly Regex Credentials=new Regex(@"(?i)\b(password|secret|session[_-]?token|token|authorization|private[_-]?key|turn[_-]?(?:uri|username|user|password)|ice[_-]?(?:pwd|password)|credential(?:[_-]?uri)?)\s*[=:]\s*(?:Bearer\s+)?(?:""[^""]*""|'[^']*'|[^\s;,]+)",RegexOptions.Compiled);
    static readonly Regex Bearer=new Regex(@"(?i)\bBearer\s+[^\s;,]+",RegexOptions.Compiled);
    static readonly Regex Pem=new Regex(@"(?is)-----BEGIN [^-\r\n]*PRIVATE KEY-----.*?(?:-----END [^-\r\n]*PRIVATE KEY-----|$)",RegexOptions.Compiled);
    public static string Clean(string value){string text=value??"";if(text.Length>131072)text=text.Substring(0,131072);return DiagnosticsPrivacyFilter.Redact(Bearer.Replace(Credentials.Replace(Pem.Replace(text,"[redacted private key]"),"$1=[redacted]"),"Bearer [redacted]"));}
}
public sealed class ExistingConnectionsProvider {
    readonly Func<ConnectionsLiveInput> read;
    public ExistingConnectionsProvider(Func<ConnectionsLiveInput> source){read=source??throw new ArgumentNullException("source");}
    static string Safe(string value){return ConnectionsSafeText.Clean(value);}
    static string Value(string value,string absent="N/A — Not instrumented"){return String.IsNullOrWhiteSpace(value)||value=="None"||value=="Unknown"?absent:Safe(value);}
    static DiagnosticPanel Card(string title,params string[] values){return new DiagnosticPanel{Title=title,Metrics=Enumerable.Range(0,values.Length/2).Select(i=>new DiagnosticMetric(values[i*2],values[i*2+1])).ToArray()};}
    static string NativeId(ConnectionsRouteObservation r){return "peer:"+Safe(r.Peer)+"/epoch:"+r.Epoch+"/"+(r.Connected?"connection:":"attempt:")+(r.Connected?r.WinnerId:r.AttemptId);}
    static string Field(string text,string label){string prefix=label+": ";foreach(string line in (text??"").Split('\n'))if(line.StartsWith(prefix,StringComparison.Ordinal))return Safe(line.Substring(prefix.Length).Trim());return "";}
    static bool Timestamp(string text,out DateTime utc){return DateTime.TryParse(text,CultureInfo.InvariantCulture,DateTimeStyles.RoundtripKind,out utc)&&(utc=utc.Kind==DateTimeKind.Utc?utc:utc.ToUniversalTime()).Kind==DateTimeKind.Utc;}
    public ConnectionPreviewSnapshot Read(){
        ConnectionsLiveInput input;try{input=read();}catch(Exception error){input=new ConnectionsLiveInput{Unavailable="Snapshot unavailable: "+error.GetType().Name};}input=input??new ConnectionsLiveInput{Unavailable="Snapshot unavailable"};
        var routes=(input.Routes??new ConnectionsRouteObservation[0]).Where(r=>r!=null).OrderByDescending(r=>r.Selected).GroupBy(r=>r.Peer??r.Conversation??"",StringComparer.Ordinal).Select(g=>g.First()).Take(256).ToArray();var selected=String.IsNullOrEmpty(input.SelectedSessionId)?routes.FirstOrDefault(r=>r.Selected):routes.FirstOrDefault(r=>NativeId(r)==input.SelectedSessionId);int winners=routes.Count(r=>r.Connected);
        var snapshot=new ConnectionPreviewSnapshot{Sample=false,Context=Safe(input.Context),Profile=Safe(input.Profile)};
        snapshot.Sessions=routes.Where(r=>r.Connected&&r.WinnerId>0||!r.Connected&&r.AttemptId>0).Select(r=>new ConnectionPreviewSession{Sample=false,StartKnown=false,Id=NativeId(r),Label=r.Connected?"Current authenticated winner":"Latest runtime attempt",Target=Value(r.Target,"Peer not mapped"),Kind=r.Connected?"Established peer-control session":"Latest observed attempt; not necessarily pending",Route=Value(r.Route,"No active session"),Outcome=r.Connected?"Authenticated / Ready":Value(r.Transport,"No active session"),CorrelationId="conversation:"+Safe(r.Conversation)+"/peer:"+Safe(r.Peer),Extra="Native epoch: "+r.Epoch+" · revision: "+r.Revision+"\nDirection: "+Value(r.Direction)+"\nCandidate: "+Value(r.Candidate)+"\nHistorical completed-session catalog: Not retained"}).ToArray();
        snapshot.Failures=(input.Failures??new FailureSnapshot[0]).Where(f=>f!=null&&f.OperationType=="Connection").Take(15).Select(f=>{
            DateTime utc;bool known=Timestamp(f.TimestampUtc,out utc);string connection=(f.Diagnostics==null?null:f.Diagnostics.Sections??new List<DiagnosticsSection>())?.FirstOrDefault(s=>s.Name=="Connection")?.Details;
            string target=Field(connection,"Conversation ID");if(String.IsNullOrEmpty(target))target=Field(connection,"Account ID");return new ConnectionPreviewFailure{Sample=false,TimeKnown=known,Id=Safe(f.Id),SessionId="Retained operation "+Value(f.OperationId,"ID not retained")+" (runtime epoch not retained)",Target="Failed peer association not retained",Reason=Value(f.Reason,"Reason not retained"),Stage=Value(f.Stage,"Stage not retained"),TimestampUtc=utc,Recovery="Not retained — no correlated recovery outcome",Extra="Captured selected context (not a failed-peer binding): "+Value(target,"Not retained")+"\nProfile-retained failure · not asserted to belong to the current login\nOperation type: Connection · generation: "+f.Generation+"\nFailure domain: "+Safe(f.FailureDomain)};
        }).OrderByDescending(f=>f.TimestampUtc).ToArray();
        snapshot.Events=ReadEvents(input.RetainedLog);
        string state=!String.IsNullOrEmpty(input.Unavailable)?Safe(input.Unavailable):winners>0?"Authenticated peer session available":input.DiscoveryConnected?"Discovery connected · no active peer session":"Discovery disconnected · no active peer session";
        snapshot.Overview=new[]{Card("Connection health","Current status",state,"Health summary","Current tracked peer-control observations (not all archive/group/media scopes); older events/failures are profile-retained, not a completed-session catalog"),Card("Server and peer","Discovery connection",!String.IsNullOrEmpty(input.Unavailable)?"Snapshot unavailable":input.DiscoveryConnected?"Authenticated event channel connected":input.AccountContextAvailable?"Disconnected; cached account context is not a live TLS session":"Disconnected; no authenticated account context","Authenticated peer",selected!=null&&selected.Connected?"Verified native winner":winners>0?"Other peer winners available; selected peer has no active session":"No active session","Control transport",selected!=null&&selected.Connected?Value(selected.Route):"No active session","WebRTC media transport","N/A — independent media session; not bound here"),Card("Sessions and recovery","Active established sessions",!String.IsNullOrEmpty(input.Unavailable)?"Snapshot unavailable":winners.ToString(CultureInfo.InvariantCulture),"Reconnect attempts","N/A — lifetime count not instrumented","Recent failures",snapshot.Failures.Length+" existing profile-retained Connection failures")};
        snapshot.Network=new[]{Card("Control connection","Route",selected!=null&&selected.Connected?Value(selected.Route):"No active session","Direct / relay",selected!=null&&selected.Connected?Value(selected.Source):"No active session","Selected control candidate",selected!=null&&selected.Connected?Value(selected.Candidate):"No active session","Establishment stages",selected==null?"No active session":Value(selected.Progress,"Not retained beyond latest progress"),"TLS",selected!=null&&selected.Connected?"Authenticated peer TLS winner":selected==null?"No active session":Value(selected.Transport),"Peer authentication",selected!=null&&selected.Connected?"Verified native winner":selected==null?"No active session":Value(selected.Trust)),Card("WebRTC media — independent transport","Selected ICE candidate","N/A — not bound to this control session","UDP / TURN media route","N/A — not bound to this control session","DTLS-SRTP","N/A — separate media owner"),Card("Network measurements","RTT","N/A — Not instrumented","Bandwidth","N/A — Not instrumented","Retransmissions","N/A — Not instrumented","Packet loss","N/A — Not instrumented","Worker wakeup latency","N/A — Not instrumented")};
        snapshot.RawText="PeerChat Connections diagnostics\nProfile: "+snapshot.Profile+"\nCurrent context: "+snapshot.Context+"\nOriginal collector current-chat projection (may differ from diagnostics-selected scope):\n"+Safe(input.ConnectionText)+"\n\nExisting profile-retained metadata log (not session history):\n"+Safe(input.RetainedLog)+"\n\nNative IDs absent from retained log: Not retained. Sensitive values are redacted.";snapshot.RawText=Safe(snapshot.RawText);return snapshot;
    }
    public static ConnectionPreviewEvent[] ReadEvents(string text,bool includeVoice=false){
        var events=new List<ConnectionPreviewEvent>();foreach(string line in (text??"").Split('\n').Reverse().Take(300).Reverse()){
            if(line.Length<25)continue;int start=line.IndexOf(" stage=",StringComparison.Ordinal);if(start<0)continue;DateTime utc;if(!Timestamp(line.Substring(0,start),out utc))continue;
            string safe=Safe(line);var keys=new[]{"stage","account","conversation","expected_peer","current_peer","candidate","source","route","result"};var fields=new Dictionary<string,string>();
            for(int i=0;i<keys.Length;i++){string marker=" "+keys[i]+"=";int at=safe.IndexOf(marker,StringComparison.Ordinal);if(at<0)continue;at+=marker.Length;int end=i==keys.Length-1?safe.Length:safe.IndexOf(" "+keys[i+1]+"=",at,StringComparison.Ordinal);if(end<at)continue;fields[keys[i]]=safe.Substring(at,end-at).Trim();}
            Func<string,string> get=key=>fields.ContainsKey(key)?fields[key]:"Not retained";string stage=get("stage");if(!includeVoice&&stage.StartsWith("voice",StringComparison.Ordinal))continue;
            ConnectionPreviewEventType type=stage=="connection-lifecycle"&&get("result")=="relay rendezvous established; peer TLS begins"?ConnectionPreviewEventType.TLS:stage=="retry"?ConnectionPreviewEventType.Retry:stage=="trust"?ConnectionPreviewEventType.Authentication:stage=="connect"||stage=="connected"?ConnectionPreviewEventType.Connect:stage=="disconnected"?ConnectionPreviewEventType.Disconnect:stage=="p2p"||stage=="repair"||stage=="network-change"?ConnectionPreviewEventType.RouteSelection:ConnectionPreviewEventType.Lifecycle;
            events.Add(new ConnectionPreviewEvent{Sample=false,TimestampUtc=utc,Type=type,Severity=stage=="failure"||includeVoice&&stage.StartsWith("voice",StringComparison.Ordinal)&&stage.EndsWith("failure",StringComparison.Ordinal)?ConnectionPreviewSeverity.Error:stage=="retry"||stage=="disconnected"||stage=="network-change"?ConnectionPreviewSeverity.Warning:ConnectionPreviewSeverity.Info,SessionId="Not retained",AttemptId="Not retained",CorrelationId="logical conversation:"+get("conversation")+" / peer:"+get("expected_peer"),Description=stage+" · "+get("result"),Extra="Profile-retained record · severity classified from recorded stage\nNative session/attempt/demand IDs: Not retained\nLogical account: "+get("account")+"\nExpected peer: "+get("expected_peer")+"\nPresented peer: "+get("current_peer")+"\nCandidate: "+get("candidate")+"\nSource: "+get("source")+"\nRoute: "+get("route")});
        }return events.OrderBy(e=>e.TimestampUtc).ToArray();
    }
}
}
