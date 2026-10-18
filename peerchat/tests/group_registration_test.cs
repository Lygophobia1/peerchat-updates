using System;
using System.IO;
using PeerChatDesktop;

internal static class GroupRegistrationTest {
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
    public static int Main(){
        string path=Path.Combine(Path.GetTempPath(),"PeerChat-group-registration-"+Guid.NewGuid().ToString("N"));
        const string group="0123456789abcdef0123456789abcdef";
        const string peer="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
        try{
            Require(DiscoveryRestoreRetryPolicy.Transient(new System.Net.Sockets.SocketException())&&DiscoveryRestoreRetryPolicy.Transient(new TimeoutException())&&DiscoveryRestoreRetryPolicy.Transient(new EndOfStreamException()),"Interrupted session restore was not retryable.");
            Require(!DiscoveryRestoreRetryPolicy.Transient(new InvalidDataException())&&!DiscoveryRestoreRetryPolicy.Transient(new System.Security.Authentication.AuthenticationException())&&!DiscoveryRestoreRetryPolicy.Transient(new DiscoveryRequestException("device_revoked"))&&!DiscoveryRestoreRetryPolicy.Transient(new DiscoveryRequestException("session_invalid")),"Authentication/protocol refusal was treated as transient.");
            Require(DiscoveryRestoreRetryPolicy.DelayMilliseconds(1)==2000&&DiscoveryRestoreRetryPolicy.DelayMilliseconds(4)==16000&&DiscoveryRestoreRetryPolicy.DelayMilliseconds(100)==30000,"Restore backoff was not bounded.");
            Require(DiscoveryRestoreRetryPolicy.SessionRejected(new DiscoveryRequestException("session_invalid"))&&DiscoveryRestoreRetryPolicy.SessionRejected(new DiscoveryRequestException("session_superseded"))&&!DiscoveryRestoreRetryPolicy.SessionRejected(new DiscoveryRequestException("device_revoked")),"Typed session refusal was not classified correctly.");
            Require(!DiscoveryRestoreRetryPolicy.ScopeCurrent(false,1,1,2,2,false,10,11),"Manual sign-in/out did not supersede an older restore attempt.");
            Require(DiscoveryRestoreRetryPolicy.ScopeCurrent(false,1,1,2,2,false)&&!DiscoveryRestoreRetryPolicy.ScopeCurrent(true,1,1,2,2,false)&&!DiscoveryRestoreRetryPolicy.ScopeCurrent(false,1,3,2,2,false)&&!DiscoveryRestoreRetryPolicy.ScopeCurrent(false,1,1,2,3,false)&&!DiscoveryRestoreRetryPolicy.ScopeCurrent(false,1,1,2,2,true),"Late restore could replace a newer login/profile.");
            var row=new System.Collections.Generic.Dictionary<string,object>{{"appliedGeneration",2L},{"proof",Convert.ToBase64String(new byte[]{1,2,3})}};
            var response=new System.Collections.Generic.Dictionary<string,object>{{"deviceApprovals",new object[]{row}}};
            var evidence=GroupRosterEvidenceProjection.Read(response,2);
            Require(evidence.Count==1&&evidence[0].AppliedGeneration==2,"Managed evidence changed the authenticated epoch");
            foreach(var invalid in new object[]{new object[]{row,row},new object[]{new System.Collections.Generic.Dictionary<string,object>{{"appliedGeneration",3L},{"proof","AQID"}}},new object[]{new System.Collections.Generic.Dictionary<string,object>{{"appliedGeneration",2L},{"proof","invalid!"}}},"not a list"}){
                bool rejected=false;try{GroupRosterEvidenceProjection.Read(new System.Collections.Generic.Dictionary<string,object>{{"deviceApprovals",invalid}},2);}catch(InvalidDataException){rejected=true;}
                Require(rejected,"Managed evidence limits/encoding/order failed closed");
            }
            Require(GroupRosterEvidenceProjection.Read(new System.Collections.Generic.Dictionary<string,object>(),2).Count==0,"Legacy roster compatibility changed");
            var store=new PendingGroupRegistrationStore(path);
            Require(store.Load(19,peer).Length==0,"A new profile contained a pending group.");
            store.Add(19,peer,group);
            Require(new PendingGroupRegistrationStore(path).Load(19,peer).Length==1,"A failed new-group registration was lost across restart.");
            Require(store.Load(20,peer).Length==0,"A different account inherited a group registration attempt.");
            Require(store.AllIds().Length==1,"A pending new group would be mislabeled as legacy after account switching.");
            var roster=new ServerGroupRoster{GroupId="fedcba9876543210fedcba9876543210",LegacyGroupId=group,RosterGeneration=1};
            roster.Members.Add(new ServerGroupMember{AccountId=19,PeerId=peer,Role="owner",State="member"});
            Require(GroupRegistrationPolicy.OwnerRosterReady(roster,group,19,peer),"Owner-only authoritative roster was not accepted.");
            Require(GroupRegistrationPolicy.CanInvite(roster,19,peer),"A registered owner was not eligible for Find People invitation.");
            Require(!GroupRegistrationPolicy.CanInvite(null,19,peer),"A legacy group was eligible for invitation.");
            Require(!GroupRegistrationPolicy.CanInvite(roster,20,peer),"A different account could invite.");
            roster.RosterGeneration=0;Require(!GroupRegistrationPolicy.OwnerRosterReady(roster,group,19,peer),"Uninitialized roster generation was accepted.");
            roster.RosterGeneration=1;roster.Members[0].Role="member";Require(!GroupRegistrationPolicy.OwnerRosterReady(roster,group,19,peer),"A non-owner creation roster was accepted.");
            roster.Members[0].Role="owner";store.Remove(19,peer,group);
            var remote=new string('b',64);
            roster.Members.Add(new ServerGroupMember{AccountId=20,PeerId=remote,State="member"});
            var demand=new DiscoveryEvent{Purpose="group_key",GroupId=roster.GroupId,RosterGeneration=1,AccountId=20,PublicIdentity=remote,TargetPublicIdentity=peer};
            Require(GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Current group demand was not admitted.");
            demand.Purpose="group_control";Require(!GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Wrong wire purpose admitted.");demand.Purpose="group_key";
            demand.RosterGeneration=2;Require(!GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Mismatched roster generation admitted.");demand.RosterGeneration=1;
            demand.AccountId=21;Require(!GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Cross-account peer substitution admitted.");demand.AccountId=20;
            demand.TargetPublicIdentity=remote;Require(!GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Another local device's demand admitted.");demand.TargetPublicIdentity=peer;
            demand.GroupId=group;Require(!GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Cross-group demand admitted.");demand.GroupId=roster.GroupId;
            roster.Members[1].State="removed";Require(!GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Removed group member admitted.");roster.Members[1].State="member";
            roster.Members[0].State="removed";Require(!GroupRegistrationPolicy.IncomingControlReady(roster,demand,19,peer),"Removed local device admitted.");
            Require(new PendingGroupRegistrationStore(path).AllIds().Length==0,"Successful registration left the new group marked pending.");
            Console.WriteLine("PASS: new group registration remains distinct from legacy until an owner-only server roster is ready, and only its authorized owner can invite.");return 0;
        }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.Message);return 1;}
        finally{if(Directory.Exists(path))Directory.Delete(path,true);}
    }
}
