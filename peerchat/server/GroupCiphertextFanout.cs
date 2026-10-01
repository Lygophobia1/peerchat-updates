using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace PeerChat.Server {
// Application fanout only. Sessions, sockets, reconnect and roster ownership stay outside.
internal sealed class GroupFanoutSession {
    public long Account;
    public string Device,Binding;
    public Func<bool> Current;
    public Action<Dictionary<string,object>> Send;
}
internal sealed class GroupCiphertextFanout {
    internal const int MaxEnvelope=65790,MaxFrame=100000,MaxRecipients=64,MaxRecent=128;
    readonly AccountDatabase database;
    readonly object gate=new object();
    sealed class Delivery {
        public string Group,Message,SenderDevice,SenderBinding;
        public long Generation,Sender,Expires;
        public byte[] Wire;
        public GroupFanoutSession Origin;
        public readonly Dictionary<string,string> Recipients=new Dictionary<string,string>(StringComparer.Ordinal);
    }
    readonly Dictionary<string,Delivery> recent=new Dictionary<string,Delivery>(StringComparer.Ordinal);
    public GroupCiphertextFanout(AccountDatabase database){this.database=database;}
    static bool Id(string value,int length){return value!=null&&value.Length==length&&value.All(c=>(c>='0'&&c<='9')||(c>='a'&&c<='f'));}
    static long Number(byte[] wire,int offset){ulong value=0;for(int i=0;i<8;i++)value=(value<<8)|wire[offset+i];return value>long.MaxValue?0:(long)value;}
    static string Text(byte[] wire,int offset,int length){return Encoding.ASCII.GetString(wire,offset,length);}
    static Dictionary<string,object> Reply(string group,string message,string state,int sessions=0,int accepted=0,int duplicates=0,int rejected=0){return new Dictionary<string,object>{{"type","group_delivery"},{"groupId",group??""},{"messageId",message??""},{"state",state},{"sessionCount",sessions},{"acceptedCount",accepted},{"duplicateCount",duplicates},{"rejectedCount",rejected}};}
    static bool Route(byte[] wire,string group,long generation,GroupFanoutSession origin,out string id){
        id="";if(wire==null||wire.Length<254||wire.Length>MaxEnvelope||Text(wire,0,4)!="PCGE"||wire[4]!=0||wire[5]!=1)return false;
        long length=((long)wire[170]<<24)|((long)wire[171]<<16)|((long)wire[172]<<8)|wire[173];
        id=Text(wire,54,32);
        return length<=65536&&wire.Length==254+length&&Id(id,32)&&Text(wire,6,32)==group&&Number(wire,38)==generation&&Number(wire,46)==generation&&Number(wire,86)==origin.Account&&Text(wire,94,64)==origin.Device;
    }
    void Purge(){long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();foreach(var key in recent.Where(x=>x.Value.Expires<=now).Select(x=>x.Key).ToArray())recent.Remove(key);}
    public Dictionary<string,object> Send(GroupFanoutSession origin,string group,long generation,byte[] wire,IList<GroupFanoutSession> sessions){
        string message;
        if(!Id(group,32)||generation<=0||!Id(origin.Device,64)||!Route(wire,group,generation,origin,out message))return Reply(group,"","malformed_request");
        lock(gate){
            Purge();if(!origin.Current()||!database.GroupMessageAuthorized(group,generation,origin.Account,origin.Device))return Reply(group,message,"not_authorized_or_stale");
            string key=group+":"+message;Delivery prior;
            if(recent.TryGetValue(key,out prior)){
                if(prior.Sender!=origin.Account||prior.SenderDevice!=origin.Device||!prior.Wire.SequenceEqual(wire))return Reply(group,message,"conflicting_message");
                return Summary(prior,"duplicate_server_request");
            }
            if(recent.Count>=MaxRecent)return Reply(group,message,"fanout_capacity");
            var roster=database.GroupRoster(group,origin.Account,origin.Device);
            var members=(List<Dictionary<string,object>>)roster["members"];
            if(members.Count(m=>Convert.ToString(m["state"])=="member")>MaxRecipients)return Reply(group,message,"group_recipient_limit");
            var delivery=new Delivery{Group=group,Message=message,Generation=generation,Sender=origin.Account,SenderDevice=origin.Device,SenderBinding=origin.Binding,Origin=origin,Wire=(byte[])wire.Clone(),Expires=DateTimeOffset.UtcNow.ToUnixTimeSeconds()+120};
            foreach(var session in sessions){
                if(session.Account==origin.Account&&session.Device==origin.Device||!session.Current())continue;
                // Roster rechecked under its mutation lock immediately through admission/write.
                bool sent=false;
                database.WithGroupMessageAuthorization(group,generation,origin.Account,origin.Device,session.Account,session.Device,delegate{
                    if(!origin.Current()||!session.Current())return;
                    try{session.Send(new Dictionary<string,object>{{"type","group_message"},{"groupId",group},{"messageId",message},{"envelope",Convert.ToBase64String(wire)}});sent=true;}catch{}
                });
                if(sent)delivery.Recipients[session.Binding]="session_delivered";
            }
            recent[key]=delivery;return Summary(delivery,"server_accepted_online_only");
        }
    }
    static Dictionary<string,object> Summary(Delivery value,string state){return Reply(value.Group,value.Message,state,value.Recipients.Count,value.Recipients.Values.Count(x=>x=="accepted"),value.Recipients.Values.Count(x=>x=="duplicate_message"),value.Recipients.Values.Count(x=>x=="rejected"));}
    public Dictionary<string,object> Ack(GroupFanoutSession session,string group,string message,string state){
        if(!Id(group,32)||!Id(message,32)||(state!="accepted"&&state!="duplicate_message"&&state!="rejected"))return Reply(group,message,"invalid_ack");
        lock(gate){Purge();Delivery delivery;
            if(!recent.TryGetValue(group+":"+message,out delivery)||!session.Current()||!delivery.Recipients.ContainsKey(session.Binding)||
                !database.GroupMessageAuthorized(group,delivery.Generation,session.Account,session.Device))return Reply(group,message,"stale_or_unsubmitted_ack");
            // A terminal receipt cannot be downgraded by a repeated/reordered ACK.
            if(delivery.Recipients[session.Binding]=="session_delivered")delivery.Recipients[session.Binding]=state;
            var summary=Summary(delivery,"recipient_acknowledged");
            if(delivery.Origin.Current()&&database.GroupMessageAuthorized(group,delivery.Generation,delivery.Sender,delivery.SenderDevice))try{delivery.Origin.Send(summary);}catch{}
            return summary;
        }
    }
}
}
