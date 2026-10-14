using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;

namespace PeerChatDesktop {
// Executes opaque native mailbox commands from the existing application pump.
// No socket/session, durable queue, delivery or retry policy is owned here.
internal sealed class MailboxExecution {
    readonly Func<uint,byte[],string,bool,byte[]> invoke;
    readonly CancellationTokenSource lifetime=new CancellationTokenSource();
    public void Stop(){lifetime.Cancel();}
    internal static bool CanStart(long account,long publishedAccount,bool stopping){
        return !stopping&&account>0&&account==publishedAccount;
    }
    internal static bool RouteNeedsReevaluation(CoreEvent value){return value.kind==17&&!value.connected;}
    internal static bool IsOperationFailure(CoreEvent value){return value.kind==8&&(value.peer??"")=="mailbox-control:operation"&&value.attempt==0;}
    byte[] Native(uint operation,byte[] input=null,string id="",bool allowed=false){
        lifetime.Token.ThrowIfCancellationRequested();
        return invoke(operation,input,id,allowed);
    }
    internal byte[] PendingUploads(bool allowed){return Native(2,null,"",allowed);}
    string receiptCursor="";
    bool registered;
    public bool NeedsContinuation {get;private set;}
    public MailboxExecution(CoreAdapter core):this((operation,input,id,allowed)=>core.Mailbox(operation,input,id,allowed)){}
    internal MailboxExecution(Func<uint,byte[],string,bool,byte[]> invoke){this.invoke=invoke;}
    public void Reset(){registered=false;receiptCursor="";}
    static void Require(bool value){if(!value)throw new InvalidOperationException("Mailbox result rejected.");}
    static string Text(Dictionary<string,object> item,string key){object value;return item.TryGetValue(key,out value)?value as string:null;}
    static byte[] Decode(string text,int max){Require(text!=null&&text.Length<=((max+2)/3)*4);var result=Convert.FromBase64String(text);Require(result.Length>0&&result.Length<=max);return result;}
    static IEnumerable<Dictionary<string,object>> Rows(Dictionary<string,object> reply,string key){object rows;Require(reply.TryGetValue(key,out rows));return ((System.Collections.IEnumerable)rows).Cast<object>().Select(x=>(Dictionary<string,object>)x);}
    internal sealed class Record : IDisposable {
        readonly BinaryReader input;readonly MemoryStream stream;
        public Record(byte[] bytes,int type){Require(bytes.Length>=4&&bytes.Length<=262144);stream=new MemoryStream(bytes,false);input=new BinaryReader(stream,Encoding.UTF8);Require(input.ReadByte()==80&&input.ReadByte()==84&&input.ReadByte()==1&&input.ReadByte()==type);}
        public long Number(){Require(stream.Length-stream.Position>=8);long value=input.ReadInt64();Require(value>=0);return value;}
        public byte[] Bytes(){long size=Number();Require(size<=65536&&size<=stream.Length-stream.Position);return input.ReadBytes((int)size);}
        public string Text(){return new UTF8Encoding(false,true).GetString(Bytes());}
        public void End(){Require(stream.Position==stream.Length);}
        public void Dispose(){input.Dispose();}
    }
    public void Service(DiscoveryClient accounts,string device,bool allowed,Action<string,bool> acknowledged,Action<string> diagnostic=null){
        NeedsContinuation=false;
        Action<string> trace=diagnostic??(_=>{});
        trace("mailbox policy="+(allowed?"allowed":"forbidden"));
        var identity=Native(7);
        Func<string,Dictionary<string,object>,Dictionary<string,object>> request=(operation,body)=>{
            lifetime.Token.ThrowIfCancellationRequested();
            var result=accounts.Mailbox(operation,device,body);
            lifetime.Token.ThrowIfCancellationRequested();
            return result;
        };
        Func<Dictionary<string,object>> signing=()=>new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(identity)}};
        if(!registered){var binding=Native(0,null,"",allowed);
            for(int pass=0;pass<2&&!registered;pass++){var body=signing();body["binding"]=Convert.ToBase64String(binding);
                Require(Text(request("mailbox_register",body),"state")=="accepted");Require(Native(1,binding).SequenceEqual(new byte[]{1}));
                var current=Native(0,null,"",allowed);registered=current.SequenceEqual(binding);binding=current;}
            Require(registered);}
        var fetched=request("mailbox_fetch",new Dictionary<string,object>());Require(Text(fetched,"state")=="ready");
        var envelopes=Rows(fetched,"envelopes").ToArray();Require(envelopes.Length<=64);
        NeedsContinuation=envelopes.Length>4;
        foreach(var item in envelopes.Take(4)){
            var wire=Decode(Text(item,"envelope"),70000);byte[] receipt;bool inserted;string peer;
            using(var record=new Record(Native(5,wire),3)){long flag=record.Number();Require(flag<=1);inserted=flag==1;receipt=record.Bytes();peer=record.Text();record.Text();record.End();}
            var body=signing();body["receipt"]=Convert.ToBase64String(receipt);Require(Text(request("mailbox_ack",body),"state")=="acknowledged");
            acknowledged(peer,inserted);
        }
        var reconciled=request("mailbox_receipts",new Dictionary<string,object>{{"afterMessageId",receiptCursor}});Require(Text(reconciled,"state")=="ready");
        var receipts=Rows(reconciled,"receipts").ToArray();Require(receipts.Length<=64);
        NeedsContinuation|=receipts.Length==64;
        foreach(var receipt in receipts){Require(Native(6,Decode(Text(receipt,"receipt"),2048)).SequenceEqual(new byte[]{1}));receiptCursor=Text(receipt,"messageId");}
        if(receipts.Length<64)receiptCursor="";
        if(!allowed)return;
        var pending=new List<Tuple<string,string,long>>();
        using(var record=new Record(PendingUploads(allowed),4)){long count=record.Number();Require(count<=64);for(long i=0;i<count;i++)pending.Add(Tuple.Create(record.Text(),record.Text(),record.Number()));record.End();}
        NeedsContinuation|=pending.Count>4;
        foreach(var message in pending.Take(4)){
            trace("mailbox message="+message.Item1+" fallback=no usable winner after bounded attempt or no eligible route");
            var reply=request("mailbox_keys",new Dictionary<string,object>{{"recipientAccountId",message.Item3.ToString(System.Globalization.CultureInfo.InvariantCulture)}});Require(Text(reply,"state")=="ready");
            var keys=Rows(reply,"keys").ToArray();Require(keys.Length<=32);
            byte[] batch;
            using(var output=new MemoryStream()){
                var domain=Encoding.ASCII.GetBytes("PeerChat/mailbox-recipient-batch/v1\0");output.Write(domain,0,domain.Length);
                output.WriteByte(0);output.WriteByte((byte)keys.Length);
                foreach(var key in keys){var binding=Decode(Text(key,"binding"),4096);for(int shift=24;shift>=0;shift-=8)output.WriteByte((byte)(binding.Length>>shift));output.Write(binding,0,binding.Length);}
                batch=output.ToArray();
            }
            var prepared=new List<byte[]>();
            using(var record=new Record(Native(8,batch,message.Item1,true),4)){long count=record.Number();Require(count<=32);for(long i=0;i<count;i++)prepared.Add(record.Bytes());record.End();}
            NeedsContinuation|=prepared.Count>4;
            foreach(var binding in prepared.Take(4)){
                var wire=Native(3,binding,message.Item1,true);if(wire.Length==0)continue;
                var body=signing();body["envelope"]=Convert.ToBase64String(wire);
                trace("mailbox message="+message.Item1+" submission=started");
                if(Text(request("mailbox_store",body),"state")=="stored"){
                    Require(Native(4,wire).SequenceEqual(new byte[]{1}));
                    trace("mailbox message="+message.Item1+" submission=accepted state=Stored; opaque wake is server-owned, receipt not yet confirmed");
                }else trace("mailbox message="+message.Item1+" submission=not accepted");
            }
        }
    }
}
}
