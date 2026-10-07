using System;
using System.ComponentModel;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
// This is the only managed/native boundary. Returned strings belong to the DLL
// and are copied before another native call. Never pass C++ objects across ABI.
public sealed class CoreAdapter : IDisposable {
    const string Library = "PeerChat.Core.dll";
    readonly object mailboxGate=new object();
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)] static extern int ui_mailbox_supported();
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_mailbox(IntPtr handle,uint operation,byte[] input,uint length,string messageId,int allowed,[Out] byte[] output,uint capacity);
    public bool MailboxSupported {get{return ui_mailbox_supported()!=0;}}
    public byte[] Mailbox(uint operation,byte[] input=null,string messageId="",bool allowed=false){lock(mailboxGate){
        if(handle==IntPtr.Zero)throw new ObjectDisposedException("CoreAdapter");
        input=input??new byte[0];if(input.Length>70000)throw new ArgumentException("Mailbox input exceeds bound.");
        var output=new byte[262144];int size=ui_mailbox(handle,operation,input,(uint)input.Length,messageId,allowed?1:0,output,(uint)output.Length);
        if(size<0||size>output.Length)throw new InvalidOperationException("Native mailbox operation rejected.");
        Array.Resize(ref output,size);return output;
    }}
    const uint LoadLibrarySearchDllLoadDir=0x00000100;
    const uint LoadLibrarySearchSystem32=0x00000800;
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string fileName,IntPtr file,uint flags);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern uint GetModuleFileName(IntPtr module,StringBuilder fileName,uint size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool FreeLibrary(IntPtr module);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_create();
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern void ui_destroy(IntPtr handle);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_rebuild_profile_identity(string previous,string fresh);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_profile_peer_id(string directory);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_start(IntPtr handle,string directory,ushort port,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_connect(IntPtr handle,string host,ushort port,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_connect_ex(IntPtr handle,string host,ushort port,string peer,string source,string route,int force);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_invalidate_route(IntPtr handle,string peer,string staleHost,ushort stalePort,int allRoutes);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_invalidate_route_v2(IntPtr handle,string peer,string staleHost,ushort stalePort,int allRoutes,ulong expectedNetworkGeneration);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_network_context(IntPtr handle,ulong generation,string fingerprint);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_media_ice_configuration(IntPtr handle,string host,ushort port,string username,string password,long expiry,ulong profileSessionGeneration,ulong discoveryGeneration,ulong configurationGeneration);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_candidates(IntPtr handle,string peer,string lanHost,ushort lanPort,string publicHost,ushort publicPort,int relay,ulong generation,int force);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_prepare_scoped_p2p_listener(IntPtr handle,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_begin_scoped_p2p(IntPtr handle,string peer,string id,string host,ushort port,ulong expiry,ulong generation);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_promote_scoped_p2p(IntPtr handle,string peer,string id);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern void ui_cancel_scoped_p2p(IntPtr handle,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_authenticated_friends(IntPtr handle,ulong localAccount,ulong generation,string encoded);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_pin_initial_authenticated_friend_identity(IntPtr handle,string peer,ulong attempt,ulong generation);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_ready(IntPtr handle,ulong attempt,int incoming);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_receive(IntPtr handle,ulong attempt,byte[] bytes,uint length);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern void ui_relay_closed(IntPtr handle,ulong attempt,string reason);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_take_output(IntPtr handle,ulong attempt,[Out] byte[] bytes,uint capacity,uint waitMilliseconds);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_approve(IntPtr handle,string peer,int accepted);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_send(IntPtr handle,string peer,string text);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_remove(IntPtr handle,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_history(IntPtr handle,string peer,long before);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_erase_history(IntPtr handle,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_account(IntPtr handle,ulong account,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_authenticated_group_roster(IntPtr handle,ulong account,ulong discoveryGeneration,string group,string legacy,ulong rosterGeneration,string distributor,string members);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern void ui_clear_authenticated_group_rosters(IntPtr handle);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_begin_group_control(IntPtr handle,string group,ulong targetAccount,string targetPeer,ulong discoveryGeneration);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_group_create(IntPtr handle,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_group_create_proof(IntPtr handle,string group);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_rename(IntPtr handle,string group,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_invite(IntPtr handle,string group,ulong account,string peer,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_respond(IntPtr handle,string group,int accepted);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_leave(IntPtr handle,string group);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_remove_member(IntPtr handle,string group,ulong account);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_role(IntPtr handle,string group,ulong account,int admin);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_send(IntPtr handle,string group,string text);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_groups(IntPtr handle);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_history(IntPtr handle,string group,long before);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_voice_call(IntPtr handle,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_screen_start(IntPtr handle,string peer,uint sourceKind,ulong sourceHandle,uint fps,uint bitrate,int cursor,uint backend);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_screen_backend(IntPtr handle,uint backend);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_screen_answer(IntPtr handle,string id,ulong viewer,uint backend);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_screen_decline(IntPtr handle,string id);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_screen_end(IntPtr handle,string id);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_screen_diagnostics(IntPtr handle);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void ScreenSourceCallback(IntPtr context,uint kind,ulong source,IntPtr name,uint width,uint height,uint refreshNumerator,uint refreshDenominator);
    [DllImport("PeerChat.ScreenMedia.dll",CallingConvention=CallingConvention.Cdecl)] static extern uint pc_screen_enumerate_sources(ScreenSourceCallback callback,IntPtr context);
    [StructLayout(LayoutKind.Sequential)] struct ScreenCapabilities {public uint size,capture,encoders,decoders,d3d11,softwareEncoders;}
    [DllImport("PeerChat.ScreenMedia.dll",CallingConvention=CallingConvention.Cdecl)] static extern int pc_screen_query_capabilities(ref ScreenCapabilities capabilities);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_voice_answer(IntPtr handle,string call);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_voice_decline(IntPtr handle,string call);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_voice_end(IntPtr handle,string call);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_voice_mute(IntPtr handle,int muted);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_voice_deafen(IntPtr handle,int deafened);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_voice_devices(IntPtr handle);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_voice_devices_select(IntPtr handle,int input,int output);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_voice_levels(IntPtr handle,int input,int output);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_voice_processing_v2(IntPtr handle,int enabled,int noiseSuppression,int automaticGain,uint noiseStrength,uint gainStrength,int echoCancellation);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_voice_diagnostics(IntPtr handle);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_voice_telemetry(IntPtr handle,out float input,out float output,out uint flags);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_share(IntPtr handle,string host,ushort port,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_add(IntPtr handle,string invite,string ownPeer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_poll(IntPtr handle);
    readonly IntPtr nativeModule;
    static readonly object screenModuleGate=new object();
    static IntPtr screenModule;
    IntPtr handle;
    readonly JavaScriptSerializer serializer=new JavaScriptSerializer { MaxJsonLength=32*1024*1024 };
    public CoreAdapter(){
        string nativePath=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,Library));
        nativeModule=LoadLibraryEx(nativePath,IntPtr.Zero,LoadLibrarySearchDllLoadDir|LoadLibrarySearchSystem32);
        if(nativeModule==IntPtr.Zero){
            int code=Marshal.GetLastWin32Error();
            throw new DllNotFoundException("Could not load native PeerChat core at '"+nativePath+"'. Windows loader error "+code+": "+new Win32Exception(code).Message+". A named module in this message is the missing or incompatible dependency when Windows provides one; otherwise verify the complete PeerChat client package and x64 architecture.");
        }
        handle=ui_create();if(handle==IntPtr.Zero)throw new InvalidOperationException("Could not create the peer core.");
    }
    public void Start(string directory,ushort port,string name){if(ui_start(handle,directory,port,name)==0)throw new InvalidOperationException("Could not start the peer core.");}
    public void RebuildProfileIdentity(string previous,string fresh){if(String.IsNullOrWhiteSpace(previous)||String.IsNullOrWhiteSpace(fresh)||ui_rebuild_profile_identity(previous,fresh)==0)throw new InvalidOperationException("Could not preserve the local cryptographic device identity while rebuilding this profile.");}
    public string ProfilePeerId(string directory){return Marshal.PtrToStringUni(ui_profile_peer_id(directory))??"";}
    public void Connect(string host,ushort port,string peer,string source="Direct",string route="manual",bool force=false){if(ui_connect_ex(handle,host,port,peer,source,route,force?1:0)==0)throw new InvalidOperationException("Invalid connection details.");}
    public void InvalidateRoute(string peer,string staleHost="",ushort stalePort=0,bool allRoutes=false,ulong expectedNetworkGeneration=0){if(!String.IsNullOrEmpty(peer))ui_invalidate_route_v2(handle,peer,staleHost??"",stalePort,allRoutes?1:0,expectedNetworkGeneration);}
    public void SetNetworkContext(ulong generation,string fingerprint){ui_network_context(handle,generation,fingerprint??"");}
    // The desktop application owns authenticated discovery orchestration.  This
    // adapter only forwards one validated, memory-only configuration to native media.
    public bool SetMediaIceConfiguration(MediaTurnCredentials value,ulong profileSessionGeneration,ulong discoveryGeneration,ulong configurationGeneration){
        if(value==null||String.IsNullOrWhiteSpace(value.Host)||String.IsNullOrWhiteSpace(value.Username)||String.IsNullOrWhiteSpace(value.Password)||value.Port<1||value.ExpiresUtc<=DateTimeOffset.UtcNow.ToUnixTimeSeconds())return false;
        return ui_media_ice_configuration(handle,value.Host,(ushort)value.Port,value.Username,value.Password,value.ExpiresUtc,profileSessionGeneration,discoveryGeneration,configurationGeneration)!=0;
    }
    public void UpdateCandidates(string peer,string lanHost,ushort lanPort,string publicHost,ushort publicPort,bool relay,ulong generation,bool force=false){ui_candidates(handle,peer,lanHost??"",lanPort,publicHost??"",publicPort,relay?1:0,generation,force?1:0);}
    public bool PrepareScopedP2pListener(string peer){return !String.IsNullOrEmpty(peer)&&ui_prepare_scoped_p2p_listener(handle,peer)!=0;}
    public bool BeginScopedP2p(string peer,string id,string host,ushort port,ulong expiry,ulong generation){return ui_begin_scoped_p2p(handle,peer,id,host,port,expiry,generation)!=0;}
    public bool PromoteScopedP2p(string peer,string id){return ui_promote_scoped_p2p(handle,peer,id)!=0;}
    public void CancelScopedP2p(string peer){if(!String.IsNullOrEmpty(peer))ui_cancel_scoped_p2p(handle,peer);}
    // This is a profile-local projection of the authenticated account friendship snapshot. The
    // native core checks it again before allowing the restricted initial identity pin operation.
    public bool SetAuthenticatedFriends(long localAccount,ulong generation,IEnumerable<FriendConversationBinding> friends){
        if(localAccount<0)return false;var rows=new List<string>();foreach(var item in friends??Enumerable.Empty<FriendConversationBinding>()){if(item==null||item.AccountId<=0||String.IsNullOrEmpty(item.CurrentPeerId))continue;var peers=(item.KnownPeerIds??new List<string>()).Where(value=>!String.IsNullOrEmpty(value)&&value.IndexOfAny(new[]{'|',',','\r','\n'})<0).Distinct(StringComparer.Ordinal);rows.Add(item.AccountId+"|"+item.CurrentPeerId+"|"+String.Join(",",peers));}return ui_authenticated_friends(handle,(ulong)localAccount,generation,String.Join("\n",rows))!=0;
    }
    public bool PinInitialAuthenticatedFriendIdentity(string peer,ulong attempt,ulong generation){return !String.IsNullOrEmpty(peer)&&attempt>0&&generation>0&&ui_pin_initial_authenticated_friend_identity(handle,peer,attempt,generation)!=0;}
    public bool RelayReady(ulong attempt,bool incoming){return ui_relay_ready(handle,attempt,incoming?1:0)!=0;}
    public bool RelayReceive(ulong attempt,byte[] bytes,int length){return bytes!=null&&length>0&&ui_relay_receive(handle,attempt,bytes,(uint)length)!=0;}
    public void RelayClosed(ulong attempt,string reason){ui_relay_closed(handle,attempt,reason??"Relay closed");}
    public int TakeRelayOutput(ulong attempt,byte[] bytes,int waitMilliseconds){return ui_relay_take_output(handle,attempt,bytes,(uint)bytes.Length,(uint)Math.Max(0,waitMilliseconds));}
    public void Approve(string peer,bool accepted){ui_approve(handle,peer,accepted?1:0);}
    public bool Send(string peer,string text){return ui_send(handle,peer,text)!=0;}
    public bool Remove(string peer){return ui_remove(handle,peer)!=0;}
    public void History(string peer,long before){ui_history(handle,peer,before);}
    public bool EraseHistory(string peer){return ui_erase_history(handle,peer)!=0;}
    public bool SetGroupAccount(long account,string name){return account>0&&ui_group_account(handle,(ulong)account,name)!=0;}
    public bool SetAuthenticatedGroupRoster(long localAccount,ulong discoveryGeneration,ServerGroupRoster roster){
        if(localAccount<=0||discoveryGeneration==0||roster==null)return false;
        var rows=new List<string>();foreach(var member in roster.Members){if(member.AccountId<=0||String.IsNullOrEmpty(member.PeerId)||String.IsNullOrEmpty(member.Role)||String.IsNullOrEmpty(member.State)||member.PeerId.IndexOfAny(new[]{'|','\r','\n'})>=0)return false;rows.Add(member.AccountId+"|"+member.PeerId+"|"+member.Role+"|"+member.State);}
        return ui_authenticated_group_roster(handle,(ulong)localAccount,discoveryGeneration,roster.GroupId,roster.LegacyGroupId,(ulong)roster.RosterGeneration,roster.DistributorPeerId,String.Join("\n",rows))!=0;
    }
    public void ClearAuthenticatedGroupRosters(){ui_clear_authenticated_group_rosters(handle);}
    public bool BeginGroupControl(string serverGroupId,long targetAccount,string targetPeer,ulong discoveryGeneration){return targetAccount>0&&ui_begin_group_control(handle,serverGroupId,(ulong)targetAccount,targetPeer,discoveryGeneration)!=0;}
    public string CreateGroup(string name){return Marshal.PtrToStringUni(ui_group_create(handle,name));}
    public string GroupCreationProof(string group){return Marshal.PtrToStringUni(ui_group_create_proof(handle,group))??"";}
    public bool RenameGroup(string group,string name){return ui_group_rename(handle,group,name)!=0;}
    public bool InviteToGroup(string group,long account,string peer,string name){return account>0&&ui_group_invite(handle,group,(ulong)account,peer,name)!=0;}
    public bool RespondToGroupInvite(string group,bool accepted){return ui_group_respond(handle,group,accepted?1:0)!=0;}
    public bool LeaveGroup(string group){return ui_group_leave(handle,group)!=0;}
    public bool RemoveGroupMember(string group,long account){return account>0&&ui_group_remove_member(handle,group,(ulong)account)!=0;}
    public bool SetGroupAdmin(string group,long account,bool admin){return account>0&&ui_group_role(handle,group,(ulong)account,admin?1:0)!=0;}
    public bool SendGroup(string group,string text){return ui_group_send(handle,group,text)!=0;}
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_group_ciphertext(IntPtr handle,byte[] data,uint size);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_message_status(IntPtr handle,string group,string message,string status,uint stage);
    public bool ReceiveGroupCiphertext(byte[] wire){return wire!=null&&wire.Length<=65790&&ui_group_ciphertext(handle,wire,(uint)wire.Length)!=0;}
    public bool GroupMessageStatus(string group,string message,string status,uint stage=0){return ui_group_message_status(handle,group,message,status,stage)!=0;}
    public void LoadGroups(){ui_groups(handle);}
    public void GroupHistory(string group,long before){ui_group_history(handle,group,before);}
    public string StartVoiceCall(string peer){return Marshal.PtrToStringUni(ui_voice_call(handle,peer));}
    public string StartScreenShare(string peer,uint sourceKind,ulong sourceHandle,uint fps,uint bitrate,bool cursor,string backend){return Marshal.PtrToStringUni(ui_screen_start(handle,peer,sourceKind,sourceHandle,fps,bitrate,cursor?1:0,backend=="WebRTC Compatibility"?1u:0u));}
    public bool SetScreenShareBackend(string backend){return ui_screen_backend(handle,backend=="WebRTC Compatibility"?1u:0u)!=0;}
    public bool AnswerScreenShare(string id,ulong viewer,string backend){return ui_screen_answer(handle,id,viewer,backend=="WebRTC Compatibility"?1u:0u)!=0;}
    public bool DeclineScreenShare(string id){return ui_screen_decline(handle,id)!=0;}
    public bool EndScreenShare(string id){return ui_screen_end(handle,id)!=0;}
    public string ScreenShareDiagnostics(){return Marshal.PtrToStringUni(ui_screen_diagnostics(handle));}
    static void EnsureScreenMediaLoaded(){lock(screenModuleGate){
        if(screenModule!=IntPtr.Zero)return;
        string path=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"PeerChat.ScreenMedia.dll"));
        if(!File.Exists(path))throw new FileNotFoundException("The packaged screen-media adapter is missing.",path);
        screenModule=LoadLibraryEx(path,IntPtr.Zero,LoadLibrarySearchDllLoadDir|LoadLibrarySearchSystem32);
        if(screenModule==IntPtr.Zero){int code=Marshal.GetLastWin32Error();throw new DllNotFoundException("Could not load the packaged screen-media adapter: Windows loader error "+code+" ("+new Win32Exception(code).Message+").");}
        var loadedPath=new StringBuilder(32768);uint length=GetModuleFileName(screenModule,loadedPath,(uint)loadedPath.Capacity);
        if(length==0||length>=(uint)loadedPath.Capacity||!String.Equals(Path.GetFullPath(loadedPath.ToString()),path,StringComparison.OrdinalIgnoreCase)){FreeLibrary(screenModule);screenModule=IntPtr.Zero;throw new DllNotFoundException("The screen-media adapter did not load from the PeerChat package directory.");}
    }}
    public ScreenSource[] ScreenSources(){
        EnsureScreenMediaLoaded();
        var sources=new List<ScreenSource>();
        ScreenSourceCallback callback=(context,kind,source,name,width,height,numerator,denominator)=>sources.Add(new ScreenSource{kind=kind,handle=source,name=Marshal.PtrToStringUni(name)??"Screen",width=width,height=height});
        pc_screen_enumerate_sources(callback,IntPtr.Zero);GC.KeepAlive(callback);return sources.ToArray();
    }
    public bool ScreenH264Available(string backend){EnsureScreenMediaLoaded();var capabilities=new ScreenCapabilities{size=(uint)Marshal.SizeOf(typeof(ScreenCapabilities))};return pc_screen_query_capabilities(ref capabilities)!=0&&capabilities.capture!=0&&((backend=="WebRTC Compatibility"?capabilities.softwareEncoders:(capabilities.encoders|capabilities.softwareEncoders))&1)!=0&&(capabilities.decoders&1)!=0&&capabilities.d3d11!=0;}
    public bool AnswerVoiceCall(string call){return ui_voice_answer(handle,call)!=0;}
    public bool DeclineVoiceCall(string call){return ui_voice_decline(handle,call)!=0;}
    public bool EndVoiceCall(string call){return ui_voice_end(handle,call)!=0;}
    public bool SetVoiceMuted(bool muted){return ui_voice_mute(handle,muted?1:0)!=0;}
    public bool SetVoiceDeafened(bool deafened){return ui_voice_deafen(handle,deafened?1:0)!=0;}
    public VoiceDevices VoiceDevices(){return serializer.Deserialize<VoiceDevices>(Marshal.PtrToStringUni(ui_voice_devices(handle)));}
    public bool SelectVoiceDevices(int input,int output){return ui_voice_devices_select(handle,input,output)!=0;}
    public bool SetVoiceVolumes(int input,int output){return ui_voice_levels(handle,input,output)!=0;}
    public bool SetVoiceProcessing(bool enabled,bool noiseSuppression,bool automaticGain,int noiseStrength,int gainStrength,bool echoCancellation=false){return ui_voice_processing_v2(handle,enabled?1:0,noiseSuppression?1:0,automaticGain?1:0,(uint)noiseStrength,(uint)gainStrength,echoCancellation?1:0)!=0;}
    public string VoiceDiagnostics(){return Marshal.PtrToStringUni(ui_voice_diagnostics(handle));}
    public VoiceTelemetry VoiceTelemetry(){float input,output;uint flags;if(ui_voice_telemetry(handle,out input,out output,out flags)==0)return new VoiceTelemetry();return new VoiceTelemetry{input=Math.Max(0,Math.Min(1,input)),output=Math.Max(0,Math.Min(1,output)),active=(flags&1)!=0,inputAvailable=(flags&2)!=0,outputAvailable=(flags&4)!=0};}
    public string Share(string host,ushort port,string peer){return Marshal.PtrToStringUni(ui_share(handle,host,port,peer));}
    public string Add(string invite,string ownPeer){return Marshal.PtrToStringUni(ui_add(handle,invite,ownPeer));}
    public CoreEvent[] Poll(){return serializer.Deserialize<CoreEvent[]>(Marshal.PtrToStringUni(ui_poll(handle)));}
    public void Dispose(){lock(mailboxGate){if(handle!=IntPtr.Zero){ui_destroy(handle);handle=IntPtr.Zero;}GC.KeepAlive(nativeModule);}}
}
public class CoreEvent { public int kind; public string peer,expected,text,detail,candidate,source,route,screenSummary,screenDetails,screenFailureDomain,screenFailureReason,screenRecoveryTrigger,authorizationGroupId; public ushort port; public long attempt,sessionEpoch,sessionRevision,networkGeneration,callLifetime,mediaGeneration,shareLifetime,shareGeneration,screenRecoveryGenerationFrom,screenRecoveryGenerationTo,authorizationRosterGeneration,authorizationTargetAccountId; public int retryMs,callState,callType,mediaState,reliableRoute,p2pAvailability,mediaRoute,shareState,screenRoute,screenWidth,screenHeight,screenFps; public double screenBitrateMbps; public bool connected,callMuted,callDeafened,callControlAvailable,screenMediaFailed; public ContactData[] contacts; public MessageData[] messages; public GroupData[] groups; public GroupEventData[] groupEvents; }
public class ContactData { public string peer,name,host,publicHost; public ushort port,publicPort; public bool verified; }
public class MessageData { public string id,sender,text,status; public long time,logical,sequence; public int delivery; }
public class GroupData { public string id,name; public long owner; public GroupMemberData[] members,invites; }
public class GroupMemberData { public long account; public string peer,name; public int role; }
public class GroupEventData { public string id,group,sender,text,status; public long account,time,logical,sequence; }
public class VoiceDevices { public VoiceDevice[] inputs,outputs; }
public class ScreenSource { public uint kind,width,height; public ulong handle; public string name; public override string ToString(){return (kind==1?"Monitor: ":"Window: ")+name+" ("+width+"×"+height+")";} }
public class VoiceDevice { public int id; public string name; public override string ToString(){return name;} }
public class VoiceTelemetry { public float input,output; public bool active,inputAvailable,outputAvailable; }
}
