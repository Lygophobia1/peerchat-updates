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
    public string Share(string host,ushort port,string peer){return Marshal.PtrToStringUni(ui_share(handle,host,port,peer));}
    public string Add(string invite,string ownPeer){return Marshal.PtrToStringUni(ui_add(handle,invite,ownPeer));}
    public CoreEvent[] Poll(){return serializer.Deserialize<CoreEvent[]>(Marshal.PtrToStringUni(ui_poll(handle)));}
    public void Dispose(){if(handle!=IntPtr.Zero){ui_destroy(handle);handle=IntPtr.Zero;}}
}
public class CoreEvent { public int kind; public string peer,text; public ushort port; public ContactData[] contacts; public MessageData[] messages; }
public class ContactData { public string peer,name,host,publicHost; public ushort port,publicPort; public bool verified; }
public class MessageData { public string id,sender,text; public long time,logical,sequence; public int delivery; }
}
