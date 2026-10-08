using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections;
using System.Collections.Generic;

namespace PeerChatDesktop {
// One synchronous application operation, called on the existing profile worker.
// No socket, timer, retry loop, signing decision or durable queue is owned here.
internal sealed class ArchiveEnrollmentExecution {
    readonly Func<uint,byte[],byte[]> native;
    readonly Func<string,Dictionary<string,object>,Dictionary<string,object>> rpc;
    long revision;
    public ArchiveEnrollmentExecution(Func<uint,byte[],byte[]> native,
        Func<string,Dictionary<string,object>,Dictionary<string,object>> rpc) {
        this.native=native;this.rpc=rpc;
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
    public sealed class Device {
        public string PeerId;public bool Local,ApprovedIssuer,MayRead,MayServe;
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
            Check(stream.Position==stream.Length);return devices.ToArray();
        }
    }
}
}
