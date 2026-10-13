using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections;
using System.Collections.Generic;

namespace PeerChatDesktop {
// Immutable scope copied from native admission or an authenticated server hint.
// This adapter never grants trust: native revalidates signed grants independently.
public sealed class ArchiveRelayScope {
    public long Account,SourceGeneration,ReaderGeneration;
    public string Issuer,Source,Reader,SourceGrantId,ReaderGrantId;
    static bool Id(string value,int size){return value!=null&&value.Length==size&&value.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f');}
    void Validate(){if(Account<=0||SourceGeneration<=0||ReaderGeneration<=0||!Id(Issuer,64)||!Id(Source,64)||!Id(Reader,64)||Source==Reader||!Id(SourceGrantId,32)||!Id(ReaderGrantId,32))throw new InvalidDataException("Invalid archive route scope.");}
    public static ArchiveRelayScope Parse(string encoded){
        if(encoded==null||encoded.Length>1400)throw new InvalidDataException("Invalid archive route scope.");
        using(var stream=new MemoryStream(Convert.FromBase64String(encoded),false))using(var r=new BinaryReader(stream,new UTF8Encoding(false,true))){
            Func<string> text=()=>{long n=r.ReadInt64();if(n<0||n>128||n>stream.Length-stream.Position)throw new InvalidDataException("Invalid archive scope field.");return new UTF8Encoding(false,true).GetString(r.ReadBytes((int)n));};
            if(!r.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,32})||text()!="PeerChat/ArchiveSession/v1"||r.ReadInt64()!=1||r.ReadInt64()!=1)throw new InvalidDataException("Invalid archive scope version.");
            var scope=new ArchiveRelayScope{Account=r.ReadInt64(),Issuer=text(),Source=text(),Reader=text(),SourceGeneration=r.ReadInt64(),ReaderGeneration=r.ReadInt64(),SourceGrantId=text(),ReaderGrantId=text()};
            scope.Validate();if(stream.Position!=stream.Length)throw new InvalidDataException("Trailing archive scope data.");return scope;
        }
    }
    public Dictionary<string,object> Fields(){Validate();return new Dictionary<string,object>{{"purpose","archive_history"},{"archiveIssuer",Issuer},{"archiveSource",Source},{"archiveReader",Reader},{"archiveSourceGeneration",SourceGeneration.ToString()},{"archiveReaderGeneration",ReaderGeneration.ToString()},{"archiveSourceGrantId",SourceGrantId},{"archiveReaderGrantId",ReaderGrantId}};}
    public static ArchiveRelayScope FromEvent(Dictionary<string,object> fields,long account){
        var scope=new ArchiveRelayScope{Account=account,Issuer=Convert.ToString(fields["archiveIssuer"]),Source=Convert.ToString(fields["archiveSource"]),Reader=Convert.ToString(fields["archiveReader"]),
            SourceGeneration=Convert.ToInt64(fields["archiveSourceGeneration"]),ReaderGeneration=Convert.ToInt64(fields["archiveReaderGeneration"]),SourceGrantId=Convert.ToString(fields["archiveSourceGrantId"]),ReaderGrantId=Convert.ToString(fields["archiveReaderGrantId"])};
        scope.Validate();return scope;
    }
    public void Verify(Dictionary<string,object> reply){foreach(var field in Fields()){object value;if(!reply.TryGetValue(field.Key,out value)||Convert.ToString(value)!=Convert.ToString(field.Value))throw new InvalidDataException("Archive scope changed during rendezvous.");}}
    public byte[] Binding(){Validate();using(var stream=new MemoryStream())using(var w=new BinaryWriter(stream,Encoding.UTF8)){
        Action<string> text=value=>{var bytes=Encoding.UTF8.GetBytes(value);w.Write((long)bytes.Length);w.Write(bytes);};
        w.Write(new byte[]{80,84,1,32});text("PeerChat/ArchiveSession/v1");w.Write(1L);w.Write(1L);w.Write(Account);text(Issuer);text(Source);text(Reader);w.Write(SourceGeneration);w.Write(ReaderGeneration);text(SourceGrantId);text(ReaderGrantId);return stream.ToArray();}}
}
// One synchronous application operation, called on the existing profile worker.
// No socket, timer, retry loop, signing decision or durable queue is owned here.
internal sealed class ArchiveEnrollmentExecution {
    readonly Func<uint,byte[],byte[]> native;
    readonly Func<string,Dictionary<string,object>,Dictionary<string,object>> rpc;
    long revision;
    public string LastFailedStage {get;private set;}
    public ArchiveEnrollmentExecution(Func<uint,byte[],byte[]> native,
        Func<string,Dictionary<string,object>,Dictionary<string,object>> rpc) {
        this.native=(operation,input)=>{try{return native(operation,input);}catch{LastFailedStage="native_operation_"+operation;throw;}};
        this.rpc=(operation,fields)=>{try{return rpc(operation,fields);}catch{LastFailedStage=operation=="archive_descriptor_register"||operation=="archive_grant_register"||operation=="archive_authorization_state"?"server_"+operation:"server_archive_request";throw;}};
    }
    static void Check(bool value){if(!value)throw new InvalidDataException("Archive metadata rejected.");}
    static string Text(Dictionary<string,object> row,string name){object value;return row.TryGetValue(name,out value)?value as string:null;}
    static byte[] Proof(string value,int max){
        Check(value!=null&&value.Length<=((max+2)/3)*4);
        var bytes=Convert.FromBase64String(value);Check(bytes.Length>0&&bytes.Length<=max);return bytes;
    }
    static Dictionary<string,object>[] Rows(Dictionary<string,object> row,string name,int max){
        object value;Check(row.TryGetValue(name,out value)&&value is IEnumerable&&!(value is string));
        var result=new List<Dictionary<string,object>>();
        foreach(var item in (IEnumerable)value){Check(result.Count<max&&item is Dictionary<string,object>);result.Add((Dictionary<string,object>)item);}
        return result.ToArray();
    }
    static byte[] Command(uint operation,Action<BinaryWriter> fields=null){
        using(var output=new MemoryStream())using(var w=new BinaryWriter(output,Encoding.UTF8)){
            w.Write(new byte[]{80,84,1,31});WriteText(w,"PeerChat/ArchiveApplication/v1");w.Write(1L);w.Write((long)operation);
            if(fields!=null)fields(w);Check(output.Length<=262144);return output.ToArray();
        }
    }
    static void WriteBytes(BinaryWriter w,byte[] value){Check(value.Length<=65536);w.Write((long)value.Length);w.Write(value);}
    static void WriteText(BinaryWriter w,string value){WriteBytes(w,new UTF8Encoding(false,true).GetBytes(value));}
    Dictionary<string,object> Request(string operation,Dictionary<string,object> fields){
        var result=rpc(operation,fields);object ok;Check(result!=null&&result.TryGetValue("ok",out ok)&&Boolean.Equals(ok,true));
        object version;Check(result.TryGetValue("archiveVersion",out version)&&Convert.ToInt64(version)==1);return result;
    }
    void Invalidate(){native(9,Command(9));}
    public string AuthorizationDiagnostics(long account,long generation){
        var bytes=native(22,Command(22,w=>{w.Write(account);w.Write(generation);}));var header=Command(22);
        Check(bytes.Length>=header.Length+8&&bytes.Take(header.Length).SequenceEqual(header));
        long length=BitConverter.ToInt64(bytes,header.Length);Check(length>=0&&length<=49152&&bytes.Length==header.Length+8+length);
        return new UTF8Encoding(false,true).GetString(bytes,header.Length+8,(int)length);
    }
    public void StartSync(long account,long generation,string source,bool newScan){native(12,Command(12,w=>{w.Write(account);w.Write(generation);WriteText(w,source);w.Write(newScan?1L:0L);}));}
    public void PauseSync(long account,long generation){native(14,Command(14,w=>{w.Write(account);w.Write(generation);}));}
    public bool AutomaticSync(long account,long generation,string peer,bool? enabled=null){
        var bytes=native(19,Command(19,w=>{w.Write(account);w.Write(generation);WriteText(w,peer);w.Write(enabled.HasValue?1L:0L);if(enabled.HasValue)w.Write(enabled.Value?1L:0L);}));
        var header=Command(19);Check(bytes.Length==header.Length+8&&bytes.Take(header.Length).SequenceEqual(header));long value=BitConverter.ToInt64(bytes,header.Length);Check(value==0||value==1);return value==1;
    }
    public string[] Providing(long account,long generation){
        var bytes=native(20,Command(20,w=>{w.Write(account);w.Write(generation);}));var header=Command(20);Check(bytes.Length>=header.Length&&bytes.Take(header.Length).SequenceEqual(header));
        using(var stream=new MemoryStream(bytes,false))using(var r=new BinaryReader(stream,Encoding.UTF8)){stream.Position=header.Length;long count=r.ReadInt64();Check(count>=0&&count<=8);var result=new List<string>();for(long i=0;i<count;i++){Check(r.ReadInt64()==64);var peer=Encoding.ASCII.GetString(r.ReadBytes(64));Check(peer.Length==64&&peer.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f'));result.Add(peer);}Check(stream.Position==stream.Length);return result.Distinct().ToArray();}
    }
    public void IncomingArchive(long account,long generation,ArchiveRelayScope scope){native(15,Command(15,w=>{w.Write(account);w.Write(generation);WriteBytes(w,scope.Binding());}));}
    public sealed class SyncProgress {
        public string State,Source;public long Processed,Conversations,Attachments,Received,HistoryRevision,LastSuccessful,ScanGeneration;public bool Complete,Running,Enabled;
        public long Deferred,RejectedBatches;
        public int Activity=-1,LastResult;public long ResultAt,ResultImported,ResultConversations,ResultDeferred,ProvidedQueued;public string ResultSource="";public string[] Providers=new string[0];
        public string CurrentActivity {get{return Activity<0?DisplayState:new[]{"Idle","Connecting","Importing","Waiting","Paused","Off","Failed"}[Activity];}}
        public string LastResultSummary {get{return LastResult==0?"No completed result recorded":new[]{"None","Full","Partial","Failed","Cancelled"}[LastResult]+" · "+ResultImported+" messages imported · "+ResultConversations+" conversations completed · "+ResultDeferred+" unresolved records";}}
        public string DisplayState {get {
            string value=(State??"").ToLowerInvariant();
            if(value.StartsWith("partial history",StringComparison.Ordinal))return "Partial";
            if(value.Contains("failed")||value.Contains("rejected"))return "Error";
            if(value.Contains("off")||value.Contains("paused"))return "Paused";
            if(!Running&&(Complete||value=="history synchronized"))return "Complete";
            if(value.Contains("unavailable")||value.Contains("waiting")||value.Contains("timed out"))return "Waiting";
            if(value.Contains("synchronizing"))return "Syncing";
            if(value.Contains("connecting"))return "Connecting";
            return "Idle";
        }}
        public string SourceState {get {return DisplayState=="Syncing"?"Available":DisplayState=="Connecting"?"Connecting":"Unavailable";}}
        public string Summary {get {return "Archive source: "+SourceState+" · Sync: "+DisplayState+"\n"+(State??"")+" · "+Processed+" messages processed · "+Conversations+" conversations completed · "+Attachments+" attachment records (not downloaded)";}}
    }
    public static bool IsArchiveEvent(string scope,string peer,string expected,int kind,string text){
        return !String.IsNullOrEmpty(scope)||(peer??"").StartsWith("archive-control:",StringComparison.Ordinal)||
            (expected??"").StartsWith("archive-control:",StringComparison.Ordinal)||(kind==8&&(text??"").StartsWith("Archive ",StringComparison.Ordinal));
    }
    public SyncProgress Progress(long account,long generation){
        var bytes=native(23,Command(23,w=>{w.Write(account);w.Write(generation);}));var header=Command(23);Check(bytes.Length>=header.Length&&bytes.Take(header.Length).SequenceEqual(header));
        using(var stream=new MemoryStream(bytes,false))using(var r=new BinaryReader(stream,new UTF8Encoding(false,true))){stream.Position=header.Length;
            Func<string> text=()=>{long n=r.ReadInt64();Check(n>=0&&n<=128&&n<=stream.Length-stream.Position);return new UTF8Encoding(false,true).GetString(r.ReadBytes((int)n));};
            var result=new SyncProgress{State=text(),Processed=r.ReadInt64(),Conversations=r.ReadInt64()};long complete=r.ReadInt64(),running=r.ReadInt64();result.Source=text();result.Attachments=r.ReadInt64();
            result.Received=r.ReadInt64();result.HistoryRevision=r.ReadInt64();result.LastSuccessful=r.ReadInt64();result.ScanGeneration=r.ReadInt64();
            long enabled=r.ReadInt64();Check(enabled==0||enabled==1);result.Enabled=enabled==1;
            if(stream.Position<stream.Length){Check(text()=="PeerChat/ArchiveScanAccounting/v1");result.Deferred=r.ReadInt64();result.RejectedBatches=r.ReadInt64();Check(result.Deferred>=0&&result.RejectedBatches>=0);}
            if(stream.Position<stream.Length){Check(text()=="PeerChat/ArchiveStatus/v1");long activity=r.ReadInt64(),outcome=r.ReadInt64();Check(activity>=0&&activity<=6&&outcome>=0&&outcome<=4);result.Activity=(int)activity;result.LastResult=(int)outcome;result.ResultAt=r.ReadInt64();result.ResultImported=r.ReadInt64();result.ResultConversations=r.ReadInt64();result.ResultDeferred=r.ReadInt64();result.ResultSource=text();result.ProvidedQueued=r.ReadInt64();long count=r.ReadInt64();Check(count>=0&&count<=8&&result.ResultAt>=0&&result.ResultAt<=253402300799999L&&result.ResultImported>=0&&result.ResultConversations>=0&&result.ResultDeferred>=0&&result.ProvidedQueued>=0);var peers=new List<string>();for(long n=0;n<count;n++){string peer=text();Check(peer.Length==64);peers.Add(peer);}result.Providers=peers.ToArray();}
            Check(result.Received>=0&&result.HistoryRevision>=0&&result.LastSuccessful>=0&&result.LastSuccessful<=253402300799999L&&result.ScanGeneration>=0);
            Check(result.Processed>=0&&result.Conversations>=0&&result.Attachments>=0&&(complete==0||complete==1)&&(running==0||running==1)&&stream.Position==stream.Length);result.Complete=complete==1;result.Running=running==1;return result;
        }
    }
    public bool HistorySync(long account,long generation,bool? enabled=null){
        var bytes=native(11,Command(11,w=>{w.Write(account);w.Write(generation);w.Write(enabled.HasValue?1L:0L);if(enabled.HasValue)w.Write(enabled.Value?1L:0L);}));
        var header=Command(11);Check(bytes.Length==header.Length+8&&bytes.Take(header.Length).SequenceEqual(header));
        long value=BitConverter.ToInt64(bytes,header.Length);Check(value==0||value==1);return value==1;
    }
    void RefreshState(long account,long generation,string device){
        var reply=Request("archive_authorization_state",new Dictionary<string,object>());
        object id;Check(reply.TryGetValue("accountId",out id)&&Convert.ToInt64(id)==account);
        string epoch=Text(reply,"serverEpoch");Check(!String.IsNullOrEmpty(epoch)&&epoch.Length<=128);
        var devices=Rows(reply,"devices",32);var grants=Rows(reply,"grants",64);
        Check(revision<Int64.MaxValue);long next=revision=Math.Max(revision+1,System.Diagnostics.Stopwatch.GetTimestamp());
        var command=Command(2,w=>{
            w.Write(account);w.Write(generation);w.Write(next);WriteText(w,device);WriteText(w,epoch);
            w.Write((long)devices.Length);foreach(var row in devices)WriteBytes(w,Proof(Text(row,"descriptor"),1024));
            w.Write((long)grants.Length);foreach(var row in grants){
                WriteBytes(w,Proof(Text(row,"grant"),2048));object eligible;
                Check(row.TryGetValue("currentlyEligible",out eligible)&&eligible is bool);w.Write((bool)eligible?1L:0L);
            }
        });
        native(2,command); // Native verifies signatures, identity, consent and freshness.
    }
    public byte[] RefreshCurrent(long account,long generation,string device){
        try{RefreshState(account,generation,device);return native(3,Command(3));}
        catch{Invalidate();throw;}
    }
    public byte[] Refresh(long account,long generation,string device){
        try{
            Invalidate();Check(account>0&&generation>0&&device!=null&&device.Length==64&&device.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f'));
            var descriptor=native(1,Command(1));Check(descriptor.Length>0&&descriptor.Length<=1024);
            Check(Text(Request("archive_descriptor_register",new Dictionary<string,object>{{"proof",Convert.ToBase64String(descriptor)}}),"state")=="accepted");
            RefreshState(account,generation,device);
            return native(3,Command(3));
        }catch{Invalidate();throw;}
    }
    public int PublishPending(long account,long generation,string device){
        try{
            Refresh(account,generation,device);var pending=new List<byte[]>();
            var bytes=native(8,Command(8));Check(bytes.Length>=4&&bytes.Length<=262144);
            using(var stream=new MemoryStream(bytes,false))using(var r=new BinaryReader(stream,new UTF8Encoding(false,true))){
                Check(r.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,31}));
                Func<byte[]> blob=()=>{long n=r.ReadInt64();Check(n>=0&&n<=65536&&n<=stream.Length-stream.Position);return r.ReadBytes((int)n);};
                Check(Encoding.UTF8.GetString(blob())=="PeerChat/ArchiveApplication/v1"&&r.ReadInt64()==1&&r.ReadInt64()==8);
                long count=r.ReadInt64();Check(count>=0&&count<=32);
                for(long i=0;i<count;i++){var proof=blob();Check(proof.Length>0&&proof.Length<=2048);pending.Add(proof);}
                Check(stream.Position==stream.Length);
            }
            int sent=0;
            foreach(var proof in pending.Take(4)){
                Check(Text(Request("archive_grant_register",new Dictionary<string,object>{{"proof",Convert.ToBase64String(proof)}}),"state")=="accepted");sent++;
            }
            RefreshState(account,generation,device);return sent;
        }catch{Invalidate();throw;} // Ambiguous publication remains durably pending; no immediate replay.
    }
    public byte[] Consent(long account,long generation,uint action,string device,long capabilities,bool consent){
        Check(account>0&&generation>0&&action>=4&&action<=7&&device!=null&&device.Length==64&&device.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f')&&capabilities>=0&&capabilities<=3);
        return native(10,Command(10,w=>{w.Write(account);w.Write(generation);w.Write((long)action);WriteText(w,device);if(action==5)w.Write(capabilities);w.Write(consent?1L:0L);}));
    }
    public Device[] SnapshotDevices(long account,long generation){
        var rows=Devices(native(3,Command(3)));foreach(var d in rows)d.Automatic=AutomaticSync(account,generation,d.PeerId);
        var bytes=native(21,Command(21,w=>{w.Write(account);w.Write(generation);w.Write(2L);}));var header=Command(21);Check(bytes.Length>=header.Length&&bytes.Take(header.Length).SequenceEqual(header));
        using(var stream=new MemoryStream(bytes,false))using(var r=new BinaryReader(stream,Encoding.UTF8)){stream.Position=header.Length;Check(r.ReadInt64()==rows.Length);var seen=new HashSet<string>();foreach(var item in rows){Check(r.ReadInt64()==64);var peer=Encoding.ASCII.GetString(r.ReadBytes(64));var d=rows.SingleOrDefault(row=>row.PeerId==peer);Check(d!=null&&seen.Add(peer));long read=r.ReadInt64(),serve=r.ReadInt64();Check(read>=0&&read<=1&&serve>=0&&serve<=1);d.GrantedRead=read==1;d.GrantedServe=serve==1;}Check(ReadApproval(r,rows));Check(stream.Position==stream.Length);}return rows;
    }
    static bool ReadApproval(BinaryReader r,Device[] rows){
        Func<string> text=()=>{long n=r.ReadInt64();Check(n>=0&&n<=128&&n<=r.BaseStream.Length-r.BaseStream.Position);return new UTF8Encoding(false,true).GetString(r.ReadBytes((int)n));};
        Func<bool> flag=()=>{long n=r.ReadInt64();Check(n==0||n==1);return n==1;};
        Check(text()=="PeerChat/ArchiveDeviceApproval/v2"&&r.ReadInt64()==rows.Length);
        var seen=new HashSet<string>();foreach(var item in rows){string peer=text();var d=rows.SingleOrDefault(row=>row.PeerId==peer);Check(d!=null&&seen.Add(peer));d.MayAuthorize=flag();d.PairAuthorized=flag();d.LocalGrantPresent=flag();d.LocalGrantRead=flag();d.LocalGrantServe=flag();d.MaySetUp=flag();d.ApprovalIssuer=text();Check(d.ApprovalIssuer.Length==0||rows.Any(row=>row.PeerId==d.ApprovalIssuer));Check(!d.MaySetUp||d.Local&&!d.MayAuthorize&&!d.LocalGrantPresent);Check(d.LocalGrantPresent==(d.LocalGrantRead||d.LocalGrantServe));d.ApprovalKnown=true;}return true;
    }
    // Explicit UI commands compose existing scoped consent operations. They do
    // not enroll on open, login, refresh, or from the History Sync ON switch.
    public void PrepareFirstDevice(long account,long generation,string local,long capabilities,bool confirmed){
        if(!confirmed)return;Check(capabilities>0&&capabilities<=3);
        var device=SnapshotDevices(account,generation).SingleOrDefault(d=>d.PeerId==local&&d.Local);Check(device!=null&&device.ApprovalKnown&&device.MaySetUp);
        Consent(account,generation,4,local,0,true);Consent(account,generation,5,local,capabilities,true);
    }
    public void ConfirmExistingIssuer(long account,long generation,string issuer,bool confirmed){
        if(!confirmed)return;var local=SnapshotDevices(account,generation).SingleOrDefault(d=>d.Local);Check(local!=null&&local.ApprovalKnown&&local.ApprovalIssuer==issuer);
        Consent(account,generation,4,issuer,0,true); // Accept only existing verified grants; issue nothing.
    }
    public sealed class Device {
        public bool NeedsApproval {get{return ApprovalKnown&&!(Local?(GrantedRead||GrantedServe):PairAuthorized)&&!LocalGrantPresent;}}
        public bool ApprovalKnown,MayAuthorize,PairAuthorized,LocalGrantPresent,LocalGrantRead,LocalGrantServe;
        public bool MaySetUp;public string ApprovalIssuer="";
        public bool Automatic=true,GrantedRead,GrantedServe;
        public string PeerId;public bool Local,ApprovedIssuer,MayRead,MayServe,Revoked,Authorized;
        public string AuthorizationState {get {return Authorized||MayRead||MayServe?"Authorized":Revoked?"Revoked":"Not authorized";}}
        public override string ToString(){return (Local?"This device · ":"Device · ")+PeerId;}
    }
    public static Device[] Devices(byte[] bytes){
        Check(bytes!=null&&bytes.Length>=4&&bytes.Length<=262144);
        using(var stream=new MemoryStream(bytes,false))using(var r=new BinaryReader(stream,new UTF8Encoding(false,true))){
            Check(r.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,31}));
            Func<string> text=()=>{long n=r.ReadInt64();Check(n>=0&&n<=65536&&n<=stream.Length-stream.Position);return new UTF8Encoding(false,true).GetString(r.ReadBytes((int)n));};
            Func<bool> flag=()=>{long n=r.ReadInt64();Check(n==0||n==1);return n==1;};
            Check(text()=="PeerChat/ArchiveApplication/v1"&&r.ReadInt64()==1&&r.ReadInt64()==3);
            long count=r.ReadInt64();Check(count>=0&&count<=32);var devices=new List<Device>();
            for(long i=0;i<count;i++){var peer=text();Check(peer.Length==64&&peer.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f')&&!devices.Any(d=>d.PeerId==peer));devices.Add(new Device{PeerId=peer,Local=flag(),ApprovedIssuer=flag(),MayRead=flag(),MayServe=flag()});}
            if(stream.Position<stream.Length){
                Check(text()=="PeerChat/ArchiveDeviceStatus/v1");long revoked=r.ReadInt64();Check(revoked>=0&&revoked<=count);
                for(long i=0;i<revoked;i++){var peer=text();var device=devices.SingleOrDefault(d=>d.PeerId==peer);Check(device!=null&&!device.Revoked);device.Revoked=true;}
                long authorized=r.ReadInt64();Check(authorized>=0&&authorized<=count);
                for(long i=0;i<authorized;i++){var peer=text();var device=devices.SingleOrDefault(d=>d.PeerId==peer);Check(device!=null&&!device.Authorized);device.Authorized=true;}
            }
            Check(stream.Position==stream.Length);return devices.ToArray();
        }
    }
}
}
