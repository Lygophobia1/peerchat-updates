using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
public sealed partial class CoreAdapter {
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl)]
    static extern int ui_group_attachment_network(IntPtr handle,uint operation,byte[] input,uint length,[Out] byte[] output,uint capacity);
    internal byte[] GroupAttachmentNetwork(uint operation,byte[] input){
        if(input!=null&&input.Length>AttachmentNetworkCodec.MaximumBytes)throw new InvalidDataException("Attachment command exceeds bound.");
        lock(mailboxGate){
            if(handle==IntPtr.Zero)throw new ObjectDisposedException("CoreAdapter");
            var output=new byte[AttachmentNetworkCodec.MaximumBytes];
            int size=ui_group_attachment_network(handle,operation,input,(uint)(input==null?0:input.Length),output,(uint)output.Length);
            if(size<0||size>output.Length)throw new InvalidOperationException("Attachment command unavailable.");
            if(size==output.Length)return output;var result=new byte[size];Buffer.BlockCopy(output,0,result,0,size);return result;
        }
    }
}
// This local ABI only carries bounded JSON values. It is not an attachment,
// encryption, history, retry or server protocol implementation.
public static class AttachmentNetworkCodec {
    public const int MaximumBytes=100000,MaximumDepth=12;
    static readonly UTF8Encoding utf8=new UTF8Encoding(false,true);
    static void Bound(BinaryWriter writer,int additional){if(additional<0||writer.BaseStream.Length>MaximumBytes-additional)throw new InvalidDataException("Attachment response exceeds bound.");}
    static void Text(BinaryWriter writer,string value){var bytes=utf8.GetBytes(value??"");Bound(writer,checked(8+bytes.Length));writer.Write((ulong)bytes.Length);writer.Write(bytes);}
    static void Number(BinaryWriter writer,ulong value){Bound(writer,8);writer.Write(value);}
    static void Tag(BinaryWriter writer,byte value){Bound(writer,1);writer.Write(value);}
    static byte[] Record(string domain,Action<BinaryWriter> write){using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream,utf8)){writer.Write(new byte[]{80,84,1,30});Text(writer,domain);write(writer);writer.Flush();return stream.ToArray();}}
    public static byte[] Session(ulong account,ulong generation,string server){
        if(account>0&&(generation==0||server==null||server.Length!=64||server.Any(ch=>!(ch>='0'&&ch<='9'||ch>='a'&&ch<='f'))))throw new InvalidDataException("Attachment session binding invalid.");
        return Record("PeerChat/AttachmentSession/v1",w=>{Number(w,account);Number(w,generation);Text(w,account==0?"":server);});
    }
    public static byte[] Response(ulong generation,string id,Dictionary<string,object> response){return Record("PeerChat/AttachmentResponse/v1",w=>{Number(w,generation);Text(w,id);Value(w,response,0);});}
    public static byte[] Failure(ulong generation,string id){return Record("PeerChat/AttachmentFailure/v1",w=>{Number(w,generation);Text(w,id);Text(w,"network_unavailable");});}
    static void Value(BinaryWriter writer,object value,int depth){
        if(depth>MaximumDepth)throw new InvalidDataException("Attachment response nesting exceeds bound.");
        if(value==null){Tag(writer,0);return;}
        if(value is bool){Tag(writer,(bool)value?(byte)2:(byte)1);return;}
        if(value is string){Tag(writer,4);Text(writer,(string)value);return;}
        var dictionary=value as IDictionary<string,object>;
        if(dictionary!=null){Tag(writer,6);Number(writer,(ulong)dictionary.Count);foreach(var pair in dictionary){Text(writer,pair.Key);Value(writer,pair.Value,depth+1);}return;}
        var list=value as IList;
        if(list!=null){Tag(writer,5);Number(writer,(ulong)list.Count);foreach(var child in list)Value(writer,child,depth+1);return;}
        if(value is byte||value is ushort||value is uint||value is ulong||value is sbyte||value is short||value is int||value is long||value is decimal){
            decimal number=Convert.ToDecimal(value,CultureInfo.InvariantCulture);
            if(number<0||number>UInt64.MaxValue||Decimal.Truncate(number)!=number)throw new InvalidDataException("Invalid attachment response integer.");
            Tag(writer,3);Number(writer,(ulong)number);return;
        }
        throw new InvalidDataException("Unsupported attachment response value.");
    }
}
// One borrowed authenticated RPC at a time; native remains the only retry owner.
internal sealed class GroupAttachmentExecution {
    readonly object gate=new object();readonly Func<uint,byte[],byte[]> native;
    readonly ulong generation;bool stopped;
    internal GroupAttachmentExecution(CoreAdapter core,ulong generation):this((op,input)=>core.GroupAttachmentNetwork(op,input),generation){}
    internal GroupAttachmentExecution(Func<uint,byte[],byte[]> native,ulong generation){this.native=native;this.generation=generation;}
    internal void Stop(){lock(gate)stopped=true;}
    internal bool Deliver(uint operation,byte[] input){lock(gate){if(stopped)return false;native(operation,input);return true;}}
    internal bool Service(Func<Dictionary<string,object>,Dictionary<string,object>> request){
        byte[] pending;lock(gate){if(stopped)return false;pending=native(1,null);}
        if(pending.Length==0)return false;
        var item=new JavaScriptSerializer{MaxJsonLength=AttachmentNetworkCodec.MaximumBytes,RecursionLimit=16}.Deserialize<Dictionary<string,object>>(new UTF8Encoding(false,true).GetString(pending));
        if(item==null||item.Count==0)return false;
        object rawId,rawGeneration;string id;
        if(!item.TryGetValue("requestId",out rawId)||(id=rawId as string)==null||id.Length!=32||id.Any(ch=>!(ch>='0'&&ch<='9'||ch>='a'&&ch<='f'))||!item.TryGetValue("generation",out rawGeneration)||Convert.ToUInt64(rawGeneration,CultureInfo.InvariantCulture)!=generation)return false;
        item.Remove("generation");
        try{
            lock(gate){if(stopped)return false;}
            var response=request(item);
            return Deliver(2,AttachmentNetworkCodec.Response(generation,id,response));
        }catch{return Deliver(3,AttachmentNetworkCodec.Failure(generation,id));}
    }
}
public partial class DesktopApplication {
    GroupAttachmentExecution groupAttachmentExecution;
    Task groupAttachmentNetworkTask=Task.FromResult(0);
    DiscoveryClient groupAttachmentNetworkOwner;
    string groupAttachmentNetworkToken="",groupAttachmentNetworkServer="",groupAttachmentNetworkPeer="";
    ulong groupAttachmentNetworkProfile,groupAttachmentNetworkSession,groupAttachmentNetworkGeneration;
    long groupAttachmentNetworkAccount;
    DateTime groupAttachmentNetworkNext=DateTime.MinValue;
    void StopGroupAttachmentNetwork(){
        if(groupAttachmentExecution!=null)groupAttachmentExecution.Stop();
        groupAttachmentExecution=null;groupAttachmentNetworkOwner=null;groupAttachmentNetworkToken="";groupAttachmentNetworkAccount=0;
        try{core.GroupAttachmentNetwork(0,AttachmentNetworkCodec.Session(0,++groupAttachmentNetworkGeneration,""));}catch(EntryPointNotFoundException){}catch(ObjectDisposedException){}catch(InvalidOperationException){}
    }
    void PumpGroupAttachmentNetwork(){
        if(closing||busy)return;
        var owner=discovery;
        bool ready=owner!=null&&owner.AccountId>0&&owner.AccountId==mailboxContextAccount&&!String.IsNullOrEmpty(owner.SessionToken)&&!String.IsNullOrEmpty(model.OwnId)&&owner.CertificateSha256!=null&&owner.CertificateSha256.Length==64;
        bool changed=!ready||owner!=groupAttachmentNetworkOwner||groupAttachmentNetworkProfile!=profileSessionGeneration||groupAttachmentNetworkSession!=discoverySessionGeneration||groupAttachmentNetworkAccount!=owner.AccountId||groupAttachmentNetworkToken!=owner.SessionToken||groupAttachmentNetworkServer!=owner.CertificateSha256.ToLowerInvariant()||groupAttachmentNetworkPeer!=model.OwnId;
        if(changed&&groupAttachmentExecution!=null)StopGroupAttachmentNetwork();
        if(!ready||!groupAttachmentNetworkTask.IsCompleted||DateTime.UtcNow<groupAttachmentNetworkNext)return;
        if(groupAttachmentExecution==null){
            try{
                var generation=++groupAttachmentNetworkGeneration;
                core.GroupAttachmentNetwork(0,AttachmentNetworkCodec.Session((ulong)owner.AccountId,generation,owner.CertificateSha256.ToLowerInvariant()));
                groupAttachmentExecution=new GroupAttachmentExecution(core,generation);groupAttachmentNetworkOwner=owner;groupAttachmentNetworkToken=owner.SessionToken;groupAttachmentNetworkServer=owner.CertificateSha256.ToLowerInvariant();groupAttachmentNetworkPeer=model.OwnId;
                groupAttachmentNetworkProfile=profileSessionGeneration;groupAttachmentNetworkSession=discoverySessionGeneration;groupAttachmentNetworkAccount=owner.AccountId;groupAttachmentNetworkNext=DateTime.MinValue;
            }catch{groupAttachmentNetworkNext=DateTime.UtcNow.AddSeconds(1);return;}
        }
        if(DateTime.UtcNow<groupAttachmentNetworkNext)return;
        var execution=groupAttachmentExecution;string device=model.OwnId,token=groupAttachmentNetworkToken,certificate=groupAttachmentNetworkServer;long account=groupAttachmentNetworkAccount;ulong profile=profileSessionGeneration,session=discoverySessionGeneration;
        groupAttachmentNetworkNext=DateTime.UtcNow.AddSeconds(1);
        groupAttachmentNetworkTask=Task.Run(()=>{
            var budget=System.Diagnostics.Stopwatch.StartNew();bool active=false;
            for(int count=0;count<16;count++){
                active=execution.Service(payload=>owner.AttachmentCache(device,payload,account,token,certificate));
                if(!active||budget.ElapsedMilliseconds>=20)break;
            }
            return active;
        }).ContinueWith(task=>{
            // A stale completion cannot reenter a new profile or rebind its route.
            window.Dispatcher.BeginInvoke((Action)(()=>{
                if(closing||groupAttachmentExecution!=execution||discovery!=owner||profileSessionGeneration!=profile||discoverySessionGeneration!=session)return;
                groupAttachmentNetworkNext=DateTime.UtcNow.AddMilliseconds(!task.IsFaulted&&task.Result?0:1000);
                if(!task.IsFaulted&&task.Result)PumpGroupAttachmentNetwork();
            }));
            if(task.IsFaulted){var ignored=task.Exception;}
        });
    }
}
}
