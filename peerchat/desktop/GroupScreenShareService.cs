using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
namespace PeerChatDesktop {
public sealed class GroupScreenRoomStatus {
    public readonly string GroupId,RoomId,State,Error;
    public readonly bool Sharing,Watching,Active;
    public readonly ulong Generation;
    internal GroupScreenRoomStatus(string g,string r,string s,string e,bool sharing,bool watching,bool active,ulong generation){GroupId=g;RoomId=r;State=s;Error=e;Sharing=sharing;Watching=watching;Active=active;Generation=generation;}
}
// Stable application owner. The existing discovery room snapshot is authority.
// No room state, RTC object or topology class is exposed to the WPF shell.
public sealed class GroupScreenShareService:IDisposable {
    const string Library="PeerChat.Core.dll";
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr pc_group_screen_create();
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern void pc_group_screen_destroy(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_screen_start(IntPtr h,string group,string room,string epoch,ulong account,string device,string participation,string share,ulong revision,ulong roster,ulong generation,int sender,uint sourceKind,ulong sourceHandle,ulong window,uint fps,uint bitrate,int cursor,string host,ushort port,string user,string password,long expiry,ulong profile,ulong discovery,ulong configuration);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_screen_reconcile(IntPtr h,ulong revision,ulong roster,string rows);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_screen_configure(IntPtr h,string host,ushort port,string user,string password,long expiry,ulong profile,ulong discovery,ulong generation);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]static extern int pc_group_screen_signal(IntPtr h,string source,string route,ulong generation,string kind,string data,string mid);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern void pc_group_screen_pump(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr pc_group_screen_take_signals(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern IntPtr pc_group_screen_snapshot(IntPtr h);
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]static extern void pc_group_screen_stop(IntPtr h);
    readonly object gate=new object();readonly Func<DiscoveryClient> discovery;readonly Func<string> device;
    readonly Func<Task<GroupVoiceMediaConfiguration>> configuration;
    readonly Dictionary<string,Session> sessions=new Dictionary<string,Session>(StringComparer.Ordinal);
    readonly JavaScriptSerializer json=new JavaScriptSerializer{MaxJsonLength=1024*1024};
    bool disposed;int pumping;ulong nextGeneration;
    sealed class PublishedState {
        internal readonly Dictionary<string,GroupScreenRoomStatus> Status=new Dictionary<string,GroupScreenRoomStatus>(StringComparer.Ordinal);
        internal readonly Dictionary<string,string> Details=new Dictionary<string,string>(StringComparer.Ordinal);
        internal readonly Dictionary<string,string[]> Metrics=new Dictionary<string,string[]>(StringComparer.Ordinal);
        internal GroupMediaObservation[] Observations=new GroupMediaObservation[0];
    }
    volatile PublishedState published=new PublishedState();
    // Called only under the worker gate. Published dictionaries are immutable
    // after replacement, so rendering never waits for native teardown/pumping.
    void Publish(){var next=new PublishedState();foreach(var s in sessions.Values)next.Status.Add(Key(s.Group,s.Room),s.Status);
        foreach(var group in sessions.Values.GroupBy(s=>s.Group))next.Details.Add(group.Key,String.Join("\n",group.Select(s=>"Room ID: "+s.Room+"\nShare generation: "+s.Share+"\nLocal role: "+(s.Sender?"Sharer":"Viewer")+"\nBackend: WebRTC Compatibility\nTopology: RelayMesh\nState: "+s.Status.State+"\n"+s.Diagnostics)));
        foreach(var group in sessions.Values.GroupBy(s=>s.Group))next.Metrics.Add(group.Key,group.Where(s=>!s.Failed&&s.Handle!=IntPtr.Zero).Select(s=>s.Diagnostics).ToArray());next.Observations=sessions.Values.Where(s=>!s.Failed&&s.Handle!=IntPtr.Zero&&s.Observation!=null&&s.Observation.Generation==s.Generation).Select(s=>s.Observation).ToArray();published=next;}
    public GroupMediaObservation[] DiagnosticObservations(string group){return published.Observations.Where(o=>o.Group==group).ToArray();}
    public event Action<GroupVoiceFailure> Failed;
    public GroupScreenShareService(Func<DiscoveryClient> discovery,Func<string> device,Func<Task<GroupVoiceMediaConfiguration>> configuration){this.discovery=discovery;this.device=device;this.configuration=configuration;}
    static string Key(string group,string room){return group+":"+room;}
    public GroupScreenRoomStatus Snapshot(string group,string room){GroupScreenRoomStatus status;return published.Status.TryGetValue(Key(group,room),out status)?status:new GroupScreenRoomStatus(group,room,"Inactive","",false,false,false,0);}
    public string Diagnostics(string group){string details;return published.Details.TryGetValue(group,out details)?details:"Local screen participation: Inactive";}
    public string[] Metrics(string group){string[] rows;return published.Metrics.TryGetValue(group,out rows)?rows.ToArray():new string[0];}
    public bool AnyActive {get{return published.Status.Values.Any(s=>s.Active);}}
    public Task<bool> StartSharing(string group,string room,ScreenSource source,ScreenSharePreferenceData preferences){return Select(group,room,true,source,0,preferences);}
    public Task<bool> Watch(string group,string room,ulong window,ScreenSharePreferenceData preferences){return Select(group,room,false,null,window,preferences);}
    Task<bool> Select(string group,string room,bool sender,ScreenSource source,ulong window,ScreenSharePreferenceData preferences){
        return Task.Run(()=>{lock(gate){var d=discovery();if(disposed||d==null||!d.EventChannelConnected||sender&&source==null||!sender&&window==0||sessions.ContainsKey(Key(group,room)))return false;
            if(!d.QueueGroupRoom(group,sender?"start_share":"join_viewer",room))return false;
            var s=new Session{Group=group,Room=room,Sender=sender,Source=source,Window=window,Fps=(uint)preferences.FramesPerSecond,Bitrate=(uint)preferences.BitrateBitsPerSecond,Cursor=preferences.CaptureCursor,Expires=DateTime.UtcNow.AddSeconds(15)};
            s.Status=new GroupScreenRoomStatus(group,room,sender?"Starting":"Joining","",sender,!sender,false,0);sessions.Add(Key(group,room),s);Publish();return true;}});
    }
    public Task<bool> Stop(string group,string room){return Task.Run(()=>{lock(gate){Session s;if(!sessions.TryGetValue(Key(group,room),out s))return true;var d=discovery();bool queued=d!=null&&d.QueueGroupRoom(group,s.Sender?"stop_share":"leave_viewer",room);Retire(s);sessions.Remove(Key(group,room));Publish();return queued;}});}
    void Retire(Session s){if(s.Handle!=IntPtr.Zero){pc_group_screen_stop(s.Handle);pc_group_screen_destroy(s.Handle);s.Handle=IntPtr.Zero;}s.Observation=null;s.Pending.Clear();}
    void Fail(Session s,string reason,string stage){
        if(s.Handle!=IntPtr.Zero)s.Diagnostics=Marshal.PtrToStringUni(pc_group_screen_snapshot(s.Handle));
        var report=Failed;if(report!=null)report(new GroupVoiceFailure(s.Group,s.Room,reason,stage,s.Diagnostics,s.Generation));
        Retire(s);s.Failed=true;s.Status=new GroupScreenRoomStatus(s.Group,s.Room,"Error",reason,s.Sender,!s.Sender,false,s.Generation);
        var d=discovery();if(d!=null)d.QueueGroupRoom(s.Group,s.Sender?"stop_share":"leave_viewer",s.Room);
    }
    // Existing application pulse drives all sessions. No additional timer,
    // discovery connection, retry scheduler or UI-thread native teardown.
    public async Task Pump(){
        if(Interlocked.Exchange(ref pumping,1)!=0)return;
        try{
            Session[] current=await Task.Run(()=>{lock(gate){return disposed?new Session[0]:sessions.Values.ToArray();}});
            foreach(var s in current){
                try {
                DiscoveryClient d=null;GroupMediaState state;GroupRoom room;GroupRoomDevice local;string localDevice=null;
                bool selected=await Task.Run(()=>{lock(gate){if(disposed||s.Failed||!sessions.ContainsKey(Key(s.Group,s.Room)))return false;d=discovery();localDevice=device();return true;}});
                if(!selected)continue;
                state=d==null?null:d.GroupRooms(s.Group);room=state==null?null:state.Rooms.FirstOrDefault(r=>r.Id==s.Room&&r.Kind=="screen_share");
                local=room==null?null:s.Sender?room.Sharer:room.Members.FirstOrDefault(m=>m.AccountId==d.AccountId&&m.PeerId==localDevice);
                if(local!=null&&(local.AccountId!=d.AccountId||local.PeerId!=localDevice))local=null;
                if(d==null||!d.EventChannelConnected||room==null||room.Sharer==null||local==null){
                    await Task.Run(()=>{lock(gate){if(disposed)return;if(s.Handle!=IntPtr.Zero||s.Bound.Length!=0||d==null||!d.EventChannelConnected){Retire(s);s.Failed=true;s.Status=new GroupScreenRoomStatus(s.Group,s.Room,"Ended","Authoritative participation ended",s.Sender,!s.Sender,false,s.Generation);}else if(DateTime.UtcNow>s.Expires)Fail(s,"Room participation was not authorized","authorization");}});continue;
                }
                string bound=state.Epoch+":"+room.Sharer.MediaGeneration+":"+local.MediaGeneration;
                var peers=(s.Sender?room.Members:new[]{room.Sharer}.AsEnumerable()).Where(m=>m.PeerId!=localDevice||m.AccountId!=d.AccountId).OrderBy(m=>m.AccountId).ThenBy(m=>m.PeerId).ToArray();
                string rows=String.Join(";",peers.Select(m=>m.AccountId+","+m.PeerId+","+m.MediaGeneration));
                bool start=s.Bound!=bound;GroupVoiceMediaConfiguration cfg=null;if(start||s.Sources!=rows)cfg=await configuration();
                await Task.Run(()=>{lock(gate){
                    if(disposed||s.Failed||!sessions.ContainsKey(Key(s.Group,s.Room))||d!=discovery()||state!=d.GroupRooms(s.Group))return;
                    if(start){Retire(s);if(cfg==null){Fail(s,"Screen media configuration unavailable","configuration");return;}s.Handle=pc_group_screen_create();s.Generation=++nextGeneration;var c=cfg.Credentials;
                        if(s.Handle==IntPtr.Zero||pc_group_screen_start(s.Handle,s.Group,s.Room,state.Epoch,(ulong)d.AccountId,localDevice,local.MediaGeneration,room.Sharer.MediaGeneration,(ulong)state.Revision,(ulong)state.RosterGeneration,s.Generation,s.Sender?1:0,s.Source==null?0:s.Source.kind,s.Source==null?0:s.Source.handle,s.Window,s.Fps,s.Bitrate,s.Cursor?1:0,c.Host,(ushort)c.Port,c.Username,c.Password,c.ExpiresUtc,cfg.ProfileGeneration,cfg.DiscoveryGeneration,cfg.ConfigurationGeneration)==0){Fail(s,"Screen media initialization failed","startup");return;}
                        s.Bound=bound;s.Share=room.Sharer.MediaGeneration;s.DiagnosticProfile=cfg.ProfileGeneration;s.DiagnosticDiscovery=cfg.DiscoveryGeneration;
                    }else if(cfg!=null){var c=cfg.Credentials;pc_group_screen_configure(s.Handle,c.Host,(ushort)c.Port,c.Username,c.Password,c.ExpiresUtc,cfg.ProfileGeneration,cfg.DiscoveryGeneration,cfg.ConfigurationGeneration);}
                    if(pc_group_screen_reconcile(s.Handle,(ulong)state.Revision,(ulong)state.RosterGeneration,rows)==0){Fail(s,"Screen authorization reconciliation failed","authorization");return;}s.Sources=rows;
                    GroupMediaSignal incoming;while((incoming=d.TakeGroupMediaSignal(s.Group,s.Room))!=null)if(incoming.Epoch==state.Epoch&&incoming.TargetGeneration==local.MediaGeneration)pc_group_screen_signal(s.Handle,incoming.SourceAccountId+":"+incoming.SourceDevice+":"+incoming.SourceGeneration,incoming.RouteId,(ulong)incoming.MediaGeneration,incoming.Kind,incoming.Data,incoming.Mid);
                    pc_group_screen_pump(s.Handle);
                    foreach(var output in json.Deserialize<List<NativeSignal>>(Marshal.PtrToStringUni(pc_group_screen_take_signals(s.Handle))))if(s.Pending.Count<64)s.Pending.Add(new Pending{Signal=output,Expires=DateTime.UtcNow.AddSeconds(10)});
                    foreach(var pending in s.Pending.ToArray()){
                        var output=pending.Signal;var target=peers.FirstOrDefault(m=>m.AccountId+":"+m.PeerId+":"+m.MediaGeneration==output.sourceKey);
                        string result=pending.Request==null?"not_queued":d.GroupMediaResult(pending.Request);
                        if(target==null||pending.Expires<DateTime.UtcNow||result=="forwarded"||result!="not_queued"&&result!="Pending"&&result!="stale_room_state"){s.Pending.Remove(pending);continue;}
                        if(result=="Pending")continue;if(pending.Attempts>=4){s.Pending.Remove(pending);continue;}
                        pending.Request=d.QueueGroupMediaSignal(s.Group,s.Room,target.AccountId,target.PeerId,output.routeId,(long)output.generation,output.kind,output.data,output.mid);++pending.Attempts;
                    }
                    s.Diagnostics=Marshal.PtrToStringUni(pc_group_screen_snapshot(s.Handle));s.Observation=new GroupMediaObservation(s.Group,s.Room,state.Epoch,d.AccountId,localDevice,local.MediaGeneration,s.Share,"screen_share",s.Generation,s.DiagnosticProfile,s.DiagnosticDiscovery,s.Sender,s.Diagnostics);var native=json.Deserialize<NativeState>(s.Diagnostics);
                    if(!native.active){Fail(s,String.IsNullOrEmpty(native.error)?"Screen media ended":native.error,"media");return;}
                    string status=s.Sender?"Sharing":native.routes.Any(r=>r.presented>0)?"Watching":native.routes.Any(r=>r.state=="Failed")?"Viewer media unavailable — stop and rejoin":"Connecting";
                    s.Status=new GroupScreenRoomStatus(s.Group,s.Room,status,native.error??"",s.Sender,!s.Sender,native.active,s.Generation);
                }});
                }catch(Exception){await Task.Run(()=>{lock(gate){if(!disposed&&sessions.ContainsKey(Key(s.Group,s.Room)))Fail(s,"Screen media operation failed","control");}});}
            }
        }
        finally{await Task.Run(()=>{lock(gate){Publish();}});Interlocked.Exchange(ref pumping,0);}
    }
    public void Dispose(){lock(gate){if(disposed)return;disposed=true;foreach(var s in sessions.Values)Retire(s);sessions.Clear();Publish();}}
    sealed class Session {public string Group,Room,Bound="",Share="",Sources="",Diagnostics="";public bool Sender,Cursor,Failed;public ScreenSource Source;public ulong Window,Generation,DiagnosticProfile,DiagnosticDiscovery;public GroupMediaObservation Observation;public uint Fps,Bitrate;public IntPtr Handle;public DateTime Expires;public GroupScreenRoomStatus Status;public readonly List<Pending> Pending=new List<Pending>();}
    sealed class Pending {public NativeSignal Signal;public string Request;public int Attempts;public DateTime Expires;}
    sealed class NativeSignal {public string sourceKey {get;set;}public string routeId {get;set;}public string kind {get;set;}public string data {get;set;}public string mid {get;set;}public ulong generation {get;set;}}
    sealed class NativeRoute {public string state {get;set;}public ulong presented {get;set;}}
    sealed class NativeState {public bool active {get;set;}public string error {get;set;}public NativeRoute[] routes {get;set;}=new NativeRoute[0];}
}
}
