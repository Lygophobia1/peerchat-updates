using System;
using System.Linq;
using System.Collections.Generic;
namespace PeerChatDesktop {
// UI contracts only. Providers cannot mutate calls, transfers, authentication or history.
public enum DiagnosticSection { FileTransfers, DirectMessageCalls, GroupCalls, HistorySync, Connections }
public enum DiagnosticExampleState { Active, Completed, Stalled, Failed, Disconnected, Unavailable }
public sealed class DiagnosticContext {
    public readonly DiagnosticSection Section; public readonly string StableId;
    public DiagnosticContext(DiagnosticSection section,string stableId){Section=section;StableId=stableId??"";}
}
public sealed class DiagnosticMetric {
    public readonly string Label,Value,Explanation;
    public DiagnosticMetric(string label,string value,string explanation=""){Label=label;Value=value??"N/A";Explanation=explanation;}
}
public sealed class DiagnosticPanel {
    public string Title; public bool Advanced; public DiagnosticMetric[] Metrics=new DiagnosticMetric[0]; public double?[] Samples=new double?[0];
}
public sealed class DiagnosticPage {
    public string Tab; public DiagnosticPanel[] Panels=new DiagnosticPanel[0];
}
public sealed class DiagnosticUiSnapshot {
    public DiagnosticContext Context;public bool Sample;public string Banner;public DiagnosticExampleState State;public DiagnosticPage[] Pages;
}
public interface IDiagnosticLayoutProvider { DiagnosticUiSnapshot Read(DiagnosticContext context,DiagnosticExampleState state); }
public static class DiagnosticNavigation {
    public static string Label(DiagnosticSection section){return new[]{"File Transfers","Direct Message Calls","Group Calls","History Sync","Connections"}[(int)section];}
    public static string[] Tabs(DiagnosticSection section){switch(section){
        case DiagnosticSection.FileTransfers:return new[]{"Overview","Network","Pipeline","Receiver","Pi Relay","Events","Settings"};
        case DiagnosticSection.DirectMessageCalls:return new[]{"Overview","Voice","Screen Share","Network","Events","Settings"};
        case DiagnosticSection.GroupCalls:return new[]{"Overview","Screen Share","Voice","Participants","Events","Export"};
        case DiagnosticSection.Connections:return new[]{"Overview","Sessions","Network","Failures","Events","Raw Logs"};
        default:return new[]{"Overview","Connections","Records","Events"};}}
}
public sealed class SampleDiagnosticLayoutProvider:IDiagnosticLayoutProvider {
    static DiagnosticPanel Panel(string title,params string[] labels){return new DiagnosticPanel{Title=title,Metrics=labels.Select(l=>new DiagnosticMetric(l,"N/A","Not connected to a measurement provider in this layout milestone.")).ToArray()};}
    public DiagnosticUiSnapshot Read(DiagnosticContext context,DiagnosticExampleState state){
        if(context.Section==DiagnosticSection.GroupCalls||context.Section==DiagnosticSection.Connections)throw new InvalidOperationException("This section has a dedicated provider.");
        var pages=DiagnosticNavigation.Tabs(context.Section).Select(t=>new DiagnosticPage{Tab=t}).ToArray();
        Action<string,DiagnosticPanel[]> set=(tab,panels)=>pages.Single(p=>p.Tab==tab).Panels=panels;
        if(context.Section==DiagnosticSection.FileTransfers){
            set("Overview",new[]{Panel("Transfer progress","State","Confirmed bytes / total","Progress","Current Mbps","Average Mbps","ETA","Total duration"),Panel("Delivery perspective","Sender / receiver","Recipient","Per-recipient progress","Aggregate confirmed payload Mbps")});
            set("Network",new[]{Panel("Authenticated transport","Route","Control transport","Payload transport","Route establishment","First payload byte","Reconnects","Backpressure")});
            var chunks=Panel("Chunk concurrency","Chunk index","Chunk size","Total chunks","Maximum in-flight chunks","Actual in-flight chunks","ACK wait","Retransmissions","Queue depth");
            var timing=Panel("Advanced stage timing","Read","Hash","Encrypt","Socket write","ACK pacing","Receive decrypt","Storage write","Flush","Atomic commit","Journal persistence");timing.Advanced=true;
            set("Pipeline",new[]{chunks,timing,Panel("Confirmed throughput graph","Rolling Mbps","Acknowledged / durably received bytes")});
            set("Receiver",new[]{Panel("Durable receipt","Verified received bytes","Write queue depth","Integrity validation","Finalization","Completion ACK","Restart journal"),Panel("Receiver processing graph","Storage write ms","Flush ms")});
            set("Pi Relay",new[]{Panel("Relay measurements — not instrumented","Pairing state","Attach role","Relay queue depth","Relay throughput","Worker wake latency","Socket backpressure")});
        }else if(context.Section==DiagnosticSection.DirectMessageCalls){
            set("Overview",new[]{Panel("Call session","State","Sender / receiver","Sharer / viewer","Verified peer","Control route","Media route"),Panel("Media summary","Opus continuity","Captured FPS","Encoded FPS","Received FPS","Decoded FPS","Presented FPS")});
            set("Voice",new[]{Panel("Opus and playout","Codec","Encode ms","Decode ms","Capture-to-playout ms","Jitter buffer","Concealment","Underruns"),Panel("Audio processing","AEC","AEC delay estimate","Capture queue","Playback queue"),Panel("Voice graph","Submitted payload Mbps","Received payload Mbps")});
            set("Screen Share",new[]{Panel("Sharer pipeline","Backend","Capture target FPS","Captured FPS","Encoded FPS","Encoder time","Capture age"),Panel("Viewer pipeline","Received packets/s","Decoded FPS","Presented FPS","Decoder queue","Frame age","Dropped stale frames"),Panel("Screen frame-rate graph","Encoded FPS","Presented FPS")});
            set("Network",new[]{Panel("Control versus media","Authenticated control TLS route","WebRTC media route","DTLS-SRTP","TURN / direct","RTT","Packet loss","Congestion / pacing")});
        }else{
            set("Overview",new[]{Panel("Current synchronization activity","State","Source / reader role","Active source","Records provided (queued incl retries)","Messages imported","Conversations completed","Unresolved records"),Panel("Last completed result — separate from current activity","Outcome","Completed UTC","Messages imported","Conversations completed","Unresolved records")});
            set("Connections",new[]{Panel("Archive route","Authorized scope","Source","Reader","Winner","Attempt","Attachment role","Retry / backoff","Cleanup reason")});
            set("Records",new[]{Panel("Verified durable imports","Records enumerated","Records provided","Messages imported","Durable cursor","Duplicates prevented","Unresolved provenance","Partial completion")});
        }
        set("Events",new[]{Panel("Bounded events","Current state transition","Failure stage","Retry reason","Context identity")});
        if(pages.Any(p=>p.Tab=="Settings"))set("Settings",new[]{Panel("Display preferences","Metric text size","Compact cards","Graph display","Pause graphs"),Panel("Protocol configuration is read-only","Chunk sizes / limits","Pacing","Encryption","No settings here change transfer or call behavior")});
        bool available=state!=DiagnosticExampleState.Unavailable&&state!=DiagnosticExampleState.Disconnected;
        foreach(var panel in pages.SelectMany(p=>p.Panels)){
            panel.Metrics=panel.Metrics.Select(m=>new DiagnosticMetric(m.Label,m.Label=="State"?state.ToString():m.Value,m.Explanation)).ToArray();
            if(available&&panel.Title.Contains("graph"))panel.Samples=Enumerable.Range(0,24).Select(i=>(double?)(state==DiagnosticExampleState.Stalled||state==DiagnosticExampleState.Failed?0:8+Math.Sin(i*.6)*2)).ToArray();
        }
        if(context.Section==DiagnosticSection.FileTransfers&&available){var p=pages[0].Panels[0];p.Metrics=new[]{new DiagnosticMetric("State",state.ToString()),new DiagnosticMetric("Confirmed bytes / total",state==DiagnosticExampleState.Completed?"10 MiB / 10 MiB":"4 MiB / 10 MiB"),new DiagnosticMetric("Progress",state==DiagnosticExampleState.Completed?"100%":"40%"),new DiagnosticMetric("Current Mbps",state==DiagnosticExampleState.Active?"8.0 (sample)":"0 (sample)"),new DiagnosticMetric("Average Mbps","7.5 (sample)"),new DiagnosticMetric("ETA",state==DiagnosticExampleState.Active?"6 s (sample)":"N/A"),new DiagnosticMetric("Total duration","11 s (sample)")};}
        if(available){
            var examples=new Dictionary<string,string>{{"Route","Authenticated TCP/TLS relay (sample)"},{"Control transport","Peer TLS (sample)"},{"Payload transport","Peer TLS (sample)"},{"Chunk index","42 (sample)"},{"Chunk size","32 KiB (sample; not a configured value)"},{"Total chunks","320 (sample)"},{"Maximum in-flight chunks","8 (sample; not a configured limit)"},{"Actual in-flight chunks",state==DiagnosticExampleState.Active?"6 (sample)":"0 (sample)"},{"ACK wait","12 ms (sample)"},{"Retransmissions","1 (sample)"},{"Queue depth","2 (sample)"},{"Codec","Opus (sample)"},{"AEC","Enabled (sample)"},{"Capture target FPS","30 (sample)"},{"Captured FPS","30 (sample)"},{"Encoded FPS","29 (sample)"},{"Presented FPS","28 (sample)"},{"Sender / receiver","Sender preview (sample)"},{"Sharer / viewer","Sharer preview (sample)"}};
            foreach(var panel in pages.SelectMany(p=>p.Panels))panel.Metrics=panel.Metrics.Select(m=>examples.ContainsKey(m.Label)?new DiagnosticMetric(m.Label,examples[m.Label],m.Explanation):m).ToArray();
            if(context.Section==DiagnosticSection.HistorySync){
                pages[0].Panels[0].Metrics=new[]{new DiagnosticMetric("State",state==DiagnosticExampleState.Active?"Importing (sample)":state==DiagnosticExampleState.Completed?"Idle (sample)":state+" (sample)"),new DiagnosticMetric("Source / reader role","Reader (sample)"),new DiagnosticMetric("Active source","Approved desktop (sample)"),new DiagnosticMetric("Records provided (queued incl retries)","N/A — separate source role"),new DiagnosticMetric("Messages imported",state==DiagnosticExampleState.Completed?"22 (sample)":"12 (sample)"),new DiagnosticMetric("Conversations completed","1 (sample)"),new DiagnosticMetric("Unresolved records","0 (sample)")};
                pages[0].Panels[1].Metrics=new[]{new DiagnosticMetric("Outcome","Full (prior sample result, independent of current activity)"),new DiagnosticMetric("Completed UTC","Example completed run"),new DiagnosticMetric("Messages imported","22 (sample)"),new DiagnosticMetric("Conversations completed","2 (sample)"),new DiagnosticMetric("Unresolved records","0 (sample)")};
            }
        }
        return new DiagnosticUiSnapshot{Context=context,State=state,Sample=true,Banner="SAMPLE / LAYOUT PREVIEW — not measurements of the selected context. N/A means no instrumentation is connected.",Pages=pages};
    }
}
}
