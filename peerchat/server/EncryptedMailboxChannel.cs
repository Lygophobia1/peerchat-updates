using System;
using System.Collections.Generic;
using System.Linq;
using System.Web.Script.Serialization;

namespace PeerChat.Server {
// Optional wake hints only, never durable work/ACKs. One bounded, coalesced
// account batch per server worker; overflow is recovered by periodic fetch.
internal sealed class MailboxHintQueue {
    readonly object gate=new object();readonly Dictionary<long,int> pending=new Dictionary<long,int>();readonly Queue<long> order=new Queue<long>();bool running,stopped;
    internal int Count {get{lock(gate)return pending.Count;}}
    internal bool Running {get{lock(gate)return running;}}
    internal bool Enqueue(long account,int kind){lock(gate){
        if(stopped||account<=0||(kind!=1&&kind!=2))return false;
        int previous;if(pending.TryGetValue(account,out previous)){pending[account]=previous|kind;return false;}
        if(pending.Count>=256)return false;
        pending.Add(account,kind);order.Enqueue(account);if(running)return false;running=true;return true;
    }}
    internal bool Take(out long account,out int kind){lock(gate){
        account=0;kind=0;if(stopped||pending.Count==0){running=false;return false;}
        account=order.Dequeue();kind=pending[account];pending.Remove(account);return true;
    }}
    internal void Stop(){lock(gate){stopped=true;pending.Clear();order.Clear();}}
}
sealed partial class DiscoveryServer {
    readonly MailboxHintQueue mailboxHints=new MailboxHintQueue();
    void QueueMailboxHint(long account,bool receipt){
        if(mailboxHints.Enqueue(account,receipt?2:1))QueueWorker(DrainMailboxHints);
    }
    void DrainMailboxHints(){
        long account;int kind;
        while(mailboxHints.Take(out account,out kind)){
            if(stopping)return;
            if((kind&1)!=0)SendMailboxHint(account,"mailbox_ready");
            if((kind&2)!=0)SendMailboxHint(account,"mailbox_receipt_ready");
        }
    }
    void SendMailboxHint(long account,string name){
        EventClient[] subscribers;lock(eventGate)subscribers=eventClients.Where(value=>value.AccountId==account).ToArray();
        var payload=System.Text.Encoding.UTF8.GetBytes(new JavaScriptSerializer().Serialize(new Dictionary<string,object>{{"ok",true},{"version",ProtocolVersion},{"type","friend_event"},{"event",name}}));
        foreach(var subscriber in subscribers){
            // Only this owned dispatcher may wait for an event writer. Bounded
            // waits requeue/coalesce fairly, never delaying the durable RPC or
            // losing the sole hint merely because another writer is active.
            if(stopping)return;
            if(!System.Threading.Monitor.TryEnter(subscriber.Gate,100)){QueueMailboxHint(account,name=="mailbox_receipt_ready");continue;}
            bool dead=false;
            try{
                if(!subscriber.Active||leaseClock.ElapsedMilliseconds-subscriber.LastHeartbeatUtc>=EventLeaseSeconds*1000L||
                    accounts.Authenticate(subscriber.SessionTokenHash)!=subscriber.AccountId||
                    !accounts.SessionAllowsDevice(subscriber.SessionTokenHash,subscriber.PublicIdentity)||!accounts.OwnsDevice(subscriber.AccountId,subscriber.PublicIdentity))continue;
                WriteFrame(subscriber.Tls,payload);
            }catch{dead=true;}
            finally{System.Threading.Monitor.Exit(subscriber.Gate);}
            if(dead){lock(eventGate){subscriber.Active=false;eventClients.Remove(subscriber);}subscriber.Dispose();}
        }
    }
    // Only a hint after durable storage, never a delivery acknowledgement.
    // Cache notices use the same existing wake path as ordinary mailbox data.
    void NotifyStoredMailbox(byte[] wire){
        var envelope=MailboxProtocol.Envelope.Parse(wire);
        QueueMailboxHint(envelope.Recipient,false);
        pushWakes.Hint(envelope.Recipient,envelope.RecipientDevice);
    }
    // Uses the existing authenticated event channel and its write/session gate.
    // This is application dispatch, not a mailbox socket or reconnect owner.
    void HandleMailboxChannel(EventClient client,Dictionary<string,object> request){
        var reply=ProcessMailboxRequest(client.AccountId,client.PublicIdentity,request,
            ()=>client.Active&&accounts.Authenticate(client.SessionTokenHash)==client.AccountId&&accounts.SessionAllowsDevice(client.SessionTokenHash,client.PublicIdentity));
        GroupSession(client).Send(reply);
    }
    Dictionary<string,object> ProcessMailboxRequest(long account,string device,Dictionary<string,object> request,Func<bool> current){
        string type=Text(request,"type"),requestId=Text(request,"requestId");
        var reply=new Dictionary<string,object>{{"type","mailbox_result"},{"requestId",requestId??""},{"operation",type??""},{"state","malformed_request"}};
        try {
            if(!MailboxProtocol.Id(requestId,32)||!current()||!accounts.OwnsDevice(account,device))throw new InvalidOperationException();
            byte[] identity=null;
            if(type=="mailbox_register"||type=="mailbox_store"||type=="mailbox_ack"||type=="mailbox_revoke")identity=MailboxProtocol.Decode(Text(request,"signingPublicKey"),72);
            if(type=="mailbox_register")reply["state"]=accounts.RegisterMailboxKey(account,device,identity,MailboxProtocol.Decode(Text(request,"binding"),4096));
            else if(type=="mailbox_revoke")reply["state"]=accounts.RevokeMailboxKey(account,device,identity,MailboxProtocol.Decode(Text(request,"revocation"),1024));
            else if(type=="mailbox_keys") {
                long target;if(!Int64.TryParse(Text(request,"recipientAccountId"),out target)||target<=0)throw new InvalidOperationException();
                reply["keys"]=accounts.MailboxKeys(account,device,target);reply["state"]="ready";
            } else if(type=="mailbox_eligibility_snapshot"||type=="mailbox_eligibility_validate") {
                long target;if(!Int64.TryParse(Text(request,"recipientAccountId"),out target)||target<=0)throw new InvalidOperationException();
                var result=type=="mailbox_eligibility_snapshot"
                    ?accounts.MailboxEligibilitySnapshot(account,device,target)
                    :accounts.ValidateMailboxEligibility(account,device,target,Text(request,"snapshotId"));
                foreach(var field in result)reply[field.Key]=field.Value;
            } else if(type=="mailbox_store") {
                byte[] wire=MailboxProtocol.Decode(Text(request,"envelope"),MailboxProtocol.MaxWire);
                string state=accounts.StoreMailbox(account,device,identity,wire);reply["state"]=state;
                if(state=="stored") {
                    // Hint only. Recipient native authenticated/decrypted durable
                    // acceptance and its signed ACK must precede notification.
                    NotifyStoredMailbox(wire);
                }
            } else if(type=="mailbox_fetch") {
                var rows=accounts.FetchMailbox(account,device,Text(request,"cursor")??"");
                reply["envelopes"]=rows;reply["nextCursor"]=rows.Count==0?"":rows[rows.Count-1]["cursor"];reply["state"]="ready";
            }
            else if(type=="mailbox_ack") {
                byte[] wire=MailboxProtocol.Decode(Text(request,"receipt"),2048);
                string state=accounts.AckMailbox(account,device,identity,wire);reply["state"]=state;
                if(state=="acknowledged") {
                    var receipt=MailboxProtocol.Receipt.Parse(wire);
                    long senderAccount=accounts.MailboxReceiptSender(receipt);
                    if(senderAccount>0)QueueMailboxHint(senderAccount,true);
                }
            } else if(type=="mailbox_receipts") {
                var rows=accounts.MailboxReceipts(account,device,Text(request,"afterMessageId")??"",Text(request,"cursor"));
                reply["receipts"]=rows;reply["nextCursor"]=rows.Count==0?"":rows[rows.Count-1]["cursor"];reply["state"]="ready";
            }
        }catch(InvalidOperationException){reply["state"]="rejected_or_storage_unavailable";}
        catch(FormatException){reply["state"]="malformed_request";}
        catch(System.Security.Cryptography.CryptographicException){reply["state"]="invalid_key_or_signature";}
        return reply;
    }
    // Ordinary authenticated server request, with a bounded second frame for
    // opaque envelope data. Does not replace the existing event subscription or
    // create another transport/session owner. Both platforms reuse their current
    // ServerChannel/DiscoveryClient TLS request execution and cancellation scope.
    void ServeMailboxRpc(System.Net.Security.SslStream tls,Dictionary<string,object> hello){
        string hash=TokenHash(Text(hello,"sessionToken")),device=Text(hello,"publicIdentity"),requestId=Text(hello,"requestId");
        long account=accounts.Authenticate(hash);
        if(account<=0||!ValidIdentity(device)||!MailboxProtocol.Id(requestId,32)||!accounts.SessionAllowsDevice(hash,device)||!accounts.OwnsDevice(account,device)){
            WriteFrame(tls,System.Text.Encoding.UTF8.GetBytes(json.Serialize(Error("mailbox_not_authorized"))));return;
        }
        tls.ReadTimeout=10000;tls.WriteTimeout=10000;
        WriteFrame(tls,System.Text.Encoding.UTF8.GetBytes(json.Serialize(Ok(new Dictionary<string,object>{{"type","mailbox_ready"},{"requestId",requestId}}))));
        byte[] bytes=ReadFrame(tls,100000);if(bytes==null)return;
        var request=groupJson.Deserialize<Dictionary<string,object>>(System.Text.Encoding.UTF8.GetString(bytes));
        if(Text(request,"requestId")!=requestId)return;
        var result=ProcessMailboxRequest(account,device,request,()=>accounts.Authenticate(hash)==account&&accounts.SessionAllowsDevice(hash,device));
        var output=System.Text.Encoding.UTF8.GetBytes(groupJson.Serialize(result));if(output.Length>100000)throw new System.IO.InvalidDataException("Mailbox response exceeds bound.");
        WriteFrame(tls,output);
    }
}
}
