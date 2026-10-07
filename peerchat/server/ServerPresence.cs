using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

namespace PeerChat.Server {
    // Ephemeral server authority. Never stores credentials, keys or message data.
    // The injected monotonic clock makes expiry independent of wall-clock changes.
    sealed class ServerPresence {
        internal sealed class Device {
            public long Account,Generation,Revision,Deadline;
            public string Identity,Owner,Session="",Host="",PublicHost="",Source="",Classification="";
            public int Port,PublicPort;
            public Device Copy(){return (Device)MemberwiseClone();}
        }
        readonly object gate=new object();
        readonly Dictionary<string,Device> devices=new Dictionary<string,Device>(StringComparer.Ordinal);
        readonly Func<long> clock;
        long revision,generation;
        public readonly string Epoch=Guid.NewGuid().ToString("N");
        public ServerPresence(Func<long> now=null){var watch=Stopwatch.StartNew();clock=now??(()=>watch.ElapsedMilliseconds);}
        public long Revision {get{lock(gate)return revision;}}
        public Device Observe(long account,string device,string owner,bool replace,string host="",int port=0,string publicHost="",int publicPort=0,string source="",string classification="",string session=""){
            if(account<=0||String.IsNullOrEmpty(device)||String.IsNullOrEmpty(owner))throw new ArgumentException("Invalid presence binding");
            lock(gate){
                Device value;
                if(!devices.TryGetValue(device,out value)||value.Account!=account||value.Owner!=owner||value.Deadline<=clock()){
                    if(!replace)return null;
                    value=new Device{Account=account,Identity=device,Owner=owner,Session=session,Generation=++generation};devices[device]=value;
                    value.Revision=++revision;
                }
                if(value.Deadline<=clock()||value.Host!=host||value.Port!=port||value.PublicHost!=publicHost||value.PublicPort!=publicPort){value.Revision=++revision;}
                value.Deadline=clock()+75000;value.Host=host;value.Port=port;value.PublicHost=publicHost;value.PublicPort=publicPort;value.Source=source;value.Classification=classification;
                return value.Copy();
            }
        }
        public Device Get(string device){lock(gate){Device value;return devices.TryGetValue(device,out value)&&value.Deadline>clock()?value.Copy():null;}}
        public List<Device> Snapshot(long account){lock(gate)return devices.Values.Where(d=>(account==0||d.Account==account)&&d.Deadline>clock()).OrderByDescending(d=>d.Revision).Select(d=>d.Copy()).ToList();}
        public Device Remove(long account,string device,string owner=null){lock(gate){Device value;if(!devices.TryGetValue(device,out value)||value.Account!=account||(owner!=null&&owner!=value.Owner))return null;devices.Remove(device);value=value.Copy();value.Revision=++revision;return value;}}
        public List<Device> Expire(Func<long,string,bool> authorized){
            Device[] observed;lock(gate)observed=devices.Values.Select(d=>d.Copy()).ToArray();
            var removed=new List<Device>();foreach(var value in observed){
                // Authorization/database work must never run under the lease lock.
                bool permitted=authorized(value.Account,value.Identity);
                lock(gate){Device current;if(!devices.TryGetValue(value.Identity,out current)||current.Generation!=value.Generation)continue;
                    if(current.Deadline<=clock()||!permitted){devices.Remove(value.Identity);var copy=current.Copy();copy.Revision=++revision;removed.Add(copy);}
                }
            }return removed;
        }
    }
}
