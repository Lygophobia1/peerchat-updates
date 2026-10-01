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
            Require(new PendingGroupRegistrationStore(path).AllIds().Length==0,"Successful registration left the new group marked pending.");
            Console.WriteLine("PASS: new group registration remains distinct from legacy until an owner-only server roster is ready, and only its authorized owner can invite.");return 0;
        }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.Message);return 1;}
        finally{if(Directory.Exists(path))Directory.Delete(path,true);}
    }
}
