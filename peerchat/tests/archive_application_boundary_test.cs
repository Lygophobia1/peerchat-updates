using System;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Web.Script.Serialization;
using System.Threading;
using System.Linq;
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
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_history(IntPtr h,string peer,long before);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern IntPtr ui_file_transfer(IntPtr h,uint op,string peer,string id,string path);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_ready(IntPtr h,ulong attempt,int incoming);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern void ui_relay_closed(IntPtr h,ulong attempt,string reason);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_receive(IntPtr h,ulong attempt,byte[] bytes,uint length);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_relay_take_output(IntPtr h,ulong attempt,[Out] byte[] bytes,uint capacity,uint wait);
    static void Check(bool condition,[System.Runtime.CompilerServices.CallerLineNumber]int line=0){if(!condition)throw new Exception("Archive C ABI regression at line "+line);}
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
        var devices=ArchiveEnrollmentExecution.Devices(Simple(h,3));Check(devices.Length==1);
        var device=devices[0];Check(device.PeerId==peer&&device.Local&&device.ApprovedIssuer==approved&&!device.MayRead&&device.MayServe==serving);
        if(serving)Check(device.AuthorizationState=="Authorized");
    }
    static string Device(byte[] descriptor){var outer=new Record(descriptor);var payload=outer.Bytes();outer.Bytes();outer.End();var r=new Record(payload);Check(r.Text()=="PeerChat/ArchiveDeviceDescriptor/v1"&&r.Number()==1&&r.Number()==8);return r.Text();}
    static void PairMetadata(IntPtr h,string peer,byte[][] descriptors,byte[][] grants,ulong revision){using(var w=new Wire(2)){
        w.Number(8);w.Number(1);w.Number(revision);w.Text(peer);w.Text("paired-node-fixture");w.Number((ulong)descriptors.Length);foreach(var d in descriptors)w.Bytes(d);
        w.Number((ulong)grants.Length);foreach(var g in grants){w.Bytes(g);w.Number(1);}Request(h,2,w);
    }}
    static ArchiveEnrollmentExecution Client(IntPtr h){return new ArchiveEnrollmentExecution((op,bytes)=>{var output=new byte[262144];int n=ui_archive(h,op,bytes,(uint)bytes.Length,output,(uint)output.Length);Check(n>0);Array.Resize(ref output,n);return output;},(op,fields)=>{throw new Exception("Unexpected fixture RPC");});}
    static ulong Relay(IntPtr h,out ArchiveRelayScope scope){scope=null;foreach(var item in new JavaScriptSerializer().Deserialize<object[]>(Marshal.PtrToStringUni(ui_poll(h)))){
        var row=(Dictionary<string,object>)item;if(Convert.ToInt32(row["kind"])==8)Console.Error.WriteLine("Fixture native error: "+Convert.ToString(row["text"]));if(Convert.ToInt32(row["kind"])!=18)continue;
        Check(Convert.ToString(row["authorizationGroupId"])=="");scope=ArchiveRelayScope.Parse(Convert.ToString(row["authorizationArchive"]));return Convert.ToUInt64(row["attempt"]);
    }return 0;}
    static void PairedNode(string root){var a=ui_create();var b=ui_create();try{
        var seed=new System.Diagnostics.ProcessStartInfo(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"archive-attachment-fixture.exe"),"\""+root+"\""){UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true};
        string attachment;using(var process=System.Diagnostics.Process.Start(seed)){attachment=process.StandardOutput.ReadToEnd();string error=process.StandardError.ReadToEnd();process.WaitForExit();if(process.ExitCode!=0)throw new Exception(error);Check(attachment.Length==32);}
        Check(ui_start(a,Path.Combine(root,"pair-source"),0,"Source")==1&&ui_start(b,Path.Combine(root,"pair-reader"),0,"Reader")==1);
        Check(ui_authenticated_friends(a,8,1,"")==1&&ui_authenticated_friends(b,8,1,"")==1);
        Check(ui_network_context(a,1,"paired-fixture")==1&&ui_network_context(b,1,"paired-fixture")==1);
        var da=Simple(a,1);var db=Simple(b,1);var pa=Device(da);var pb=Device(db);var descriptors=new[]{da,db};
        PairMetadata(a,pa,descriptors,new byte[0][],1);PairMetadata(b,pb,descriptors,new byte[0][],1);
        foreach(var h in new[]{a,b})using(var approve=new Wire(4)){approve.Text(pa);approve.Number(1);Request(h,4,approve);}
        byte[] serve,read;using(var w=new Wire(5)){w.Text(pa);w.Number(2);w.Number(1);serve=Request(a,5,w);}
        read=Client(a).Consent(8,1,5,pb,1,true); // Same scoped explicit cross-device command as Windows Manage.
        PairMetadata(a,pa,descriptors,new[]{serve,read},2);PairMetadata(b,pb,descriptors,new[]{serve,read},2);
        var source=Client(a);var reader=Client(b);Check(reader.Progress(8,1).Running); // Actual native automatic authorized initial sync.
        var sourceRows=source.SnapshotDevices(8,1);var target=sourceRows.Single(d=>d.PeerId==pb);
        Check(target.ApprovalKnown&&target.MayAuthorize&&target.PairAuthorized&&target.LocalGrantPresent&&target.LocalGrantRead&&!target.LocalGrantServe&&!target.NeedsApproval);
        var sourceSelf=sourceRows.Single(d=>d.PeerId==pa);Check(!sourceSelf.LocalGrantRead&&sourceSelf.LocalGrantServe); // Existing one-way custom grants are not reset by projection.
        var recipient=reader.SnapshotDevices(8,1).Single(d=>d.PeerId==pa);Check(recipient.ApprovalKnown&&recipient.PairAuthorized&&!recipient.LocalGrantPresent&&!recipient.NeedsApproval);
        var admission=reader.AuthorizationDiagnostics(8,1);Check(admission.Length<=49152&&admission.Contains("local_device="+pb)&&admission.Contains("source_provide=allowed reader_receive=allowed"));
        Check(admission.Contains("History Sync execution")&&admission.Contains("active_scope_issuer="+pa)&&admission.Contains("archive_route")&&admission.Contains("messages_processed="));
        ulong ra=0,rb=0;ArchiveRelayScope scope;var until=DateTime.UtcNow.AddSeconds(10);
        while(rb==0&&DateTime.UtcNow<until){rb=Relay(b,out scope);if(rb!=0)source.IncomingArchive(8,1,scope);else Thread.Sleep(10);}
        while(ra==0&&DateTime.UtcNow<until){ra=Relay(a,out scope);if(ra==0)Thread.Sleep(10);}
        Check(ra>0&&rb>0&&ui_relay_ready(a,ra,0)==1&&ui_relay_ready(b,rb,1)==1);
        var bytes=new byte[65536];bool complete=false;
        while(DateTime.UtcNow<until){
            int n=ui_relay_take_output(a,ra,bytes,(uint)bytes.Length,0);if(n>0)Check(ui_relay_receive(b,rb,bytes,(uint)n)==1);
            n=ui_relay_take_output(b,rb,bytes,(uint)bytes.Length,0);if(n>0)Check(ui_relay_receive(a,ra,bytes,(uint)n)==1);
            var progress=reader.Progress(8,1);if(progress.Complete){Check(!progress.Running&&progress.Processed==25&&progress.Received==25&&progress.HistoryRevision>0&&progress.LastSuccessful>0);complete=true;break;}Thread.Sleep(10);
        }
        Check(complete);Check(reader.Progress(8,1).Attachments==1);
        Check(ui_history(b,new string('a',64),Int64.MaxValue)==1);
        int historyCount=0,localAuthors=0;until=DateTime.UtcNow.AddSeconds(3);
        while(historyCount==0&&DateTime.UtcNow<until){
            foreach(Dictionary<string,object> e in new JavaScriptSerializer().Deserialize<object[]>(Marshal.PtrToStringUni(ui_poll(b))))if(Convert.ToInt32(e["kind"])==6){
                foreach(Dictionary<string,object> message in (object[])e["messages"]){
                    historyCount++;string sender=(string)message["sender"];
                    Check(sender==pa||sender==new string('a',64));
                    Check(Convert.ToInt32(message["delivery"])==1); // Imported Sent, never manufactured Delivered.
                    Check((bool)message["localAuthor"]==(sender==pa));if((bool)message["localAuthor"])localAuthors++;
                }
            }
            if(historyCount==0)Thread.Sleep(10);
        }
        Check(historyCount==25&&localAuthors==12); // Preserved authors, rendered account-local without a DM route.
        // The byte-only fixture must also propagate EOF when history demand
        // retires. A real RelayTransport does this; silently leaving its old
        // opposite pipe alive would manufacture a stale winner in the harness.
        ui_relay_closed(a,ra,"Fixture propagates completed history relay EOF");ui_relay_closed(b,rb,"Fixture propagates completed history relay EOF");Thread.Sleep(100);
        var output=ui_file_transfer(b,5,pa,attachment,Path.Combine(root,"downloads"));Check(output!=IntPtr.Zero);var transfer=Marshal.PtrToStringUni(output);Check(transfer.Length==32);
        until=DateTime.UtcNow.AddSeconds(20);bool downloaded=false;
        while(DateTime.UtcNow<until){
            var request=Relay(b,out scope);if(request!=0){rb=request;source.IncomingArchive(8,1,scope);Check(ui_relay_ready(b,rb,1)==1);}
            request=Relay(a,out scope);if(request!=0){ra=request;Check(ui_relay_ready(a,ra,0)==1);}
            int n=ui_relay_take_output(a,ra,bytes,(uint)bytes.Length,0);if(n>0)Check(ui_relay_receive(b,rb,bytes,(uint)n)==1);
            n=ui_relay_take_output(b,rb,bytes,(uint)bytes.Length,0);if(n>0)Check(ui_relay_receive(a,ra,bytes,(uint)n)==1);
            var rows=new JavaScriptSerializer().Deserialize<object[]>(Marshal.PtrToStringUni(ui_file_transfer(b,0,"","","")));
            foreach(Dictionary<string,object> row in rows)if((string)row["id"]==transfer&&Convert.ToInt32(row["state"])==4)downloaded=true;
            if(downloaded)break;Thread.Sleep(10);
        }
        if(!downloaded){Console.Error.WriteLine("Reader transfer states: "+Marshal.PtrToStringUni(ui_file_transfer(b,0,"","","")));Console.Error.WriteLine("Source transfer states: "+Marshal.PtrToStringUni(ui_file_transfer(a,0,"","","")));}
        Check(downloaded);var files=Directory.GetFiles(Path.Combine(root,"downloads"));Check(files.Length==1);
        Check(Convert.ToBase64String(File.ReadAllBytes(files[0]))==Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,"source-file.bin"))));
        Console.WriteLine("PASS: actual paired Node workers, zero friends, scoped opaque relay, mutual Schannel TLS, catalog sync and verified Unicode attachment download through ONE TransferManager. HOST ONLY");
    }finally{ui_destroy(b);ui_destroy(a);}}
    public static int Main(string[] args){try{
        Check(ArchiveEnrollmentExecution.IsArchiveEvent("scope","a","b",8,"TLS failed"));
        Check(ArchiveEnrollmentExecution.IsArchiveEvent("","archive-control:8","",17,"Connecting"));
        Check(ArchiveEnrollmentExecution.IsArchiveEvent("","","",8,"Archive authorization/provenance rejected"));
        Check(!ArchiveEnrollmentExecution.IsArchiveEvent("","a","b",8,"TLS failed"));
        Check(new ArchiveEnrollmentExecution.SyncProgress{State="Connecting to archive",Running=true}.DisplayState=="Connecting");
        Check(new ArchiveEnrollmentExecution.SyncProgress{State="Archive authorization unavailable",Running=true}.DisplayState=="Waiting");
        Check(new ArchiveEnrollmentExecution.SyncProgress{State="Archive synchronization failed closed"}.DisplayState=="Error");
        Check(new ArchiveEnrollmentExecution.SyncProgress{State="History synchronized",Complete=true}.DisplayState=="Complete");
        Check(args.Length==2&&LoadLibraryEx(Path.Combine(args[0],"PeerChat.Core.dll"),IntPtr.Zero,0x1100)!=IntPtr.Zero);
        IntPtr handle=ui_create();Check(handle!=IntPtr.Zero);byte[] descriptor=null,grant=null;string peer=null;
        try{
            for(int cycle=0;cycle<5;++cycle){
                Check(ui_start(handle,args[1],0,"Isolated archive test")==1);
                Simple(handle,1,true); // No cached prior-profile authentication.
                bool archiveError=false;
                foreach(Dictionary<string,object> e in new JavaScriptSerializer().Deserialize<object[]>(Marshal.PtrToStringUni(ui_poll(handle))))if(Convert.ToInt32(e["kind"])==8){
                    Check(ArchiveEnrollmentExecution.IsArchiveEvent("",(string)e["peer"],(string)e["expected"],8,(string)e["text"]));archiveError=true;
                }
                Check(archiveError); // Archive rejection cannot be projected as a friend failure.
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
            var safeAdmission=execution.AuthorizationDiagnostics(8,4);Check(safeAdmission.Contains("account=8")&&safeAdmission.Contains("local_generation=1")&&safeAdmission.Contains("server_generation=1"));
            var service=new PeerChatDesktop.DiagnosticsService(new PeerChatDesktop.DiagnosticsSources{HistorySync=()=>safeAdmission});
            Check(PeerChatDesktop.DiagnosticsService.FormatFull(service.Capture()).Contains("local_device="+peer));
            Check(execution.HistorySync(8,4));
            Check(!execution.HistorySync(8,4,false));Views(handle,peer,true,false);
            bool stalePolicy=false;try{execution.HistorySync(8,3,true);}catch(Exception){stalePolicy=true;}Check(stalePolicy);
            Check(!execution.HistorySync(8,4));Check(execution.HistorySync(8,4,true));Views(handle,peer,true,true);
            bool stale=false;try{execution.Consent(8,3,6,peer,0,true);}catch(Exception){stale=true;}Check(stale&&execution.LastFailedStage=="native_operation_10");Views(handle,peer,true,true);
            execution.Consent(8,4,6,peer,0,true);
            failPublication=true;bool failed=false;try{execution.PublishPending(8,4,peer);}catch(IOException){failed=true;}
            Check(failed&&publications==0&&execution.LastFailedStage=="server_archive_grant_register");Simple(handle,3,true);
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
