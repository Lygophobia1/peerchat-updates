using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
namespace PeerChatDesktop {
// Immutable application presentation, independent of media topology/codec APIs.
public sealed class GroupVoiceRoomStatus {
    public readonly string GroupId,RoomId,State,Error;
    public readonly bool Joined,Muted,Deafened;
    internal GroupVoiceRoomStatus(string group,string room,string state,string error,bool joined,bool muted=false,bool deafened=false){GroupId=group;RoomId=room;State=state;Error=error;Joined=joined;Muted=muted;Deafened=deafened;}
}
public sealed class GroupVoiceMediaConfiguration {
    internal readonly MediaTurnCredentials Credentials;
    internal readonly ulong ProfileGeneration,DiscoveryGeneration,ConfigurationGeneration;
    public GroupVoiceMediaConfiguration(MediaTurnCredentials credentials,ulong profile,ulong discovery,ulong configuration){Credentials=credentials;ProfileGeneration=profile;DiscoveryGeneration=discovery;ConfigurationGeneration=configuration;}
}
public sealed class GroupVoiceFailure {
    public readonly string GroupId,RoomId,Reason,Stage,Details;
    public readonly long Generation;
    internal GroupVoiceFailure(string group,string room,string reason,string stage,string details,ulong generation){GroupId=group;RoomId=room;Reason=reason;Stage=stage;Details=details;Generation=(long)generation;}
}
// Application owner. Authoritative membership remains DiscoveryClient/server;
// native GroupVoiceSession owns audio. Existing application/event pulses drive
// reconciliation; this service creates no socket, timer or reconnect scheduler.
public sealed class GroupVoiceService:IDisposable {
    const string Library="PeerChat.Core.dll";
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr pc_group_voice_create();
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern void pc_group_voice_destroy(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int pc_group_voice_aec(IntPtr h,int enabled);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_voice_start(IntPtr h,string group,string room,string epoch,ulong account,string device,string participation,ulong revision,ulong roster,ulong generation,string host,ushort port,string username,string password,long expiry,ulong profileGeneration,ulong discoveryGeneration,ulong configurationGeneration,string receiveDll,int input,int output,int inputVolume,int outputVolume,int processing,int ns,int agc,uint nsStrength,uint agcStrength);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_voice_reconcile(IntPtr h,ulong revision,ulong roster,string rows);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_voice_signal(IntPtr h,string source,string route,ulong generation,string kind,string data,string mid);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr pc_group_voice_take_signals(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr pc_group_voice_snapshot(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern void pc_group_voice_stop(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_voice_configure(IntPtr h,string host,ushort port,string username,string password,long expiry,ulong profile,ulong discovery,ulong generation);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern int pc_group_voice_mute(IntPtr h,int value);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern void pc_group_voice_deafen(IntPtr h,int value);
    readonly object gate=new object();readonly Func<DiscoveryClient> discovery;readonly Func<string> device;
    readonly Func<Task<GroupVoiceMediaConfiguration>> configuration;readonly Func<VoicePreferenceData> preferences;
    readonly Func<bool> otherVoiceActive;readonly string receiveDll;readonly JavaScriptSerializer json=new JavaScriptSerializer();
    IntPtr handle;string desiredGroup="",desiredRoom="",boundEpoch="",boundParticipation="",boundSources="",failedParticipation="";ulong generation;
    readonly List<PendingSignal> signals=new List<PendingSignal>();
    volatile string diagnosticState="Local participation: Not joined";
    public event Action<GroupVoiceFailure> Failed;
    ulong apmFailureReported;
    void Fail(string group,string room,string reason,string stage,string participation){
        // Freeze the owner's safe snapshot before stop resets native state.
        string details=handle==IntPtr.Zero?"Native session not initialized":Marshal.PtrToStringUni(pc_group_voice_snapshot(handle));
        var failure=new GroupVoiceFailure(group,room,reason,stage,details,generation);
        var report=Failed;if(report!=null)report(failure);
        failedParticipation=participation;Stop();diagnosticState="Local participation: Joined\nState: Error\nReason: "+reason;
        status=new GroupVoiceRoomStatus(group,room,"Error",reason,true);
    }
    public string Diagnostics(string group){var current=status;return current.GroupId==group?"Group voice: live room session\n"+diagnosticState:"Local participation: Not joined\nAEC enabled: false";}
    bool disposed;int pumping;volatile GroupVoiceRoomStatus status=new GroupVoiceRoomStatus("","","Not joined","",false);
    public GroupVoiceService(Func<DiscoveryClient> discovery,Func<string> device,Func<Task<GroupVoiceMediaConfiguration>> configuration,Func<VoicePreferenceData> preferences,Func<bool> otherVoiceActive,string receiveDll){this.discovery=discovery;this.device=device;this.configuration=configuration;this.preferences=preferences;this.otherVoiceActive=otherVoiceActive;this.receiveDll=receiveDll;}
    public GroupVoiceRoomStatus Snapshot(){return status;}
    public Task<bool> Join(string group,string room){if(otherVoiceActive())return Task.FromResult(false);return Task.Run(()=>{lock(gate){if(disposed)return false;var d=discovery();if(d==null||!d.QueueGroupRoom(group,"join_voice",room))return false;failedParticipation="";desiredGroup=group;desiredRoom=room;status=new GroupVoiceRoomStatus(group,room,"Joining","",false);return true;}});}
    public Task<bool> Leave(){return Task.Run(()=>{lock(gate){var d=discovery();bool sent=d!=null&&!String.IsNullOrEmpty(desiredGroup)&&d.QueueGroupRoom(desiredGroup,"leave_voice",desiredRoom);Stop();desiredGroup=desiredRoom="";status=new GroupVoiceRoomStatus("","","Not joined","",false);return sent;}});}
    public bool CreateRoom(string group,string kind,string name){var d=discovery();return d!=null&&(kind=="voice"||kind=="screen_share")&&d.QueueGroupRoom(group,"create","",kind,name);}
    public bool RoomAction(string group,string room,string operation){if(operation!="rename"&&operation!="close"&&operation!="start_share"&&operation!="stop_share"&&operation!="join_viewer"&&operation!="leave_viewer")return false;var d=discovery();return d!=null&&d.QueueGroupRoom(group,operation,room);}
    public Task SetMuted(bool value){return Task.Run(()=>{lock(gate){if(handle!=IntPtr.Zero&&pc_group_voice_mute(handle,value?1:0)!=0)status=new GroupVoiceRoomStatus(status.GroupId,status.RoomId,status.State,status.Error,status.Joined,value,status.Deafened);}});}
    public Task SetDeafened(bool value){return Task.Run(()=>{lock(gate){if(handle!=IntPtr.Zero){pc_group_voice_deafen(handle,value?1:0);status=new GroupVoiceRoomStatus(status.GroupId,status.RoomId,status.State,status.Error,status.Joined,status.Muted,value);}}});}
    void Stop(){if(handle!=IntPtr.Zero)pc_group_voice_stop(handle);boundEpoch=boundParticipation=boundSources="";signals.Clear();}
    // Called on the existing application pulse. The asynchronous control work
    // remains serialized and never blocks the WPF dispatcher on device teardown.
    public async Task Pump(){
        if(Interlocked.Exchange(ref pumping,1)!=0)return;
        string observedParticipation="";
        try{
            DiscoveryClient d;string group,room,local;GroupMediaState state;GroupRoom voice;GroupRoomDevice member;
            lock(gate){if(disposed||String.IsNullOrEmpty(desiredGroup))return;d=discovery();group=desiredGroup;room=desiredRoom;local=device();}
            state=d==null?null:d.GroupRooms(group);voice=state==null?null:state.Rooms.FirstOrDefault(r=>r.Id==room&&r.Kind=="voice");
            member=voice==null?null:voice.Members.FirstOrDefault(m=>m.AccountId==d.AccountId&&m.PeerId==local);
            observedParticipation=member==null?"":member.MediaGeneration;
            if(d==null||!d.EventChannelConnected||member==null){await Task.Run(()=>{lock(gate){if(disposed)return;Stop();status=new GroupVoiceRoomStatus(group,room,"Not joined",d==null||!d.EventChannelConnected?"Server channel unavailable":"Awaiting authorized room participation",false);}});return;}
            string sources=String.Join(";",voice.Members.Where(m=>m.PeerId!=local).OrderBy(m=>m.AccountId).ThenBy(m=>m.PeerId).Select(m=>m.AccountId+","+m.PeerId+","+m.MediaGeneration));
            bool start;lock(gate){if(failedParticipation==member.MediaGeneration)return;start=boundEpoch!=state.Epoch||boundParticipation!=member.MediaGeneration;}
            GroupVoiceMediaConfiguration cfg=null;
            if(start||sources!=boundSources)cfg=await configuration();
            var settings=preferences();
            await Task.Run(()=>{lock(gate){
                if(disposed||d!=discovery()||desiredGroup!=group||desiredRoom!=room||state!=d.GroupRooms(group))return;
                if(member.MediaGeneration.Length!=32){Fail(group,room,"Server does not support live room participation","authorization",member.MediaGeneration);return;}
                if(start){
                    Stop();if(cfg==null){Fail(group,room,"Media configuration unavailable; leave and rejoin to retry","configuration",member.MediaGeneration);return;}
                    if(handle==IntPtr.Zero)handle=pc_group_voice_create();var c=cfg.Credentials;
                    if(handle==IntPtr.Zero||pc_group_voice_aec(handle,settings.EchoCancellationEnabled?1:0)==0||pc_group_voice_start(handle,group,room,state.Epoch,(ulong)d.AccountId,local,member.MediaGeneration,(ulong)state.Revision,(ulong)state.RosterGeneration,++generation,c.Host,(ushort)c.Port,c.Username,c.Password,c.ExpiresUtc,cfg.ProfileGeneration,cfg.DiscoveryGeneration,cfg.ConfigurationGeneration,receiveDll,settings.InputDeviceId,settings.OutputDeviceId,settings.InputVolume,settings.OutputVolume,settings.AudioProcessingEnabled?1:0,settings.NoiseSuppressionEnabled?1:0,settings.AutomaticGainEnabled?1:0,(uint)settings.NoiseSuppressionStrength,(uint)settings.AutomaticGainStrength)==0){Fail(group,room,"Voice audio/media initialization failed; leave and rejoin to retry","startup",member.MediaGeneration);return;}
                    boundEpoch=state.Epoch;boundParticipation=member.MediaGeneration;
                }
                else if(cfg!=null){var c=cfg.Credentials;pc_group_voice_configure(handle,c.Host,(ushort)c.Port,c.Username,c.Password,c.ExpiresUtc,cfg.ProfileGeneration,cfg.DiscoveryGeneration,cfg.ConfigurationGeneration);}
                if(pc_group_voice_reconcile(handle,(ulong)state.Revision,(ulong)state.RosterGeneration,sources)==0){Fail(group,room,"Authorized participant reconciliation failed","participants",member.MediaGeneration);return;}
                boundSources=sources;
                GroupMediaSignal signal;while((signal=d.TakeGroupMediaSignal(group,room))!=null){if(signal.GroupId==group&&signal.RoomId==room&&signal.Epoch==boundEpoch&&signal.TargetGeneration==boundParticipation)pc_group_voice_signal(handle,signal.SourceAccountId+":"+signal.SourceDevice+":"+signal.SourceGeneration,signal.RouteId,(ulong)signal.MediaGeneration,signal.Kind,signal.Data,signal.Mid);}
                var outbound=json.Deserialize<List<NativeSignal>>(Marshal.PtrToStringUni(pc_group_voice_take_signals(handle)));
                foreach(var output in outbound)if(signals.Count<64)signals.Add(new PendingSignal{Signal=output,Expires=DateTime.UtcNow.AddSeconds(10)});
                foreach(var pending in signals.ToArray()){
                    var output=pending.Signal;var target=voice.Members.FirstOrDefault(m=>m.AccountId+":"+m.PeerId+":"+m.MediaGeneration==output.sourceKey);
                    string result=pending.Request==null?"not_queued":d.GroupMediaResult(pending.Request);
                    if(target==null||pending.Expires<DateTime.UtcNow||result=="forwarded"||result!="not_queued"&&result!="Pending"&&result!="stale_room_state"){signals.Remove(pending);continue;}
                    if(result=="Pending")continue;
                    if(pending.Attempts>=4){signals.Remove(pending);continue;}
                    pending.Request=d.QueueGroupMediaSignal(group,room,target.AccountId,target.PeerId,output.routeId,(long)output.generation,output.kind,output.data,output.mid);++pending.Attempts;
                }
                var current=json.Deserialize<NativeState>(Marshal.PtrToStringUni(pc_group_voice_snapshot(handle)));
                if(current.apmFailure!=0&&apmFailureReported!=generation){apmFailureReported=generation;var report=Failed;if(report!=null)report(new GroupVoiceFailure(group,room,"Audio processing degraded; raw microphone audio continues","audio_processing",Marshal.PtrToStringUni(pc_group_voice_snapshot(handle)),generation));}
                string connection=voice.Members.Count==1?"In voice · alone":current.routes.Any(r=>r.connected)?"In voice · connected":current.routes.Any(r=>r.state=="4"||r.state=="5")?"In voice · participant media unavailable":"In voice · connecting";
                status=new GroupVoiceRoomStatus(group,room,connection,current.error??"",current.active,current.muted,current.deafened);
                diagnosticState="Room ID: "+room+"\nLocal participation: "+(current.active?"Joined":"Not joined")+"\nState: "+connection+"\nParticipant count: "+voice.Members.Count+"\nMedia generation: "+generation+"\nCapture encodes: "+current.encodedPackets+"\nFanout copies: "+current.fanoutCopies+"\nAPM active: "+current.apmActive+"\nAPM failure: "+current.apmFailure+"\nCapture 10 ms blocks: "+current.captureBlocks+"\nRTP accepted: "+current.acceptedPackets+"\nMixed 10 ms blocks: "+current.mixedBlocks+"\nPlayout busy/silence blocks: "+current.underrunBlocks+"\naec_enabled: "+current.aec_enabled+"\nrender_reference_active: "+current.aec_render_reference_active+"\nrender_blocks: "+current.aec_render_blocks+"\nreference_underruns: "+current.aec_reference_underruns+"\naec_resets: "+current.aec_resets+"\naec_delay_ms: "+current.aec_delay_ms+"\n"+String.Join("\n",current.routes.Select(r=>"Source: "+r.sourceKey+" · "+(r.turn?"TURN Relay":"Connecting / unavailable")+" · sent="+r.sentPackets+" · received="+r.receivedPackets));
            }});
        }catch(Exception){await Task.Run(()=>{lock(gate){if(!disposed)Fail(desiredGroup,desiredRoom,"Voice operation failed","control",observedParticipation);}});}
        finally{Interlocked.Exchange(ref pumping,0);}
    }
    public void Dispose(){lock(gate){if(disposed)return;disposed=true;Stop();if(handle!=IntPtr.Zero)pc_group_voice_destroy(handle);handle=IntPtr.Zero;}}
    sealed class PendingSignal {public NativeSignal Signal;public string Request;public int Attempts;public DateTime Expires;}
    sealed class NativeSignal {public string sourceKey {get;set;} public string routeId {get;set;} public string kind {get;set;} public string data {get;set;} public string mid {get;set;} public ulong generation {get;set;}}
    sealed class NativeRoute {public string sourceKey {get;set;} public bool connected {get;set;} public bool turn {get;set;} public string state {get;set;} public string reason {get;set;} public ulong sentPackets {get;set;} public ulong receivedPackets {get;set;}}
    sealed class NativeState {public bool aec_enabled {get;set;} public bool aec_render_reference_active {get;set;} public ulong aec_render_blocks {get;set;} public ulong aec_reference_underruns {get;set;} public ulong aec_resets {get;set;} public uint aec_delay_ms {get;set;} public bool active {get;set;} public bool muted {get;set;} public bool deafened {get;set;} public bool apmActive {get;set;} public ulong captureBlocks {get;set;} public ulong acceptedPackets {get;set;} public ulong mixedBlocks {get;set;} public ulong underrunBlocks {get;set;} public uint apmFailure {get;set;} public ulong encodedPackets {get;set;} public ulong fanoutCopies {get;set;} public string error {get;set;} public NativeRoute[] routes {get;set;}=new NativeRoute[0];}
}
}
