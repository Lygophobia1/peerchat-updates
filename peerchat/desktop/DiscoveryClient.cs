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
public sealed partial class DiscoveryClient {
    readonly JavaScriptSerializer json=new JavaScriptSerializer { MaxJsonLength=16*1024 };
    public string Host, CertificateSha256, SessionToken, Username, RegisteredPublicIdentity;
    public long AccountId;
    public int Port;
    // Called only for an unpinned development server or a server identity replacement. The caller
    // must present an explicit UI confirmation; this client never silently trusts first contact.
    readonly Func<string,int,string,bool,bool> certificateTrustPrompt;
    readonly object rpcGate=new object();
    TcpClient rpcTcp;SslStream rpcTls;Timer rpcIdle;
    bool supportsRpc;string rpcDevice="",rpcBinding="";long rpcOpened,rpcUsed;int rpcCount;
    internal int RpcConnectionsOpened {get;private set;}
    readonly object rpcTimingGate=new object();
    readonly Queue<string> rpcTimings=new Queue<string>();long rpcTimingCount;
    static string TimingId(string value){return value!=null&&value.Length==32&&value.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f')?value:"none";}
    static long TimingUs(long ticks){return (long)(Math.Max(0,ticks)*1000000.0/System.Diagnostics.Stopwatch.Frequency);}
    internal void RecordRpcTiming(string operation,string request,string command,long queue,long opened,long exchange,long total,bool success,long connect=0,long authenticate=0){
        string kind=AttachmentCacheOperation(operation)||new[]{"mailbox_register","mailbox_revoke","mailbox_keys","mailbox_store","mailbox_fetch","mailbox_ack","mailbox_receipts","mailbox_eligibility_snapshot","mailbox_eligibility_validate","friend_state","search","resolve","turn_credentials"}.Contains(operation)?operation:"other";
        string row="rpc_request_id="+TimingId(request)+" command_id="+TimingId(command)+" operation="+kind+" queue_us="+TimingUs(queue)+" tcp_connect_us="+TimingUs(connect)+" tls_auth_us="+TimingUs(authenticate)+" open_tls_ready_us="+TimingUs(opened)+" exchange_us="+TimingUs(exchange)+" total_us="+TimingUs(total)+" result="+(success?"ok":"failed");
        lock(rpcTimingGate){rpcTimingCount++;while(rpcTimings.Count>=32)rpcTimings.Dequeue();rpcTimings.Enqueue(row);}
    }
    public string RpcTimingDiagnostics {get{lock(rpcTimingGate)return "rpc_timing_count="+rpcTimingCount+" retained="+rpcTimings.Count+" units=local_monotonic_us exchange_includes_server_and_network_not_RTT\n"+String.Join("\n",rpcTimings);}}
    void CloseRpcLocked(){if(rpcIdle!=null){rpcIdle.Dispose();rpcIdle=null;}try{if(rpcTcp!=null)rpcTcp.Close();}catch{}try{if(rpcTls!=null)rpcTls.Dispose();}catch{}rpcTcp=null;rpcTls=null;rpcBinding="";}
    public void CloseRpc(){lock(rpcGate)CloseRpcLocked();}
    Dictionary<string,object> Rpc(string type,string device,Dictionary<string,object> fields,string expectedSession=null,string expectedCertificate=null,long expectedAccount=0){
        long started=System.Diagnostics.Stopwatch.GetTimestamp(),queue=0,opened=0,exchange=0,phase=0,connect=0,authenticate=0;string traceId=null;bool success=false;
        if(!Monitor.TryEnter(rpcGate,10000)){long ended=System.Diagnostics.Stopwatch.GetTimestamp();RecordRpcTiming(type,null,null,ended-started,0,0,ended-started,false);throw new IOException("RPC serialization deadline");}
        queue=System.Diagnostics.Stopwatch.GetTimestamp()-started;
        try {
            Action validateAttachment=()=>{if(expectedSession!=null&&(SessionToken!=expectedSession||AccountId!=expectedAccount||!String.Equals(CertificateSha256,expectedCertificate,StringComparison.OrdinalIgnoreCase)))throw new InvalidOperationException("Attachment authenticated context changed.");};
            validateAttachment();
            long now=System.Diagnostics.Stopwatch.GetTimestamp();long frequency=System.Diagnostics.Stopwatch.Frequency;
            string binding=Host+"|"+Port+"|"+CertificateSha256+"|"+SessionToken+"|"+device;
            if(rpcTls!=null&&(binding!=rpcBinding||now-rpcUsed>=10*frequency||now-rpcOpened>=100*frequency||rpcCount>=60||
                (rpcTcp.Client.Poll(0,SelectMode.SelectRead)&&rpcTcp.Available==0)))CloseRpcLocked();
            try {
                if(rpcTls==null){
                    phase=System.Diagnostics.Stopwatch.GetTimestamp();
                    using(var opening=new Timer(_=>{try{if(rpcTcp!=null)rpcTcp.Close();}catch{}},null,10000,Timeout.Infinite))rpcTls=OpenAuthenticatedStream(out rpcTcp,false,(tcpTicks,tlsTicks)=>{connect=tcpTicks;authenticate=tlsTicks;});RpcConnectionsOpened++;
                    rpcTls.ReadTimeout=10000;rpcTls.WriteTimeout=10000;
                    validateAttachment();
                    WriteFrame(rpcTls,Encoding.UTF8.GetBytes(channelJson.Serialize(new Dictionary<string,object>{{"version",1},{"type","rpc_open"},{"sessionToken",expectedSession??SessionToken},{"publicIdentity",device}})));
                    var ready=channelJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(rpcTls)));
                    if(Text(ready,"type")!="rpc_ready"||Number(ready,"rpcVersion")!=1)throw new IOException("RPC authorization rejected");
                    rpcBinding=binding;rpcOpened=System.Diagnostics.Stopwatch.GetTimestamp();rpcCount=0;
                    opened=System.Diagnostics.Stopwatch.GetTimestamp()-phase;phase=0;
                }
                if(rpcIdle!=null)rpcIdle.Change(Timeout.Infinite,Timeout.Infinite);
                // A bounded socket abort covers blocked writes as well as reads.
                var socket=rpcTcp;using(var deadline=new Timer(_=>{try{socket.Close();}catch{}},null,10000,Timeout.Infinite)){
                    validateAttachment();
                    string id=Guid.NewGuid().ToString("N");traceId=id;var request=new Dictionary<string,object>(fields);
                    request["version"]=1;request["type"]=type;request["rpcRequestId"]=id;if(type.StartsWith("mailbox_",StringComparison.Ordinal))request["requestId"]=id;request["sessionToken"]=expectedSession??SessionToken;request["publicIdentity"]=device;
                    var bytes=Encoding.UTF8.GetBytes(channelJson.Serialize(request));
                    if(bytes.Length>(type.StartsWith("mailbox_",StringComparison.Ordinal)||AttachmentCacheOperation(type)?100000:4096))throw new InvalidDataException("RPC request exceeds bound");
                    phase=System.Diagnostics.Stopwatch.GetTimestamp();WriteFrame(rpcTls,bytes);
                    var reply=channelJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(rpcTls,100000)));
                    exchange=System.Diagnostics.Stopwatch.GetTimestamp()-phase;phase=0;
                    if(Text(reply,"type")!="rpc_result"||Text(reply,"rpcRequestId")!=id||Text(reply,"operation")!=type)throw new InvalidDataException("RPC correlation failed");
                    var body=reply["body"] as Dictionary<string,object>;if(body==null)throw new InvalidDataException("RPC body missing");
                    rpcUsed=System.Diagnostics.Stopwatch.GetTimestamp();rpcCount++;
                    if(!reply.ContainsKey("more")||!Boolean.Equals(reply["more"],true))CloseRpcLocked();
                    else {if(rpcIdle==null)rpcIdle=new Timer(_=>{lock(rpcGate)CloseRpcLocked();},null,10000,Timeout.Infinite);else rpcIdle.Change(10000,Timeout.Infinite);}
                    validateAttachment();success=true;return body;
                }
            }catch{CloseRpcLocked();throw;} // Never replay an ambiguous operation.
        }finally{try{long ended=System.Diagnostics.Stopwatch.GetTimestamp();if(phase!=0){if(traceId==null)opened=ended-phase;else exchange=ended-phase;}RecordRpcTiming(type,traceId,fields==null?null:Text(fields,"requestId"),queue,opened,exchange,ended-started,success,connect,authenticate);}catch(Exception error){if(error is OutOfMemoryException)throw;}finally{Monitor.Exit(rpcGate);}}
    }
    readonly object eventGate=new object();
    readonly PresenceSequence presenceSequence=new PresenceSequence();
    public string PresenceEpoch {get;private set;}
    public List<DiscoverySearchResult> InitialPresenceSnapshot {get;private set;}
    readonly JavaScriptSerializer channelJson=new JavaScriptSerializer{MaxJsonLength=100000};
    readonly Queue<Dictionary<string,object>> groupChannelOutput=new Queue<Dictionary<string,object>>();
    readonly Dictionary<string,Dictionary<string,object>> groupChannelPending=new Dictionary<string,Dictionary<string,object>>(StringComparer.Ordinal);
    readonly Queue<Dictionary<string,object>> roomOutput=new Queue<Dictionary<string,object>>();
    readonly Dictionary<string,string> roomPending=new Dictionary<string,string>(StringComparer.Ordinal);
    readonly HashSet<string> roomTargets=new HashSet<string>(StringComparer.Ordinal);
    readonly Dictionary<string,GroupMediaState> roomStates=new Dictionary<string,GroupMediaState>(StringComparer.Ordinal);
    readonly Dictionary<string,string> roomResults=new Dictionary<string,string>(StringComparer.Ordinal);
    readonly Queue<Dictionary<string,object>> mediaOutput=new Queue<Dictionary<string,object>>();
    readonly Dictionary<string,DateTime> mediaPending=new Dictionary<string,DateTime>(StringComparer.Ordinal);
    readonly Queue<GroupMediaSignal> mediaSignals=new Queue<GroupMediaSignal>();
    readonly Dictionary<string,string> mediaResults=new Dictionary<string,string>(StringComparer.Ordinal);
    public string GroupMediaResult(string requestId){lock(eventGate){string state;return mediaResults.TryGetValue(requestId,out state)?state:"Pending";}}
    static IEnumerable<GroupRoomDevice> MediaDevices(GroupRoom room){return room.Kind=="screen_share"&&room.Sharer!=null?room.Members.Concat(new[]{room.Sharer}):room.Members;}
    static bool MediaRoles(GroupRoom room,GroupRoomDevice source,GroupRoomDevice target){return room.Kind=="voice"||room.Kind=="screen_share"&&room.Sharer!=null&&(source==room.Sharer)!=(target==room.Sharer);}
    public GroupMediaSignal TakeGroupMediaSignal(string group=null,string roomId=null){lock(eventGate){
        int count=mediaSignals.Count;
        for(int i=0;i<count;i++){
            var signal=mediaSignals.Dequeue();GroupMediaState state;
            if((group!=null&&signal.GroupId!=group)||(roomId!=null&&signal.RoomId!=roomId)){if(DateTime.UtcNow-signal.ReceivedUtc<=TimeSpan.FromSeconds(10))mediaSignals.Enqueue(signal);continue;}
            if(DateTime.UtcNow-signal.ReceivedUtc>TimeSpan.FromSeconds(10)||!roomStates.TryGetValue(signal.GroupId,out state)||state.Epoch!=signal.Epoch||state.RosterGeneration>signal.RosterGeneration)continue;
            // A broadcast to another member can race this targeted frame. Wait
            // boundedly for the authoritative snapshot, never infer membership.
            if(signal.Revision>state.Revision||signal.RosterGeneration>state.RosterGeneration){mediaSignals.Enqueue(signal);continue;}
            var room=state.Rooms.FirstOrDefault(r=>r.Id==signal.RoomId);
            if(room==null)continue;
            var source=MediaDevices(room).FirstOrDefault(d=>d.AccountId==signal.SourceAccountId&&d.PeerId==signal.SourceDevice&&d.MediaGeneration==signal.SourceGeneration);
            var target=MediaDevices(room).FirstOrDefault(d=>d.AccountId==AccountId&&d.PeerId==eventPublicIdentity&&d.MediaGeneration==signal.TargetGeneration);
            if(source!=null&&target!=null&&MediaRoles(room,source,target)&&signal.TargetAccountId==AccountId&&signal.TargetDevice==eventPublicIdentity)return signal;
        }
        return null;
    }}
    // Called by the media owner, not WPF. Authoritative snapshots supply identity
    // and participation generations; no friend relationship is manufactured.
    public string QueueGroupMediaSignal(string group,string room,long targetAccount,string targetDevice,string route,long generation,string kind,string data,string mid="0"){
        lock(eventGate){
            GroupMediaState state;if(eventStop||!EventChannelConnected||mediaOutput.Count+mediaPending.Count>=64||!roomStates.TryGetValue(group,out state))return null;
            var voice=state.Rooms.FirstOrDefault(r=>r.Id==room);if(voice==null)return null;
            var source=MediaDevices(voice).FirstOrDefault(d=>d.AccountId==AccountId&&d.PeerId==eventPublicIdentity);
            var target=MediaDevices(voice).FirstOrDefault(d=>d.AccountId==targetAccount&&d.PeerId==targetDevice);
            if(source==null||target==null||source==target||!MediaRoles(voice,source,target)||source.MediaGeneration.Length!=32||target.MediaGeneration.Length!=32||route==null||route.Length!=32||generation<=0||mid==null||mid.Length>32||data==null||
                (kind!="offer"&&kind!="answer"&&kind!="candidate"&&kind!="close")||data.Length>(kind=="candidate"?4096:32768)||(kind!="close"&&String.IsNullOrWhiteSpace(data))||(kind=="close"&&data.Length!=0))return null;
            string id=Guid.NewGuid().ToString("N");
            mediaOutput.Enqueue(new Dictionary<string,object>{{"type","group_media"},{"requestId",id},{"groupId",group},{"roomId",room},{"epoch",state.Epoch},{"revision",state.Revision},{"rosterGeneration",state.RosterGeneration},{"sourceGeneration",source.MediaGeneration},{"targetGeneration",target.MediaGeneration},{"targetAccountId",targetAccount},{"targetDevice",targetDevice},{"routeId",route},{"mediaGeneration",generation},{"kind",kind},{"data",data},{"mid",mid}});
            return id;
        }
    }
    void DrainMediaOutput(SslStream tls){for(int i=0;i<8;i++){
        Dictionary<string,object> request;lock(eventGate){
            foreach(string id in mediaPending.Where(p=>DateTime.UtcNow-p.Value>TimeSpan.FromSeconds(15)).Select(p=>p.Key).ToArray()){
                mediaPending.Remove(id);if(mediaResults.Count>=128)mediaResults.Clear();mediaResults[id]="signaling_timeout";
            }
            if(mediaOutput.Count==0)return;request=mediaOutput.Dequeue();mediaPending[Text(request,"requestId")]=DateTime.UtcNow;
        }
        WriteFrame(tls,Encoding.UTF8.GetBytes(channelJson.Serialize(request)));
    }}
    bool MediaNotification(Dictionary<string,object> response){
        string type=Text(response,"type");if(type!="group_media_signal"&&type!="group_media_result")return false;
        if(type=="group_media_result"){
            lock(eventGate){string id=Text(response,"requestId");if(!mediaPending.Remove(id))return true;
                if(mediaResults.Count>=128)mediaResults.Clear();mediaResults[id]=Text(response,"state");
                object raw;if(response.TryGetValue("snapshot",out raw)){
                    var next=GroupMediaState.Parse((Dictionary<string,object>)raw);GroupMediaState current;
                    if(roomStates.TryGetValue(next.GroupId,out current)&&current.Accepts(next))roomStates[next.GroupId]=next;
                }
            }
        }else{
            var signal=new GroupMediaSignal(response);
            lock(eventGate){GroupMediaState state;if(!roomStates.TryGetValue(signal.GroupId,out state)||state.Epoch!=signal.Epoch||signal.TargetAccountId!=AccountId||signal.TargetDevice!=eventPublicIdentity)return true;
                if(mediaSignals.Count<128)mediaSignals.Enqueue(signal);
            }
        }
        if(eventHandler!=null)eventHandler(new DiscoveryEvent{Event=type,GroupId=Text(response,"groupId"),State=Text(response,"state")});
        return true;
    }
    readonly Dictionary<string,string> roomMutations=new Dictionary<string,string>(StringComparer.Ordinal),roomRejections=new Dictionary<string,string>(StringComparer.Ordinal);
    public string GroupRoomDiagnostics(string group){lock(eventGate){string mutation,rejection;roomMutations.TryGetValue(group,out mutation);roomRejections.TryGetValue(group,out rejection);return "Last room result: "+GroupRoomResult(group)+"\nLast room mutation: "+(mutation??"None")+"\nLast room rejection: "+(rejection??"None");}}
    public GroupMediaState GroupRooms(string group){lock(eventGate){GroupMediaState state;return roomStates.TryGetValue(group,out state)?state:null;}}
    public string GroupRoomResult(string group){lock(eventGate){string state;return roomResults.TryGetValue(group,out state)?state:"Not requested";}}
    public void ReconcileGroupRoomTargets(IEnumerable<string> groups){lock(eventGate){var keep=new HashSet<string>(groups,StringComparer.Ordinal);foreach(string id in roomTargets.Where(id=>!keep.Contains(id)).ToArray()){roomTargets.Remove(id);roomStates.Remove(id);roomResults.Remove(id);roomMutations.Remove(id);roomRejections.Remove(id);}}}
    public bool QueueGroupRoom(string group,string operation,string room="",string kind="",string name=""){
        lock(eventGate){
            if(eventStop||!EventChannelConnected||roomOutput.Count+roomPending.Count>=256||roomTargets.Count>=256&&!roomTargets.Contains(group))return false;
            GroupMediaState state;roomStates.TryGetValue(group,out state);
            if(operation!="snapshot"&&state==null)return false;
            roomTargets.Add(group);string id=Guid.NewGuid().ToString("N");
            var request=new Dictionary<string,object>{{"type","group_room"},{"requestId",id},{"groupId",group},{"operation",operation},{"roomId",room},{"kind",kind},{"name",name}};
            if(state!=null){request["epoch"]=state.Epoch;request["revision"]=state.Revision;request["rosterGeneration"]=state.RosterGeneration;}
            roomOutput.Enqueue(request);roomResults[group]="Pending: "+operation;return true;
        }
    }
    void DrainRoomOutput(SslStream tls){for(int i=0;i<8;i++){
        Dictionary<string,object> request;lock(eventGate){if(roomOutput.Count==0)return;request=roomOutput.Dequeue();roomPending[Text(request,"requestId")]=Text(request,"groupId");}
        WriteFrame(tls,Encoding.UTF8.GetBytes(channelJson.Serialize(request)));
    }}
    void RoomChannelReady(){lock(eventGate){foreach(string group in roomTargets.ToArray())QueueGroupRoom(group,"snapshot");}}
    void RoomChannelLost(){lock(eventGate){
        foreach(string group in roomTargets)roomResults[group]="Disconnected; explicit rejoin required";
        roomStates.Clear();roomOutput.Clear();roomPending.Clear();
        mediaOutput.Clear();mediaPending.Clear();mediaSignals.Clear();mediaResults.Clear();
    }}
    bool RoomNotification(Dictionary<string,object> response){
        string type=Text(response,"type");if(type!="group_room_snapshot"&&type!="group_room_result")return false;
        string group=Text(response,"groupId"),result=Text(response,"state");
        GroupMediaState snapshot=null;object raw;
        if(response.TryGetValue("snapshot",out raw))snapshot=GroupMediaState.Parse((Dictionary<string,object>)raw);
        if(snapshot!=null)group=snapshot.GroupId;
        lock(eventGate){
            if(type=="group_room_result")roomPending.Remove(Text(response,"requestId"));
            if(!roomTargets.Contains(group))return true;
            if(type=="group_room_result"){roomPending.Remove(Text(response,"requestId"));roomResults[group]=result;if(result=="group_not_authorized")roomStates.Remove(group);}
            if(type=="group_room_result"){
                string operation=Text(response,"operation");
                if(operation!="snapshot")roomMutations[group]=operation+": "+result;
                if(result!="accepted"&&result!="snapshot")roomRejections[group]=result;
            }
            GroupMediaState current;
            if(snapshot!=null&&(!roomStates.TryGetValue(group,out current)||current.Accepts(snapshot)))roomStates[group]=snapshot;
        }
        if(eventHandler!=null)eventHandler(new DiscoveryEvent{Event=type,GroupId=group,State=result});
        return true;
    }
    public bool QueueGroupMessage(string group,string message,long generation,string envelope){
        if(String.IsNullOrEmpty(group)||group.Length!=32||String.IsNullOrEmpty(message)||message.Length!=32||generation<=0||envelope==null||envelope.Length>87720)return false;
        lock(eventGate){if(eventStop||!EventChannelConnected||groupChannelOutput.Count+groupChannelPending.Count>=32)return false;groupChannelOutput.Enqueue(new Dictionary<string,object>{{"type","group_send"},{"groupId",group},{"messageId",message},{"rosterGeneration",generation.ToString(System.Globalization.CultureInfo.InvariantCulture)},{"envelope",envelope}});return true;}
    }
    public bool QueueGroupAck(string group,string message,string state){lock(eventGate){if(eventStop||!EventChannelConnected||groupChannelOutput.Count>=32)return false;groupChannelOutput.Enqueue(new Dictionary<string,object>{{"type","group_ack"},{"groupId",group},{"messageId",message},{"state",state}});return true;}}
    void DrainGroupOutput(SslStream tls){
        DrainRoomOutput(tls);
        DrainMediaOutput(tls);
        for(int i=0;i<8;i++){
            Dictionary<string,object> request;lock(eventGate){if(groupChannelOutput.Count==0)return;request=groupChannelOutput.Dequeue();if(Text(request,"type")=="group_send")groupChannelPending[Text(request,"messageId")]=request;}
            WriteFrame(tls,Encoding.UTF8.GetBytes(channelJson.Serialize(request)));
        }
    }
    void GroupChannelLost(){
        RoomChannelLost();
        List<Dictionary<string,object>> lost;lock(eventGate){lost=groupChannelPending.Values.Concat(groupChannelOutput.Where(x=>Text(x,"type")=="group_send")).ToList();groupChannelPending.Clear();groupChannelOutput.Clear();}
        foreach(var request in lost)try{if(eventHandler!=null)eventHandler(new DiscoveryEvent{Event="group_delivery",GroupId=Text(request,"groupId"),MessageId=Text(request,"messageId"),State="delivery_unknown_after_disconnect"});}catch{}
    }
    bool GroupChannelNotification(Dictionary<string,object> response){
        if(RoomNotification(response))return true;
        if(MediaNotification(response))return true;
        string type=Text(response,"type");if(type!="group_message"&&type!="group_delivery")return false;
        if(type=="group_delivery")lock(eventGate)groupChannelPending.Remove(Text(response,"messageId"));
        if(eventHandler!=null)eventHandler(new DiscoveryEvent{Event=type,GroupId=Text(response,"groupId"),MessageId=Text(response,"messageId"),Envelope=Text(response,"envelope"),State=Text(response,"state"),SessionCount=Number(response,"sessionCount"),AcceptedCount=Number(response,"acceptedCount"),DuplicateCount=Number(response,"duplicateCount"),RejectedCount=Number(response,"rejectedCount")});
        return true;
    }
    Thread eventThread;
    volatile bool eventStop;
    // Read by the UI refresh scheduler without taking the event-channel lock.  A healthy
    // push channel lets the client use a very slow safety snapshot; a dropped channel uses
    // a bounded retry cadence while it reconnects.
    public volatile bool EventChannelConnected;
    // Absent on legacy servers. Privacy-first clients must fail closed rather than submit a
    // direct endpoint to a server that cannot represent relay-only presence.
    public bool SupportsRelayOnlyPresence { get; private set; }
    readonly ManualResetEvent eventStopSignal=new ManualResetEvent(false);
    Action<DiscoveryEvent> eventHandler;
    int eventReconnectDelayMs=1000;
    DateTime eventConnectedUtc=DateTime.MinValue;
    public DiscoveryClient(string host,int port,string certificateSha256,Func<string,int,string,bool,bool> trustPrompt=null){Host=host;Port=port;CertificateSha256=NormalizePin(certificateSha256);certificateTrustPrompt=trustPrompt;}
    static string NormalizePin(string value){return (value??"").Replace(" ","").Replace(":","").ToUpperInvariant();}
    static string CertificateFingerprint(X509Certificate certificate){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(certificate.GetRawCertData())).Replace("-","");}
    internal static byte[] ReadExact(Stream stream,int length){var buffer=new byte[length];int offset=0;while(offset<length){int read=stream.Read(buffer,offset,length-offset);if(read==0)throw new IOException("Discovery server closed the connection.");offset+=read;}return buffer;}
    internal static void WriteFrame(Stream stream,byte[] data){var header=new[]{(byte)(data.Length>>24),(byte)(data.Length>>16),(byte)(data.Length>>8),(byte)data.Length};stream.Write(header,0,4);stream.Write(data,0,data.Length);}
    internal static byte[] ReadFrame(Stream stream,int maximum=16*1024){var h=ReadExact(stream,4);int length=(h[0]<<24)|(h[1]<<16)|(h[2]<<8)|h[3];if(length<1||length>maximum)throw new InvalidDataException("Invalid server response size.");return ReadExact(stream,length);}
    internal SslStream OpenAuthenticatedStream(out TcpClient tcp,bool longLived=false,Action<long,long> observe=null){
        long start=System.Diagnostics.Stopwatch.GetTimestamp();tcp=new TcpClient{NoDelay=true};tcp.ReceiveTimeout=10000;tcp.SendTimeout=10000;tcp.Connect(Host,Port);long connected=System.Diagnostics.Stopwatch.GetTimestamp();
        var tls=new SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>{if(certificate==null)return false;string observed=CertificateFingerprint(certificate);string trusted=CertificateSha256;if(trusted.Length==64&&String.Equals(observed,trusted,StringComparison.OrdinalIgnoreCase))return true;bool replacement=trusted.Length==64;bool accepted=certificateTrustPrompt!=null&&certificateTrustPrompt(Host,Port,observed,replacement);if(accepted)CertificateSha256=observed;return accepted;});
        try{long began=System.Diagnostics.Stopwatch.GetTimestamp();tls.AuthenticateAsClient(Host,null,SslProtocols.Tls12|SslProtocols.Tls13,false);if(observe!=null)observe(connected-start,System.Diagnostics.Stopwatch.GetTimestamp()-began);if(longLived)tcp.ReceiveTimeout=0;return tls;}catch{tls.Dispose();tcp.Close();throw;}
    }
    public Dictionary<string,object> Archive(string operation,string device,Dictionary<string,object> payload){
        if(!new[]{"archive_descriptor_register","archive_grant_register","archive_authorization_state"}.Contains(operation))throw new ArgumentException("Unsupported archive operation.");
        if(!supportsRpc||AccountId<=0||String.IsNullOrEmpty(SessionToken))throw new InvalidOperationException("Archive requires authenticated RPC support.");
        return Rpc(operation,device,payload);
    }
    internal static bool AttachmentCacheOperation(string operation){return new[]{"cache_admit","cache_status","cache_context","cache_acquire","cache_append","cache_complete","cache_read","cache_progress","cache_release","cache_publish","cache_announcement","cache_announcement_ack","cache_recipient_keys","cache_publish_key","cache_key","cache_key_ack","cache_pending_keys","cache_pending","cache_verified","cache_receipts","cache_dm_capability","cache_dm_register","cache_dm_prepare","cache_dm_admit","cache_dm_status","cache_dm_context","cache_dm_acquire","cache_dm_append","cache_dm_complete","cache_dm_read","cache_dm_progress","cache_dm_release","cache_dm_publish","cache_dm_verified","cache_dm_receipts"}.Contains(operation);}
    public Dictionary<string,object> AttachmentCache(string device,Dictionary<string,object> payload,long expectedAccount,string expectedSession,string expectedCertificate){
        object rawType,rawId;string operation;
        if(payload==null||!payload.TryGetValue("type",out rawType)||(operation=rawType as string)==null||!AttachmentCacheOperation(operation)||!payload.TryGetValue("requestId",out rawId)||!(rawId is string)||((string)rawId).Length!=32)throw new ArgumentException("Unsupported attachment cache request.");
        if(!supportsRpc||AccountId<=0||String.IsNullOrEmpty(SessionToken)||String.IsNullOrEmpty(device)||CertificateSha256==null||CertificateSha256.Length!=64)throw new InvalidOperationException("Group attachments require pinned authenticated RPC support.");
        if(expectedAccount<=0||AccountId!=expectedAccount||String.IsNullOrEmpty(expectedSession)||SessionToken!=expectedSession||expectedCertificate==null||expectedCertificate.Length!=64||!String.Equals(CertificateSha256,expectedCertificate,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("Attachment authenticated context changed.");
        return Rpc(operation,device,payload,expectedSession,expectedCertificate,expectedAccount);
    }
    public Dictionary<string,object> Mailbox(string operation,string device,Dictionary<string,object> payload){
        if(!new[]{"mailbox_register","mailbox_revoke","mailbox_keys","mailbox_store","mailbox_fetch","mailbox_ack","mailbox_receipts","mailbox_eligibility_snapshot","mailbox_eligibility_validate"}.Contains(operation))throw new ArgumentException("Unsupported mailbox operation.");
        if(supportsRpc)return Rpc(operation,device,payload);
        string requestId=Guid.NewGuid().ToString("N");var wireJson=new JavaScriptSerializer{MaxJsonLength=100000};
        TcpClient tcp;using(var tls=OpenAuthenticatedStream(out tcp))using(tcp){
            tls.ReadTimeout=10000;tls.WriteTimeout=10000;
            WriteFrame(tls,Encoding.UTF8.GetBytes(wireJson.Serialize(new Dictionary<string,object>{{"version",1},{"type","mailbox_rpc"},{"requestId",requestId},{"sessionToken",SessionToken},{"publicIdentity",device}})));
            var ready=wireJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls,16384)));
            object ok;if(!ready.TryGetValue("ok",out ok)||!Boolean.Equals(ok,true)||Text(ready,"type")!="mailbox_ready"||Text(ready,"requestId")!=requestId)throw new InvalidOperationException("Mailbox authorization rejected.");
            payload=new Dictionary<string,object>(payload);payload["type"]=operation;payload["requestId"]=requestId;
            var bytes=Encoding.UTF8.GetBytes(wireJson.Serialize(payload));if(bytes.Length>100000)throw new InvalidDataException("Mailbox request exceeds bound.");
            WriteFrame(tls,bytes);var reply=wireJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls,100000)));
            if(Text(reply,"type")!="mailbox_result"||Text(reply,"requestId")!=requestId||Text(reply,"operation")!=operation)throw new InvalidDataException("Mailbox response correlation failed.");
            return reply;
        }
    }
    Dictionary<string,object> Request(string type,Dictionary<string,object> fields) {
        if(String.IsNullOrWhiteSpace(Host)||Port<1||Port>65535)throw new InvalidOperationException("The configured PeerChat server endpoint is invalid.");
        if(supportsRpc&&!String.IsNullOrEmpty(SessionToken)&&!String.IsNullOrEmpty(rpcDevice)&&fields.ContainsKey("sessionToken")&&(!fields.ContainsKey("publicIdentity")||Text(fields,"publicIdentity")==rpcDevice)&&!type.StartsWith("remember_",StringComparison.Ordinal)){var reused=Rpc(type,rpcDevice,fields);if(!reused.ContainsKey("ok")||!Boolean.Equals(reused["ok"],true))throw new DiscoveryRequestException(Text(reused,"error")??"request_failed");return reused;}
        fields["version"]=1;fields["type"]=type;
        using(var tcp=new TcpClient{NoDelay=true}) { tcp.ReceiveTimeout=10000;tcp.SendTimeout=10000;tcp.Connect(Host,Port); using(var tls=new SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>{
            if(certificate==null)return false;string observed=CertificateFingerprint(certificate);string trusted=CertificateSha256;
            if(trusted.Length==64&&String.Equals(observed,trusted,StringComparison.OrdinalIgnoreCase))return true;
            bool replacement=trusted.Length==64;bool accepted=certificateTrustPrompt!=null&&certificateTrustPrompt(Host,Port,observed,replacement);
            if(accepted)CertificateSha256=observed;return accepted;
        })) { tls.AuthenticateAsClient(Host,null,SslProtocols.Tls12|SslProtocols.Tls13,false);var data=Encoding.UTF8.GetBytes(json.Serialize(fields));if(data.Length>4096)throw new InvalidDataException("Discovery request is too large.");WriteFrame(tls,data);var response=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls)));object ok;if(!response.TryGetValue("ok",out ok)||!(bool)ok){object error;response.TryGetValue("error",out error);throw new DiscoveryRequestException(error as string??"request_failed");}return response; } }
    }
    public void Register(string username,string password,string publicIdentity){var r=Request("register",new Dictionary<string,object>{{"username",username},{"password",password},{"publicIdentity",publicIdentity}});SaveSession(username,r);}
    public void Login(string username,string password){var r=Request("login",new Dictionary<string,object>{{"username",username},{"password",password}});SaveSession(username,r);}
    public void HintPendingPeer(long accountId,string device,string localDevice){Request("push_hint",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice},{"targetAccountId",accountId.ToString(System.Globalization.CultureInfo.InvariantCulture)},{"targetPublicIdentity",device}});}
    void SaveSession(string username,Dictionary<string,object> result){CloseRpc();supportsRpc=Number(result,"rpcVersion")==1;object token,accountId,returnedUsername,relayOnlyPresence;if(!result.TryGetValue("sessionToken",out token)||String.IsNullOrEmpty(token as string))throw new InvalidDataException("Discovery server returned no session.");Username=result.TryGetValue("username",out returnedUsername)?returnedUsername as string:username;SessionToken=(string)token;if(result.TryGetValue("accountId",out accountId))AccountId=Convert.ToInt64(accountId);SupportsRelayOnlyPresence=result.TryGetValue("relayOnlyPresence",out relayOnlyPresence)&&Convert.ToBoolean(relayOnlyPresence);}
    // A device reports its currently reachable listener candidate with presence. The server gives
    // that candidate only to an authenticated friend; public username search remains endpoint-free.
    public PresenceResult ReportPresence(string publicIdentity,string endpointHost="",int endpointPort=0,string publicEndpointHost="",int publicEndpointPort=0,bool directCandidates=true){var r=Request("presence",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",publicIdentity},{"directCandidates",directCandidates},{"endpointHost",directCandidates?(endpointHost??""):""},{"endpointPort",directCandidates?endpointPort.ToString():"0"},{"publicEndpointHost",directCandidates?(publicEndpointHost??""):""},{"publicEndpointPort",directCandidates?publicEndpointPort.ToString():"0"}});rpcDevice=publicIdentity;object value;if(r.TryGetValue("accountId",out value))AccountId=Convert.ToInt64(value);if(r.TryGetValue("username",out value)&&value is string)Username=(string)value;return new PresenceResult{AccountId=AccountId,Username=Username??"",RegisteredPublicIdentity=r.ContainsKey("registeredPublicIdentity")?r["registeredPublicIdentity"] as string:"",EndpointHost=r.ContainsKey("endpointHost")?r["endpointHost"] as string:"",EndpointPort=r.ContainsKey("endpointPort")?Convert.ToInt32(r["endpointPort"]):0,PublicEndpointHost=r.ContainsKey("publicEndpointHost")?r["publicEndpointHost"] as string:"",PublicEndpointPort=r.ContainsKey("publicEndpointPort")?Convert.ToInt32(r["publicEndpointPort"]):0,PublicCandidateSource=r.ContainsKey("publicCandidateSource")?r["publicCandidateSource"] as string:"",ObservedAddressClassification=r.ContainsKey("observedAddressClassification")?r["observedAddressClassification"] as string:"",DirectCandidatesEnabled=r.ContainsKey("directCandidatesEnabled")&&Convert.ToBoolean(r["directCandidatesEnabled"])};}
    public void RefreshSessionInfo(){var r=Request("session_info",new Dictionary<string,object>{{"sessionToken",SessionToken??""}});supportsRpc=Number(r,"rpcVersion")==1;AccountId=Convert.ToInt64(r["accountId"]);Username=(string)r["username"];RegisteredPublicIdentity=r.ContainsKey("publicIdentity")?r["publicIdentity"] as string:"";SupportsRelayOnlyPresence=r.ContainsKey("relayOnlyPresence")&&Convert.ToBoolean(r["relayOnlyPresence"]);}
    public void Logout(){if(String.IsNullOrEmpty(SessionToken))return;try{Request("logout",new Dictionary<string,object>{{"sessionToken",SessionToken}});}finally{CloseRpc();SessionToken="";EventChannelConnected=false;}}
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
    // Shared search has lease-derived availability, but no device identity.
    // A friend or group action must perform its own authorized
    // follow-up rather than treating a public search result as peer trust.
    public List<PublicAccountResult> SearchPeople(string query){
        var response=Request("people_search",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"query",query??""}});
        var results=new List<PublicAccountResult>();object raw;
        if(response.TryGetValue("accounts",out raw)){var values=raw as IEnumerable;if(values!=null)foreach(object value in values){var item=value as Dictionary<string,object>;if(item==null)continue;results.Add(new PublicAccountResult{AccountId=Convert.ToInt64(item["accountId"]),Username=Convert.ToString(item["username"]),Online=item.ContainsKey("online")&&Convert.ToBoolean(item["online"])});}}
        return results;
    }
    public Dictionary<string,object> InviteAccountToGroup(string groupId,long rosterGeneration,long targetAccountId,string localDevice){return Request("group_invite_account",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""},{"groupId",groupId??""},{"rosterGeneration",rosterGeneration.ToString()},{"targetAccountId",targetAccountId.ToString()}});}
    public ServerGroupRoster RegisterNewGroup(string creationProof,string localDevice){return ParseGroupRoster(Request("group_register",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""},{"createProof",creationProof??""}}));}
    public ServerGroupRoster GetGroupRoster(string groupId,string localDevice){return ParseGroupRoster(Request("group_roster",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""},{"groupId",groupId??""}}));}
    // Public evidence only; native signed-ledger admission remains authoritative.
    public ServerGroupRecoveryPage GetGroupDeviceRecovery(string groupId,long afterGeneration,string localDevice){
        if(afterGeneration<=0)throw new ArgumentOutOfRangeException("afterGeneration");
        var response=Request("group_device_recovery",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""},{"groupId",groupId??""},{"afterGeneration",afterGeneration.ToString(System.Globalization.CultureInfo.InvariantCulture)}});
        return GroupRecoveryEvidenceProjection.Read(response,groupId,afterGeneration);
    }
    public List<ServerGroupRoster> GroupState(string localDevice){var response=Request("group_state",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""}});var rosters=new List<ServerGroupRoster>();object raw;if(response.TryGetValue("groups",out raw)){var values=raw as IEnumerable;if(values!=null)foreach(object value in values){var item=value as Dictionary<string,object>;if(item!=null)rosters.Add(ParseGroupRoster(item));}}return rosters;}
    internal static ServerGroupRoster ParseGroupRoster(Dictionary<string,object> response){var roster=new ServerGroupRoster{GroupId=Convert.ToString(response["groupId"]),LegacyGroupId=Convert.ToString(response["legacyGroupId"]),RosterGeneration=Convert.ToInt64(response["rosterGeneration"]),DistributorPeerId=Convert.ToString(response["distributorIdentity"])};object creation;if(response.TryGetValue("creationProof",out creation))roster.CreationProof=Convert.ToString(creation);roster.DeviceApprovals=GroupRosterEvidenceProjection.Read(response,roster.RosterGeneration);object raw;if(response.TryGetValue("members",out raw)){var values=raw as IEnumerable;if(values!=null)foreach(object value in values){var member=value as Dictionary<string,object>;if(member==null)continue;object username;member.TryGetValue("username",out username);roster.Members.Add(new ServerGroupMember{AccountId=Convert.ToInt64(member["accountId"]),PeerId=Convert.ToString(member["publicIdentity"]),Role=Convert.ToString(member["role"]),State=Convert.ToString(member["state"]),Username=Convert.ToString(username)});}}return roster;}
    public List<GroupInviteResult> GroupInvites(){var response=Request("group_invite_state",new Dictionary<string,object>{{"sessionToken",SessionToken??""}});var results=new List<GroupInviteResult>();object raw;if(response.TryGetValue("invites",out raw)){var values=raw as IEnumerable;if(values!=null)foreach(object value in values){var item=value as Dictionary<string,object>;if(item==null)continue;results.Add(new GroupInviteResult{InviteId=Convert.ToString(item["inviteId"]),GroupId=Convert.ToString(item["groupId"]),InviterAccountId=Convert.ToInt64(item["inviterAccountId"]),InviterUsername=Convert.ToString(item["inviterUsername"]),CreatedUtc=Convert.ToInt64(item["createdUtc"])});}}return results;}
    public Dictionary<string,object> RespondToGroupInvite(string inviteId,string localDevice,bool accept){return Request("group_invite_respond",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""},{"inviteId",inviteId??""},{"action",accept?"accept":"decline"}});}
    public Dictionary<string,object> RevokeGroupInvite(string inviteId,string localDevice){return Request("group_invite_revoke",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""},{"inviteId",inviteId??""}});}
    public ServerGroupRoster RemoveGroupMember(string groupId,long rosterGeneration,long targetAccountId,string targetPeerId,string localDevice){return ParseGroupRoster(Request("group_remove",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localDevice??""},{"groupId",groupId??""},{"rosterGeneration",rosterGeneration.ToString()},{"targetAccountId",targetAccountId.ToString()},{"targetPublicIdentity",targetPeerId??""}}));}
    public string SendFriendRequest(long accountId){return State(Request("friend_request",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    public string RespondToFriendRequest(long accountId,bool accept){return State(Request("friend_respond",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()},{"action",accept?"accept":"reject"}}));}
    public string CancelFriendRequest(long accountId){return State(Request("friend_cancel",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    public string RemoveFriend(long accountId){return State(Request("friend_remove",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    public string SetBlocked(long accountId,bool blocked){return State(Request(blocked?"friend_block":"friend_unblock",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"accountId",accountId.ToString()}}));}
    // Ephemeral TURN REST credentials are returned only for the authenticated local device.
    // This object is deliberately not a profile setting or a discovery/presence record; later
    // media configuration consumes it in memory and discards it at expiry/logout/switch.
    public MediaTurnCredentials GetMediaTurnCredentials(string publicIdentity){var response=Request("media_turn_credentials",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",publicIdentity??""}});object host,port,transport,username,password,expires;if(!response.TryGetValue("host",out host)||!response.TryGetValue("port",out port)||!response.TryGetValue("transport",out transport)||!response.TryGetValue("username",out username)||!response.TryGetValue("password",out password)||!response.TryGetValue("expiresUtc",out expires))throw new InvalidDataException("Discovery server returned incomplete TURN credentials.");int resolvedPort=Convert.ToInt32(port);long expiry=Convert.ToInt64(expires);if(!String.Equals(transport as string,"udp",StringComparison.OrdinalIgnoreCase)||String.IsNullOrWhiteSpace(host as string)||String.IsNullOrWhiteSpace(username as string)||String.IsNullOrWhiteSpace(password as string)||resolvedPort<1||resolvedPort>65535||expiry<=DateTimeOffset.UtcNow.ToUnixTimeSeconds())throw new InvalidDataException("Discovery server returned invalid TURN credentials.");return new MediaTurnCredentials{Host=(string)host,Port=resolvedPort,Transport="udp",Username=(string)username,Password=(string)password,ExpiresUtc=expiry};}
    // Discovery-only temporary authorization. These APIs neither start a native connection nor
    // persist endpoints; the following checkpoint will consume their ephemeral results.
    public Dictionary<string,object> RequestP2p(long accountId,string localPeer,string targetPeer){return Request("p2p_request",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localPeer},{"targetAccountId",accountId.ToString()},{"targetPublicIdentity",targetPeer}});}
    public Dictionary<string,object> ConsentP2p(string requestId,string localPeer,bool accept){return Request("p2p_consent",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localPeer},{"requestId",requestId},{"accept",accept}});}
    public Dictionary<string,object> SubmitP2pCandidate(string requestId,string localPeer,string host,int port){return Request("p2p_candidates",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localPeer},{"requestId",requestId},{"endpointHost",host??""},{"endpointPort",port.ToString()}});}
    public Dictionary<string,object> CancelP2p(string requestId,string localPeer){return Request("p2p_cancel",new Dictionary<string,object>{{"sessionToken",SessionToken??""},{"publicIdentity",localPeer},{"requestId",requestId}});}
    static string State(Dictionary<string,object> response){object state;return response.TryGetValue("state",out state)?state as string:"";}
    static List<DiscoverySearchResult> Profiles(Dictionary<string,object> response,string key){var results=new List<DiscoverySearchResult>();object raw;if(!response.TryGetValue(key,out raw))return results;var values=raw as IEnumerable;if(values==null)return results;foreach(object value in values){var item=value as Dictionary<string,object>;if(item==null)continue;object host,port,publicHost,publicPort,seen,source,classification;item.TryGetValue("endpointHost",out host);item.TryGetValue("endpointPort",out port);item.TryGetValue("publicEndpointHost",out publicHost);item.TryGetValue("publicEndpointPort",out publicPort);item.TryGetValue("candidateSeenUtc",out seen);item.TryGetValue("publicCandidateSource",out source);item.TryGetValue("observedAddressClassification",out classification);results.Add(new DiscoverySearchResult{AccountId=Convert.ToInt64(item["accountId"]),Username=(string)item["username"],PublicIdentity=(string)item["publicIdentity"],Online=Convert.ToBoolean(item["online"]),EndpointHost=host as string??"",EndpointPort=port==null?0:Convert.ToInt32(port),PublicEndpointHost=publicHost as string??"",PublicEndpointPort=publicPort==null?0:Convert.ToInt32(publicPort),CandidateSeenUtc=seen==null?0:Convert.ToInt64(seen),PublicCandidateSource=source as string??"",ObservedAddressClassification=classification as string??""});}return results;}
    public FriendState GetFriendState(){var response=Request("friend_state",new Dictionary<string,object>{{"sessionToken",SessionToken??""}});return new FriendState{Incoming=Profiles(response,"incoming"),Outgoing=Profiles(response,"outgoing"),Friends=Profiles(response,"friends"),Blocked=Profiles(response,"blocked")};}
    // The push channel is deliberately only a notification stream. Each event causes the UI to
    // request a fresh friend snapshot, so reconnects or missed frames cannot make client state
    // authoritative. It uses the same TLS pin/explicit development trust prompt as Request.
    string eventPublicIdentity="";TcpClient eventSocket;
    // The authenticated event channel is also the normal presence lease. It is identity-bound,
    // so an old profile or session cannot keep another device online.
    public void StartEventChannel(string publicIdentity,Action<DiscoveryEvent> handler){if(handler==null||String.IsNullOrEmpty(SessionToken)||String.IsNullOrEmpty(publicIdentity))return;lock(eventGate){eventHandler=handler;eventPublicIdentity=publicIdentity;if(eventThread!=null&&eventThread.IsAlive)return;eventStop=false;eventStopSignal.Reset();eventThread=new Thread(EventLoop){IsBackground=true,Name="PeerChat discovery events"};eventThread.Start();}}
    public void StopEventChannel(){CloseRpc();Thread current;TcpClient socket;lock(eventGate){eventStop=true;eventStopSignal.Set();current=eventThread;socket=eventSocket;}if(socket!=null)try{socket.Close();}catch{}if(current!=null&&current!=Thread.CurrentThread)current.Join(2000);}
    static string Text(Dictionary<string,object> value,string key){return value.ContainsKey(key)?value[key] as string??"":"";}static long Number(Dictionary<string,object> value,string key){return value.ContainsKey(key)?Convert.ToInt64(value[key]):0;}
    void EventLoop(){while(!eventStop){TcpClient tcp=null;SslStream tls=null;try{tcp=new TcpClient{NoDelay=true};lock(eventGate){if(eventStop){tcp.Close();break;}eventSocket=tcp;}tcp.ReceiveTimeout=1000;tcp.SendTimeout=10000;tcp.Connect(Host,Port);tls=new SslStream(tcp.GetStream(),false,(sender,certificate,chain,errors)=>{if(certificate==null)return false;string observed=CertificateFingerprint(certificate);string trusted=CertificateSha256;if(trusted.Length==64&&String.Equals(observed,trusted,StringComparison.OrdinalIgnoreCase))return true;bool replacement=trusted.Length==64;bool accepted=certificateTrustPrompt!=null&&certificateTrustPrompt(Host,Port,observed,replacement);if(accepted)CertificateSha256=observed;return accepted;});tls.AuthenticateAsClient(Host,null,SslProtocols.Tls12|SslProtocols.Tls13,false);var request=new Dictionary<string,object>{{"version",1},{"type","events"},{"sessionToken",SessionToken??""},{"publicIdentity",eventPublicIdentity??""},{"clientHeartbeats",true}};WriteFrame(tls,Encoding.UTF8.GetBytes(channelJson.Serialize(request)));var ready=channelJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls,100000)));object ok;if(!ready.TryGetValue("ok",out ok)||!(bool)ok){string reason=Text(ready,"error");if(reason=="event_session_superseded")eventStop=true;throw new InvalidOperationException("Discovery event subscription rejected: "+reason); }PresenceEpoch=Text(ready,"serverEpoch");presenceSequence.Reset(PresenceEpoch);InitialPresenceSnapshot=Profiles(ready,"presenceSnapshot");EventChannelConnected=true;RoomChannelReady();eventConnectedUtc=DateTime.UtcNow;if(eventHandler!=null)eventHandler(new DiscoveryEvent{Event="channel_connected"});long nextHeartbeat=0;while(!eventStop){long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();if(now>=nextHeartbeat){WriteFrame(tls,Encoding.UTF8.GetBytes(channelJson.Serialize(new Dictionary<string,object>{{"type","event_heartbeat"}})));nextHeartbeat=now+20;}DrainGroupOutput(tls);if(!tcp.Client.Poll(250000,SelectMode.SelectRead))continue;if(tcp.Available==0)throw new IOException("Discovery event channel closed.");var response=channelJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(ReadFrame(tls,100000)));if(GroupChannelNotification(response))continue;object type;if(!response.TryGetValue("type",out type)||!String.Equals(type as string,"friend_event",StringComparison.Ordinal))continue;if(Text(response,"event")=="presence_changed"&&!presenceSequence.Accept(Text(response,"serverEpoch"),Number(response,"accountId"),Number(response,"presenceRevision")))continue;var notification=new DiscoveryEvent{ServerEpoch=Text(response,"serverEpoch"),PresenceRevision=Number(response,"presenceRevision"),Event=Text(response,"event"),Purpose=Text(response,"purpose"),AccountId=Number(response,"accountId"),PublicIdentity=Text(response,"publicIdentity"),TargetPublicIdentity=Text(response,"targetPublicIdentity"),RequestId=Text(response,"requestId"),PeerHost=Text(response,"peerHost"),PeerPublicIdentity=Text(response,"peerPublicIdentity"),PeerPort=(int)Number(response,"peerPort"),ExpiresUtc=Number(response,"expiresUtc"),EndpointHost=Text(response,"endpointHost"),EndpointPort=(int)Number(response,"endpointPort"),PublicEndpointHost=Text(response,"publicEndpointHost"),PublicEndpointPort=(int)Number(response,"publicEndpointPort")};if(notification.Purpose=="archive_history")notification.ArchiveScope=ArchiveRelayScope.FromEvent(response,notification.AccountId);try{if(eventHandler!=null)eventHandler(notification);}catch{}}}catch(Exception error){if(!eventStop){System.Diagnostics.Debug.WriteLine("Discovery event channel reconnect in "+eventReconnectDelayMs+" ms: "+error.GetType().Name);eventReconnectDelayMs=Math.Min(30000,eventReconnectDelayMs*2);}}finally{if(EventChannelConnected&&DateTime.UtcNow-eventConnectedUtc>TimeSpan.FromSeconds(15))eventReconnectDelayMs=1000;bool wasConnected=EventChannelConnected;EventChannelConnected=false;if(wasConnected&&!eventStop&&eventHandler!=null)try{eventHandler(new DiscoveryEvent{Event="channel_disconnected"});}catch{}GroupChannelLost();lock(eventGate){if(Object.ReferenceEquals(eventSocket,tcp))eventSocket=null;}try{if(tls!=null)tls.Dispose();}catch{}try{if(tcp!=null)tcp.Close();}catch{}}if(!eventStop)eventStopSignal.WaitOne(eventReconnectDelayMs);}}
}
public sealed class PresenceSequence {
    string epoch="";readonly Dictionary<long,long> revisions=new Dictionary<long,long>();
    public void Reset(string value){epoch=value??"";revisions.Clear();}
    public bool Accept(string value,long account,long revision){if(epoch.Length==0)return String.IsNullOrEmpty(value);long previous;if(value!=epoch||account<=0||revision<=0||(revisions.TryGetValue(account,out previous)&&revision<=previous))return false;revisions[account]=revision;return true;}
}
// Immutable server projections. No media objects, networking or local membership authority.
public sealed class GroupMediaSignal {
    internal readonly DateTime ReceivedUtc=DateTime.UtcNow;
    public readonly string GroupId,RoomId,Epoch,SourceDevice,TargetDevice,SourceGeneration,TargetGeneration,RouteId,Kind,Data,Mid;
    public readonly long Revision,RosterGeneration,SourceAccountId,TargetAccountId,MediaGeneration;
    internal GroupMediaSignal(Dictionary<string,object> value){
        GroupId=Convert.ToString(value["groupId"]);RoomId=Convert.ToString(value["roomId"]);Epoch=Convert.ToString(value["epoch"]);
        SourceDevice=Convert.ToString(value["sourceDevice"]);TargetDevice=Convert.ToString(value["targetDevice"]);SourceGeneration=Convert.ToString(value["sourceGeneration"]);TargetGeneration=Convert.ToString(value["targetGeneration"]);
        RouteId=Convert.ToString(value["routeId"]);Kind=Convert.ToString(value["kind"]);Data=Convert.ToString(value["data"]);Mid=Convert.ToString(value["mid"]);
        Revision=Convert.ToInt64(value["revision"]);RosterGeneration=Convert.ToInt64(value["rosterGeneration"]);SourceAccountId=Convert.ToInt64(value["sourceAccountId"]);TargetAccountId=Convert.ToInt64(value["targetAccountId"]);MediaGeneration=Convert.ToInt64(value["mediaGeneration"]);
        if(GroupId.Length!=32||RoomId.Length!=32||Epoch.Length!=32||RouteId.Length!=32||SourceDevice.Length!=64||TargetDevice.Length!=64||SourceGeneration.Length!=32||TargetGeneration.Length!=32||SourceAccountId<=0||TargetAccountId<=0||MediaGeneration<=0||Revision<0||RosterGeneration<=0||Mid.Length>32||
            (Kind!="offer"&&Kind!="answer"&&Kind!="candidate"&&Kind!="close")||Data.Length>(Kind=="candidate"?4096:32768))throw new InvalidDataException("Invalid group media signaling.");
    }
}
public sealed class GroupRoomDevice {
    public readonly long AccountId;
    public readonly string PeerId,Username,MediaGeneration;
    internal GroupRoomDevice(Dictionary<string,object> value){AccountId=Convert.ToInt64(value["accountId"]);PeerId=Convert.ToString(value["publicIdentity"]);Username=Convert.ToString(value["username"]);object generation;MediaGeneration=value.TryGetValue("mediaGeneration",out generation)?Convert.ToString(generation):"";if(AccountId<=0||PeerId.Length!=64||(MediaGeneration.Length!=0&&MediaGeneration.Length!=32))throw new InvalidDataException("Invalid room device.");}
}
public sealed class GroupRoom {
    public readonly string Id,Kind,Name,CreatorDevice;
    public readonly long CreatorAccountId;
    public readonly GroupRoomDevice Sharer;
    public readonly System.Collections.ObjectModel.ReadOnlyCollection<GroupRoomDevice> Members;
    internal GroupRoom(Dictionary<string,object> value){
        Id=Convert.ToString(value["roomId"]);Kind=Convert.ToString(value["kind"]);Name=Convert.ToString(value["name"]);CreatorAccountId=Convert.ToInt64(value["creatorAccountId"]);CreatorDevice=Convert.ToString(value["creatorDevice"]);
        if(Id.Length!=32||(Kind!="voice"&&Kind!="screen_share")||Name.Length>64)throw new InvalidDataException("Invalid room.");
        if(value["sharer"]!=null)Sharer=new GroupRoomDevice((Dictionary<string,object>)value["sharer"]);
        var members=new List<GroupRoomDevice>();foreach(object member in (IEnumerable)value["members"]){if(members.Count>=64)throw new InvalidDataException("Room member limit.");members.Add(new GroupRoomDevice((Dictionary<string,object>)member));}Members=members.AsReadOnly();
    }
}
public sealed class GroupMediaState {
    public readonly string GroupId,Epoch;
    public readonly long Revision,RosterGeneration;
    public readonly System.Collections.ObjectModel.ReadOnlyCollection<GroupRoom> Rooms;
    GroupMediaState(Dictionary<string,object> value){
        GroupId=Convert.ToString(value["groupId"]);Epoch=Convert.ToString(value["epoch"]);Revision=Convert.ToInt64(value["revision"]);RosterGeneration=Convert.ToInt64(value["rosterGeneration"]);
        if(GroupId.Length!=32||Epoch.Length!=32||Revision<0||RosterGeneration<=0)throw new InvalidDataException("Invalid room snapshot.");
        var rooms=new List<GroupRoom>();foreach(object room in (IEnumerable)value["rooms"]){if(rooms.Count>=16)throw new InvalidDataException("Room limit.");rooms.Add(new GroupRoom((Dictionary<string,object>)room));}Rooms=rooms.AsReadOnly();
    }
    public static GroupMediaState Parse(Dictionary<string,object> value){return new GroupMediaState(value);}
    public bool Accepts(GroupMediaState next){return next!=null&&next.GroupId==GroupId&&next.Epoch==Epoch&&next.Revision>=Revision&&next.RosterGeneration>=RosterGeneration;}
    public string Diagnostics(long localAccount,string localDevice,string kind=""){
          var text=new StringBuilder();text.AppendLine("Group media: authoritative room state");text.AppendLine("Group ID: "+GroupId);text.AppendLine("Room revision: "+Revision);text.AppendLine("Roster generation: "+RosterGeneration);
        foreach(var room in Rooms.Where(r=>String.IsNullOrEmpty(kind)||r.Kind==kind)){
            text.AppendLine("Room: "+room.Name+" | "+room.Id+" | "+room.Kind);
            text.AppendLine((room.Kind=="voice"?"Participant count: ":"Viewer count: ")+room.Members.Count);
            text.AppendLine("Local device membership: "+room.Members.Any(d=>d.AccountId==localAccount&&d.PeerId==localDevice));
            text.AppendLine("Local device is sharer: "+(room.Sharer!=null&&room.Sharer.AccountId==localAccount&&room.Sharer.PeerId==localDevice));
            text.AppendLine("Sharer: "+(room.Sharer==null?"None":room.Sharer.AccountId+" / "+room.Sharer.PeerId));
            foreach(var device in room.Members)text.AppendLine("Member: "+device.AccountId+" / "+device.PeerId+" | "+device.Username);
        }
        return text.ToString();
    }
}
public sealed class DiscoveryRequestException : InvalidOperationException { public string ErrorCode { get; private set; } public DiscoveryRequestException(string code):base("Discovery request was rejected."){ErrorCode=String.IsNullOrWhiteSpace(code)?"request_failed":code;} }
public sealed class DiscoverySearchResult { public long AccountId,CandidateSeenUtc; public string Username,PublicIdentity,EndpointHost,PublicEndpointHost,PublicCandidateSource,ObservedAddressClassification; public int EndpointPort,PublicEndpointPort; public bool Online; }
public sealed class PublicAccountResult { public long AccountId; public string Username; public bool Online; }
public sealed class GroupInviteResult { public string InviteId,GroupId,InviterUsername; public long InviterAccountId,CreatedUtc; }
public sealed class ServerGroupMember { public long AccountId; public string PeerId,Role,State,Username; }
// Transport shape checks only, NOT cryptographic approval or key authority.
internal static class GroupRecoveryEvidenceProjection {
    static bool Id(string text,int size){return text!=null&&text.Length==size&&text.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f');}
    static long Integer(Dictionary<string,object> row,string field){object value;if(!row.TryGetValue(field,out value)||!(value is int||value is long))throw new InvalidDataException("Invalid group recovery number.");return Convert.ToInt64(value);}
    static void Roster(ServerGroupRoster row,string group,long generation){
        if(row.GroupId!=group||!Id(row.LegacyGroupId,32)||row.RosterGeneration!=generation||!Id(row.DistributorPeerId,64)||row.Members.Count<1||row.Members.Count>256)throw new InvalidDataException("Invalid group recovery roster.");
        var peers=new HashSet<string>(StringComparer.Ordinal);
        foreach(var member in row.Members)if(member.AccountId<=0||!Id(member.PeerId,64)||!peers.Add(member.PeerId)||
            member.Role!="owner"&&member.Role!="admin"&&member.Role!="member"||member.State!="member"&&member.State!="invited")throw new InvalidDataException("Invalid group recovery device mapping.");
    }
    static ServerGroupRoster ReadRoster(object value){
        var row=value as Dictionary<string,object>;if(row==null||Integer(row,"rosterGeneration")<=0)throw new InvalidDataException("Invalid group recovery roster encoding.");
        var members=row["members"] as IEnumerable;if(members==null||members is string)throw new InvalidDataException("Invalid group recovery members.");int count=0;
        foreach(object member in members){var item=member as Dictionary<string,object>;if(++count>256||item==null||Integer(item,"accountId")<=0)throw new InvalidDataException("Invalid group recovery member encoding.");}
        return DiscoveryClient.ParseGroupRoster(row);
    }
    internal static ServerGroupRecoveryPage Read(Dictionary<string,object> response,string expectedGroup,long requestedAfter){
        try{
            if(response==null||!Id(expectedGroup,32)||requestedAfter<=0||Encoding.UTF8.GetByteCount(new JavaScriptSerializer{MaxJsonLength=8192}.Serialize(response))>8192)throw new InvalidDataException("Group recovery page exceeds its bound.");
            var page=new ServerGroupRecoveryPage{GroupId=Convert.ToString(response["groupId"]),FromGeneration=Integer(response,"fromGeneration"),NextGeneration=Integer(response,"nextGeneration"),CurrentGeneration=Integer(response,"currentGeneration"),Reason=Convert.ToString(response["reason"])};
            object complete;if(!response.TryGetValue("complete",out complete)||!(complete is bool))throw new InvalidDataException("Invalid group recovery completion state.");page.Complete=(bool)complete;
            if(page.GroupId!=expectedGroup||page.FromGeneration!=requestedAfter||page.NextGeneration<requestedAfter||page.CurrentGeneration<page.NextGeneration||page.Complete!=(page.NextGeneration==page.CurrentGeneration)||
                !new[]{"none","page_limit","signed_ledger_gap","epoch_evidence_unavailable","epoch_evidence_too_large"}.Contains(page.Reason)||page.Complete!=(page.Reason=="none"))throw new InvalidDataException("Group recovery cursor/scope mismatch.");
            var rows=response["transitions"] as IEnumerable;if(rows==null||rows is string)throw new InvalidDataException("Invalid group recovery transition list.");long cursor=requestedAfter;string legacy=null,root=null;
            foreach(object value in rows){
                if(page.Steps.Count>=32||cursor==Int64.MaxValue)throw new InvalidDataException("Group recovery transition limit.");
                var row=value as Dictionary<string,object>;if(row==null)throw new InvalidDataException("Invalid group recovery transition.");
                var before=ReadRoster(row["before"]);var after=ReadRoster(row["after"]);
                Roster(before,expectedGroup,cursor);Roster(after,expectedGroup,cursor+1);
                if(before.LegacyGroupId!=after.LegacyGroupId||before.CreationProof!=after.CreationProof||legacy!=null&&(legacy!=before.LegacyGroupId||root!=before.CreationProof))throw new InvalidDataException("Group recovery root/scope mismatch.");
                string proof=Convert.ToString(row["proof"]);
                var bounded=new Dictionary<string,object>{{"deviceApprovals",new object[]{new Dictionary<string,object>{{"appliedGeneration",cursor+1},{"proof",proof}}}}};
                GroupRosterEvidenceProjection.Read(bounded,cursor+1);
                page.Steps.Add(new ServerGroupRecoveryStep{Before=before,After=after,Proof=proof});legacy=before.LegacyGroupId;root=before.CreationProof;cursor++;
            }
            if(cursor!=page.NextGeneration||page.Reason=="page_limit"&&page.Steps.Count==0||page.Reason=="epoch_evidence_too_large"&&page.Steps.Count!=0)throw new InvalidDataException("Group recovery page omitted or fabricated an epoch.");
            return page;
        }catch(InvalidDataException){throw;}catch(Exception error){if(error is KeyNotFoundException||error is FormatException||error is InvalidCastException||error is InvalidOperationException||error is ArgumentException||error is OverflowException)throw new InvalidDataException("Malformed group recovery evidence.");throw;}
    }
}
public sealed class ServerGroupRecoveryPage { public string GroupId,Reason;public long FromGeneration,NextGeneration,CurrentGeneration;public bool Complete;public List<ServerGroupRecoveryStep> Steps=new List<ServerGroupRecoveryStep>(); }
public sealed class ServerGroupRecoveryStep { public ServerGroupRoster Before,After;public string Proof; }
internal static class GroupRosterEvidenceProjection {
    internal static List<ServerGroupApproval> Read(Dictionary<string,object> response,long generation){
        var result=new List<ServerGroupApproval>();object raw;if(!response.TryGetValue("deviceApprovals",out raw))return result;
        var rows=raw as IEnumerable;if(rows==null||raw is string)throw new InvalidDataException("Invalid group evidence list.");
        long previous=0;
        foreach(object value in rows){
            if(result.Count>=32)throw new InvalidDataException("Group evidence limit exceeded.");
            var row=value as Dictionary<string,object>;if(row==null)throw new InvalidDataException("Invalid group evidence record.");
            long applied=Convert.ToInt64(row["appliedGeneration"]);string encoded=Convert.ToString(row["proof"]);
            if(applied<=previous||applied>generation||String.IsNullOrEmpty(encoded)||encoded.Length>5464)throw new InvalidDataException("Invalid group evidence epoch.");
            byte[] wire;try{wire=Convert.FromBase64String(encoded);}catch(FormatException){throw new InvalidDataException("Invalid group evidence encoding.");}
            if(wire.Length==0||wire.Length>4096||Convert.ToBase64String(wire)!=encoded)throw new InvalidDataException("Invalid group evidence size.");
            result.Add(new ServerGroupApproval{AppliedGeneration=applied,Proof=encoded});previous=applied;
        }
        return result;
    }
}
public sealed class ServerGroupApproval { public long AppliedGeneration; public string Proof; }
public sealed class ServerGroupRoster { public string GroupId,LegacyGroupId,DistributorPeerId,CreationProof; public long RosterGeneration; public List<ServerGroupMember> Members=new List<ServerGroupMember>(); public List<ServerGroupApproval> DeviceApprovals=new List<ServerGroupApproval>(); }
public sealed class FriendState { public List<DiscoverySearchResult> Incoming=new List<DiscoverySearchResult>(),Outgoing=new List<DiscoverySearchResult>(),Friends=new List<DiscoverySearchResult>(),Blocked=new List<DiscoverySearchResult>(); }
public sealed class DiscoveryEvent { public ArchiveRelayScope ArchiveScope; public string ServerEpoch;public long PresenceRevision; public string GroupId,MessageId,Envelope,State,Purpose;public long SessionCount,AcceptedCount,DuplicateCount,RejectedCount; public string Event,PublicIdentity,EndpointHost,PublicEndpointHost,RequestId,TargetPublicIdentity,PeerHost,PeerPublicIdentity; public long AccountId,ExpiresUtc; public int EndpointPort,PublicEndpointPort,PeerPort; public bool Online; }
public sealed class PresenceResult { public long AccountId; public string Username,RegisteredPublicIdentity,EndpointHost,PublicEndpointHost,PublicCandidateSource,ObservedAddressClassification; public int EndpointPort,PublicEndpointPort; public bool DirectCandidatesEnabled; }
public sealed class MediaTurnCredentials { public string Host,Transport,Username,Password; public int Port; public long ExpiresUtc; }
}
