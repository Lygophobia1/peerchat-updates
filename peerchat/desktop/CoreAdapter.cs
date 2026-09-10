using System;
using System.Runtime.InteropServices;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
// This is the only managed/native boundary. Returned strings belong to the DLL
// and are copied before another native call. Never pass C++ objects across ABI.
public sealed class CoreAdapter : IDisposable {
    const string Library = "PeerChat.Core.dll";
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_create();
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern void ui_destroy(IntPtr handle);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_start(IntPtr handle,string directory,ushort port,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_connect(IntPtr handle,string host,ushort port,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_approve(IntPtr handle,string peer,int accepted);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_send(IntPtr handle,string peer,string text);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_remove(IntPtr handle,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_history(IntPtr handle,string peer,long before);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_account(IntPtr handle,ulong account,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_group_create(IntPtr handle,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_rename(IntPtr handle,string group,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_invite(IntPtr handle,string group,ulong account,string peer,string name);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_respond(IntPtr handle,string group,int accepted);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_leave(IntPtr handle,string group);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_remove_member(IntPtr handle,string group,ulong account);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_role(IntPtr handle,string group,ulong account,int admin);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_group_send(IntPtr handle,string group,string text);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern int ui_groups(IntPtr handle);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_voice_call(IntPtr handle,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_voice_answer(IntPtr handle,string call);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_voice_decline(IntPtr handle,string call);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern int ui_voice_end(IntPtr handle,string call);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_share(IntPtr handle,string host,ushort port,string peer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl, CharSet=CharSet.Unicode)] static extern IntPtr ui_add(IntPtr handle,string invite,string ownPeer);
    [DllImport(Library, CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_poll(IntPtr handle);
    IntPtr handle=ui_create();
    readonly JavaScriptSerializer serializer=new JavaScriptSerializer { MaxJsonLength=32*1024*1024 };
    public CoreAdapter(){if(handle==IntPtr.Zero)throw new InvalidOperationException("Could not create the peer core.");}
    public void Start(string directory,ushort port,string name){if(ui_start(handle,directory,port,name)==0)throw new InvalidOperationException("Could not start the peer core.");}
    public void Connect(string host,ushort port,string peer){if(ui_connect(handle,host,port,peer)==0)throw new InvalidOperationException("Invalid connection details.");}
    public void Approve(string peer,bool accepted){ui_approve(handle,peer,accepted?1:0);}
    public bool Send(string peer,string text){return ui_send(handle,peer,text)!=0;}
    public bool Remove(string peer){return ui_remove(handle,peer)!=0;}
    public void History(string peer,long before){ui_history(handle,peer,before);}
    public bool SetGroupAccount(long account,string name){return account>0&&ui_group_account(handle,(ulong)account,name)!=0;}
    public string CreateGroup(string name){return Marshal.PtrToStringUni(ui_group_create(handle,name));}
    public bool RenameGroup(string group,string name){return ui_group_rename(handle,group,name)!=0;}
    public bool InviteToGroup(string group,long account,string peer,string name){return account>0&&ui_group_invite(handle,group,(ulong)account,peer,name)!=0;}
    public bool RespondToGroupInvite(string group,bool accepted){return ui_group_respond(handle,group,accepted?1:0)!=0;}
    public bool LeaveGroup(string group){return ui_group_leave(handle,group)!=0;}
    public bool RemoveGroupMember(string group,long account){return account>0&&ui_group_remove_member(handle,group,(ulong)account)!=0;}
    public bool SetGroupAdmin(string group,long account,bool admin){return account>0&&ui_group_role(handle,group,(ulong)account,admin?1:0)!=0;}
    public bool SendGroup(string group,string text){return ui_group_send(handle,group,text)!=0;}
    public void LoadGroups(){ui_groups(handle);}
    public string StartVoiceCall(string peer){return Marshal.PtrToStringUni(ui_voice_call(handle,peer));}
    public bool AnswerVoiceCall(string call){return ui_voice_answer(handle,call)!=0;}
    public bool DeclineVoiceCall(string call){return ui_voice_decline(handle,call)!=0;}
    public bool EndVoiceCall(string call){return ui_voice_end(handle,call)!=0;}
    public string Share(string host,ushort port,string peer){return Marshal.PtrToStringUni(ui_share(handle,host,port,peer));}
    public string Add(string invite,string ownPeer){return Marshal.PtrToStringUni(ui_add(handle,invite,ownPeer));}
    public CoreEvent[] Poll(){return serializer.Deserialize<CoreEvent[]>(Marshal.PtrToStringUni(ui_poll(handle)));}
    public void Dispose(){if(handle!=IntPtr.Zero){ui_destroy(handle);handle=IntPtr.Zero;}}
}
public class CoreEvent { public int kind; public string peer,text,detail; public ushort port; public ContactData[] contacts; public MessageData[] messages; public GroupData[] groups; public GroupEventData[] groupEvents; }
public class ContactData { public string peer,name,host,publicHost; public ushort port,publicPort; public bool verified; }
public class MessageData { public string id,sender,text; public long time,logical,sequence; public int delivery; }
public class GroupData { public string id,name; public long owner; public GroupMemberData[] members,invites; }
public class GroupMemberData { public long account; public string peer,name; public int role; }
public class GroupEventData { public string id,group,sender,text; public long account,time,logical; }
}
