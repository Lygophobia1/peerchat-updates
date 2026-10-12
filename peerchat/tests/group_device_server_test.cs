using System;
using System.IO;
using System.Security.Cryptography;
using PeerChat.Server;
internal static class GroupDeviceServerTest {
    static void Require(bool value,string reason){if(!value)throw new Exception(reason);}
    static GroupDeviceProof Read(string root,string name){return GroupDeviceProof.Parse(Convert.ToBase64String(File.ReadAllBytes(Path.Combine(root,name))));}
    static string Peer(byte[] key){using(var sha=SHA256.Create())return GroupRosterProof.Hex(sha.ComputeHash(key));}
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
