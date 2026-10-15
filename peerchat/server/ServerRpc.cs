using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Security;
using System.Text;

namespace PeerChat.Server {
sealed partial class DiscoveryServer {
    partial void ProcessAttachmentCacheRequest(long account,string device,Func<bool> current,Dictionary<string,object> request,ref Dictionary<string,object> reply);
    partial void MaintainAttachmentCache();
    partial void CloseAttachmentCache();
    // Serialized ordinary application RPC only. Events, relay and peer TLS
    // remain separate. No delivery retry or background connection owner.
    void ServeRpc(SslStream tls,Dictionary<string,object> hello,string observedHost){
        string token=Text(hello,"sessionToken"),hash=TokenHash(token),device=Text(hello,"publicIdentity");
        long account=accounts.Authenticate(hash);
        Func<bool> current=()=>account>0&&accounts.Authenticate(hash)==account&&
            accounts.SessionAllowsDevice(hash,device)&&accounts.OwnsDevice(account,device);
        if(!ValidIdentity(device)||!current()){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("session_invalid"))));return;}
        tls.ReadTimeout=15000;tls.WriteTimeout=10000;
        WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Ok(new Dictionary<string,object>{{"type","rpc_ready"},{"rpcVersion",1}}))));
        var age=Stopwatch.StartNew();
        for(int count=0;count<64&&!stopping&&age.ElapsedMilliseconds<120000;count++){
            var bytes=ReadFrame(tls,100000);if(bytes==null)return;
            var request=groupJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(bytes));
            string id=Text(request,"rpcRequestId"),type=Text(request,"type");
            if(!MailboxProtocol.Id(id,32)||Text(request,"sessionToken")!=token||Text(request,"publicIdentity")!=device||!current())return;
            if(type=="events"||type=="relay_attach"||type=="rpc_open"||type=="mailbox_rpc"||
                type=="login"||type=="register"||type==null||type.StartsWith("remember_",StringComparison.Ordinal))return;
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
            var output=Encoding.UTF8.GetBytes(groupJson.Serialize(reply));
            if(output.Length>100000)throw new InvalidDataException("RPC response exceeds bound");
            WriteFrame(tls,output);if(!more)return;
        }
    }
}
}
