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
    string legacyReceiptCursor="",fetchCursor="";
    sealed class Retry { internal int Failures;internal DateTime Due;internal string Reason; }
    readonly Dictionary<string,Retry> retries=new Dictionary<string,Retry>(StringComparer.Ordinal);
    readonly Dictionary<string,int> bindingOffset=new Dictionary<string,int>(StringComparer.Ordinal);
    bool registered;
    public bool NeedsContinuation {get;private set;}
    public DateTime NextRetryUtc {get;private set;}
    public MailboxExecution(CoreAdapter core):this((operation,input,id,allowed)=>core.Mailbox(operation,input,id,allowed)){}
    internal MailboxExecution(Func<uint,byte[],string,bool,byte[]> invoke){this.invoke=invoke;}
    public void Reset(){registered=false;receiptCursor=legacyReceiptCursor=fetchCursor="";retries.Clear();bindingOffset.Clear();NextRetryUtc=DateTime.MinValue;}
    static void Require(bool value){if(!value)throw new InvalidOperationException("Mailbox result rejected.");}
    static string Text(Dictionary<string,object> item,string key){object value;return item.TryGetValue(key,out value)?value as string:null;}
    static byte[] Decode(string text,int max){Require(text!=null&&text.Length<=((max+2)/3)*4);var result=Convert.FromBase64String(text);Require(result.Length>0&&result.Length<=max);return result;}
    static IEnumerable<Dictionary<string,object>> Rows(Dictionary<string,object> reply,string key){object rows;Require(reply.TryGetValue(key,out rows));return ((System.Collections.IEnumerable)rows).Cast<object>().Select(x=>(Dictionary<string,object>)x);}
    static bool AuthenticationFailure(Exception error){
        var rejected=error as DiscoveryRequestException;
        if(rejected!=null)return true;
        return error is OperationCanceledException||error is ObjectDisposedException;
    }
    static string FailureReason(Exception error){
        if(error is IOException||error is System.Net.Sockets.SocketException||error is TimeoutException)return "transport_unavailable";
        if(error is FormatException||error is DecoderFallbackException||error is InvalidDataException)return "invalid_mailbox_record";
        return "native_or_protocol_rejected";
    }
    static bool Permanent(string reason){return reason=="invalid_mailbox_record"||reason=="invalid_key_or_signature"||reason=="recipient_key_retired"||reason=="recipient_key_unavailable"||reason=="expired_or_invalid_timestamp"||reason=="envelope_conflict"||reason=="not_authorized"||reason=="no_eligible_recipient_device";}
    bool Ready(string key,DateTime now){Retry state;return !retries.TryGetValue(key,out state)||state.Due<=now;}
    void Succeeded(string key){retries.Remove(key);}
    void Failed(string key,string reason,Action<string> trace){
        Retry state;if(!retries.TryGetValue(key,out state)){
            if(retries.Count>=512)retries.Remove(retries.OrderBy(x=>x.Value.Due).First().Key);
            retries[key]=state=new Retry();
        }
        state.Failures=Math.Min(state.Failures+1,16);state.Reason=reason;
        // Permanent refusals remain visible and are rechecked occasionally after account/key changes.
        int seconds=Permanent(reason)?900:Math.Min(300,5*(1<<Math.Min(state.Failures-1,6)));
        state.Due=DateTime.UtcNow.AddSeconds(seconds);
        trace("mailbox "+(key.StartsWith("outgoing:",StringComparison.Ordinal)?"message="+key.Substring(9):"incoming/receipt item")+" retry pending reason="+reason+" in="+seconds+"s");
    }
    void ScheduleRetries(){
        var future=retries.Values.Where(x=>x.Due>DateTime.UtcNow).Select(x=>x.Due).ToArray();
        NextRetryUtc=future.Length==0?DateTime.MinValue:future.Min();
    }
    internal sealed class Record : IDisposable {
        readonly BinaryReader input;readonly MemoryStream stream;
        public Record(byte[] bytes,int type){Require(bytes.Length>=4&&bytes.Length<=262144);stream=new MemoryStream(bytes,false);input=new BinaryReader(stream,Encoding.UTF8);Require(input.ReadByte()==80&&input.ReadByte()==84&&input.ReadByte()==1&&input.ReadByte()==type);}
        public long Number(){Require(stream.Length-stream.Position>=8);long value=input.ReadInt64();Require(value>=0);return value;}
        public byte[] Bytes(){long size=Number();Require(size<=65536&&size<=stream.Length-stream.Position);return input.ReadBytes((int)size);}
        public string Text(){return new UTF8Encoding(false,true).GetString(Bytes());}
        public void End(){Require(stream.Position==stream.Length);}
        public void Dispose(){input.Dispose();}
    }
    public void Service(DiscoveryClient accounts,string device,bool allowed,Action<string,bool> acknowledged,Action<string> diagnostic=null){Service(accounts,device,allowed,acknowledged,diagnostic,null);}
    internal void Service(DiscoveryClient accounts,string device,bool allowed,Action<string,bool> acknowledged,Action<string> diagnostic,Func<string,Dictionary<string,object>,Dictionary<string,object>> requestOverride){
        NeedsContinuation=false;
        NextRetryUtc=DateTime.MinValue;
        Action<string> trace=diagnostic??(_=>{});
        trace("mailbox policy="+(allowed?"allowed":"forbidden"));
        var identity=Native(7);
        Func<string,Dictionary<string,object>,Dictionary<string,object>> request=(operation,body)=>{
            lifetime.Token.ThrowIfCancellationRequested();
            var result=requestOverride!=null?requestOverride(operation,body):accounts.Mailbox(operation,device,body);
            lifetime.Token.ThrowIfCancellationRequested();
            return result;
        };
        Func<Dictionary<string,object>> signing=()=>new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(identity)}};
        if(!registered){var binding=Native(0,null,"",allowed);
            for(int pass=0;pass<2&&!registered;pass++){var body=signing();body["binding"]=Convert.ToBase64String(binding);
                Require(Text(request("mailbox_register",body),"state")=="accepted");Require(Native(1,binding).SequenceEqual(new byte[]{1}));
                var current=Native(0,null,"",allowed);registered=current.SequenceEqual(binding);binding=current;}
            Require(registered);}
        string requestedFetchCursor=fetchCursor;
        var fetched=request("mailbox_fetch",new Dictionary<string,object>{{"cursor",fetchCursor}});Require(Text(fetched,"state")=="ready");
        var envelopes=Rows(fetched,"envelopes").ToArray();Require(envelopes.Length<=64);
        bool fetchPaging=fetched.ContainsKey("nextCursor");
        NeedsContinuation=envelopes.Length>4;
        foreach(var item in envelopes.Take(4)){
            string rowCursor=Text(item,"cursor");
            if(fetchPaging&&String.IsNullOrEmpty(rowCursor))throw new InvalidDataException("Mailbox fetch cursor missing.");
            string rowKey="incoming:"+(rowCursor??Text(item,"messageId")??"");
            try{
                if(Ready(rowKey,DateTime.UtcNow)){
                    var wire=Decode(Text(item,"envelope"),70000);byte[] receipt;bool inserted;string peer;
                    using(var record=new Record(Native(5,wire),3)){long flag=record.Number();Require(flag<=1);inserted=flag==1;receipt=record.Bytes();peer=record.Text();record.Text();record.End();}
                    var body=signing();body["receipt"]=Convert.ToBase64String(receipt);
                    string state=Text(request("mailbox_ack",body),"state");
                    if(state!="acknowledged"){Failed(rowKey,state??"ack_rejected",trace);}else{Succeeded(rowKey);acknowledged(peer,inserted);trace("mailbox incoming durable import and signed ACK accepted");}
                }
            }catch(Exception error){if(AuthenticationFailure(error))throw;Failed(rowKey,FailureReason(error),trace);}
            if(fetchPaging)fetchCursor=rowCursor;
        }
        if(fetchPaging){
            if(envelopes.Length==0)fetchCursor="";
            else if(fetchCursor==requestedFetchCursor){fetchCursor="";trace("mailbox fetch pagination stalled; retry pending");}
            else NeedsContinuation=true;
        }else fetchCursor="";
        string requestedReceiptCursor=receiptCursor;
        var reconciled=request("mailbox_receipts",new Dictionary<string,object>{{"afterMessageId",legacyReceiptCursor},{"cursor",receiptCursor}});Require(Text(reconciled,"state")=="ready");
        var receipts=Rows(reconciled,"receipts").ToArray();Require(receipts.Length<=64);
        bool receiptPaging=reconciled.ContainsKey("nextCursor");
        NeedsContinuation|=receipts.Length==64;
        foreach(var receipt in receipts){
            string rowCursor=Text(receipt,"cursor");
            if(receiptPaging&&String.IsNullOrEmpty(rowCursor))throw new InvalidDataException("Mailbox receipt cursor missing.");
            string rowKey="receipt:"+(rowCursor??Text(receipt,"messageId")??"");
            try{
                if(Ready(rowKey,DateTime.UtcNow)){
                    Require(Native(6,Decode(Text(receipt,"receipt"),2048)).SequenceEqual(new byte[]{1}));
                    Succeeded(rowKey);
                }
            }catch(Exception error){if(AuthenticationFailure(error))throw;Failed(rowKey,FailureReason(error),trace);}
            legacyReceiptCursor=Text(receipt,"messageId")??"";
            if(receiptPaging)receiptCursor=rowCursor;
        }
        if(receiptPaging){
            if(receipts.Length==0){receiptCursor="";legacyReceiptCursor="";}
            else if(receiptCursor==requestedReceiptCursor){receiptCursor="";legacyReceiptCursor="";trace("mailbox receipt pagination stalled; retry pending");}
            else NeedsContinuation=true;
        }else if(receipts.Length<64){receiptCursor="";legacyReceiptCursor="";}
        if(!receiptPaging)receiptCursor="";
        if(!allowed){ScheduleRetries();return;}
        var pending=new List<Tuple<string,string,long>>();
        using(var record=new Record(PendingUploads(allowed),4)){long count=record.Number();Require(count<=64);for(long i=0;i<count;i++)pending.Add(Tuple.Create(record.Text(),record.Text(),record.Number()));record.End();}
        int attempted=0;
        foreach(var message in pending){
            if(attempted>=4){if(Ready("outgoing:"+message.Item1,DateTime.UtcNow))NeedsContinuation=true;continue;}
            string messageKey="outgoing:"+message.Item1;
            if(!Ready(messageKey,DateTime.UtcNow))continue;
            attempted++;
            try{
            trace("mailbox message="+message.Item1+" fallback=no usable winner after bounded attempt or no eligible route");
            var reply=request("mailbox_keys",new Dictionary<string,object>{{"recipientAccountId",message.Item3.ToString(System.Globalization.CultureInfo.InvariantCulture)}});
            string keyState=Text(reply,"state");if(keyState!="ready"){Failed(messageKey,keyState??"keys_rejected",trace);continue;}
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
            if(prepared.Count==0){Failed(messageKey,"no_eligible_recipient_device",trace);continue;}
            int offset;bindingOffset.TryGetValue(message.Item1,out offset);
            if(prepared.Count>0){offset%=prepared.Count;bindingOffset[message.Item1]=(offset+Math.Min(4,prepared.Count))%prepared.Count;}
            NeedsContinuation|=prepared.Count>4;
            bool failed=false;
            foreach(var binding in prepared.Skip(offset).Concat(prepared.Take(offset)).Take(4)){
                var wire=Native(3,binding,message.Item1,true);if(wire.Length==0)continue;
                var body=signing();body["envelope"]=Convert.ToBase64String(wire);
                trace("mailbox message="+message.Item1+" submission=started");
                string storeState=Text(request("mailbox_store",body),"state");
                if(storeState=="stored"){
                    Require(Native(4,wire).SequenceEqual(new byte[]{1}));
                    trace("mailbox message="+message.Item1+" submission=accepted state=Stored; opaque wake is server-owned, receipt not yet confirmed");
                }else{failed=true;Failed(messageKey,storeState??"store_rejected",trace);}
            }
            if(!failed)Succeeded(messageKey);
            }catch(Exception error){if(AuthenticationFailure(error))throw;Failed(messageKey,FailureReason(error),trace);}
        }
        var pendingIds=new HashSet<string>(pending.Select(x=>x.Item1),StringComparer.Ordinal);
        foreach(var id in bindingOffset.Keys.ToArray())if(!pendingIds.Contains(id))bindingOffset.Remove(id);
        ScheduleRetries();
    }
}
}
