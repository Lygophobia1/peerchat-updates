using System;
using System.Linq;
using System.Collections.Generic;
using System.Globalization;
namespace PeerChatDesktop {
// Passive projection of existing snapshots. No native operations, persistence or retry ownership.
public sealed class TransferDiagnosticIdentity {
    public readonly string Scope,Id,Peer;public readonly bool Sending;public readonly long RecipientAccount;
    public TransferDiagnosticIdentity(string scope,CoreAdapter.FileTransferData row){Scope=scope;Id=row.id;Peer=row.peer;Sending=row.sending;RecipientAccount=row.account;}
    public bool Matches(string scope,CoreAdapter.FileTransferData row){return row!=null&&Scope==scope&&Id==row.id&&Peer==row.peer&&Sending==row.sending&&RecipientAccount==row.account;}
    public string Key {get{return Scope+"/transfer:"+Id+"/peer:"+Peer+"/sending:"+Sending+"/recipient-account:"+RecipientAccount;}}
}
public sealed class TransferDiagnosticSample {public DateTime Utc;public double Mbps;}
public sealed class TransferDiagnosticHistory {
    public readonly List<TransferDiagnosticSample> Samples=new List<TransferDiagnosticSample>();
    public readonly List<string> Events=new List<string>();string key="",lastState="";DateTime last;
    public bool Paused;
    public void Clear(){key=lastState="";last=DateTime.MinValue;Samples.Clear();Events.Clear();Paused=false;}
    public void Observe(TransferDiagnosticIdentity identity,CoreAdapter.FileTransferData row,DateTime utc){
        if(identity==null||row==null)return;if(key!=identity.Key){Clear();key=identity.Key;}
        string state=row.state+":"+ConnectionsSafeText.Clean(row.error??"");
        if(state!=lastState){Events.Add(utc.ToString("o")+" observed state="+ChatAttachmentProjection.Status(row)+" transfer="+identity.Id+" peer="+identity.Peer+" sending="+row.sending+" confirmed="+row.done+" reason="+ConnectionsSafeText.Clean(row.error??""));if(Events.Count>64)Events.RemoveAt(0);lastState=state;}
        if(!Paused&&(utc-last).TotalSeconds>=1){last=utc;Samples.Add(new TransferDiagnosticSample{Utc=utc,Mbps=row.state==3?Math.Max(0,row.rateBits)/1000000.0:0});if(Samples.Count>300)Samples.RemoveAt(0);}
    }
}
public static class TransferDiagnosticProjection {
    public const string Missing="N/A — Not instrumented";
    static string Number(double n){return n.ToString("0.###",CultureInfo.InvariantCulture);}
    static DiagnosticMetric M(string label,string value){return new DiagnosticMetric(label,value);}
    static DiagnosticPanel P(string title,params DiagnosticMetric[] metrics){return new DiagnosticPanel{Title=title,Metrics=metrics};}
    public static Dictionary<string,long> Counters(string text,CoreAdapter.FileTransferData row){
        var result=new Dictionary<string,long>();if(row==null||row.isArchive)return result;
        // Exact ID + direction + size corroboration, never filename lookup or aggregate-line guessing.
        foreach(string line in (text??"").Split('\n').Take(64)){
            var tokens=line.Trim().Split(' ');if(!tokens.Contains("transfer="+row.id)||!tokens.Contains("sending="+(row.sending?"1":"0"))||!tokens.Contains("size="+row.size.ToString(CultureInfo.InvariantCulture)))continue;
            foreach(var token in tokens){int at=token.IndexOf('=');long n;if(at>0&&Int64.TryParse(token.Substring(at+1),NumberStyles.None,CultureInfo.InvariantCulture,out n))result[token.Substring(0,at)]=n;}break;
        }return result;
    }
    public static DiagnosticUiSnapshot Read(TransferDiagnosticIdentity identity,string scope,CoreAdapter.FileTransferData[] rows,string counters,string peerRoute,bool megabytes,TransferDiagnosticHistory history,string associatedPeerRoute="No active associated peer control session"){
        var matches=identity==null?new CoreAdapter.FileTransferData[0]:(rows??new CoreAdapter.FileTransferData[0]).Where(r=>identity.Matches(scope,r)).ToArray();
        var row=matches.Length==1?matches[0]:null;var c=Counters(counters,row);
        Func<string,string,string> metric=(key,unit)=>c.ContainsKey(key)?c[key].ToString(CultureInfo.InvariantCulture)+unit:Missing;
        string unavailable=identity==null?"Select an attachment's Open Diagnostics button":"Not retained or profile/account context changed";
        string rateUnit=megabytes?" MB/s":" Mbps";Func<double,string> rate=n=>Number(n/(megabytes?8:1))+rateUnit;
        long done=row==null?0:Math.Max(0,Math.Min(row.size,row.done));bool measured=row!=null&&row.statisticsKnown;
        string current=row==null?unavailable:rate(row.state==3?Math.Max(0,row.rateBits)/1000000.0:0);
        string average=measured&&row.durationMs>0?rate(row.statisticsBytes*0.008/row.durationMs):"Not retained / waiting for samples";
        string eta=row!=null&&row.state==3&&row.etaReliable&&row.rateBits>0&&row.size>done&&Math.Ceiling((row.size-done)*8.0/row.rateBits)<=604800?Number(Math.Ceiling((row.size-done)*8.0/row.rateBits))+" s":"N/A — ETA unreliable or transfer inactive";
        string integrity=row==null?unavailable:row.state==4?"SHA-256 verified; local completion committed":row.state==6||row.state==8?"Not completed; inspect failure reason":"Not completed — verification pending";
        string confirmed=row==null?unavailable:done+" / "+row.size+" bytes";
        var pages=new[]{
          new DiagnosticPage{Tab="Overview",Panels=new[]{P("Transfer progress",M("Original authenticated filename",row==null?unavailable:ConnectionsSafeText.Clean(row.name)),M("State",row==null?unavailable:ChatAttachmentProjection.Status(row)),M("Confirmed bytes / total",confirmed),M("Progress",row==null?unavailable:Number(row.size>0?100.0*done/row.size:row.state==4?100:0)+"%"),M("Current payload rate",current),M("Average payload rate",average),M("Elapsed / completed duration",measured?Number(row.durationMs/1000.0)+" s (end-to-end including preparation/wait/resume)":"Not retained"),M("ETA",eta)),P("Delivery perspective",M("Direction",row==null?unavailable:row.sending?"Sender — peer acknowledged bytes":"Receiver — durable bytes"),M("Peer / recipient",row==null?unavailable:row.peer+" / account reference "+row.account),M("Integrity",integrity),M("Completion summary",row!=null&&row.state==4&&measured?"Protected retained summary; supplemental to transfer state":"Not retained / not complete"),M("Failure reason",row==null?unavailable:String.IsNullOrEmpty(row.error)?"None":ConnectionsSafeText.Clean(row.error))) }},
          new DiagnosticPage{Tab="Network",Panels=new[]{P("Authenticated transport",M("Selected transfer route",row!=null&&row.state==3?peerRoute:"Not retained / no active transfer"),M("Associated peer control owner",ConnectionsSafeText.Clean(associatedPeerRoute)),M("Payload transport",row!=null?"Authenticated peer TLS; not WebRTC media":"No selected transfer"),M("Current confirmed application payload rate",current),M("Payload bytes submitted / received (session)",metric("payload_bytes"," bytes — not confirmed delivery")),M("Route establishment since observation",metric("route_delay_ms"," ms; zero may mean not observed")),M("First payload since observation",metric("first_payload_delay_ms"," ms; zero may mean not observed")),M("Route changes (session)",metric("route_changes"," (includes initial route)")),M("Backpressure observations",metric("backpressure"," ticks")),M("Wire bitrate / RTT / packet loss",Missing))}},
          new DiagnosticPage{Tab="Pipeline",Panels=new[]{P("Chunk concurrency",M("Protocol chunk maximum (read-only)","32,768 bytes"),M("Confirmed full chunks",row==null?unavailable:(done/32768).ToString()+"; final partial chunk counted in bytes"),M("Total payload chunks",row==null?unavailable:((row.size+32767)/32768).ToString()),M("Maximum payload in-flight chunks (read-only)","1 — existing ACK-gated TransferManager"),M("Actual in-flight chunks",Missing),M("ACK gating observations",metric("ack_wait_ticks"," worker ticks — not elapsed ACK wait")),M("Last ACK runtime clock",metric("last_ack_ms"," ms (monotonic clock; not ACK latency)")),M("ACK latency / retries / timeout count / queue depth",Missing)),P("Advanced stage timing — selected transfer, current runtime",M("Runtime observation duration",metric("elapsed_ms"," ms; current process only")),M("Read",metric("read_us"," µs cumulative")),M("Hash",metric("hash_us"," µs cumulative")),M("Local storage write",metric("write_us"," µs cumulative")),M("Local flush",metric("flush_us"," µs cumulative")),M("Local atomic commit",metric("commit_us"," µs cumulative")),M("Encrypt / decrypt / socket write timing",Missing)),P("TransferManager owner — all local DM transfers",M("Scope","Profile/account runtime owner; not the selected file"),M("Journal persistence",metric("journal_all_transfers_us"," µs cumulative across ALL transfers"))),P("Confirmed throughput graph",M("Source","Observed native RateMeter samples while this context is open; no pre-open history"))}},
          new DiagnosticPage{Tab="Receiver",Panels=new[]{P("Durable receipt",M("Receiver location",row==null?unavailable:row.sending?"Remote — receiver processing costs not reported":"This Windows device"),M("Confirmed receive / ACK progress",confirmed),M("Integrity validation",integrity),M("Local receiver write",row!=null&&!row.sending?metric("write_us"," µs cumulative"):Missing),M("Local receiver flush",row!=null&&!row.sending?metric("flush_us"," µs cumulative"):Missing),M("Local receiver atomic commit",row!=null&&!row.sending?metric("commit_us"," µs cumulative"):Missing),M("Remote processing / receiver queue / completion ACK timing",Missing))}},
          new DiagnosticPage{Tab="Pi Relay",Panels=new[]{P("Relay measurements — attribution boundary",M("Exact transfer route",row!=null&&row.state==3?peerRoute:"Not retained / no active transfer"),M("Associated peer control owner",ConnectionsSafeText.Clean(associatedPeerRoute)),M("Pi host identity / pairing ID / attachment role",Missing),M("Pi RX/TX / resource usage / queue / wake latency",Missing),M("Boundary","Application payload rate is not Pi wire throughput; no server telemetry endpoint is queried"))}},
          new DiagnosticPage{Tab="Events",Panels=new[]{P("Bounded observed lifecycle — not historical reconstruction",M("Exact transfer context",identity==null?unavailable:ConnectionsSafeText.Clean(identity.Key)),M("Observation history",row==null?unavailable:history==null||history.Events.Count==0?"Not retained — no observations yet":String.Join("\n",history.Events)),M("Historical offer / accept / retry timeline","Not retained by this view; no events reconstructed"))}},
          new DiagnosticPage{Tab="Settings",Panels=new[]{P("Display preferences",M("Rate units",megabytes?"MB/s (decimal)":"Mbps (decimal)"),M("Graph range","Up to 5 minutes of observed samples; bounded to 300"),M("Protocol limits (read-only)","256 MiB file maximum; 32 KiB chunks; 4 active transfers"),M("No protocol changes","Graph pause, follow and units affect this window only"))}}
        };
        pages[2].Panels[1].Advanced=true;pages[2].Panels[2].Advanced=true;
        return new DiagnosticUiSnapshot{Sample=false,Context=new DiagnosticContext(DiagnosticSection.FileTransfers,identity==null?"":identity.Id),Banner="REAL FILE TRANSFER SNAPSHOT · "+(row==null?unavailable:"confirmed "+(row.sending?"ACK":"durable receive")+" bytes; runtime costs are not historical summaries"),Pages=pages};
    }
    public static string Export(DiagnosticUiSnapshot s){return ConnectionsSafeText.Clean("PeerChat transfer diagnostics\nBuild: "+PeerChat.VersionInfo.Display+"\nCaptured UTC: "+DateTime.UtcNow.ToString("o")+"\n"+s.Banner+"\n"+String.Join("\n",s.Pages.SelectMany(p=>p.Panels.Select(panel=>p.Tab+" / "+panel.Title+"\n"+String.Join("\n",panel.Metrics.Select(m=>m.Label+": "+m.Value))))));}
}
}
