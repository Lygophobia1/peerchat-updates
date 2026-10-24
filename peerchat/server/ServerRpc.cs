using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Security;
using System.Text;

namespace PeerChat.Server {
sealed partial class DiscoveryServer {
    partial void ProcessAttachmentCacheRequest(long account,string device,Func<bool> current,Dictionary<string,object> request,ref Dictionary<string,object> reply);
    partial void ProcessAttachmentCacheBatch(long account,string device,Func<bool> current,IList<Dictionary<string,object>> requests,ref List<Dictionary<string,object>> replies,ref long[] components);
    partial void MaintainAttachmentCache();
    partial void CloseAttachmentCache();
    // Serialized ordinary application RPC only. Events, relay and peer TLS
    // remain separate. No delivery retry or background connection owner.
    void ServeRpc(SslStream tls,Dictionary<string,object> hello,string observedHost){
        string token=Text(hello,"sessionToken"),hash=TokenHash(token),device=Text(hello,"publicIdentity");
        long account=accounts.Authenticate(hash);
        Func<bool> current=()=>account>0&&accounts.Authenticate(hash)==account&&
            accounts.SessionAllowsDevice(hash,device)&&accounts.ActiveDevice(account,device);
        if(!ValidIdentity(device)||!current()){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("session_invalid"))));return;}
        tls.ReadTimeout=15000;tls.WriteTimeout=10000;
        WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Ok(new Dictionary<string,object>{{"type","rpc_ready"},{"rpcVersion",1}}))));
        var age=Stopwatch.StartNew();
        for(int count=0;count<64&&!stopping&&age.ElapsedMilliseconds<120000;count++){
            var bytes=ReadFrame(tls,100000);if(bytes==null)return;
            long processingStarted=Stopwatch.GetTimestamp(); // Complete frame read, not transport arrival.
            var request=groupJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(bytes));
            long firstParseDone=Stopwatch.GetTimestamp();
            string id=Text(request,"rpcRequestId"),type=Text(request,"type");
            if(!MailboxProtocol.Id(id,32)||Text(request,"sessionToken")!=token||Text(request,"publicIdentity")!=device)return;
            long firstAuthStarted=Stopwatch.GetTimestamp();
            if(!current())return;
            long firstAuthTicks=Math.Max(0,Stopwatch.GetTimestamp()-firstAuthStarted);
            if(type=="events"||type=="relay_attach"||type=="rpc_open"||type=="mailbox_rpc"||
                type=="login"||type=="register"||type==null||type.StartsWith("remember_",StringComparison.Ordinal))return;
            if(request.ContainsKey("batchCount")){
                object countValue=request["batchCount"];int batchCount=countValue is int?(int)countValue:0;
                if(type!="cache_dm_append"||batchCount<2||batchCount>8||count+batchCount>64||age.ElapsedMilliseconds>=110000)return;
                if(Text(request,"requestId")!=id)return;
                var batch=new List<Dictionary<string,object>>{request};var ids=new List<string>{id};
                var batchRead=Stopwatch.StartNew();int totalBytes=bytes.Length;
                long batchFrameReadTicks=0,batchFrameReadMaxTicks=0,batchParseTicks=Math.Max(0,firstParseDone-processingStarted),batchAuthTicks=firstAuthTicks;
                var frameReadTiming=new FrameReadTiming();
                for(int i=1;i<batchCount;i++){
                    int remaining=15000-(int)batchRead.ElapsedMilliseconds;if(remaining<=0)return;
                    long frameReadStarted=Stopwatch.GetTimestamp();
                    var pending=ReadFrameAsync(tls,100000,frameReadTiming);
                    if(!pending.Wait(remaining)){pending.ContinueWith(t=>{var ignored=t.Exception;},System.Threading.Tasks.TaskContinuationOptions.OnlyOnFaulted);return;}
                    var nextBytes=pending.GetAwaiter().GetResult();if(nextBytes==null)return;
                    long frameReadTicks=Math.Max(0,Stopwatch.GetTimestamp()-frameReadStarted);
                    batchFrameReadTicks+=frameReadTicks;batchFrameReadMaxTicks=Math.Max(batchFrameReadMaxTicks,frameReadTicks);
                    totalBytes+=nextBytes.Length;if(totalBytes>8*100000)return;
                    long parseStarted=Stopwatch.GetTimestamp();
                    var next=groupJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(nextBytes));
                    batchParseTicks+=Math.Max(0,Stopwatch.GetTimestamp()-parseStarted);
                    string nextId=Text(next,"rpcRequestId");
                    if(!MailboxProtocol.Id(nextId,32)||ids.Contains(nextId)||Text(next,"type")!="cache_dm_append"||
                        Text(next,"requestId")!=nextId||next.ContainsKey("batchCount")||Text(next,"sessionToken")!=token||Text(next,"publicIdentity")!=device)return;
                    long frameAuthStarted=Stopwatch.GetTimestamp();
                    if(!current())return;
                    batchAuthTicks+=Math.Max(0,Stopwatch.GetTimestamp()-frameAuthStarted);
                    batch.Add(next);ids.Add(nextId);
                }
                long collectionDone=Stopwatch.GetTimestamp();
                Func<bool> batchCurrent=()=>current()&&accounts.SessionDevice(hash)==device;
                if(!batchCurrent())return;
                List<Dictionary<string,object>> bodies=null;long[] components=null;
                long executionStarted=Stopwatch.GetTimestamp();
                ProcessAttachmentCacheBatch(account,device,batchCurrent,batch,ref bodies,ref components);
                long executionDone=Stopwatch.GetTimestamp();
                if(bodies==null||bodies.Count!=batchCount)return;
                count+=batchCount-1;
                long replyEncodeTicks=0,replyWriteTicks=0;
                for(int i=0;i<batchCount;i++){
                    bool batchMore=i<batchCount-1||count<63&&age.ElapsedMilliseconds<110000&&current();
                    var item=new Dictionary<string,object>{{"type","rpc_result"},{"rpcRequestId",ids[i]},{"operation",type},{"body",bodies[i]},{"more",batchMore}};
                    long encoding=Stopwatch.GetTimestamp();
                    var outputBytes=Encoding.UTF8.GetBytes(groupJson.Serialize(item));if(outputBytes.Length>100000)throw new InvalidDataException("RPC response exceeds bound");
                    replyEncodeTicks+=Math.Max(0,Stopwatch.GetTimestamp()-encoding);
                    long writing=Stopwatch.GetTimestamp();try{WriteFrame(tls,outputBytes);}finally{replyWriteTicks+=Math.Max(0,Stopwatch.GetTimestamp()-writing);}
                }
                try{if(Convert.ToString(bodies[0]["state"])=="accepted"){
                    long finished=Stopwatch.GetTimestamp();
                    string row=timing.Record("cache_append_components",type,id,processingStarted,finished,components);
                    if(row!=null){
                        double scale=1000.0/Stopwatch.Frequency;
                        row+=" batch_chunks="+batchCount+" batch_wire_bytes="+totalBytes+
                            " batch_collect_ms="+(Math.Max(0,collectionDone-processingStarted)*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_frame_read_ms="+(batchFrameReadTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_frame_read_max_ms="+(batchFrameReadMaxTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_frame_header_ms="+(frameReadTiming.HeaderTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_frame_body_ms="+(frameReadTiming.BodyTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_frame_header_reads="+frameReadTiming.HeaderReads+
                            " batch_frame_body_reads="+frameReadTiming.BodyReads+
                            " batch_frame_async_awaits="+(frameReadTiming.HeaderAsyncWaits+frameReadTiming.BodyAsyncWaits)+
                            " batch_frame_thread_hops="+frameReadTiming.ThreadHops+
                            " batch_json_parse_ms="+(batchParseTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_first_parse_ms="+(Math.Max(0,firstParseDone-processingStarted)*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_frame_auth_ms="+(batchAuthTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_execute_ms="+(Math.Max(0,executionDone-executionStarted)*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_reply_encode_ms="+(replyEncodeTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture)+
                            " batch_reply_write_ms="+(replyWriteTicks*scale).ToString("F3",System.Globalization.CultureInfo.InvariantCulture);
                        Log("DEBUG",row);
                    }
                }}catch(Exception error){if(error is OutOfMemoryException)throw;}
                if(count>=63||age.ElapsedMilliseconds>=110000)return;
                continue;
            }
            Dictionary<string,object> body;
            if(type.StartsWith("cache_",StringComparison.Ordinal)){
                // A legacy account-password token can name any owned device.
                // New cached historical access requires the existing device-
                // proof-bound session; do not weaken ordinary client compatibility.
                Func<bool> cacheCurrent=()=>current()&&accounts.SessionDevice(hash)==device;
                body=new Dictionary<string,object>{{"state",cacheCurrent()?"cache_disabled":"cache_device_session_required"}};
                if(cacheCurrent())ProcessAttachmentCacheRequest(account,device,cacheCurrent,request,ref body);
            }
            else if(type.StartsWith("mailbox_",StringComparison.Ordinal))body=ProcessMailboxRequest(account,device,request,current);
            else {if(bytes.Length>4096)throw new InvalidDataException("Ordinary RPC exceeds bound");body=Handle(request,observedHost,0);}
            bool more=count<63&&age.ElapsedMilliseconds<110000&&type!="logout"&&current();
            var reply=new Dictionary<string,object>{{"type","rpc_result"},{"rpcRequestId",id},{"operation",type},{"body",body},{"more",more}};
            bool identityReply=type.StartsWith("device_identity_",StringComparison.Ordinal);
            var output=Encoding.UTF8.GetBytes(identityReply?new System.Web.Script.Serialization.JavaScriptSerializer{MaxJsonLength=150000}.Serialize(reply):groupJson.Serialize(reply));
            if(output.Length>(identityReply?150000:100000))throw new InvalidDataException("RPC response exceeds bound");
            RecordTiming("rpc_processing",type,id,processingStarted);
            long writeStarted=Stopwatch.GetTimestamp();
            try{WriteFrame(tls,output);}finally{RecordTiming("rpc_response_write",type,id,writeStarted);}
            if(!more)return;
        }
    }
}
}
