using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;

namespace PeerChatDesktop {
// Bounded transport translation only. Native owns all retries, immutable
// notices, crypto, progress and journals. One borrowed RPC per Service call.
public sealed class DmAttachmentExecution {
    readonly Func<uint,string,string> native;readonly ulong generation;bool stopped;
    public string LastState {get;private set;}
    static readonly UTF8Encoding utf8=new UTF8Encoding(false,true);
    public DmAttachmentExecution(Func<uint,string,string> native,ulong generation){this.native=native;this.generation=generation;}
    public void Stop(){stopped=true;}
    static void Require(bool value){if(!value)throw new InvalidDataException("Invalid encrypted attachment transport record.");}
    static string Text(IDictionary<string,object> value,string key){object result;return value.TryGetValue(key,out result)?result as string??"":"";}
    static ulong Number(IDictionary<string,object> value,string key){object result;return value.TryGetValue(key,out result)&&result!=null?Convert.ToUInt64(result,CultureInfo.InvariantCulture):0;}
    static bool Flag(IDictionary<string,object> value,string key){object result;return value.TryGetValue(key,out result)&&Boolean.Equals(result,true);}
    static string Url(byte[] data){return Convert.ToBase64String(data).TrimEnd('=').Replace('+','-').Replace('/','_');}
    static byte[] Data(IDictionary<string,object> value,string key,int maximum){string text=Text(value,key);Require(text.Length<=(maximum*4+2)/3&&text.All(c=>Char.IsLetterOrDigit(c)&&c<128||c=='-'||c=='_')&&text.Length%4!=1);var result=Convert.FromBase64String(text.Replace('-','+').Replace('_','/')+new string('=',(4-text.Length%4)%4));Require(result.Length<=maximum&&Url(result)==text);return result;}
    static void Bytes(BinaryWriter w,byte[] bytes){Require(bytes.Length<=65536);w.Write((ulong)bytes.Length);w.Write(bytes);}
    static void Text(BinaryWriter w,string text){Bytes(w,utf8.GetBytes(text??""));}
    static byte[] Record(string domain,Action<BinaryWriter> body){using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream,utf8)){writer.Write(new byte[]{80,84,1,30});Text(writer,domain);body(writer);writer.Flush();Require(stream.Length<=100000);return stream.ToArray();}}
    sealed class Reader:IDisposable {
        readonly MemoryStream stream;readonly BinaryReader reader;
        internal Reader(byte[] wire,string domain){Require(wire!=null&&wire.Length<=100000);stream=new MemoryStream(wire,false);reader=new BinaryReader(stream,utf8);Require(reader.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,30})&&Text()==domain);}
        internal ulong Number(){Require(stream.Length-stream.Position>=8);return reader.ReadUInt64();}
        internal byte[] Bytes(int maximum=65536){ulong count=Number();Require(count<=(ulong)maximum&&count<=(ulong)(stream.Length-stream.Position));return reader.ReadBytes((int)count);}
        internal string Text(){return utf8.GetString(Bytes());}internal void End(){Require(stream.Position==stream.Length);}public void Dispose(){reader.Dispose();stream.Dispose();}
    }
    public static byte[] Session(ulong account,ulong generation,string server){return Record("PeerChat/DMCacheSession/v1",w=>{w.Write(account);w.Write(generation);Text(w,server);});}
    public static byte[] Execute(byte[] command,Func<Dictionary<string,object>,Dictionary<string,object>> request){
        ulong generation,recipient,offset,maximum;string id,operation,attachment,lease;bool upload;byte[] descriptor,audience,data;
        using(var r=new Reader(command,"PeerChat/DMCacheCommand/v1")){generation=r.Number();id=r.Text();operation=r.Text();recipient=r.Number();attachment=r.Text();lease=r.Text();offset=r.Number();maximum=r.Number();var flag=r.Number();Require(flag<=1);upload=flag==1;descriptor=r.Bytes(2048);audience=r.Bytes(32768);data=r.Bytes();r.End();}
        Require(generation>0&&id.Length==32&&id.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f'));
        var payload=new Dictionary<string,object>{{"type",operation},{"requestId",id}};
        switch(operation){
        case "cache_dm_capability":break;
        case "cache_dm_register":payload["proof"]=Url(data);break;
        case "cache_dm_prepare":payload["recipientAccountId"]=recipient;break;
        case "cache_dm_admit":payload["descriptor"]=Url(descriptor);payload["audience"]=Url(audience);break;
        case "cache_dm_status":case "cache_dm_context":payload["id"]=attachment;break;
        case "cache_dm_acquire":payload["id"]=attachment;payload["upload"]=upload;break;
        case "cache_dm_append":payload["lease"]=lease;payload["offset"]=offset;payload["data"]=Url(data);break;
        case "cache_dm_complete":payload["lease"]=lease;break;
        case "cache_dm_read":payload["lease"]=lease;payload["offset"]=offset;payload["maximum"]=maximum;break;
        case "cache_dm_progress":payload["lease"]=lease;payload["offset"]=offset;break;
        case "cache_dm_release":payload["lease"]=lease;payload["upload"]=upload;break;
        case "cache_dm_publish":payload["id"]=attachment;payload["envelope"]=Url(data);break;
        case "cache_dm_verified":payload["id"]=attachment;payload["receipt"]=Url(data);break;
        case "cache_dm_receipts":payload["id"]=attachment;payload["after"]=utf8.GetString(data);break;
        default:throw new InvalidDataException("Unsupported encrypted attachment cache operation.");
        }
        var reply=request(payload);string state=Text(reply,"state");Require(state.Length>0&&state.Length<=128);
        if(state=="accepted")Require(Text(reply,"type")=="cache_result"&&Text(reply,"operation")==operation&&Text(reply,"requestId")==id);
        var rows=new List<IDictionary<string,object>>();object raw;if(reply.TryGetValue("items",out raw)){var list=raw as IEnumerable;Require(list!=null);foreach(var item in list){Require(rows.Count<8&&item is IDictionary<string,object>);rows.Add((IDictionary<string,object>)item);}}
        return Record("PeerChat/DMCacheReply/v1",w=>{w.Write(generation);Text(w,id);Text(w,state);w.Write(Number(reply,"cacheVersion"));w.Write(Number(reply,"dmAttachmentVersion"));w.Write(Number(reply,"durableOffset"));w.Write(Number(reply,"ciphertextSize"));w.Write(Number(reply,"expiresUtc"));w.Write(Flag(reply,"ciphertextComplete")?1UL:0UL);Text(w,Text(reply,"lease"));Bytes(w,Data(reply,"descriptor",2048));Bytes(w,Data(reply,"audience",32768));Bytes(w,Data(reply,"data",65536));w.Write(Flag(reply,"newNoticeStored")?1UL:0UL);w.Write((ulong)rows.Count);foreach(var row in rows){w.Write(Number(row,"accountId"));Text(w,Text(row,"device"));w.Write(Number(row,"durableOffset"));w.Write(Flag(row,"plaintextVerified")?1UL:0UL);Bytes(w,Data(row,"receipt",4096));}Text(w,Text(reply,"cursor"));w.Write(Flag(reply,"more")?1UL:0UL);});
    }
    public static byte[] Failure(byte[] command){using(var r=new Reader(command,"PeerChat/DMCacheCommand/v1")){ulong generation=r.Number();string id=r.Text();return Record("PeerChat/DMCacheFailure/v1",w=>{w.Write(generation);Text(w,id);Text(w,"network_unavailable");});}}
    public bool Service(Func<Dictionary<string,object>,Dictionary<string,object>> request){
        if(stopped)return false;string pending=native(25,"");if(String.IsNullOrEmpty(pending))return false;byte[] command=Convert.FromBase64String(pending);
        using(var r=new Reader(command,"PeerChat/DMCacheCommand/v1"))Require(r.Number()==generation);
        byte[] response;uint operation;try{response=Execute(command,request);using(var r=new Reader(response,"PeerChat/DMCacheReply/v1")){r.Number();r.Text();LastState=r.Text();}operation=26;}catch{LastState="network_unavailable";response=Failure(command);operation=27;}
        if(stopped)return false;
        try{native(operation,Convert.ToBase64String(response));}
        catch{
            // The native reply may have been rejected before it consumed this
            // borrowed request. Let its own correlation and retry owner release
            // that request now, without changing any server-side outcome.
            if(operation==26&&!stopped){LastState="native_reply_rejected";try{native(27,Convert.ToBase64String(Failure(command)));}catch{}}
            throw;
        }
        return true;
    }
}
}
