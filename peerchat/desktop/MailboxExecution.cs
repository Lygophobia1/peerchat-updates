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
    readonly object diagnosticsGate=new object();
    sealed class Timing { internal long Count,LastMilliseconds,MaximumMilliseconds; }
    readonly Dictionary<string,Timing> timings=new Dictionary<string,Timing>(StringComparer.Ordinal);
    readonly Queue<string> recentStages=new Queue<string>();
    DateTime lastStageUtc=DateTime.MinValue;
    string retrySummary="Retry pending: none";
    string eligibilitySummary="Eligibility refresh: no active recipient";
    void Note(string stage){lock(diagnosticsGate){lastStageUtc=DateTime.UtcNow;recentStages.Enqueue(lastStageUtc.ToString("O")+" "+stage);while(recentStages.Count>32)recentStages.Dequeue();}}
    void Measure(string stage,long started){
        long elapsed=(long)((System.Diagnostics.Stopwatch.GetTimestamp()-started)*1000.0/System.Diagnostics.Stopwatch.Frequency);
        lock(diagnosticsGate){Timing sample;if(!timings.TryGetValue(stage,out sample))timings[stage]=sample=new Timing();sample.Count++;sample.LastMilliseconds=elapsed;sample.MaximumMilliseconds=Math.Max(sample.MaximumMilliseconds,elapsed);}
    }
    public string DiagnosticsSnapshot(){lock(diagnosticsGate){
        var result=new StringBuilder();result.Append("Mailbox last stage UTC: ").Append(lastStageUtc==DateTime.MinValue?"N/A":lastStageUtc.ToString("O"));
        result.Append("\n").Append(retrySummary);
        result.Append("\n").Append(eligibilitySummary);
        foreach(var item in timings.OrderBy(x=>x.Key,StringComparer.Ordinal))result.Append("\n").Append(item.Key).Append(": count=").Append(item.Value.Count).Append(" last=").Append(item.Value.LastMilliseconds).Append("ms max=").Append(item.Value.MaximumMilliseconds).Append("ms");
        foreach(var stage in recentStages)result.Append("\n").Append(stage);
        return result.ToString();
    }}
    public void Stop(){lifetime.Cancel();}
    internal static bool CanStart(long account,long publishedAccount,bool stopping){
        return !stopping&&account>0&&account==publishedAccount;
    }
    internal static bool RouteNeedsReevaluation(CoreEvent value){return value.kind==17&&!value.connected;}
    internal static bool IsOperationFailure(CoreEvent value){return value.kind==8&&(value.peer??"")=="mailbox-control:operation"&&value.attempt==0;}
    byte[] Native(uint operation,byte[] input=null,string id="",bool allowed=false){
        lifetime.Token.ThrowIfCancellationRequested();
        long started=System.Diagnostics.Stopwatch.GetTimestamp();
        try{return invoke(operation,input,id,allowed);}finally{
            if(operation==3||operation==8||operation==11)Measure(operation==3?"Native envelope encryption/preparation":operation==8?"Native recipient selection":"Native eligibility snapshot install",started);
        }
    }
    internal byte[] PendingUploads(bool allowed){return Native(2,null,"",allowed);}
    string receiptCursor="";
    string legacyReceiptCursor="",fetchCursor="";
    sealed class Retry { internal int Failures;internal DateTime Due;internal string Reason; }
    readonly Dictionary<string,Retry> retries=new Dictionary<string,Retry>(StringComparer.Ordinal);
    readonly Dictionary<string,int> bindingOffset=new Dictionary<string,int>(StringComparer.Ordinal);
    readonly Dictionary<string,DateTime> deviceDeferredUntil=new Dictionary<string,DateTime>(StringComparer.Ordinal);
    static string DeviceRetryKey(string id,byte[] binding){using(var hash=System.Security.Cryptography.SHA256.Create())return "outgoing-device:"+id+":"+Convert.ToBase64String(hash.ComputeHash(binding));}
    readonly Dictionary<long,DateTime> eligibilityRefreshUtc=new Dictionary<long,DateTime>();
    int eligibilityOffset;
    long eligibilityGeneration,eligibilityLocalAccount;
    string eligibilityLocalDevice="";
    bool registered;
    public bool NeedsContinuation {get;private set;}
    public DateTime NextRetryUtc {get;private set;}
    public DateTime NextEligibilityUtc {get;private set;}
    public MailboxExecution(CoreAdapter core):this((operation,input,id,allowed)=>core.Mailbox(operation,input,id,allowed)){}
    internal MailboxExecution(Func<uint,byte[],string,bool,byte[]> invoke){this.invoke=invoke;}
    public void Reset(){registered=false;receiptCursor=legacyReceiptCursor=fetchCursor="";retries.Clear();bindingOffset.Clear();deviceDeferredUntil.Clear();eligibilityRefreshUtc.Clear();eligibilityOffset=0;eligibilityGeneration=eligibilityLocalAccount=0;eligibilityLocalDevice="";NextRetryUtc=NextEligibilityUtc=DateTime.MinValue;lock(diagnosticsGate){timings.Clear();recentStages.Clear();lastStageUtc=DateTime.MinValue;retrySummary="Retry pending: none";eligibilitySummary="Eligibility refresh: no active recipient";}}
    static void Require(bool value){if(!value)throw new InvalidOperationException("Mailbox result rejected.");}
    static string Text(Dictionary<string,object> item,string key){object value;return item.TryGetValue(key,out value)?value as string:null;}
    static byte[] Decode(string text,int max){Require(text!=null&&text.Length<=((max+2)/3)*4);var result=Convert.FromBase64String(text);Require(result.Length>0&&result.Length<=max);return result;}
    static IEnumerable<Dictionary<string,object>> Rows(Dictionary<string,object> reply,string key){object rows;Require(reply.TryGetValue(key,out rows));return ((System.Collections.IEnumerable)rows).Cast<object>().Select(x=>(Dictionary<string,object>)x);}
    static long Integer(Dictionary<string,object> item,string key){object value;Require(item.TryGetValue(key,out value)&&(value is int||value is long));long number=Convert.ToInt64(value);Require(number>=0);return number;}
    static bool Boolean(Dictionary<string,object> item,string key){object value;Require(item.TryGetValue(key,out value)&&value is bool);return (bool)value;}
    static bool Hex(string value,int length){return value!=null&&value.Length==length&&value.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f'||c>='A'&&c<='F');}
    static void UInt32(Stream output,long value){Require(value>=0&&value<=uint.MaxValue);for(int shift=24;shift>=0;shift-=8)output.WriteByte((byte)(value>>shift));}
    static void UInt64(Stream output,long value){Require(value>=0);for(int shift=56;shift>=0;shift-=8)output.WriteByte((byte)(value>>shift));}
    static void Field(Stream output,byte[] value){UInt32(output,value.Length);output.Write(value,0,value.Length);}
    static void TextField(Stream output,string value){Field(output,new UTF8Encoding(false,true).GetBytes(value));}
    // The authenticated RPC response is still treated as untrusted input here.
    // Native operation 11 independently checks the current identity/generation.
    internal static byte[] EligibilitySnapshotWire(Dictionary<string,object> reply,long localAccount,string localDevice,long friendGeneration,long recipientAccount,DateTime now){
        Require(reply!=null&&Text(reply,"state")=="ready"&&localAccount>0&&recipientAccount>0&&friendGeneration>0&&Hex(localDevice,64));
        Require(Integer(reply,"snapshotVersion")==1&&Integer(reply,"senderAccountId")==localAccount&&String.Equals(Text(reply,"senderDevice"),localDevice,StringComparison.OrdinalIgnoreCase));
        Require(Integer(reply,"recipientAccountId")==recipientAccount);
        string snapshotId=Text(reply,"snapshotId");Require(Hex(snapshotId,32));
        long sequence=Integer(reply,"snapshotSequence"),issued=Integer(reply,"issuedUtcMs"),freshUntil=Integer(reply,"freshUntilUtcMs");
        long current=new DateTimeOffset(now.ToUniversalTime()).ToUnixTimeMilliseconds();
        Require(sequence>0&&issued>0&&freshUntil>issued&&freshUntil-issued<=120000&&issued<=current+30000&&freshUntil>current);
        var candidates=Rows(reply,"candidates").OrderBy(x=>Text(x,"device"),StringComparer.Ordinal).ToArray();Require(candidates.Length<=32);
        var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        using(var output=new MemoryStream()){
            var domain=Encoding.ASCII.GetBytes("PeerChat/mailbox-eligibility-snapshot/v1\0");output.Write(domain,0,domain.Length);
            UInt64(output,localAccount);UInt64(output,friendGeneration);TextField(output,localDevice);UInt64(output,recipientAccount);
            UInt32(output,1);TextField(output,snapshotId);UInt64(output,sequence);UInt64(output,issued);UInt64(output,freshUntil);output.WriteByte((byte)candidates.Length);
            foreach(var candidate in candidates){
                string device=Text(candidate,"device"),encoded=Text(candidate,"binding");
                Require(Hex(device,64)&&seen.Add(device)&&encoded!=null&&encoded.Length<=5464);
                byte[] binding=encoded.Length==0?new byte[0]:Convert.FromBase64String(encoded);Require(binding.Length<=4096);
                TextField(output,device);output.WriteByte(Boolean(candidate,"online")?(byte)1:(byte)0);Field(output,binding);
            }
            Require(output.Length<=70000);return output.ToArray();
        }
    }
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
    void RetainStoreRetry(string deviceKey,byte[] wire,string reason){
        Retry retry;if(!retries.TryGetValue(deviceKey,out retry))return;
        try{using(var output=new MemoryStream()){
            var domain=Encoding.ASCII.GetBytes("PeerChat/mailbox-store-retry/v1\0");output.Write(domain,0,domain.Length);
            UInt64(output,new DateTimeOffset(retry.Due).ToUnixTimeMilliseconds());TextField(output,reason);Field(output,wire);
            Require(Native(12,output.ToArray()).SequenceEqual(new byte[]{1}));
        }}catch(Exception){Note("Per-device retry metadata unavailable; exact encrypted attempt retained");}
    }
    void Failed(string key,string reason,Action<string> trace){
        if(String.IsNullOrEmpty(reason)||reason.Length>64||reason.Any(c=>!(c>='a'&&c<='z')&&!(c>='0'&&c<='9')&&c!='_'))reason="unclassified_server_refusal";
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
        string reasons=String.Join(", ",retries.Values.GroupBy(x=>x.Reason??"unknown",StringComparer.Ordinal).OrderBy(x=>x.Key,StringComparer.Ordinal).Select(x=>x.Key+"="+x.Count()));
        lock(diagnosticsGate)retrySummary="Retry pending: "+retries.Count+(retries.Count==0?"":"; reasons: "+reasons+"; next due UTC: "+(NextRetryUtc==DateTime.MinValue?"now":NextRetryUtc.ToString("O")));
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
    void RefreshEligibility(long[] recipients,long friendGeneration,long localAccount,string localDevice,Func<string,Dictionary<string,object>,Dictionary<string,object>> request,Action<string> trace){
        if(recipients==null||recipients.Length==0||friendGeneration<=0||localAccount<=0||!Hex(localDevice,64)){NextEligibilityUtc=DateTime.MinValue;lock(diagnosticsGate)eligibilitySummary="Eligibility refresh: no active authenticated recipient";return;}
        if(eligibilityGeneration!=friendGeneration||eligibilityLocalAccount!=localAccount||!String.Equals(eligibilityLocalDevice,localDevice,StringComparison.OrdinalIgnoreCase)){
            eligibilityRefreshUtc.Clear();eligibilityOffset=0;eligibilityGeneration=friendGeneration;eligibilityLocalAccount=localAccount;eligibilityLocalDevice=localDevice;
        }
        var active=recipients.Where(x=>x>0&&x!=localAccount).Distinct().OrderBy(x=>x).ToArray();
        var activeSet=new HashSet<long>(active);
        foreach(long stale in eligibilityRefreshUtc.Keys.Where(x=>!activeSet.Contains(x)).ToArray())eligibilityRefreshUtc.Remove(stale);
        if(active.Length==0){NextEligibilityUtc=DateTime.MinValue;lock(diagnosticsGate)eligibilitySummary="Eligibility refresh: no active authenticated recipient";return;}
        eligibilityOffset%=active.Length;int attempts=0,installed=0,unavailable=0;
        for(int step=0;step<active.Length;step++){
            long recipient=active[(eligibilityOffset+step)%active.Length];DateTime due;
            if(eligibilityRefreshUtc.TryGetValue(recipient,out due)&&due>DateTime.UtcNow)continue;
            if(attempts>=4){NeedsContinuation=true;continue;}
            attempts++;
            try{
                var reply=request("mailbox_eligibility_snapshot",new Dictionary<string,object>{{"recipientAccountId",recipient.ToString(System.Globalization.CultureInfo.InvariantCulture)}});
                string state=Text(reply,"state");
                if(state=="session_invalidated")throw new DiscoveryRequestException(state);
                if(state!="ready"){
                    string reason=state!=null&&state.Length<=64&&state.All(c=>c>='a'&&c<='z'||c>='0'&&c<='9'||c=='_')?state:"snapshot_refused";
                    eligibilityRefreshUtc[recipient]=DateTime.UtcNow.AddSeconds(15);unavailable++;
                    trace("mailbox eligibility recipient="+recipient+" refresh refused reason="+reason+"; native selection remains unresolved without a fresh snapshot");
                    continue;
                }
                var wire=EligibilitySnapshotWire(reply,localAccount,localDevice,friendGeneration,recipient,DateTime.UtcNow);
                Require(Native(11,wire).SequenceEqual(new byte[]{1}));
                eligibilityRefreshUtc[recipient]=DateTime.UtcNow.AddSeconds(60);
                installed++;
                trace("mailbox eligibility recipient="+recipient+" authenticated snapshot installed; next refresh in 60s");
            }catch(Exception error){
                if(AuthenticationFailure(error))throw;
                eligibilityRefreshUtc[recipient]=DateTime.UtcNow.AddSeconds(15);
                unavailable++;
                trace("mailbox eligibility recipient="+recipient+" refresh unavailable reason="+FailureReason(error)+"; native selection remains unresolved without a fresh snapshot");
            }
        }
        eligibilityOffset=(eligibilityOffset+Math.Max(1,attempts))%active.Length;
        NextEligibilityUtc=active.Select(x=>eligibilityRefreshUtc.ContainsKey(x)?eligibilityRefreshUtc[x]:DateTime.UtcNow).Min();
        lock(diagnosticsGate)eligibilitySummary="Eligibility refresh: active="+active.Length+" installed_this_pass="+installed+" unavailable_this_pass="+unavailable+"; next due UTC="+NextEligibilityUtc.ToString("O");
    }
    public void Service(DiscoveryClient accounts,string device,bool allowed,Action<string,bool> acknowledged,Action<string> diagnostic=null){Service(accounts,device,allowed,acknowledged,diagnostic,null);}
    internal void Service(DiscoveryClient accounts,string device,bool allowed,Action<string,bool> acknowledged,Action<string> diagnostic,Func<string,Dictionary<string,object>,Dictionary<string,object>> requestOverride){
        Service(accounts,device,allowed,acknowledged,diagnostic,requestOverride,null,0,0);
    }
    internal void Service(DiscoveryClient accounts,string device,bool allowed,Action<string,bool> acknowledged,Action<string> diagnostic,Func<string,Dictionary<string,object>,Dictionary<string,object>> requestOverride,long[] eligibleRecipients,long friendGeneration,long localAccount){
        NeedsContinuation=false;
        NextRetryUtc=NextEligibilityUtc=DateTime.MinValue;
        Action<string> trace=message=>{
            string safe=message;
            int marker=safe.IndexOf("message=",StringComparison.Ordinal);
            if(marker>=0){int end=safe.IndexOf(' ',marker+8);safe=safe.Substring(0,marker+8)+"[message id]"+(end<0?"":safe.Substring(end));}
            Note(safe);
            if(diagnostic!=null)diagnostic(message);
        };
        trace("mailbox policy="+(allowed?"allowed":"forbidden"));
        var identity=Native(7);
        Func<string,Dictionary<string,object>,Dictionary<string,object>> request=(operation,body)=>{
            lifetime.Token.ThrowIfCancellationRequested();
            long started=System.Diagnostics.Stopwatch.GetTimestamp();
            try{
                var result=requestOverride!=null?requestOverride(operation,body):accounts.Mailbox(operation,device,body);
                lifetime.Token.ThrowIfCancellationRequested();
                return result;
            }finally{Measure("Authenticated "+operation+" RPC",started);}
        };
        Func<Dictionary<string,object>> signing=()=>new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(identity)}};
        if(!registered){var binding=Native(0,null,"",allowed);
            for(int pass=0;pass<2&&!registered;pass++){var body=signing();body["binding"]=Convert.ToBase64String(binding);
                Require(Text(request("mailbox_register",body),"state")=="accepted");Require(Native(1,binding).SequenceEqual(new byte[]{1}));
                var current=Native(0,null,"",allowed);registered=current.SequenceEqual(binding);binding=current;}
            Require(registered);}
        RefreshEligibility(eligibleRecipients,friendGeneration,localAccount,device,request,trace);
        string requestedFetchCursor=fetchCursor;
        var fetched=request("mailbox_fetch",new Dictionary<string,object>{{"cursor",fetchCursor}});Require(Text(fetched,"state")=="ready");
        var envelopes=Rows(fetched,"envelopes").ToArray();Require(envelopes.Length<=64);
        bool fetchPaging=fetched.ContainsKey("nextCursor");
        NeedsContinuation|=envelopes.Length>4;
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
            DateTime deviceDue;
            if(!Ready(messageKey,DateTime.UtcNow)||(deviceDeferredUntil.TryGetValue(message.Item1,out deviceDue)&&deviceDue>DateTime.UtcNow))continue;
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
            if(prepared.Count==0){Succeeded(messageKey);trace("mailbox upload waiting for eligible target, retry deadline, or receipt");continue;}
            int offset;bindingOffset.TryGetValue(message.Item1,out offset);
            if(prepared.Count>0){offset%=prepared.Count;bindingOffset[message.Item1]=(offset+Math.Min(4,prepared.Count))%prepared.Count;}
            NeedsContinuation|=prepared.Count>4;
            foreach(var binding in prepared.Skip(offset).Concat(prepared.Take(offset)).Take(4)){
                string deviceKey=DeviceRetryKey(message.Item1,binding);
                if(!Ready(deviceKey,DateTime.UtcNow))continue;
                try{
                var wire=Native(3,binding,message.Item1,true);if(wire.Length==0)continue;
                var body=signing();body["envelope"]=Convert.ToBase64String(wire);
                trace("mailbox message="+message.Item1+" submission=started");
                string storeState=Text(request("mailbox_store",body),"state");
                if(storeState=="stored"){
                    Require(Native(4,wire).SequenceEqual(new byte[]{1}));
                    Succeeded(deviceKey);
                    trace("mailbox message="+message.Item1+" submission=accepted state=Stored; opaque wake is server-owned, receipt not yet confirmed");
                }else{Failed(deviceKey,storeState??"store_rejected",trace);RetainStoreRetry(deviceKey,wire,storeState??"store_rejected");}
                }catch(Exception error){if(AuthenticationFailure(error))throw;Failed(deviceKey,FailureReason(error),trace);}
            }
            var deferred=prepared.Select(binding=>DeviceRetryKey(message.Item1,binding)).ToArray();
            if(deferred.All(key=>!Ready(key,DateTime.UtcNow)))deviceDeferredUntil[message.Item1]=deferred.Min(key=>retries[key].Due);
            else deviceDeferredUntil.Remove(message.Item1);
            Succeeded(messageKey);
            }catch(Exception error){if(AuthenticationFailure(error))throw;Failed(messageKey,FailureReason(error),trace);}
        }
        var pendingIds=new HashSet<string>(pending.Select(x=>x.Item1),StringComparer.Ordinal);
        foreach(var id in bindingOffset.Keys.ToArray())if(!pendingIds.Contains(id))bindingOffset.Remove(id);
        foreach(var id in deviceDeferredUntil.Keys.ToArray())if(!pendingIds.Contains(id))deviceDeferredUntil.Remove(id);
        foreach(var key in retries.Keys.Where(x=>x.StartsWith("outgoing-device:",StringComparison.Ordinal)).ToArray())if(!pendingIds.Contains(key.Substring(16,key.IndexOf(':',16)-16)))retries.Remove(key);
        ScheduleRetries();
    }
}
}
