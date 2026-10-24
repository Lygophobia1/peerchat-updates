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
using System.Windows;

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
    DmAttachmentExecution dmAttachmentExecution;
    Task dmAttachmentNetworkTask=Task.FromResult(0);
    DiscoveryClient dmAttachmentNetworkOwner,dmAttachmentNetworkClient;
    ulong dmAttachmentGeneration,dmAttachmentProfile,dmAttachmentSession;
    long dmAttachmentAccount;
    string dmAttachmentToken="",dmAttachmentPin="",dmAttachmentPeer="";
    DateTime dmAttachmentNext=DateTime.MinValue;
    void StopDmAttachmentNetwork(){
        if(dmAttachmentExecution!=null)dmAttachmentExecution.Stop();dmAttachmentExecution=null;dmAttachmentNetworkOwner=null;dmAttachmentNetworkClient=null;
        try{core.FileTransfer(24,path:Convert.ToBase64String(DmAttachmentExecution.Session(0,++dmAttachmentGeneration,"")));}catch{}
    }
    void PumpDmAttachmentNetwork(){
        if(closing||busy)return;var owner=discovery;var client=groupAttachmentDeviceClient??owner;
        bool ready=owner!=null&&owner.AccountId>0&&owner.AccountId==mailboxContextAccount&&!String.IsNullOrEmpty(model.OwnId)&&client!=null&&!String.IsNullOrEmpty(client.SessionToken)&&client.CertificateSha256!=null&&client.CertificateSha256.Length==64&&
            mailboxService==owner&&mailboxExecutionSessionGeneration==discoverySessionGeneration&&mailboxExecutionProfileGeneration==profileSessionGeneration&&mailboxExecution!=null&&mailboxExecution.RegistrationAccepted;
        bool changed=!ready||owner!=dmAttachmentNetworkOwner||client!=dmAttachmentNetworkClient||profileSessionGeneration!=dmAttachmentProfile||discoverySessionGeneration!=dmAttachmentSession||client.AccountId!=dmAttachmentAccount||client.SessionToken!=dmAttachmentToken||client.CertificateSha256.ToLowerInvariant()!=dmAttachmentPin||model.OwnId!=dmAttachmentPeer;
        if(changed&&dmAttachmentExecution!=null)StopDmAttachmentNetwork();
        if(!ready||!dmAttachmentNetworkTask.IsCompleted||!groupAttachmentNetworkTask.IsCompleted||DateTime.UtcNow<dmAttachmentNext)return;
        if(dmAttachmentExecution==null){
            if(dmAttachmentProfile!=profileSessionGeneration||dmAttachmentAccount!=client.AccountId||dmAttachmentPin!=client.CertificateSha256.ToLowerInvariant()){dmHistoryProfile=profileSessionGeneration;dmHistoryCursor="";dmHistoryScope="";dmHistoryMore=true;++dmHistoryEpoch;foreach(var conversation in model.Conversations.Values)foreach(var row in conversation.Messages.Where(value=>value.Attachment!=null&&value.Attachment.dmCache).ToArray())conversation.Messages.Remove(row);}
            dmAttachmentNetworkOwner=owner;dmAttachmentNetworkClient=client;dmAttachmentProfile=profileSessionGeneration;dmAttachmentSession=discoverySessionGeneration;dmAttachmentAccount=client.AccountId;dmAttachmentToken=client.SessionToken;dmAttachmentPin=client.CertificateSha256.ToLowerInvariant();dmAttachmentPeer=model.OwnId;
            var generation=++dmAttachmentGeneration;try{core.FileTransfer(24,path:Convert.ToBase64String(DmAttachmentExecution.Session((ulong)client.AccountId,generation,dmAttachmentPin)));dmAttachmentExecution=new DmAttachmentExecution((op,input)=>core.FileTransfer(op,path:input),generation);}catch{dmAttachmentNext=DateTime.UtcNow.AddSeconds(1);return;}
        }
        var execution=dmAttachmentExecution;string device=dmAttachmentPeer,token=dmAttachmentToken,pin=dmAttachmentPin;long account=dmAttachmentAccount;ulong profile=dmAttachmentProfile,session=dmAttachmentSession;
        dmAttachmentNext=DateTime.UtcNow.AddSeconds(1);
        dmAttachmentNetworkTask=Task.Run(()=>execution.ServiceBurst(payload=>client.AttachmentCache(device,payload,account,token,pin))).ContinueWith(task=>{
            window.Dispatcher.BeginInvoke((Action)(()=>{if(closing||dmAttachmentExecution!=execution||discovery!=owner||profileSessionGeneration!=profile||discoverySessionGeneration!=session)return;dmAttachmentNext=DateTime.UtcNow.AddMilliseconds(!task.IsFaulted&&task.Result?0:1000);if(!task.IsFaulted&&task.Result)PumpDmAttachmentNetwork();}));if(task.IsFaulted){var ignored=task.Exception;}
        });
    }
    bool DmLegacyFallbackAllowed {get{return dmAttachmentExecution!=null&&new[]{"cache_dm_feature_gated","cache_unsupported_operation","cache_unsupported"}.Contains(dmAttachmentExecution.LastState);}}
    async Task<bool> EnsureDmAttachmentReady(bool notify=true){
        if(!await EnsureGroupAttachmentReady())return false;
        var owner=discovery;ulong profile=profileSessionGeneration;var wait=System.Diagnostics.Stopwatch.StartNew();
        while(wait.ElapsedMilliseconds<5000&&!closing&&owner==discovery&&profile==profileSessionGeneration){PumpDmAttachmentNetwork();var status=await Task.Run(()=>core.DmAttachments());if(status.supported)return true;await Task.Delay(100);}
        if(notify)Notify("Encrypted direct attachments require a verified device session and compatible PeerChat versions on the server and every recipient device. Update the devices and retry Attach.");return false;
    }
    DiscoveryClient groupAttachmentDeviceClient;
    Task<DiscoveryClient> groupAttachmentDeviceTask;
    Task groupAttachmentDeviceDrain=Task.FromResult(0);
    long groupAttachmentDeviceLifetime;
    bool groupAttachmentDeviceAttempted;
    string groupAttachmentDeviceError="";
    string groupAttachmentCapabilityDiagnostic="\ngroup_attachment_capability=not_observed";
    void RecordAttachmentCapability(CoreAdapter.GroupAttachmentSnapshot value){
        Func<string,string> safe=text=>text!=null&&text.Length<=64&&text.All(ch=>Char.IsLetterOrDigit(ch)||ch=='_')?text:"unavailable";
        groupAttachmentCapabilityDiagnostic="\ngroup_attachment_capability_stage="+safe(value.capabilityStage)+"\ngroup_attachment_capability_code="+safe(value.capabilityCode)+"\ngroup_attachment_capability_generation="+value.capabilityGeneration+"\ngroup_attachment_capability_ready="+value.supported;
        // Normal signed-in refresh drives setup, not an attachment click. Only
        // an exact current server rejection permits first enrollment; never
        // turn revocation, corrupt saved state, or a network error into enrollment.
        if(!value.supported&&value.capabilityCode=="cache_device_session_required"&&
            value.capabilityGeneration==groupAttachmentNetworkGeneration&&
            groupAttachmentExecution!=null&&!groupAttachmentDeviceAttempted&&
            groupAttachmentDeviceClient==null&&groupAttachmentDeviceTask==null){
            var owner=discovery;
            if(owner!=null&&AttachmentOwnerCurrent(owner,groupAttachmentNetworkProfile,groupAttachmentNetworkSession,groupAttachmentNetworkToken,groupAttachmentNetworkPeer)){
                bool saved=HasSavedAttachmentSession(owner);
                if(String.IsNullOrEmpty(groupAttachmentDeviceError)){var ignored=VerifyAttachmentDevice(!saved);}
            }
        }
    }
    bool HasSavedAttachmentSession(DiscoveryClient owner){try{return AttachmentDeviceSession.HasCredential(owner,owner.AccountId,model.OwnId,directory);}catch(Exception error){groupAttachmentDeviceError=AttachmentDeviceSession.SafeFailure(error);return false;}}
    bool AttachmentOwnerCurrent(DiscoveryClient owner,ulong profile,ulong session,string token,string device){return !closing&&discovery==owner&&profileSessionGeneration==profile&&discoverySessionGeneration==session&&owner.AccountId==mailboxContextAccount&&owner.SessionToken==token&&model.OwnId==device;}
    async Task<bool> VerifyAttachmentDevice(bool allowEnrollment){
        var owner=discovery;var profile=profileSessionGeneration;var session=discoverySessionGeneration;long lifetime=System.Threading.Interlocked.Read(ref groupAttachmentDeviceLifetime);string device=model.OwnId,token=owner==null?"":owner.SessionToken;
        if(owner==null||owner.AccountId<=0||owner.AccountId!=mailboxContextAccount||String.IsNullOrEmpty(device)||String.IsNullOrEmpty(token))return false;
        if(groupAttachmentDeviceClient!=null)return true;
        if(groupAttachmentDeviceTask==null){
            groupAttachmentDeviceAttempted=true;groupAttachmentDeviceError="";
            groupAttachmentDeviceTask=Task.Run(()=>AttachmentDeviceSession.Open(owner,core,owner.AccountId,device,directory,allowEnrollment,()=>System.Threading.Interlocked.Read(ref groupAttachmentDeviceLifetime)==lifetime&&AttachmentOwnerCurrent(owner,profile,session,token,device)));
        }
        var pending=groupAttachmentDeviceTask;
        try{
            var verified=await pending;
            if(!AttachmentOwnerCurrent(owner,profile,session,token,device)||groupAttachmentDeviceTask!=pending){if(groupAttachmentDeviceClient!=verified)verified.CloseRpc();return false;}
            if(groupAttachmentDeviceClient==null){groupAttachmentDeviceClient=verified;groupAttachmentNetworkNext=DateTime.MinValue;
                // A capability rejection belongs to the old session. Reset only
                // this native cache route; normal messaging remains untouched.
                if(groupAttachmentExecution!=null)groupAttachmentExecution.Stop();groupAttachmentExecution=null;
                core.GroupAttachmentNetwork(0,AttachmentNetworkCodec.Session((ulong)owner.AccountId,++groupAttachmentNetworkGeneration,owner.CertificateSha256.ToLowerInvariant()));
            }
            return true;
        }catch(Exception error){
            if(AttachmentOwnerCurrent(owner,profile,session,token,device)&&groupAttachmentDeviceTask==pending){groupAttachmentDeviceTask=null;groupAttachmentDeviceError=AttachmentDeviceSession.SafeFailure(error);groupAttachmentState=new GroupAttachmentSnapshotState{Profile=profile,Owner=owner,Reason=groupAttachmentDeviceError};}
            return false;
        }
    }
    async Task<bool> EnsureGroupAttachmentReady(){
        var owner=discovery;var profile=profileSessionGeneration;var session=discoverySessionGeneration;string token=owner==null?"":owner.SessionToken,device=model.OwnId;
        if(owner==null||owner.AccountId<=0||owner.AccountId!=mailboxContextAccount){Notify("Wait for authenticated account and device information, then retry Attach.");return false;}
        PumpGroupAttachmentNetwork();
        bool existing=HasSavedAttachmentSession(owner);
        if(!existing&&groupAttachmentDeviceClient==null&&groupAttachmentDeviceTask==null){
            // Never replace an already device-bound main credential. Only the
            // server's explicit device-session rejection permits enrollment.
            var probe=System.Diagnostics.Stopwatch.StartNew();bool verificationRequired=false;
            while(probe.ElapsedMilliseconds<5000&&AttachmentOwnerCurrent(owner,profile,session,token,device)){
                PumpGroupAttachmentNetwork();var status=await Task.Run(()=>core.GroupAttachments(""));
                if(!AttachmentOwnerCurrent(owner,profile,session,token,device))return false;
                RecordAttachmentCapability(status);
                groupAttachmentState=new GroupAttachmentSnapshotState{Supported=status.supported,Profile=profile,Owner=owner,Reason=status.reason};
                if(status.supported)return true;
                if(status.capabilityCode=="cache_device_session_required"){verificationRequired=true;break;}
                if(status.capabilityStage=="rejected"||status.capabilityStage=="invalid_response"||status.capabilityStage=="network_unavailable")return false;
                await Task.Delay(100);
            }
            if(!verificationRequired)return false;
        }
        if(groupAttachmentDeviceClient==null){
            if(!String.IsNullOrEmpty(groupAttachmentDeviceError))return false;
            if(!await VerifyAttachmentDevice(!existing))return false;
        }
        var wait=System.Diagnostics.Stopwatch.StartNew();
        while(wait.ElapsedMilliseconds<5000&&AttachmentOwnerCurrent(owner,profile,session,token,device)){
            PumpGroupAttachmentNetwork();
            var snapshot=await Task.Run(()=>core.GroupAttachments(""));
            if(!AttachmentOwnerCurrent(owner,profile,session,token,device))return false;
            RecordAttachmentCapability(snapshot);
            groupAttachmentState=new GroupAttachmentSnapshotState{Supported=snapshot.supported,Profile=profile,Owner=owner,Reason=snapshot.reason};
            if(snapshot.supported)return true;
            if(snapshot.capabilityStage=="rejected"||snapshot.capabilityStage=="device_verification_required")return false;
            await Task.Delay(100);
        }
        return false;
    }
    GroupAttachmentExecution groupAttachmentExecution;
    Task groupAttachmentNetworkTask=Task.FromResult(0);
    DiscoveryClient groupAttachmentNetworkOwner;
    string groupAttachmentNetworkToken="",groupAttachmentNetworkServer="",groupAttachmentNetworkPeer="";
    ulong groupAttachmentNetworkProfile,groupAttachmentNetworkSession,groupAttachmentNetworkGeneration;
    long groupAttachmentNetworkAccount;
    DateTime groupAttachmentNetworkNext=DateTime.MinValue;
    void StopGroupAttachmentNetwork(){
        StopDmAttachmentNetwork();
        groupAttachmentDeviceDrain=Task.WhenAll(groupAttachmentDeviceDrain,dmAttachmentNetworkTask);
        System.Threading.Interlocked.Increment(ref groupAttachmentDeviceLifetime);
        groupAttachmentCapabilityDiagnostic="\ngroup_attachment_capability=retired";
        if(groupAttachmentExecution!=null)groupAttachmentExecution.Stop();
        groupAttachmentExecution=null;groupAttachmentNetworkOwner=null;groupAttachmentNetworkToken="";groupAttachmentNetworkAccount=0;
        var cache=groupAttachmentDeviceClient;var pending=groupAttachmentDeviceTask;groupAttachmentDeviceClient=null;groupAttachmentDeviceTask=null;groupAttachmentDeviceAttempted=false;groupAttachmentDeviceError="";
        if(pending!=null)groupAttachmentDeviceDrain=Task.WhenAll(groupAttachmentDeviceDrain,pending.ContinueWith(task=>{if(task.IsFaulted){var observed=task.Exception;}else if(task.Status==TaskStatus.RanToCompletion)task.Result.CloseRpc();}));
        if(cache!=null)Task.Run(()=>cache.CloseRpc());
        try{core.GroupAttachmentNetwork(0,AttachmentNetworkCodec.Session(0,++groupAttachmentNetworkGeneration,""));}catch(EntryPointNotFoundException){}catch(ObjectDisposedException){}catch(InvalidOperationException){}
    }
    void PumpGroupAttachmentNetwork(){
        if(closing||busy)return;
        var owner=discovery;
        bool ready=owner!=null&&owner.AccountId>0&&owner.AccountId==mailboxContextAccount&&!String.IsNullOrEmpty(owner.SessionToken)&&!String.IsNullOrEmpty(model.OwnId)&&owner.CertificateSha256!=null&&owner.CertificateSha256.Length==64;
        bool changed=!ready||owner!=groupAttachmentNetworkOwner||groupAttachmentNetworkProfile!=profileSessionGeneration||groupAttachmentNetworkSession!=discoverySessionGeneration||groupAttachmentNetworkAccount!=owner.AccountId||groupAttachmentNetworkToken!=owner.SessionToken||groupAttachmentNetworkServer!=owner.CertificateSha256.ToLowerInvariant()||groupAttachmentNetworkPeer!=model.OwnId;
        if(changed&&(groupAttachmentExecution!=null||groupAttachmentDeviceClient!=null||groupAttachmentDeviceTask!=null))StopGroupAttachmentNetwork();
        if(!ready||!groupAttachmentNetworkTask.IsCompleted||DateTime.UtcNow<groupAttachmentNetworkNext)return;
        if(groupAttachmentDeviceClient==null&&!groupAttachmentDeviceAttempted&&HasSavedAttachmentSession(owner)){
            var ignored=VerifyAttachmentDevice(false);
        }
        if(groupAttachmentExecution==null){
            try{
                var generation=++groupAttachmentNetworkGeneration;
                core.GroupAttachmentNetwork(0,AttachmentNetworkCodec.Session((ulong)owner.AccountId,generation,owner.CertificateSha256.ToLowerInvariant()));
                groupAttachmentExecution=new GroupAttachmentExecution(core,generation);groupAttachmentNetworkOwner=owner;groupAttachmentNetworkToken=owner.SessionToken;groupAttachmentNetworkServer=owner.CertificateSha256.ToLowerInvariant();groupAttachmentNetworkPeer=model.OwnId;
                groupAttachmentNetworkProfile=profileSessionGeneration;groupAttachmentNetworkSession=discoverySessionGeneration;groupAttachmentNetworkAccount=owner.AccountId;groupAttachmentNetworkNext=DateTime.MinValue;
            }catch{groupAttachmentNetworkNext=DateTime.UtcNow.AddSeconds(1);return;}
        }
        if(DateTime.UtcNow<groupAttachmentNetworkNext)return;
        if(groupAttachmentDeviceClient!=null&&!groupAttachmentDeviceClient.MatchesAttachmentDeviceSession(groupAttachmentNetworkAccount,model.OwnId)){
            groupAttachmentDeviceError="Attachment device-session binding changed. Reconnect this profile before retrying.";return;
        }
        var execution=groupAttachmentExecution;var cacheOwner=groupAttachmentDeviceClient??owner;string device=model.OwnId,token=cacheOwner.SessionToken,certificate=groupAttachmentNetworkServer;long account=groupAttachmentNetworkAccount;ulong profile=profileSessionGeneration,session=discoverySessionGeneration;
        groupAttachmentNetworkNext=DateTime.UtcNow.AddSeconds(1);
        groupAttachmentNetworkTask=Task.Run(()=>{
            var budget=System.Diagnostics.Stopwatch.StartNew();bool active=false;
            for(int count=0;count<16;count++){
                active=execution.Service(payload=>cacheOwner.AttachmentCache(device,payload,account,token,certificate));
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
