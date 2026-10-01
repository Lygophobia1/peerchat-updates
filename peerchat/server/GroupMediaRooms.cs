using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace PeerChat.Server {
// Ephemeral application state only: no sockets, media, timers, keys or reconnect owner.
internal sealed class GroupMediaRooms {
    internal const int MaxRooms=16,MaxDevices=64,MaxGroups=256;
    readonly AccountDatabase database;
    readonly object gate=new object();
    readonly string epoch=Guid.NewGuid().ToString("N");
    sealed class Device { public long Account; public string Peer,Binding; }
    sealed class Room {
        public string Id,Kind,Name,CreatorDevice;
        public long CreatorAccount;
        public Device Sharer;
        public readonly Dictionary<string,Device> People=new Dictionary<string,Device>(StringComparer.Ordinal);
    }
    sealed class Group {
        public long Revision;
        public readonly Dictionary<string,Room> Rooms=new Dictionary<string,Room>(StringComparer.Ordinal);
    }
    readonly Dictionary<string,Group> groups=new Dictionary<string,Group>(StringComparer.Ordinal);
    public GroupMediaRooms(AccountDatabase database){this.database=database;}
    static string Text(Dictionary<string,object> value,string key){object raw;return value.TryGetValue(key,out raw)?raw as string??"":"";}
    static long Number(Dictionary<string,object> value,string key){object raw;long n;return value.TryGetValue(key,out raw)&&Int64.TryParse(Convert.ToString(raw),out n)?n:-1;}
    static bool Id(string value){return value!=null&&value.Length==32&&value.All(c=>"0123456789abcdef".Contains(c));}
    static bool Name(string name){try{return !String.IsNullOrWhiteSpace(name)&&name.Length<=64&&!name.Any(Char.IsControl)&&new UTF8Encoding(false,true).GetByteCount(name)<=192;}catch{return false;}}
    static string Key(long account,string device){return account+":"+device;}
    static List<Dictionary<string,object>> Members(Dictionary<string,object> roster){return (List<Dictionary<string,object>>)roster["members"];}
    static bool Authorized(Dictionary<string,object> roster,long account,string device){return Members(roster).Any(m=>Convert.ToInt64(m["accountId"])==account&&Convert.ToString(m["publicIdentity"])==device&&Convert.ToString(m["state"])=="member");}
    static bool Moderator(Dictionary<string,object> roster,long account,string device){return Members(roster).Any(m=>Convert.ToInt64(m["accountId"])==account&&Convert.ToString(m["publicIdentity"])==device&&(Convert.ToString(m["role"])=="owner"||Convert.ToString(m["role"])=="admin")&&Convert.ToString(m["state"])=="member");}
    static bool Prune(Group group,Func<Device,bool> valid){
        bool changed=false;
        foreach(var room in group.Rooms.Values){
            foreach(var key in room.People.Where(p=>!valid(p.Value)).Select(p=>p.Key).ToArray()){room.People.Remove(key);changed=true;}
            if(room.Sharer!=null&&!valid(room.Sharer)){room.Sharer=null;room.People.Clear();changed=true;}
        }
        if(changed)group.Revision++;return changed;
    }
    static Dictionary<string,object> DeviceWire(Device value,Dictionary<string,object> roster){
        if(value==null)return null;
        var member=Members(roster).FirstOrDefault(m=>Convert.ToInt64(m["accountId"])==value.Account&&Convert.ToString(m["publicIdentity"])==value.Peer);
        return new Dictionary<string,object>{{"accountId",value.Account},{"publicIdentity",value.Peer},{"username",member==null?"":Convert.ToString(member["username"])}};
    }
    Dictionary<string,object> Snapshot(string id,Group group,Dictionary<string,object> roster){
        var rooms=group.Rooms.Values.OrderBy(r=>r.Id,StringComparer.Ordinal).Select(r=>(object)new Dictionary<string,object>{
            {"roomId",r.Id},{"kind",r.Kind},{"name",r.Name},{"creatorAccountId",r.CreatorAccount},{"creatorDevice",r.CreatorDevice},
            {"sharer",DeviceWire(r.Sharer,roster)},{"members",r.People.Values.OrderBy(d=>d.Account).ThenBy(d=>d.Peer,StringComparer.Ordinal).Select(d=>(object)DeviceWire(d,roster)).ToArray()}
        }).ToArray();
        return new Dictionary<string,object>{{"groupId",id},{"epoch",epoch},{"revision",group.Revision},{"rosterGeneration",roster["rosterGeneration"]},{"rooms",rooms}};
    }
    public Dictionary<string,object> Apply(GroupFanoutSession actor,Dictionary<string,object> request,out bool changed){
        bool mutation=false;string groupId=Text(request,"groupId"),operation=Text(request,"operation"),requestId=Text(request,"requestId");
        var result=new Dictionary<string,object>{{"type","group_room_result"},{"groupId",Id(groupId)?groupId:""},{"requestId",Id(requestId)?requestId:""},{"state","malformed_request"}};
        result["operation"]=new[]{"snapshot","create","rename","close","join_voice","leave_voice","start_share","stop_share","join_viewer","leave_viewer"}.Contains(operation)?operation:"invalid";
        changed=false;
        var allowed=new[]{"type","groupId","requestId","operation","roomId","kind","name","epoch","revision","rosterGeneration"};
        if(!Id(groupId)||!Id(requestId)||request.Keys.Any(k=>!allowed.Contains(k)))return result;
        lock(gate){
            bool admitted=database.WithGroupRosterAuthorization(groupId,actor.Account,actor.Device,roster=>{
                if(!actor.Current()){result["state"]="stale_session";return;}
                Group group;
                if(!groups.TryGetValue(groupId,out group)){
                    if(groups.Count>=MaxGroups){result["state"]="room_capacity";return;}
                    groups[groupId]=group=new Group();
                }
                mutation=Prune(group,d=>Authorized(roster,d.Account,d.Peer));
                if(operation=="snapshot"){result["state"]="snapshot";result["snapshot"]=Snapshot(groupId,group,roster);return;}
                if(Text(request,"epoch")!=epoch||Number(request,"revision")!=group.Revision||Number(request,"rosterGeneration")!=Convert.ToInt64(roster["rosterGeneration"])){
                    result["state"]="stale_room_state";result["snapshot"]=Snapshot(groupId,group,roster);return;
                }
                string roomId=Text(request,"roomId"),kind=Text(request,"kind"),name=Text(request,"name");
                Room room=null;bool altered=false;string state="accepted";
                if(operation=="create"){
                    if((kind!="voice"&&kind!="screen_share")||!Name(name)){result["state"]="malformed_request";return;}
                    if(group.Rooms.Count>=MaxRooms){result["state"]="room_capacity";return;}
                    room=new Room{Id=Guid.NewGuid().ToString("N"),Name=name,Kind=kind,CreatorAccount=actor.Account,CreatorDevice=actor.Device};
                    group.Rooms.Add(room.Id,room);result["roomId"]=room.Id;altered=true;
                }else{
                    if(!Id(roomId)||!group.Rooms.TryGetValue(roomId,out room)){result["state"]="room_not_found";return;}
                    bool owns=room.CreatorAccount==actor.Account&&room.CreatorDevice==actor.Device||Moderator(roster,actor.Account,actor.Device);
                    string key=Key(actor.Account,actor.Device);
                    var device=new Device{Account=actor.Account,Peer=actor.Device,Binding=actor.Binding};
                    if((operation=="join_voice"||operation=="join_viewer"||operation=="start_share")&&
                        !room.People.ContainsKey(key)&&!(room.Sharer!=null&&room.Sharer.Account==actor.Account&&room.Sharer.Peer==actor.Device)&&
                        group.Rooms.Values.Sum(r=>r.People.Count+(r.Sharer==null?0:1))>=256){
                        result["state"]="group_media_capacity";return;
                    }
                    switch(operation){
                        case "rename":
                            if(!owns){state="room_not_owned";break;}
                            if(!Name(name)){state="malformed_request";break;}
                            if(room.Name!=name){room.Name=name;altered=true;}break;
                        case "close":
                            if(!owns){state="room_not_owned";break;}
                            group.Rooms.Remove(room.Id);altered=true;break;
                        case "join_voice":
                            if(room.Kind!="voice"){state="wrong_room_kind";break;}
                            if(!room.People.ContainsKey(key)&&room.People.Count>=MaxDevices){state="room_capacity";break;}
                            // One account occupies one voice room in this group; devices
                            // may coexist in that room. Moving ejects its other-room devices.
                            foreach(var other in group.Rooms.Values.Where(r=>r.Kind=="voice"&&r!=room))
                                foreach(var old in other.People.Where(p=>p.Value.Account==actor.Account).Select(p=>p.Key).ToArray()){other.People.Remove(old);altered=true;}
                            if(!room.People.ContainsKey(key)||room.People[key].Binding!=actor.Binding){room.People[key]=device;altered=true;}break;
                        case "leave_voice":
                            if(room.Kind!="voice"){state="wrong_room_kind";break;}
                            altered=room.People.Remove(key);break;
                        case "start_share":
                            if(room.Kind!="screen_share"){state="wrong_room_kind";break;}
                            if(room.Sharer!=null&&(room.Sharer.Account!=actor.Account||room.Sharer.Peer!=actor.Device)){state="sharer_busy";break;}
                            if(room.Sharer==null||room.Sharer.Binding!=actor.Binding){room.Sharer=device;room.People.Remove(key);altered=true;}break;
                        case "stop_share":
                            if(room.Kind!="screen_share"){state="wrong_room_kind";break;}
                            if(room.Sharer!=null&&!owns&&(room.Sharer.Account!=actor.Account||room.Sharer.Peer!=actor.Device)){state="not_sharer";break;}
                            if(room.Sharer!=null){room.Sharer=null;room.People.Clear();altered=true;}break;
                        case "join_viewer":
                            if(room.Kind!="screen_share"){state="wrong_room_kind";break;}
                            if(room.Sharer==null){state="share_inactive";break;}
                            if(room.Sharer.Account==actor.Account&&room.Sharer.Peer==actor.Device){state="already_sharer";break;}
                            if(!room.People.ContainsKey(key)&&room.People.Count>=MaxDevices){state="room_capacity";break;}
                            if(!room.People.ContainsKey(key)||room.People[key].Binding!=actor.Binding){room.People[key]=device;altered=true;}break;
                        case "leave_viewer":
                            if(room.Kind!="screen_share"){state="wrong_room_kind";break;}
                            altered=room.People.Remove(key);break;
                        default: state="malformed_request";break;
                    }
                }
                if(altered){group.Revision++;mutation=true;}
                result["state"]=state;result["snapshot"]=Snapshot(groupId,group,roster);
            });
            if(!admitted)result["state"]="group_not_authorized";
        }
        changed=mutation;return result;
    }
    public string[] Disconnect(string binding){
        lock(gate)return groups.Where(pair=>Prune(pair.Value,d=>d.Binding!=binding)).Select(pair=>pair.Key).ToArray();
    }
    public void Reconcile(string groupId){
        lock(gate){Group group;if(!groups.TryGetValue(groupId,out group))return;
            Prune(group,d=>database.GroupRosterMember(groupId,d.Account,d.Peer));
        }
    }
}
}
