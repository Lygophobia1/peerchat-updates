using System;
using System.Linq;
using System.Collections.Generic;
using System.Globalization;
namespace PeerChatDesktop {
// Layout fixtures only: no connection collectors, retained diagnostics or profile storage.
public enum ConnectionPreviewState { Connected, Connecting, Disconnected, AuthenticationFailed, RelayFailed, Reconnecting, Recovered, HistoricalCompleted, Empty, Loading, Unavailable }
public enum ConnectionPreviewSeverity { Info, Warning, Error }
public enum ConnectionPreviewEventType { Discovery, Authentication, TLS, RouteSelection, Connect, Disconnect, Retry }
public static class ConnectionPreviewDisplay {
    public static string Id(string value){return String.IsNullOrEmpty(value)?"N/A":value.Length<=16?value:value.Substring(0,8)+"…"+value.Substring(value.Length-6);}
    public static string Compact(string value,int limit=180){return String.IsNullOrEmpty(value)?"N/A":value.Length<=limit?value:value.Substring(0,limit)+"…";}
    public static string Utc(DateTime utc){return DateTime.SpecifyKind(utc,DateTimeKind.Utc).ToString("o",CultureInfo.InvariantCulture);}
    public static string Local(DateTime utc,TimeZoneInfo zone){var instant=new DateTimeOffset(DateTime.SpecifyKind(utc,DateTimeKind.Utc));var local=TimeZoneInfo.ConvertTime(instant,zone);return local.ToString("yyyy-MM-dd HH:mm:ss zzz",CultureInfo.InvariantCulture)+" · "+(zone.IsDaylightSavingTime(instant)?zone.DaylightName:zone.StandardName);}
    public static string Local(DateTime utc){return Local(utc,TimeZoneInfo.Local);}
}
public sealed class ConnectionPreviewSession {
    public string Id,Label,Target,Kind,Route,Outcome,CorrelationId;public DateTime StartedUtc;public TimeSpan? Duration;
    public string UtcTimestamp {get{return "UTC: "+ConnectionPreviewDisplay.Utc(StartedUtc);}}
    public string Summary {get{return ConnectionPreviewDisplay.Compact(Label,60)+" · "+ConnectionPreviewDisplay.Compact(Target,60)+" · "+ConnectionPreviewDisplay.Compact(Outcome,60)+"\n"+ConnectionPreviewDisplay.Local(StartedUtc)+" · "+ConnectionPreviewDisplay.Id(Id);}}
    public string DetailsUtc {get{return "SAMPLE · "+Label+"\nContact / group: "+Target+"\nType: "+Kind+"\nSession ID: "+Id+"\nStart UTC: "+ConnectionPreviewDisplay.Utc(StartedUtc)+"\nDuration: "+(Duration.HasValue?Duration.Value.ToString():"N/A")+"\nOutcome: "+Outcome+"\nRoute: "+Route+"\nAttempt correlation: "+CorrelationId;}}
}
public sealed class ConnectionPreviewFailure {
    public string Id,SessionId,Target,Reason,Stage,Recovery;public DateTime TimestampUtc;
    public string UtcTimestamp {get{return "UTC: "+ConnectionPreviewDisplay.Utc(TimestampUtc);}}
    public string Summary {get{return ConnectionPreviewDisplay.Compact(Target,60)+" · "+ConnectionPreviewDisplay.Compact(Reason)+"\n"+ConnectionPreviewDisplay.Local(TimestampUtc)+" · "+ConnectionPreviewDisplay.Id(SessionId);}}
    public string DetailsUtc {get{return "SAMPLE failure: "+Id+"\nReason: "+Reason+"\nStage: "+Stage+"\nAssociated contact / group: "+Target+"\nTime UTC: "+ConnectionPreviewDisplay.Utc(TimestampUtc)+"\nSession / attempt: "+SessionId+"\nRecovery: "+Recovery;}}
}
public sealed class ConnectionPreviewEvent {
    public string SessionId,AttemptId,CorrelationId,Description;public DateTime TimestampUtc;public ConnectionPreviewSeverity Severity;public ConnectionPreviewEventType Type;
    public string UtcTimestamp {get{return "UTC: "+ConnectionPreviewDisplay.Utc(TimestampUtc);}}
    public string Summary {get{return ConnectionPreviewDisplay.Compact(Description)+"\n"+ConnectionPreviewDisplay.Local(TimestampUtc)+" · "+Severity+" · "+Type+"\nSession "+ConnectionPreviewDisplay.Id(SessionId)+" · Attempt "+ConnectionPreviewDisplay.Id(AttemptId)+" · Correlation "+ConnectionPreviewDisplay.Id(CorrelationId);}}
    public string DetailsUtc {get{return (String.IsNullOrEmpty(Description)||!Description.StartsWith("[SAMPLE]",StringComparison.Ordinal)?"[SAMPLE] ":"")+Description+"\nUTC: "+ConnectionPreviewDisplay.Utc(TimestampUtc)+"\nSeverity: "+Severity+" · Type: "+Type+"\nSession ID: "+SessionId+"\nAttempt ID: "+AttemptId+"\nCorrelation ID: "+CorrelationId;}}
}
public sealed class ConnectionPreviewSnapshot {
    public const string SampleBanner="SAMPLE CONNECTIONS PREVIEW — isolated layout fixtures, not current or retained connection reports. N/A = unavailable instrumentation.";
    public ConnectionPreviewState State;public DiagnosticPanel[] Overview,Network;
    public ConnectionPreviewSession[] Sessions=new ConnectionPreviewSession[0];public ConnectionPreviewFailure[] Failures=new ConnectionPreviewFailure[0];public ConnectionPreviewEvent[] Events=new ConnectionPreviewEvent[0];
    public string RawText="";
}
public interface IConnectionPreviewProvider { ConnectionPreviewSnapshot Read(ConnectionPreviewState state); }
public sealed class SampleConnectionPreviewProvider:IConnectionPreviewProvider {
    public static string Label(ConnectionPreviewState state){switch(state){case ConnectionPreviewState.AuthenticationFailed:return "Authentication failed";case ConnectionPreviewState.RelayFailed:return "Relay connection failed";case ConnectionPreviewState.HistoricalCompleted:return "Historical completed session";default:return state.ToString();}}
    static DiagnosticPanel Card(string title,params string[] pairs){return new DiagnosticPanel{Title=title,Metrics=Enumerable.Range(0,pairs.Length/2).Select(i=>new DiagnosticMetric(pairs[i*2],pairs[i*2+1])).ToArray()};}
    public ConnectionPreviewSnapshot Read(ConnectionPreviewState state){
        bool unavailable=state==ConnectionPreviewState.Unavailable||state==ConnectionPreviewState.Loading||state==ConnectionPreviewState.Empty;
        bool connected=state==ConnectionPreviewState.Connected||state==ConnectionPreviewState.Recovered;
        bool failed=state==ConnectionPreviewState.AuthenticationFailed||state==ConnectionPreviewState.RelayFailed;
        string status=Label(state)+" (sample)";string route=unavailable?"N/A":"Server relay · TCP / peer TLS (sample)";
        var snapshot=new ConnectionPreviewSnapshot{State=state,
          Overview=new[]{Card("Connection health","Current status",status,"Health summary",unavailable?"No sample measurements available":failed?"Selected sample attempt failed; no real connection was changed":connected?"Authenticated sample session established":"Sample lifecycle in progress or completed"),Card("Server and peer","Discovery connection",unavailable?"N/A":"Connected (sample)","Authenticated peer",unavailable?"N/A":state==ConnectionPreviewState.AuthenticationFailed?"Rejected (sample)":connected?"Verified (sample)":"Not established (sample)","Control transport",route,"WebRTC media transport","N/A — not inferred from control TLS"),Card("Sessions and recovery","Active established sessions",unavailable?"N/A":connected?"1 (sample)":"0 (sample)","Reconnect attempts",unavailable?"N/A":"2 (sample)","Recent failures",unavailable?"N/A":"2 sample failures (current / historical)")},
          Network=new[]{Card("Control connection — sample route","Route",route,"Direct / relay",unavailable?"N/A":"Relay (sample)","Selected control candidate",unavailable?"N/A":"example.invalid:7443 (sample, not contacted)","Establishment stages",unavailable?"N/A":"Discovery → authenticated attach → peer TLS → identity verification (sample)","TLS",unavailable?"N/A":connected?"Established (sample)":"Not established (sample)","Peer authentication",unavailable?"N/A":connected?"Verified (sample)":state==ConnectionPreviewState.AuthenticationFailed?"Rejected (sample)":"Pending / unavailable (sample)"),Card("WebRTC media — independent transport","Selected ICE candidate","N/A","UDP / TURN media route","N/A","DTLS-SRTP","N/A — no media instrumentation attached"),Card("Future network measurements","RTT","N/A","Bandwidth","N/A","Retransmissions","N/A","Packet loss","N/A","Worker wakeup latency","N/A")}};
        if(unavailable){snapshot.RawText=ConnectionPreviewSnapshot.SampleBanner+"\n[SAMPLE] "+Label(state)+"; no logs collected. Sensitive values are redacted.\nRTT=N/A bandwidth=N/A";return snapshot;}
        var date=new DateTime(2026,10,13,18,0,0,DateTimeKind.Utc);
        snapshot.Sessions=new[]{
          new ConnectionPreviewSession{Id="sample-session-003",Label=connected?"Established session 3":state==ConnectionPreviewState.HistoricalCompleted?"Historical completed session 3":"Connection attempt 3",Target="Example contact",Kind=connected?"Established DM control":state==ConnectionPreviewState.HistoricalCompleted?"Historical DM control session":"DM control attempt (not established)",Route=route,Outcome=connected?"Connected":state==ConnectionPreviewState.HistoricalCompleted?"Completed":Label(state),StartedUtc=date.AddMinutes(4),Duration=TimeSpan.FromSeconds(42),CorrelationId="sample-demand-A"},
          new ConnectionPreviewSession{Id="sample-attempt-002",Label="Reconnect attempt 2",Target="Example contact",Kind="Reconnect attempt (not established)",Route=route,Outcome="Relay connection failed",StartedUtc=date.AddMinutes(3),Duration=TimeSpan.FromSeconds(8),CorrelationId="sample-demand-A"},
          new ConnectionPreviewSession{Id="sample-session-001",Label="Completed session 1",Target="Example group",Kind="Historical group control session",Route="Direct TCP / peer TLS (sample)",Outcome="Completed",StartedUtc=date,Duration=TimeSpan.FromMinutes(2),CorrelationId="sample-demand-B"}};
        snapshot.Failures=new[]{new ConnectionPreviewFailure{Id="sample-failure-002",SessionId="sample-attempt-002",Target="Example contact",TimestampUtc=date.AddMinutes(3),Reason=state==ConnectionPreviewState.AuthenticationFailed?"Authentication rejected (sample)":"Relay pairing failed (sample)",Stage=state==ConnectionPreviewState.AuthenticationFailed?"Peer identity verification":"Relay attachment / pairing",Recovery=connected?"Recovered in sample-session-003":"Retry pending in preview only"},new ConnectionPreviewFailure{Id="sample-failure-001",SessionId="sample-session-001",Target="Example group",TimestampUtc=date.AddMinutes(1),Reason="Sample route interrupted",Stage="Control transport receive",Recovery="Historical session completed; no recovery action executed"}};
        var types=new[]{ConnectionPreviewEventType.Discovery,ConnectionPreviewEventType.Authentication,ConnectionPreviewEventType.TLS,ConnectionPreviewEventType.RouteSelection,ConnectionPreviewEventType.Disconnect,ConnectionPreviewEventType.Retry,ConnectionPreviewEventType.Discovery,ConnectionPreviewEventType.TLS,ConnectionPreviewEventType.Authentication,ConnectionPreviewEventType.Connect};var seconds=new[]{0,1,2,3,8,9,60,61,62,63};
        snapshot.Events=types.Select((type,i)=>new ConnectionPreviewEvent{Type=type,Severity=i==4?ConnectionPreviewSeverity.Error:i==5?ConnectionPreviewSeverity.Warning:ConnectionPreviewSeverity.Info,SessionId=i<6?"sample-attempt-002":"sample-session-003",AttemptId=i<6?"sample-attempt-002":"sample-attempt-003",CorrelationId="sample-demand-A",TimestampUtc=date.AddMinutes(3).AddSeconds(seconds[i]),Description="[SAMPLE] "+type+" lifecycle example"}).ToArray();
        snapshot.RawText=ConnectionPreviewSnapshot.SampleBanner+"\n[SAMPLE] credential=[REDACTED] token=[REDACTED] private_key=[REDACTED]\n"+String.Join("\n",snapshot.Events.Select(e=>e.DetailsUtc))+"\n[SAMPLE] RTT=N/A bandwidth=N/A. No crash or application logs collected.";
        return snapshot;
    }
}
public static class ConnectionPreviewQuery {
    public static ConnectionPreviewEvent[] Filter(ConnectionPreviewSnapshot snapshot,string severity,string type,string session){return snapshot.Events.Where(e=>(severity=="All"||e.Severity.ToString()==severity)&&(type=="All"||e.Type.ToString()==type)&&(session=="All"||e.SessionId==session)).OrderBy(e=>e.TimestampUtc).ToArray();}
    public static string Logs(ConnectionPreviewSnapshot snapshot,string query,bool structured){string text=structured?String.Join("\n",snapshot.Events.Select(e=>"[SAMPLE] utc="+e.TimestampUtc.ToString("o")+" severity="+e.Severity+" type="+e.Type+" session="+e.SessionId+" attempt="+e.AttemptId+" correlation="+e.CorrelationId+" description="+e.Description)):snapshot.RawText;var lines=text.Split(new[]{'\n'},StringSplitOptions.None);return ConnectionPreviewSnapshot.SampleBanner+"\nSensitive values: [REDACTED] · sample text only\n"+String.Join("\n",lines.Where(l=>String.IsNullOrEmpty(query)||l.IndexOf(query,StringComparison.OrdinalIgnoreCase)>=0));}
    public static string Export(ConnectionPreviewSnapshot snapshot){return ConnectionPreviewSnapshot.SampleBanner+"\nState: "+SampleConnectionPreviewProvider.Label(snapshot.State)+"\n\n"+String.Join("\n",snapshot.Overview.Concat(snapshot.Network).Select(p=>p.Title+"\n"+String.Join("\n",p.Metrics.Select(m=>m.Label+": "+m.Value))))+"\n\nSAMPLE sessions:\n"+String.Join("\n",snapshot.Sessions.Select(s=>s.DetailsUtc))+"\n\nSAMPLE failures:\n"+String.Join("\n",snapshot.Failures.Select(f=>f.DetailsUtc))+"\n\n"+snapshot.RawText;}
}
}
