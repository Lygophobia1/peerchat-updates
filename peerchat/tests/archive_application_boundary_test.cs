using System;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using System.Threading;
using PeerChatDesktop;

static class ArchiveApplicationBoundaryTest {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_create();
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern void ui_destroy(IntPtr h);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_start(IntPtr h,string directory,ushort port,string name);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_authenticated_friends(IntPtr h,ulong account,ulong generation,string peers);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_archive(IntPtr h,uint op,byte[] input,uint length,[Out] byte[] output,uint capacity);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_network_context(IntPtr h,ulong generation,string fingerprint);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_poll(IntPtr h);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_ready(IntPtr h,ulong attempt,int incoming);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_receive(IntPtr h,ulong attempt,byte[] bytes,uint length);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_take_output(IntPtr h,ulong attempt,[Out] byte[] bytes,uint capacity,uint wait);
    static void Check(bool condition){if(!condition)throw new Exception("Archive C ABI regression");}
    sealed class Wire:IDisposable {
        MemoryStream stream=new MemoryStream();BinaryWriter writer;
        public Wire(uint op){writer=new BinaryWriter(stream);writer.Write(new byte[]{80,84,1,31});Text("PeerChat/ArchiveApplication/v1");Number(1);Number(op);}
        public void Number(ulong value){writer.Write(value);}
        public void Text(string value){Bytes(Encoding.UTF8.GetBytes(value));}
        public void Bytes(byte[] value){Number((ulong)value.Length);writer.Write(value);}
        public byte[] Value(){return stream.ToArray();}
        public void Dispose(){writer.Dispose();stream.Dispose();}
    }
    sealed class Record {
        byte[] bytes;int at=4;
        public Record(byte[] value){bytes=value;Check(value.Length>=4&&value[0]==80&&value[1]==84&&value[2]==1&&value[3]==31);}
        public ulong Number(){Check(at<=bytes.Length-8);ulong value=BitConverter.ToUInt64(bytes,at);at+=8;return value;}
        public byte[] Bytes(){ulong size=Number();Check(size<=(ulong)(bytes.Length-at));var value=new byte[(int)size];Buffer.BlockCopy(bytes,at,value,0,value.Length);at+=value.Length;return value;}
        public string Text(){return Encoding.UTF8.GetString(Bytes());}
        public void End(){Check(at==bytes.Length);}
    }
    static byte[] Request(IntPtr h,uint op,Wire wire,bool reject=false){
        var bytes=wire.Value();var output=new byte[262144];int size=ui_archive(h,op,bytes,(uint)bytes.Length,output,(uint)output.Length);
        if(reject){Check(size<0);return null;}Check(size>0&&size<=output.Length);Array.Resize(ref output,size);return output;
    }
    static byte[] Simple(IntPtr h,uint op,bool reject=false){using(var wire=new Wire(op))return Request(h,op,wire,reject);}
    static void Metadata(IntPtr h,string peer,byte[] descriptor,byte[] grant,ulong generation,ulong revision,ulong account=8,bool reject=false){
        using(var wire=new Wire(2)){
            wire.Number(account);wire.Number(generation);wire.Number(revision);wire.Text(peer);wire.Text("isolated-native-boundary-fixture");
            wire.Number(1);wire.Bytes(descriptor);wire.Number(grant==null?0UL:1UL);
            if(grant!=null){wire.Bytes(grant);wire.Number(1);}Request(h,2,wire,reject);
        }
    }
    static void Views(IntPtr h,string peer,bool approved,bool serving){
        var record=new Record(Simple(h,3));Check(record.Text()=="PeerChat/ArchiveApplication/v1"&&record.Number()==1&&record.Number()==3&&record.Number()==1);
        Check(record.Text()==peer&&record.Number()==1&&record.Number()==(approved?1UL:0UL)&&record.Number()==0&&record.Number()==(serving?1UL:0UL));record.End();
    }
    static string Device(byte[] descriptor){var outer=new Record(descriptor);var payload=outer.Bytes();outer.Bytes();outer.End();var r=new Record(payload);Check(r.Text()=="PeerChat/ArchiveDeviceDescriptor/v1"&&r.Number()==1&&r.Number()==8);return r.Text();}
    static void PairMetadata(IntPtr h,string peer,byte[][] descriptors,byte[][] grants,ulong revision){using(var w=new Wire(2)){
        w.Number(8);w.Number(1);w.Number(revision);w.Text(peer);w.Text("paired-node-fixture");w.Number((ulong)descriptors.Length);foreach(var d in descriptors)w.Bytes(d);
        w.Number((ulong)grants.Length);foreach(var g in grants){w.Bytes(g);w.Number(1);}Request(h,2,w);
    }}
    static ArchiveEnrollmentExecution Client(IntPtr h){return new ArchiveEnrollmentExecution((op,bytes)=>{var output=new byte[262144];int n=ui_archive(h,op,bytes,(uint)bytes.Length,output,(uint)output.Length);Check(n>0);Array.Resize(ref output,n);return output;},(op,fields)=>{throw new Exception("Unexpected fixture RPC");});}
    static ulong Relay(IntPtr h,out ArchiveRelayScope scope){scope=null;foreach(var item in new JavaScriptSerializer().Deserialize<object[]>(Marshal.PtrToStringUni(ui_poll(h)))){
        var row=(Dictionary<string,object>)item;if(Convert.ToInt32(row["kind"])!=18)continue;
        Check(Convert.ToString(row["authorizationGroupId"])=="");scope=ArchiveRelayScope.Parse(Convert.ToString(row["authorizationArchive"]));return Convert.ToUInt64(row["attempt"]);
    }return 0;}
    static void PairedNode(string root){var a=ui_create();var b=ui_create();try{
        Check(ui_start(a,Path.Combine(root,"pair-source"),0,"Source")==1&&ui_start(b,Path.Combine(root,"pair-reader"),0,"Reader")==1);
        Check(ui_authenticated_friends(a,8,1,"")==1&&ui_authenticated_friends(b,8,1,"")==1);
        Check(ui_network_context(a,1,"paired-fixture")==1&&ui_network_context(b,1,"paired-fixture")==1);
        var da=Simple(a,1);var db=Simple(b,1);var pa=Device(da);var pb=Device(db);var descriptors=new[]{da,db};
        PairMetadata(a,pa,descriptors,new byte[0][],1);PairMetadata(b,pb,descriptors,new byte[0][],1);
        foreach(var h in new[]{a,b})using(var approve=new Wire(4)){approve.Text(pa);approve.Number(1);Request(h,4,approve);}
        byte[] serve,read;using(var w=new Wire(5)){w.Text(pa);w.Number(2);w.Number(1);serve=Request(a,5,w);}
        using(var w=new Wire(5)){w.Text(pb);w.Number(1);w.Number(1);read=Request(a,5,w);}
        PairMetadata(a,pa,descriptors,new[]{serve,read},2);PairMetadata(b,pb,descriptors,new[]{serve,read},2);
        var source=Client(a);var reader=Client(b);reader.StartSync(8,1,pa,false);
        ulong ra=0,rb=0;ArchiveRelayScope scope;var until=DateTime.UtcNow.AddSeconds(10);
        while(rb==0&&DateTime.UtcNow<until){rb=Relay(b,out scope);if(rb!=0)source.IncomingArchive(8,1,scope);else Thread.Sleep(10);}
        while(ra==0&&DateTime.UtcNow<until){ra=Relay(a,out scope);if(ra==0)Thread.Sleep(10);}
        Check(ra>0&&rb>0&&ui_relay_ready(a,ra,0)==1&&ui_relay_ready(b,rb,1)==1);
        var bytes=new byte[65536];bool complete=false;
        while(DateTime.UtcNow<until){
            int n=ui_relay_take_output(a,ra,bytes,(uint)bytes.Length,0);if(n>0)Check(ui_relay_receive(b,rb,bytes,(uint)n)==1);
            n=ui_relay_take_output(b,rb,bytes,(uint)bytes.Length,0);if(n>0)Check(ui_relay_receive(a,ra,bytes,(uint)n)==1);
            var progress=reader.Progress(8,1);if(progress.Complete){Check(!progress.Running&&progress.Processed==0);complete=true;break;}Thread.Sleep(10);
        }
        Check(complete);Console.WriteLine("PASS: actual paired Node workers, zero friends, scoped opaque relay, mutual Schannel TLS, manifest completion and bounded retirement. HOST ONLY");
    }finally{ui_destroy(b);ui_destroy(a);}}
    public static int Main(string[] args){try{
        Check(args.Length==2&&LoadLibraryEx(Path.Combine(args[0],"PeerChat.Core.dll"),IntPtr.Zero,0x1100)!=IntPtr.Zero);
        IntPtr handle=ui_create();Check(handle!=IntPtr.Zero);byte[] descriptor=null,grant=null;string peer=null;
        try{
            for(int cycle=0;cycle<5;++cycle){
                Check(ui_start(handle,args[1],0,"Isolated archive test")==1);
                Simple(handle,1,true); // No cached prior-profile authentication.
                Check(ui_authenticated_friends(handle,8,1,"")==1);
                var current=Simple(handle,1);
                if(descriptor==null){
                    descriptor=current;var outer=new Record(descriptor);var payload=outer.Bytes();outer.Bytes();outer.End();
                    var record=new Record(payload);Check(record.Text()=="PeerChat/ArchiveDeviceDescriptor/v1"&&record.Number()==1&&record.Number()==8);peer=record.Text();
                }else Check(Convert.ToBase64String(current)==Convert.ToBase64String(descriptor));
                Metadata(handle,peer,descriptor,grant,1,1);
                Views(handle,peer,cycle>0,cycle>0);
                if(cycle==0){
                    using(var approve=new Wire(4)){approve.Text(peer);approve.Number(0);Request(handle,4,approve,true);}
                    Views(handle,peer,false,false);
                    using(var approve=new Wire(4)){approve.Text(peer);approve.Number(1);Request(handle,4,approve);}
                    using(var authorize=new Wire(5)){authorize.Text(peer);authorize.Number(2);authorize.Number(1);grant=Request(handle,5,authorize);}
                    Views(handle,peer,true,false); // Unpublished local signature is NOT permission.
                    Metadata(handle,peer,descriptor,grant,1,2);Views(handle,peer,true,true);
                }
                Metadata(handle,peer,descriptor,grant,1,3,9,true);Simple(handle,3,true);
                Metadata(handle,peer,descriptor,grant,1,4);Views(handle,peer,true,true);
                Check(ui_authenticated_friends(handle,0,2,"")==1);Simple(handle,3,true);
                Check(ui_authenticated_friends(handle,8,3,"")==1);Simple(handle,3,true); // Fresh login still needs fresh metadata.
                Metadata(handle,peer,descriptor,grant,3,1);Views(handle,peer,true,true);
            }
            Check(ui_authenticated_friends(handle,8,4,"")==1);
            byte[] registeredGrant=grant;bool wrongAccount=false,failPublication=false;int registrations=0,publications=0;
            var execution=new ArchiveEnrollmentExecution((op,input)=>{
                var output=new byte[262144];int n=ui_archive(handle,op,input,(uint)input.Length,output,(uint)output.Length);
                Check(n>0&&n<=output.Length);Array.Resize(ref output,n);return output;
            },(operation,fields)=>{
                var reply=new Dictionary<string,object>{{"ok",true},{"archiveVersion",1}};
                if(operation=="archive_descriptor_register"){
                    Check((string)fields["proof"]==Convert.ToBase64String(descriptor));registrations++;reply["state"]="accepted";
                }else if(operation=="archive_grant_register"){
                    if(failPublication)throw new IOException("Isolated ambiguous transport failure");
                    registeredGrant=Convert.FromBase64String((string)fields["proof"]);publications++;reply["state"]="accepted";
                }else{
                    Check(operation=="archive_authorization_state");reply["accountId"]=wrongAccount?9:8;reply["serverEpoch"]="isolated-application-rpc-fixture";
                    reply["devices"]=new[]{new Dictionary<string,object>{{"descriptor",Convert.ToBase64String(descriptor)}}};
                    reply["grants"]=new[]{new Dictionary<string,object>{{"grant",Convert.ToBase64String(registeredGrant)},{"currentlyEligible",publications==0}}};
                }
                return reply;
            });
            execution.Refresh(8,4,peer);Views(handle,peer,true,true);Check(publications==0);
            Check(execution.HistorySync(8,4));
            Check(!execution.HistorySync(8,4,false));Views(handle,peer,true,false);
            bool stalePolicy=false;try{execution.HistorySync(8,3,true);}catch(Exception){stalePolicy=true;}Check(stalePolicy);
            Check(!execution.HistorySync(8,4));Check(execution.HistorySync(8,4,true));Views(handle,peer,true,true);
            bool stale=false;try{execution.Consent(8,3,6,peer,0,true);}catch(Exception){stale=true;}Check(stale);Views(handle,peer,true,true);
            execution.Consent(8,4,6,peer,0,true);
            failPublication=true;bool failed=false;try{execution.PublishPending(8,4,peer);}catch(IOException){failed=true;}
            Check(failed&&publications==0);Simple(handle,3,true);
            failPublication=false;Check(execution.PublishPending(8,4,peer)==1&&publications==1);Views(handle,peer,true,false);
            Check(execution.PublishPending(8,4,peer)==0&&publications==1);
            wrongAccount=true;failed=false;try{execution.Refresh(8,4,peer);}catch(InvalidDataException){failed=true;}
            Check(failed);Simple(handle,3,true);wrongAccount=false;execution.Refresh(8,4,peer);Views(handle,peer,true,false);
            Check(registrations>=5&&ArchiveEnrollmentExecution.Devices(execution.Refresh(8,4,peer)).Length==1);
        }finally{ui_destroy(handle);}
        PairedNode(args[1]);
        Console.WriteLine("PASS: packaged Windows archive C ABI, five actual Node profile-worker restarts, exact registration identity, explicit consent, publication gating, account rejection, logout/relogin and fresh metadata. HOST ONLY");
        return 0;
    }catch(Exception e){Console.Error.WriteLine(e.Message);return 1;}}
}
