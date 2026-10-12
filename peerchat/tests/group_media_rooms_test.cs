#if PEERCHAT_PORTABLE
using ECDsaCng = PortableTestKey;
#endif
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;

internal static class GroupMediaRoomsTest {
    static void Require(bool value,string reason){if(!value)throw new Exception(reason);}
    static string Identity(ECDsaCng key){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(key.Key.Export(CngKeyBlobFormat.EccPublicBlob))).Replace("-","").ToLowerInvariant();}
    static void Wait(Func<bool> done,string reason){var end=DateTime.UtcNow.AddSeconds(12);while(DateTime.UtcNow<end){if(done())return;Thread.Sleep(20);}throw new Exception(reason);}
    static GroupMediaState Snapshot(GroupMediaRooms service,GroupFanoutSession actor,string group){
        bool changed;var reply=service.Apply(actor,new Dictionary<string,object>{{"groupId",group},{"requestId",Guid.NewGuid().ToString("N")},{"operation","snapshot"}},out changed);
        Require(Convert.ToString(reply["state"])=="snapshot","Snapshot rejected.");return GroupMediaState.Parse((Dictionary<string,object>)reply["snapshot"]);
    }
    static Dictionary<string,object> Request(GroupMediaState state,string op,string room="",string kind="",string name=""){
        return new Dictionary<string,object>{{"type","group_room"},{"groupId",state.GroupId},{"requestId",Guid.NewGuid().ToString("N")},{"operation",op},{"roomId",room},{"kind",kind},{"name",name},{"epoch",state.Epoch},{"revision",state.Revision},{"rosterGeneration",state.RosterGeneration}};
    }
    static GroupMediaState Apply(GroupMediaRooms service,GroupFanoutSession actor,GroupMediaState state,string op,string room="",string kind="",string name="",string expected="accepted"){
        bool changed;var reply=service.Apply(actor,Request(state,op,room,kind,name),out changed);
        Require(Convert.ToString(reply["state"])==expected,op+": "+reply["state"]+" expected "+expected);
        object raw;return reply.TryGetValue("snapshot",out raw)?GroupMediaState.Parse((Dictionary<string,object>)raw):state;
    }
    static GroupRoom Room(GroupMediaState state,string name){return state.Rooms.Single(r=>r.Name==name);}
    static void StateTests(AccountDatabase db,string group,string[] devices){
        var service=new GroupMediaRooms(db);bool current=true;
        Func<int,GroupFanoutSession> session=i=>new GroupFanoutSession{Account=i+1,Device=devices[i],Binding="binding-"+i,Current=()=>current,Send=x=>{}};
        var a=session(0);var b=session(1);var c=session(2);var outsider=session(3);
        var state=Snapshot(service,a,group);
        state=Apply(service,a,state,"create",kind:"voice",name:"A");string va=Room(state,"A").Id;
        state=Apply(service,a,state,"create",kind:"voice",name:"B");string vb=Room(state,"B").Id;
        state=Apply(service,a,state,"join_voice",va);Require(Room(state,"A").Members.Count==1,"One participant invalid.");
        state=Apply(service,b,state,"join_voice",va);state=Apply(service,c,state,"join_voice",vb);
        Require(Room(state,"A").Members.Count==2&&Room(state,"B").Members.Count==1,"Separate rooms mixed.");
        var old=state;state=Apply(service,b,state,"join_voice",vb);
        Require(Room(state,"A").Members.Count==1&&Room(state,"B").Members.Count==2,"Atomic move failed.");
        Require(state.Accepts(old)==false&&old.Accepts(state),"Client revision rollback permitted.");
        state=Apply(service,b,state,"join_voice",vb);Require(state.Revision==old.Revision+1,"Duplicate join mutated revision.");
        state=Apply(service,b,state,"leave_voice",vb);state=Apply(service,b,state,"leave_voice",vb);
        state=Apply(service,a,state,"rename",va,name:"Renamed");Require(Room(state,"Renamed").Id==va,"Rename changed identity.");
        Apply(service,b,state,"close",va,expected:"room_not_owned");
        state=Apply(service,a,state,"close",va);Require(state.Rooms.All(r=>r.Id!=va),"Occupied close failed.");
        state=Apply(service,c,state,"leave_voice",vb);state=Apply(service,a,state,"close",vb);
        state=Apply(service,a,state,"create",kind:"screen_share",name:"S1");string s1=Room(state,"S1").Id;
        state=Apply(service,c,state,"create",kind:"screen_share",name:"S2");string s2=Room(state,"S2").Id;
        // Creator and sharer need not be the same device.
        state=Apply(service,b,state,"start_share",s1);Require(Room(state,"S1").Sharer.AccountId==2&&Room(state,"S1").Members.Count==0,"Zero-viewer share or independent sharer failed.");
        state=Apply(service,c,state,"start_share",s2);
        var replacement=new GroupFanoutSession{Account=c.Account,Device=c.Device,Binding="replacement",Current=()=>true,Send=x=>{}};
        state=Apply(service,replacement,state,"start_share",s2);
        service.Disconnect(c.Binding);state=Snapshot(service,a,group);
        Require(Room(state,"S2").Sharer!=null,"Retired session removed explicitly reclaimed sharer state.");
        state=Apply(service,a,state,"join_viewer",s1);state=Apply(service,c,state,"join_viewer",s1);
        state=Apply(service,b,state,"join_viewer",s2);
        Require(Room(state,"S1").Members.Count==2&&Room(state,"S2").Members.Count==1,"Multiple independent shares failed.");
        Require(state.Rooms.All(r=>r.Kind!="voice"),"Viewing implicitly joined voice.");
        state=Apply(service,a,state,"leave_viewer",s1);state=Apply(service,a,state,"leave_viewer",s1);
        state=Apply(service,b,state,"stop_share",s1);
        Require(Room(state,"S1").Sharer==null&&Room(state,"S1").Members.Count==0&&Room(state,"S2").Sharer!=null,"Stopping one share affected another.");
        state=Apply(service,a,state,"create",kind:"voice",name:"Live");string live=Room(state,"Live").Id;
        state=Apply(service,b,state,"join_voice",live);state=Apply(service,a,state,"join_viewer",s2);
        var secondDevice=new GroupFanoutSession{Account=2,Device=new string('f',64),Binding="second-device",Current=()=>true,Send=x=>{}};
        // Unknown account/device is not authorized merely because account membership exists.
        Apply(service,secondDevice,state,"join_voice",live,expected:"group_not_authorized");
        var otherGroup=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(TestOwner,new string('b',32),1)));
        Require(db.RegisterGroup(1,devices[0],otherGroup)!=null,"Second group fixture failed.");
        var otherState=Snapshot(service,a,otherGroup.ServerGroupId);
        Apply(service,a,otherState,"join_voice",live,expected:"room_not_found");
        Apply(service,outsider,state,"create",kind:"voice",name:"Bad",expected:"group_not_authorized");
        Apply(service,outsider,state,"join_voice",live,expected:"group_not_authorized");
        Apply(service,a,state,"rename",live,name:"bad\nname",expected:"malformed_request");
        Apply(service,a,state,"create",kind:"voice",name:"\ud800",expected:"malformed_request");
        bool changed;var malformed=Request(state,"join_voice",live);malformed["accountId"]=1;
        Require(Convert.ToString(service.Apply(a,malformed,out changed)["state"])=="malformed_request","Untrusted identity field accepted.");
        var stale=Request(state,"leave_voice",live);stale["revision"]=0;
        Require(Convert.ToString(service.Apply(a,stale,out changed)["state"])=="stale_room_state","Stale revision accepted.");
        current=false;Apply(service,a,state,"join_voice",live,expected:"stale_session");current=true;
        service.Disconnect(b.Binding);state=Snapshot(service,a,group);
        Require(Room(state,"Live").Members.Count==0&&Room(state,"S2").Members.All(d=>d.AccountId!=2),"Disconnected device retained memberships.");
        Require(db.AddDevice(2,secondDevice.Device),"Second device binding failed.");
        Require(db.GroupInvite(group,1,devices[0],2,secondDevice.Device,state.RosterGeneration)!=null,"Second device invitation failed.");
        Require(db.GroupRespond(group,2,secondDevice.Device,true,state.RosterGeneration+1)!=null,"Second device acceptance failed.");
        state=Snapshot(service,a,group);state=Apply(service,b,state,"join_voice",live);state=Apply(service,secondDevice,state,"join_voice",live);
        Require(Room(state,"Live").Members.Count==2,"Two devices for one account not represented.");
        state=Apply(service,a,state,"create",kind:"voice",name:"Move");string move=Room(state,"Move").Id;
        state=Apply(service,secondDevice,state,"join_voice",move);
        Require(Room(state,"Live").Members.Count==0&&Room(state,"Move").Members.Count==1,"Account-wide atomic room move failed.");
        state=Apply(service,b,state,"join_viewer",s2);
        Require(db.GroupRemove(group,1,devices[0],2,devices[1],state.RosterGeneration)!=null,"Removal fixture failed.");
        service.Reconcile(group);state=Snapshot(service,a,group);
        Require(state.Rooms.All(r=>r.Members.All(d=>d.AccountId!=2)),"Removed account retained a viewer or sibling voice device.");
        Apply(service,b,state,"join_voice",live,expected:"group_not_authorized");
        Apply(service,secondDevice,state,"join_voice",move,expected:"group_not_authorized");
        Require(db.GroupRemove(group,1,devices[0],2,secondDevice.Device,state.RosterGeneration)==null,"Account removal left a separately removable sibling authorization.");
        service.Reconcile(group);state=Snapshot(service,a,group);Require(Room(state,"Move").Members.Count==0,"Removed voice member retained.");
        Require(db.GroupRemove(group,1,devices[0],3,devices[2],state.RosterGeneration)!=null,"Sharer removal failed.");
        service.Reconcile(group);state=Snapshot(service,a,group);Require(Room(state,"S2").Sharer==null&&Room(state,"S2").Members.Count==0,"Removed sharer did not clear viewers.");
        var immutable=GroupMediaState.Parse((Dictionary<string,object>)service.Apply(a,Request(state,"snapshot"),out changed)["snapshot"]);
        state=Apply(service,a,state,"rename",live,name:"New");
        Require(Room(immutable,"Live").Id==live,"Frozen snapshot mutated.");
        var restart=new GroupMediaRooms(db);Require(!state.Accepts(Snapshot(restart,a,group)),"Old epoch accepted server restart without channel reset.");
        Console.WriteLine("PASS: independent voice/screen rooms, voluntary device membership, atomic move, zero/one participants, moderation, malformed/stale/cross-group denial, immutable revisions, removal and disconnect cleanup.");
    }
    static ECDsaCng TestOwner;
    static void ScreenAuthorization(AccountDatabase db,string group,string[] devices){
        var service=new GroupMediaRooms(db);var received=new List<Dictionary<string,object>>();
        var sessions=Enumerable.Range(0,4).Select(i=>new GroupFanoutSession{Account=i+1,Device=devices[i],Binding="screen-"+i,Current=()=>true,Send=x=>received.Add(x)}).ToArray();
        var state=Snapshot(service,sessions[0],group);state=Apply(service,sessions[0],state,"create",kind:"screen_share",name:"Screen auth");string room=Room(state,"Screen auth").Id;
        state=Apply(service,sessions[0],state,"start_share",room);state=Apply(service,sessions[1],state,"join_viewer",room);state=Apply(service,sessions[2],state,"join_viewer",room);
        var screen=Room(state,"Screen auth");var sharer=screen.Sharer;var b=screen.Members.Single(m=>m.AccountId==2);var c=screen.Members.Single(m=>m.AccountId==3);
        var request=SignalRequest(state,room,sharer,b);
        Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="forwarded","Sharer-to-viewer signaling denied");
        Require(Convert.ToString(service.Signal(sessions[1],SignalRequest(state,room,b,sharer),sessions)["state"])=="forwarded","Viewer-to-sharer signaling denied");
        Require(Convert.ToString(service.Signal(sessions[1],SignalRequest(state,room,b,c),sessions)["state"])=="screen_role_not_authorized","Viewer-to-viewer signaling admitted");
        Require(Convert.ToString(service.Signal(sessions[3],request,sessions)["state"])=="group_not_authorized","Nonmember screen signaling admitted");
        var replacement=new GroupFanoutSession{Account=1,Device=devices[0],Binding="wrong-screen-binding",Current=()=>true,Send=x=>{}};
        Require(Convert.ToString(service.Signal(replacement,request,sessions)["state"])=="source_not_authorized","Replacement event session inherited sharer");
        state=Apply(service,sessions[1],state,"leave_viewer",room);request["revision"]=state.Revision;
        Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="target_not_authorized","Stopped viewer retained authorization");
        state=Apply(service,sessions[1],state,"join_viewer",room);request["revision"]=state.Revision;
        Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="target_not_authorized","Old viewer token survived rejoin");
        b=Room(state,"Screen auth").Members.Single(m=>m.AccountId==2);request=SignalRequest(state,room,b,sharer);
        state=Apply(service,sessions[0],state,"stop_share",room);state=Apply(service,sessions[0],state,"start_share",room);state=Apply(service,sessions[1],state,"join_viewer",room);
        var next=Room(state,"Screen auth");Require(next.Sharer.MediaGeneration!=sharer.MediaGeneration,"Restart reused share generation");
        request["revision"]=state.Revision;request["sourceGeneration"]=next.Members.Single(m=>m.AccountId==2).MediaGeneration;
        Require(Convert.ToString(service.Signal(sessions[1],request,sessions)["state"])=="target_not_authorized","Old sharer generation admitted");
        state=Apply(service,sessions[0],state,"close",room);request["revision"]=state.Revision;
        Require(Convert.ToString(service.Signal(sessions[1],request,sessions)["state"])=="room_not_found","Closed room admitted screen signaling");
        Console.WriteLine("PASS: screen sharer/viewer roles, both signaling directions, viewer-to-viewer/nonmember/session denial, stop/rejoin tokens, share generation replacement and room-close revocation");
    }
    static Dictionary<string,object> SignalRequest(GroupMediaState state,string room,GroupRoomDevice source,GroupRoomDevice target){
        return new Dictionary<string,object>{{"type","group_media"},{"groupId",state.GroupId},{"roomId",room},{"requestId",Guid.NewGuid().ToString("N")},{"epoch",state.Epoch},{"revision",state.Revision},{"rosterGeneration",state.RosterGeneration},{"sourceGeneration",source.MediaGeneration},{"targetGeneration",target.MediaGeneration},{"targetAccountId",target.AccountId},{"targetDevice",target.PeerId},{"routeId",Guid.NewGuid().ToString("N")},{"mediaGeneration",1},{"kind","offer"},{"data","test SDP; no audio"},{"mid","0"}};
    }
    static void MediaAuthorization(AccountDatabase db,string group,string[] devices){
        var service=new GroupMediaRooms(db);var received=new List<Dictionary<string,object>>();bool current=true;
        var sessions=Enumerable.Range(0,4).Select(i=>new GroupFanoutSession{Account=i+1,Device=devices[i],Binding="signal-"+i,Current=()=>current,Send=x=>received.Add(x)}).ToArray();
        var state=Snapshot(service,sessions[0],group);state=Apply(service,sessions[0],state,"create",kind:"voice",name:"Signal");string room=Room(state,"Signal").Id;
        state=Apply(service,sessions[0],state,"join_voice",room);state=Apply(service,sessions[1],state,"join_voice",room);
        var a=Room(state,"Signal").Members.Single(d=>d.AccountId==1);var b=Room(state,"Signal").Members.Single(d=>d.AccountId==2);
        var request=SignalRequest(state,room,a,b);
        Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="forwarded","Authorized voice signal not forwarded.");
        Require(received.Count==1&&Convert.ToInt64(received[0]["sourceAccountId"])==1&&Convert.ToString(received[0]["sourceDevice"])==devices[0],"Sender was not bound to authenticated actor.");
        request["epoch"]=Guid.NewGuid().ToString("N");Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="stale_room_state","Wrong server epoch accepted.");request["epoch"]=state.Epoch;
        request["rosterGeneration"]=state.RosterGeneration-1;Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="stale_room_state","Stale roster accepted.");request["rosterGeneration"]=state.RosterGeneration;
        request["roomId"]=Guid.NewGuid().ToString("N");Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="room_not_found","Wrong room accepted.");request["roomId"]=room;
        request["sourceGeneration"]=Guid.NewGuid().ToString("N");Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="source_not_authorized","Wrong source generation accepted.");request["sourceGeneration"]=a.MediaGeneration;
        var replacement=new GroupFanoutSession{Account=1,Device=devices[0],Binding="different-event-session",Current=()=>true,Send=x=>{}};
        Require(Convert.ToString(service.Signal(replacement,request,sessions)["state"])=="source_not_authorized","Unjoined replacement event session inherited media authority.");
        request["sourceAccountId"]=3;Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="malformed_request","Forged sender field accepted.");request.Remove("sourceAccountId");
        Require(Convert.ToString(service.Signal(sessions[3],request,sessions)["state"])=="group_not_authorized","Nonmember signaling accepted.");
        Require(Convert.ToString(service.Signal(sessions[2],request,sessions)["state"])=="source_not_authorized","Nonparticipant signaling accepted.");
        request["targetDevice"]=devices[2];Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="target_not_authorized","Wrong target device accepted.");request["targetDevice"]=devices[1];
        request["data"]=new string('x',32769);Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="malformed_request","Unbounded SDP accepted.");request["data"]="test SDP";
        current=false;Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="stale_session","Expired event session accepted.");current=true;
        state=Apply(service,sessions[2],state,"join_voice",room);
        Require(Room(state,"Signal").Members.Single(d=>d.AccountId==1).MediaGeneration==a.MediaGeneration&&Room(state,"Signal").Members.Single(d=>d.AccountId==2).MediaGeneration==b.MediaGeneration,"Unrelated join invalidated established endpoint generations.");
        Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="stale_room_state","Old room revision accepted.");request=SignalRequest(state,room,a,b);
        Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="forwarded","Existing source route cannot use fresh authority after another joins.");
        state=Apply(service,sessions[1],state,"leave_voice",room);state=Apply(service,sessions[1],state,"join_voice",room);
        request["revision"]=state.Revision;Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="target_not_authorized","Old participation token resurrected after rejoin.");
        b=Room(state,"Signal").Members.Single(d=>d.AccountId==2);Require(b.MediaGeneration!=Convert.ToString(request["targetGeneration"]),"Rejoin reused old generation.");
        request=SignalRequest(state,room,a,b);service.Disconnect(sessions[1].Binding);state=Snapshot(service,sessions[0],group);request["revision"]=state.Revision;
        Require(Convert.ToString(service.Signal(sessions[0],request,sessions)["state"])=="target_not_authorized","Disconnect retained media authorization.");
        Console.WriteLine("PASS: room-bound media signaling, authenticated sender/device, nonmember/nonparticipant denial, stable incremental generations, bounded SDP, stale revision/session/rejoin/disconnect rejection.");
    }
    static void Network(string directory,string certificate,string password,string[] devices,string group,AccountDatabase database){
        var listener=new TcpListener(IPAddress.Loopback,0);listener.Start();int port=((IPEndPoint)listener.LocalEndpoint).Port;listener.Stop();
        var server=new DiscoveryServer(new ServerConfiguration{Port=port,DatabasePath=Path.Combine(directory,"rooms.db"),DataDirectory=directory,LogDirectory=Path.Combine(directory,"logs"),CertificatePath=certificate,CertificatePassword=password});
        Exception failure=null;var thread=new Thread(()=>{try{server.Run();}catch(Exception e){failure=e;}}){IsBackground=true};thread.Start();
        string pin;using(var cert=new X509Certificate2(certificate,password))using(var sha=SHA256.Create())pin=BitConverter.ToString(sha.ComputeHash(cert.RawData)).Replace("-","");
        var clients=new List<DiscoveryClient>();
        try{
            for(int i=0;i<3;i++){
                var client=new DiscoveryClient("localhost",port,pin);clients.Add(client);
                string token=Guid.NewGuid().ToString("N"),hash;using(var sha=SHA256.Create())hash=BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(token))).Replace("-","").ToLowerInvariant();
                database.CreateSession(hash,i+1,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());client.AccountId=i+1;client.SessionToken=token;client.StartEventChannel(devices[i],e=>{});
            }
            Wait(()=>clients.All(c=>c.EventChannelConnected),"Room event channels not connected.");
            foreach(var client in clients)Require(client.QueueGroupRoom(group,"snapshot"),"Snapshot not queued.");
            Wait(()=>clients.All(c=>c.GroupRooms(group)!=null),"Initial snapshots missing.");
            Action<int,string,string,string,string> op=(index,operation,room,kind,name)=>{
                Require(clients[index].QueueGroupRoom(group,operation,room,kind,name),"Operation not queued: "+operation);
                Wait(()=>clients[index].GroupRoomResult(group)=="accepted","Operation rejected: "+operation+" / "+clients[index].GroupRoomResult(group));
                long revision=clients[index].GroupRooms(group).Revision;
                Wait(()=>clients.All(c=>c.GroupRooms(group)!=null&&c.GroupRooms(group).Revision==revision),"Three clients did not converge.");
            };
            op(0,"create","","voice","A");string va=Room(clients[0].GroupRooms(group),"A").Id;
            op(0,"create","","voice","B");string vb=Room(clients[0].GroupRooms(group),"B").Id;
            op(0,"join_voice",va,"","");op(1,"join_voice",va,"","");op(2,"join_voice",vb,"","");
            var route=Guid.NewGuid().ToString("N");
            string signalId=clients[0].QueueGroupMediaSignal(group,va,2,devices[1],route,1,"offer","test SDP; opaque to server");
            Require(signalId!=null,"Media signal not queued on existing event channel.");
            Wait(()=>clients[0].GroupMediaResult(signalId)=="forwarded","Server did not admit authenticated voice-room signal.");
            GroupMediaSignal signal=null;Wait(()=>{signal=clients[1].TakeGroupMediaSignal();return signal!=null;},"Target event channel did not receive signal.");
            Require(signal.SourceAccountId==1&&signal.SourceDevice==devices[0]&&signal.TargetAccountId==2&&signal.RouteId==route&&signal.Data=="test SDP; opaque to server","Server media signaling changed identity/payload.");
            Require(clients[0].QueueGroupMediaSignal(group,va,3,devices[2],route,1,"offer","test")==null,"Cross-room media queued.");
            op(0,"create","","screen_share","S1");string s1=Room(clients[0].GroupRooms(group),"S1").Id;
            op(2,"create","","screen_share","S2");string s2=Room(clients[0].GroupRooms(group),"S2").Id;
            op(0,"start_share",s1,"","");op(2,"start_share",s2,"","");op(1,"join_viewer",s1,"","");op(0,"join_viewer",s2,"","");
            string screenSignalId=clients[0].QueueGroupMediaSignal(group,s1,2,devices[1],Guid.NewGuid().ToString("N"),1,"offer","screen signaling over existing channel");
            Require(screenSignalId!=null,"Screen signal not queued");Wait(()=>clients[0].GroupMediaResult(screenSignalId)=="forwarded","Screen signal not forwarded");
            Require(clients[1].TakeGroupMediaSignal(group,va)==null,"Voice consumer stole screen signal");
            GroupMediaSignal screenSignal=null;Wait(()=>{screenSignal=clients[1].TakeGroupMediaSignal(group,s1);return screenSignal!=null;},"Screen consumer did not receive authenticated signal");
            Require(screenSignal.SourceAccountId==1&&screenSignal.SourceDevice==devices[0],"Screen sender identity not server-bound");
            Require(Room(clients[1].GroupRooms(group),"A").Members.Count==2&&Room(clients[1].GroupRooms(group),"B").Members.Count==1,"Voice convergence failed.");
            clients[2].StopEventChannel();
            Wait(()=>Room(clients[0].GroupRooms(group),"B").Members.Count==0&&Room(clients[0].GroupRooms(group),"S2").Sharer==null,"Disconnect did not clear participant/sharer/viewers.");
            clients[2].StartEventChannel(devices[2],e=>{});
            try{Wait(()=>clients[2].EventChannelConnected&&clients[2].GroupRooms(group)!=null,"Reconnect did not get authoritative snapshot.");}catch(Exception error){throw new Exception(error.Message+" connected="+clients[2].EventChannelConnected+" result="+clients[2].GroupRoomResult(group),error);}
            Require(Room(clients[2].GroupRooms(group),"B").Members.Count==0&&Room(clients[2].GroupRooms(group),"S2").Sharer==null,"Reconnect resurrected membership.");
            op(2,"join_voice",vb,"","");op(2,"join_viewer",s1,"","");
            var roster=clients[0].GetGroupRoster(group,devices[0]);
            clients[0].RemoveGroupMember(group,roster.RosterGeneration,3,devices[2],devices[0]);
            Wait(()=>Room(clients[0].GroupRooms(group),"B").Members.Count==0&&Room(clients[0].GroupRooms(group),"S1").Members.All(d=>d.AccountId!=3),"Authenticated membership removal did not update rooms.");
            Require(clients[2].QueueGroupRoom(group,"join_voice",vb),"Removed-device denial request not queued.");
            Wait(()=>clients[2].GroupRoomResult(group)=="group_not_authorized","Removed device retained room authorization.");
            // Restore fixture for the independent deterministic state suite.
            roster=clients[0].GetGroupRoster(group,devices[0]);Require(database.GroupInvite(group,1,devices[0],3,devices[2],roster.RosterGeneration)!=null&&database.GroupRespond(group,3,devices[2],true,roster.RosterGeneration+1)!=null,"Fixture restore failed.");
            Console.WriteLine("PASS: three real authenticated TLS event channels converge across rooms, independent viewers, session loss/reconnect and server membership-removal notifications; no media or new connection.");
        }finally{
            foreach(var client in clients)client.StopEventChannel();
            typeof(DiscoveryServer).GetMethod("RequestStop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,null);
            Require(thread.Join(6000),"Room server did not stop.");if(failure!=null)throw failure;
        }
    }
    public static int Main(string[] args){try{
        Directory.CreateDirectory(args[0]);
        using(var a=new ECDsaCng(256))using(var b=new ECDsaCng(256))using(var c=new ECDsaCng(256))using(var d=new ECDsaCng(256))
        using(var db=new AccountDatabase(Path.Combine(args[0],"rooms.db"))){
            TestOwner=a;var devices=new[]{Identity(a),Identity(b),Identity(c),Identity(d)};
            for(int i=0;i<4;i++)Require(db.CreateAccountWithDevice("room-user-"+i,new byte[16],new byte[32],devices[i])==i+1,"Accounts fixture failed.");
            var proof=GroupRosterProof.Parse(Convert.ToBase64String(GroupRosterProofTest.CreateProof(a,new string('a',32),1)));string group=proof.ServerGroupId;
            Require(db.RegisterGroup(1,devices[0],proof)!=null,"Register failed.");
            Require(db.GroupInvite(group,1,devices[0],2,devices[1],1)!=null&&db.GroupRespond(group,2,devices[1],true,2)!=null,"Join B failed.");
            Require(db.GroupInvite(group,1,devices[0],3,devices[2],3)!=null&&db.GroupRespond(group,3,devices[2],true,4)!=null,"Join C failed.");
            Network(args[0],args[1],args[2],devices,group,db);ScreenAuthorization(db,group,devices);MediaAuthorization(db,group,devices);StateTests(db,group,devices);
        }
        return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
