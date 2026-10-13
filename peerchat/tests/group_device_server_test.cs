using System;
using System.IO;
using System.Security.Cryptography;
using PeerChat.Server;
internal static class GroupDeviceServerTest {
    static void Require(bool value,string reason){if(!value)throw new Exception(reason);}
    static GroupDeviceProof Read(string root,string name){return GroupDeviceProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,name))));}
    static string Peer(byte[] key){using(var sha=SHA256.Create())return GroupRosterProof.Hex(sha.ComputeHash(key));}
    // Exercise the actual ordinary-RPC dispatcher against an isolated DB,
    // without opening listeners or injecting any production test hook.
    static System.Collections.Generic.Dictionary<string,object> RecoveryRpc(AccountDatabase db,string token,string device,string group,string cursor){
        var type=typeof(DiscoveryServer);
        var server=System.Runtime.Serialization.FormatterServices.GetUninitializedObject(type);
        type.GetField("accounts",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).SetValue(server,db);
        var request=new System.Collections.Generic.Dictionary<string,object>{{"version",1},{"type","group_device_recovery"},{"sessionToken",token},{"publicIdentity",device},{"groupId",group},{"afterGeneration",cursor}};
        return (System.Collections.Generic.Dictionary<string,object>)type.GetMethod("Handle",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic).Invoke(server,new object[]{request,"",0});
    }
    static void Accounts(AccountDatabase db,GroupRosterProof creation,string member,string sibling){
        Require(db.CreateAccountWithDevice("owner",new byte[16],new byte[32],creation.OwnerIdentity)==1,"Owner registration");
        Require(db.CreateAccountWithDevice("member",new byte[16],new byte[32],member)==2,"Member registration");
        Require(db.AddDevice(2,sibling),"Sibling registration");Require(db.RegisterGroup(1,creation.OwnerIdentity,creation)!=null,"Group registration");
    }
    public static int Main(string[] args){try{
        string root=args[0];var creation=GroupRosterProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,"create.bin"))));
        GroupDeviceProof approve=Read(root,"approve.bin"),revoke=Read(root,"revoke.bin"),reenroll=Read(root,"reenroll.bin");
        string member=Peer(File.ReadAllBytes(Path.Combine(root,"member-public.bin"))),sibling=Peer(File.ReadAllBytes(Path.Combine(root,"sibling-public.bin")));
        var identity=GroupDeviceIdentityProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,"identity.bin"))));
        Require(identity.Account==1&&identity.Device==approve.Target,"Native identity proof mapping changed");
        var wrongIdentity=GroupDeviceIdentityProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,"identity-wrong-account.bin"))));
        foreach(string corruption in new[]{"signature","truncated","trailing","purpose"}){
            byte[] wire=File.ReadAllBytes(Path.Combine(root,corruption=="purpose"?"approve.bin":"identity.bin"));
            if(corruption=="signature")wire[wire.Length-1]^=1;
            else if(corruption=="truncated")Array.Resize(ref wire,wire.Length-1);
            else if(corruption=="trailing")Array.Resize(ref wire,wire.Length+1);
            bool rejected=false;try{GroupDeviceIdentityProof.Parse(Convert.ToBase64String(wire));}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Invalid device possession evidence accepted: "+corruption);
        }
        string sessionPath=Path.Combine(root,"session-admission.sqlite"),oldEpoch="";
        using(var db=new AccountDatabase(sessionPath)){
            oldEpoch=db.Live.Epoch;
            Accounts(db,creation,member,sibling);Require(db.AddDevice(1,approve.Target),"Session target registration");
            string actorToken=new string('a',64),targetToken=new string('b',64),replacementToken=new string('c',64),foreignToken=new string('d',64);
            long expires=DateTimeOffset.UtcNow.AddMinutes(10).ToUnixTimeSeconds();
            db.CreateSession(actorToken,1,expires);db.CreateSession(targetToken,1,expires);db.CreateSession(replacementToken,1,expires);db.CreateSession(foreignToken,2,expires);
            Require(db.ApplyGroupDeviceProofForSession(actorToken,approve.Actor,db.Live.Epoch,1,approve)==null,"Account session without live device lease approved enrollment");
            var actorLease=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","actor-old",true,actorToken);
            var targetLease=db.TouchPresence(1,approve.Target,"",0,"",0,"test","test","target",true,targetToken);
            Require(!db.PublishGroupDeviceIdentityForSession(actorToken,approve.Target,db.Live.Epoch,targetLease.Generation,identity),"Other device session published target possession");
            Require(db.PublishGroupDeviceIdentityForSession(targetToken,approve.Target,db.Live.Epoch,targetLease.Generation,identity),"Live target could not publish possession");
            Require(db.ReadGroupDeviceIdentityForSession(actorToken,approve.Actor,db.Live.Epoch,actorLease.Generation,approve.Target)==identity.Encoded,"Live approver could not read target evidence");
            Require(db.ReadGroupDeviceIdentityForSession(foreignToken,approve.Actor,db.Live.Epoch,actorLease.Generation,approve.Target)==null,"Foreign session read same-account evidence");
            Require(db.ApplyGroupDeviceProofForSession(actorToken,approve.Actor,db.Live.Epoch,actorLease.Generation+1,approve)==null,"Invented lease generation approved device");
            Require(db.ApplyGroupDeviceProofForSession(actorToken,approve.Actor,new string('0',32),actorLease.Generation,approve)==null,"Wrong server epoch approved device");
            Require(db.ApplyGroupDeviceProofForSession(foreignToken,approve.Actor,db.Live.Epoch,actorLease.Generation,approve)==null,"Foreign account session approved device");
            string boundToken=new string('e',64),credentialHash=new string('f',64);
            db.RememberDevice(1,approve.Target,credentialHash);db.CreateDeviceSession(boundToken,1,approve.Target,expires,credentialHash);
            var boundLease=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","wrong-bound-device",true,boundToken);
            Require(db.ApplyGroupDeviceProofForSession(boundToken,approve.Actor,db.Live.Epoch,boundLease.Generation,approve)==null,"Remembered target session substituted approver device");
            actorLease=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","actor-restored",true,actorToken);
            var fresh=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","actor-new",true,replacementToken);
            Require(fresh.Generation!=actorLease.Generation&&db.ApplyGroupDeviceProofForSession(actorToken,approve.Actor,db.Live.Epoch,actorLease.Generation,approve)==null,"Superseded lease approved device");
            Require(db.ApplyGroupDeviceProofForSession(replacementToken,approve.Actor,db.Live.Epoch,actorLease.Generation,approve)==null,"Fresh session reused old generation");
            Require(db.ApplyGroupDeviceProofForSession(replacementToken,approve.Actor,db.Live.Epoch,fresh.Generation,approve)!=null,"Exact live session failed signed approval");
            db.RevokeSession(replacementToken);
            Require(db.ApplyGroupDeviceProofForSession(replacementToken,approve.Actor,db.Live.Epoch,fresh.Generation,revoke)==null,"Revoked authenticated session retained group authority");
            db.CreateSession(replacementToken,1,expires);db.ClearPresence(1,approve.Actor,"actor-new");
            Require(db.ApplyGroupDeviceProofForSession(replacementToken,approve.Actor,db.Live.Epoch,fresh.Generation,revoke)==null,"Disconnected device retained captured authority");
            fresh=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","actor-final",true,replacementToken);
            Require(db.ApplyGroupDeviceProofForSession(replacementToken,approve.Actor,db.Live.Epoch,fresh.Generation,revoke)!=null,"Fresh reconnect could not revoke enrolled device");
            db.RevokeSession(actorToken);db.CreateSession(actorToken,1,DateTimeOffset.UtcNow.AddMinutes(-1).ToUnixTimeSeconds());
            fresh=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","actor-expired",true,actorToken);
            Require(db.ApplyGroupDeviceProofForSession(actorToken,approve.Actor,db.Live.Epoch,fresh.Generation,reenroll)==null,"Expired session approved device");
            Require(Convert.ToInt64(db.GroupRoster(approve.GroupId,1,approve.Actor)["rosterGeneration"])==3,"Rejected session attempts mutated roster");
            string recoveryToken=new string('2',64);db.CreateSession(recoveryToken,1,expires);
            var recoveryLease=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","recovery",true,recoveryToken);
            var recovery=db.ReadGroupDeviceRecoveryForSession(recoveryToken,approve.Actor,db.Live.Epoch,recoveryLease.Generation,approve.GroupId,1);
            var currentRecovery=db.ReadGroupDeviceRecoveryForCurrentSession(recoveryToken,approve.Actor,approve.GroupId,1);
            Require(currentRecovery!=null&&Convert.ToInt64(currentRecovery["nextGeneration"])==3,"Current authenticated recovery boundary did not return durable epochs");
            Require(db.ReadGroupDeviceRecoveryForCurrentSession(replacementToken,approve.Actor,approve.GroupId,1)==null,"Obsolete session inherited fresh recovery lease");
            Require(db.ReadGroupDeviceRecoveryForCurrentSession(foreignToken,approve.Actor,approve.GroupId,1)==null,"Foreign account obtained current recovery evidence");
            Require(db.ReadGroupDeviceRecoveryForCurrentSession(targetToken,approve.Target,approve.GroupId,1)==null,"Revoked group device obtained recovery evidence");
            Require(db.ReadGroupDeviceRecoveryForCurrentSession(recoveryToken,approve.Actor,approve.GroupId,0)==null,"Zero recovery cursor was accepted");
            Require(db.ReadGroupDeviceRecoveryForCurrentSession(recoveryToken,approve.Actor,approve.GroupId,4)==null,"Future recovery cursor was accepted");
            Require(db.ReadGroupDeviceRecoveryForCurrentSession(recoveryToken,null,approve.GroupId,1)==null,"Missing device was not contained");
            Require(db.ReadGroupDeviceRecoveryForCurrentSession(null,approve.Actor,approve.GroupId,1)==null,"Missing token hash was not contained");
            string rpcToken="isolated-group-recovery-fixture",rpcHash=Peer(System.Text.Encoding.UTF8.GetBytes(rpcToken));db.CreateSession(rpcHash,1,expires);
            Require(Convert.ToString(RecoveryRpc(db,rpcToken,approve.Actor,approve.GroupId,"1")["error"])=="group_not_authorized","RPC inherited another authenticated session's lease");
            db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","recovery-rpc",true,rpcHash);
            var rpc=RecoveryRpc(db,rpcToken,approve.Actor,approve.GroupId,"1");
            Require(Convert.ToBoolean(rpc["ok"])&&Convert.ToBoolean(rpc["complete"])&&Convert.ToInt64(rpc["nextGeneration"])==3,"Production RPC did not return verified durable evidence");
            var page=PeerChatDesktop.GroupRecoveryEvidenceProjection.Read(rpc,approve.GroupId,1);
            Require(page.Steps.Count==2&&page.Steps[0].Before.RosterGeneration==1&&page.Steps[1].After.RosterGeneration==3,"Managed transport altered the real server recovery epochs");
            var serializer=new System.Web.Script.Serialization.JavaScriptSerializer();
            foreach(string tamper in new[]{"group","cursor","fraction","complete","proof","omitted","duplicate","mapping"}){
                var bad=serializer.Deserialize<System.Collections.Generic.Dictionary<string,object>>(serializer.Serialize(rpc));
                var transitions=System.Linq.Enumerable.ToArray(System.Linq.Enumerable.Cast<object>((System.Collections.IEnumerable)bad["transitions"]));
                if(tamper=="group")bad["groupId"]=new string('0',32);
                else if(tamper=="cursor")bad["nextGeneration"]=2;
                else if(tamper=="fraction")bad["currentGeneration"]=3.5;
                else if(tamper=="complete")bad["complete"]=false;
                else if(tamper=="proof")((System.Collections.Generic.Dictionary<string,object>)transitions[0])["proof"]="invalid!";
                else if(tamper=="omitted")bad["transitions"]=new object[]{transitions[1]};
                else if(tamper=="duplicate")bad["transitions"]=new object[]{transitions[0],transitions[0]};
                else {var before=(System.Collections.Generic.Dictionary<string,object>)((System.Collections.Generic.Dictionary<string,object>)transitions[0])["before"];before["groupId"]=new string('0',32);}
                bool rejected=false;try{PeerChatDesktop.GroupRecoveryEvidenceProjection.Read(bad,approve.GroupId,1);}catch(InvalidDataException){rejected=true;}
                Require(rejected,"Malformed transport page accepted: "+tamper);
            }
            foreach(string malformed in new[]{"0","-1","1.5","9223372036854775808"})
                Require(Convert.ToString(RecoveryRpc(db,rpcToken,approve.Actor,approve.GroupId,malformed)["error"])=="invalid_group_recovery_request","Malformed RPC recovery cursor accepted");
            Require(Convert.ToString(RecoveryRpc(db,rpcToken,approve.Target,approve.GroupId,"1")["error"])=="group_not_authorized","RPC substituted a different/revoked device");
            db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","recovery-rpc-superseded",true,recoveryToken);
            Require(Convert.ToString(RecoveryRpc(db,rpcToken,approve.Actor,approve.GroupId,"1")["error"])=="group_not_authorized","Superseded RPC session retained recovery authority");
            db.RevokeSession(rpcHash);Require(Convert.ToString(RecoveryRpc(db,rpcToken,approve.Actor,approve.GroupId,"1")["error"])=="session_invalid","Revoked RPC session retained recovery authority");
            Require(recovery!=null&&Convert.ToBoolean(recovery["complete"])&&Convert.ToInt64(recovery["nextGeneration"])==3,"Authenticated recovery missed consecutive persisted epochs");
            var rows=(System.Collections.Generic.List<System.Collections.Generic.Dictionary<string,object>>)recovery["transitions"];
            Require(rows.Count==2&&Convert.ToString(rows[0]["proof"])==approve.Encoded&&Convert.ToString(rows[1]["proof"])==revoke.Encoded,"Recovery altered proof provenance or ordering");
            Require(db.ReadGroupDeviceRecoveryForSession(targetToken,approve.Target,db.Live.Epoch,targetLease.Generation,approve.GroupId,1)==null,"Revoked device fetched group recovery evidence");
            Require(db.ReadGroupDeviceRecoveryForSession(foreignToken,approve.Actor,db.Live.Epoch,recoveryLease.Generation,approve.GroupId,1)==null,"Foreign session fetched recovery evidence");
            Require(db.ReadGroupDeviceRecoveryForSession(recoveryToken,approve.Actor,db.Live.Epoch,recoveryLease.Generation+1,approve.GroupId,1)==null,"Stale lease fetched recovery evidence");
        }
        using(var db=new AccountDatabase(sessionPath)){
            string token=new string('1',64);db.CreateSession(token,1,DateTimeOffset.UtcNow.AddMinutes(10).ToUnixTimeSeconds());
            var fresh=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","after-server-restart",true,token);
            Require(db.Live.Epoch!=oldEpoch&&db.ApplyGroupDeviceProofForSession(token,approve.Actor,oldEpoch,fresh.Generation,reenroll)==null,"Server restart reused old issuing context");
            Require(db.ApplyGroupDeviceProofForSession(token,approve.Actor,db.Live.Epoch,fresh.Generation,reenroll)!=null,"Fresh server epoch could not explicitly reenroll");
            var recovery=db.ReadGroupDeviceRecoveryForSession(token,approve.Actor,db.Live.Epoch,fresh.Generation,approve.GroupId,1);
            int restoredRows=((System.Collections.Generic.List<System.Collections.Generic.Dictionary<string,object>>)recovery["transitions"]).Count;
            if(!Convert.ToBoolean(recovery["complete"])){
                Require(Convert.ToString(recovery["reason"])=="page_limit","Server restart recovery stopped unexpectedly");
                recovery=db.ReadGroupDeviceRecoveryForSession(token,approve.Actor,db.Live.Epoch,fresh.Generation,approve.GroupId,Convert.ToInt64(recovery["nextGeneration"]));
                restoredRows+=((System.Collections.Generic.List<System.Collections.Generic.Dictionary<string,object>>)recovery["transitions"]).Count;
            }
            Require(restoredRows==3&&Convert.ToBoolean(recovery["complete"])&&Convert.ToInt64(recovery["nextGeneration"])==4,"Server restart lost durable recovery epochs");
            db.Execute("DELETE FROM group_device_epochs WHERE proof_id='"+revoke.Id+"';");
            recovery=db.ReadGroupDeviceRecoveryForSession(token,approve.Actor,db.Live.Epoch,fresh.Generation,approve.GroupId,1);
            Require(!Convert.ToBoolean(recovery["complete"])&&Convert.ToString(recovery["reason"])=="epoch_evidence_unavailable"&&Convert.ToInt64(recovery["nextGeneration"])==2,"Missing historical epoch was fabricated or skipped");
        }
        string identityPath=Path.Combine(root,"device-identity.sqlite");
        using(var db=new AccountDatabase(identityPath)){
            Accounts(db,creation,member,sibling);
            Require(!db.PublishGroupDeviceIdentity(1,identity.Device,identity),"Unregistered target published identity proof");
            Require(db.AddDevice(1,identity.Device),"Identity target registration failed");
            Require(!db.PublishGroupDeviceIdentity(2,member,identity)&&!db.PublishGroupDeviceIdentity(1,creation.OwnerIdentity,identity),"Account session substituted signing device");
            Require(!db.PublishGroupDeviceIdentity(1,identity.Device,wrongIdentity),"Valid signature for wrong account accepted");
            Require(db.PublishGroupDeviceIdentity(1,identity.Device,identity)&&db.PublishGroupDeviceIdentity(1,identity.Device,identity),"Identity publication/idempotent retry failed");
            Require(db.ReadGroupDeviceIdentity(1,creation.OwnerIdentity,identity.Device)==identity.Encoded,"Own-account approver could not read exact target evidence");
            Require(db.ReadGroupDeviceIdentity(2,member,identity.Device)==null&&db.ReadGroupDeviceIdentity(1,member,identity.Device)==null,"Cross-account or unowned requesting device read evidence");
            Require(!db.GroupRosterMember(creation.ServerGroupId,1,identity.Device)&&Convert.ToInt64(db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity)["rosterGeneration"])==1,"Possession publication implicitly approved group membership");
        }
        using(var db=new AccountDatabase(identityPath)){
            Require(db.ReadGroupDeviceIdentity(1,creation.OwnerIdentity,identity.Device)==identity.Encoded,"Identity evidence lost across restart");
            db.Execute("UPDATE group_device_identity_proofs SET signed_proof='"+wrongIdentity.Encoded+"';");
            bool rejected=false;try{db.ReadGroupDeviceIdentity(1,creation.OwnerIdentity,identity.Device);}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Corrupt persisted account mapping did not fail closed");
            rejected=false;try{db.PublishGroupDeviceIdentity(1,identity.Device,identity);}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Publication silently replaced mismatched stored evidence");
            db.Execute("DELETE FROM devices WHERE public_identity='"+identity.Device+"';");
            Require(db.ReadGroupDeviceIdentity(1,creation.OwnerIdentity,identity.Device)==null&&!db.PublishGroupDeviceIdentity(1,identity.Device,identity),"Removed account device retained proof eligibility");
        }
        foreach(string corruption in new[]{"tampered","truncated"}){
            byte[] wire=File.ReadAllBytes(Path.Combine(root,"approve.bin"));if(corruption=="tampered")wire[wire.Length-1]^=1;else Array.Resize(ref wire,wire.Length-1);
            bool rejected=false;try{GroupDeviceProof.Parse(Convert.ToBase64String(wire));}catch(InvalidOperationException){rejected=true;}Require(rejected,"Corrupt proof accepted");
        }
        foreach(string invalid in new[]{"bad-target-signature.bin","target-wrong-account.bin","bad-inner-signature.bin"}){
            bool rejected=false;try{Read(root,invalid);}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Valid outer signature concealed invalid identity/event: "+invalid);
        }
        string path=Path.Combine(root,"server.sqlite");using(var db=new AccountDatabase(path)){
            Accounts(db,creation,member,sibling);
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,approve)==null,"Unregistered target was admitted");
            Require(Convert.ToString(db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity)["creationProof"])==creation.Encoded,"Verified root was not retained exactly");
            Require(db.GroupRoster(creation.ServerGroupId,2,member)==null,"Unenrolled account could read group root");
            Require(db.RegisterGroup(1,creation.OwnerIdentity,creation)!=null&&Convert.ToInt64(db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity)["rosterGeneration"])==1,"Registration retry mutated generation");
            Require(db.AddDevice(1,approve.Target),"Recipient registration");
            Require(db.ApplyGroupDeviceProof(2,member,approve)==null,"Wrong account session approved device");
            Require(db.ApplyGroupDeviceProof(1,member,approve)==null,"Wrong device session approved device");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,Read(root,"wrong-account.bin"))==null,"Wrong signed account binding accepted");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,Read(root,"substituted-group.bin"))==null,"Substituted group accepted");
            Require(!db.GroupRosterMember(approve.GroupId,1,approve.Target),"Account login implicitly enrolled device");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,approve)!=null,"Signed enrollment rejected");
            Require(db.GroupRosterMember(approve.GroupId,1,approve.Target),"Approved device not provisioned in roster");
            Require(Convert.ToInt64(db.ApplyGroupDeviceProof(1,approve.Actor,approve)["rosterGeneration"])==2,"Duplicate proof changed generation");
        }
        using(var db=new AccountDatabase(Path.Combine(root,"bounded.sqlite"))){
            Accounts(db,creation,member,sibling);
            for(int i=0;i<8;i++){
                var proof=Read(root,"bounded-"+i+".bin");Require(db.AddDevice(1,proof.Target),"Bounded target registration");
                Require((db.ApplyGroupDeviceProof(1,proof.Actor,proof)!=null)==(i<7),"Eight-device limit violated");
                Require(db.GroupRosterMember(proof.GroupId,1,proof.Target)==(i<7),"Rejected target leaked into roster");
            }
        }
        using(var db=new AccountDatabase(Path.Combine(root,"window.sqlite"))){
            Accounts(db,creation,member,sibling);Require(db.AddDevice(1,approve.Target),"Window target registration");
            for(int i=0;i<40;i++){var proof=Read(root,"window-"+i+".bin");Require(db.ApplyGroupDeviceProof(1,proof.Actor,proof)!=null,"Window approval/revoke failed");}
            var roster=db.GroupRoster(approve.GroupId,1,approve.Actor);
            var window=(System.Collections.Generic.List<System.Collections.Generic.Dictionary<string,object>>)roster["deviceApprovals"];
            var json=new System.Web.Script.Serialization.JavaScriptSerializer();
            Require(Convert.ToBoolean(roster["deviceApprovalsTruncated"])&&window.Count>0&&window.Count<32,"Evidence window did not report truncation");
            Require(System.Text.Encoding.UTF8.GetByteCount(json.Serialize(window))<=8192,"Evidence exceeded reserved frame budget");
            Require(Convert.ToInt64(window[window.Count-1]["appliedGeneration"])==41,"Window lost newest proof");
            long prior=0;foreach(var row in window){long gen=Convert.ToInt64(row["appliedGeneration"]);Require(gen>prior,"Window reordered evidence");prior=gen;}
            Require(System.Text.Encoding.UTF8.GetByteCount(json.Serialize(roster))<16*1024,"Small roster exceeded legacy response bound");
            string token=new string('3',64);db.CreateSession(token,1,DateTimeOffset.UtcNow.AddMinutes(10).ToUnixTimeSeconds());
            var lease=db.TouchPresence(1,approve.Actor,"",0,"",0,"test","test","window",true,token);
            long cursor=1;int pages=0;
            while(cursor<41){var page=db.ReadGroupDeviceRecoveryForSession(token,approve.Actor,db.Live.Epoch,lease.Generation,approve.GroupId,cursor);
                long next=Convert.ToInt64(page["nextGeneration"]);Require(next>cursor&&next<=41,"Bounded recovery did not advance");
                Require(System.Text.Encoding.UTF8.GetByteCount(json.Serialize(page))<16*1024,"Recovery page exceeded frame budget");cursor=next;pages++;
            }
            Require(pages>1,"Large recovery did not paginate");
        }
        using(var db=new AccountDatabase(Path.Combine(root,"race.sqlite"))){
            Accounts(db,creation,member,sibling);var competing=Read(root,"stale.bin");
            Require(db.AddDevice(1,approve.Target)&&db.AddDevice(1,competing.Target),"Race target registration");
            bool first=false,second=false;
            var a=System.Threading.Tasks.Task.Run(()=>{first=db.ApplyGroupDeviceProof(1,approve.Actor,approve)!=null;});
            var b=System.Threading.Tasks.Task.Run(()=>{second=db.ApplyGroupDeviceProof(1,competing.Actor,competing)!=null;});
            System.Threading.Tasks.Task.WaitAll(a,b);Require(first!=second,"Concurrent epoch approvals both won or both failed");
            Require(Convert.ToInt64(db.GroupRoster(approve.GroupId,1,approve.Actor)["rosterGeneration"])==2,"Race advanced multiple epochs");
        }
        using(var db=new AccountDatabase(Path.Combine(root,"primary.sqlite"))){
            Accounts(db,creation,member,sibling);Require(db.AddDevice(1,approve.Target),"Primary replacement target");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,approve)!=null,"Primary replacement enrollment");
            var replacement=Read(root,"revoke-primary.bin");
            Require(db.ApplyGroupDeviceProof(1,replacement.Actor,replacement)!=null,"Authorized sibling could not retire original device");
            Require(!db.GroupRosterMember(approve.GroupId,1,approve.Actor)&&db.GroupRosterMember(approve.GroupId,1,approve.Target),"Primary revoke changed wrong device");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,approve)==null,"Removed approver retained proof authority");
            Require(Convert.ToString(db.GroupRoster(approve.GroupId,1,approve.Target)["creationProof"])==creation.Encoded,"Revoking original creator erased immutable root evidence");
            Require(db.GroupRoster(approve.GroupId,1,approve.Actor)==null,"Revoked creator could read current roster/root");
        }
        using(var db=new AccountDatabase(path)){
            Require(Convert.ToString(db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity)["creationProof"])==creation.Encoded,"Root evidence lost across restart");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,approve)!=null,"Exact receipt did not survive restart");
            var evidence=(System.Collections.Generic.List<System.Collections.Generic.Dictionary<string,object>>)db.GroupRoster(approve.GroupId,1,approve.Actor)["deviceApprovals"];
            Require(evidence.Count==1&&Convert.ToInt64(evidence[0]["appliedGeneration"])==2&&Convert.ToString(evidence[0]["proof"])==approve.Encoded,"Verified approval projection lost durable proof");
            var bulk=db.GroupRostersForDevice(1,approve.Actor);
            Require(bulk.Count==1&&!Convert.ToBoolean(bulk[0]["deviceApprovalsIncluded"])&&((System.Collections.IList)bulk[0]["deviceApprovals"]).Count==0,"Bulk rosters multiplied historical proof payloads");
            Require(db.AddDevice(1,sibling)==false,"Device could move accounts");
            Require(db.AddDevice(1,Read(root,"stale.bin").Target),"Stale fixture same-account registration");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,Read(root,"stale.bin"))==null,"Stale generation accepted");
            Require(db.ApplyGroupDeviceProof(1,revoke.Actor,revoke)!=null&&!db.GroupRosterMember(approve.GroupId,1,approve.Target),"Device revocation failed");
            Require(db.ApplyGroupDeviceProof(1,approve.Actor,approve)==null,"Old approval resurrected revoked device");
            Require(db.ApplyGroupDeviceProof(1,reenroll.Actor,reenroll)!=null,"Fresh explicit reenrollment failed");
            evidence=(System.Collections.Generic.List<System.Collections.Generic.Dictionary<string,object>>)db.GroupRoster(approve.GroupId,1,approve.Actor)["deviceApprovals"];
            Require(evidence.Count==3&&Convert.ToInt64(evidence[2]["appliedGeneration"])==4,"Approval/revocation projection order changed");
        }
        var memberProof=Read(root,"member.bin");using(var db=new AccountDatabase(Path.Combine(root,"ordinary.sqlite"))){
            Accounts(db,creation,member,sibling);
            Require(db.GroupInvite(creation.ServerGroupId,1,creation.OwnerIdentity,2,member,1)!=null,"Member invite");
            Require(db.GroupRespond(creation.ServerGroupId,2,member,true,2)!=null,"Member accept");
            Require(db.ApplyGroupDeviceProof(2,member,memberProof)!=null,"Ordinary member could not approve own account device");
            Require(db.GroupLeave(creation.ServerGroupId,2,member,4)!=null&&!db.GroupRosterMember(creation.ServerGroupId,2,sibling),"Leave retained sibling authorization");
            Require(db.ApplyGroupDeviceProof(2,member,memberProof)==null,"Departed issuer reused approval");
            Require(db.GroupInvite(creation.ServerGroupId,1,creation.OwnerIdentity,2,member,5)!=null&&db.GroupRespond(creation.ServerGroupId,2,member,true,6)!=null,"Member rejoin");
            Require(!db.GroupRosterMember(creation.ServerGroupId,2,sibling)&&db.ApplyGroupDeviceProof(2,member,memberProof)==null,"Rejoin restored old device authorization");
            Require(db.ApplyGroupDeviceProof(2,member,Read(root,"member-rejoin.bin"))!=null,"Fresh post-rejoin approval failed");
            Require(db.GroupRemove(creation.ServerGroupId,1,creation.OwnerIdentity,2,member,8)!=null&&!db.GroupRosterMember(creation.ServerGroupId,2,sibling),"Account removal retained sibling authorization");
        }
        string legacy=Path.Combine(root,"legacy-root.sqlite");
        using(var db=new AccountDatabase(legacy)){
            // Isolated fixture models the pre-column schema, with no production data.
            db.Execute("DROP TABLE group_device_proofs; DROP TABLE group_invites; DROP TABLE group_roster_devices; DROP TABLE group_rosters;");
            db.Execute("CREATE TABLE group_rosters(group_id TEXT PRIMARY KEY,legacy_group_id TEXT NOT NULL,owner_account_id INTEGER NOT NULL,owner_identity TEXT NOT NULL,distributor_identity TEXT NOT NULL,roster_generation INTEGER NOT NULL,creation_event_id TEXT NOT NULL DEFAULT '',created_utc INTEGER NOT NULL);");
        }
        using(var db=new AccountDatabase(legacy)){
            Accounts(db,creation,member,sibling);
            db.Execute("UPDATE group_rosters SET creation_proof='';");
            Require(Convert.ToString(db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity)["creationProof"])=="","Missing historical root was fabricated");
            Require(db.RegisterGroup(2,member,creation)==null,"Wrong account backfilled root");
            Require(db.RegisterGroup(1,creation.OwnerIdentity,creation)!=null&&Convert.ToString(db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity)["creationProof"])==creation.Encoded,"Exact verified registration retry failed to backfill");
            Require(Convert.ToInt64(db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity)["rosterGeneration"])==1,"Backfill altered roster epoch");
            db.Execute("UPDATE group_rosters SET creation_proof='AAAA';");
            bool rejected=false;try{db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity);}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Corrupt persisted root did not fail closed");
            rejected=false;try{db.RegisterGroup(1,creation.OwnerIdentity,creation);}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Registration silently overwrote corrupt root");
            var substituted=GroupRosterProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,"other-create.bin"))));
            db.Execute("UPDATE group_rosters SET creation_proof='"+substituted.Encoded+"';");
            rejected=false;try{db.GroupRoster(creation.ServerGroupId,1,creation.OwnerIdentity);}catch(InvalidOperationException){rejected=true;}
            Require(rejected,"Valid signature from another group was accepted as the stored root");
        }
        Console.WriteLine("PASS: actual native double-signature proofs; authoritative account/device/group checks; exact epochs; durable idempotency; revocation; ordinary-member approval; leave/rejoin and account removal");return 0;
    }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.GetType().Name+": "+error.Message);return 1;}}
}
