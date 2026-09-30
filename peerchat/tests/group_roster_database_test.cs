using System;
using System.IO;
using System.Security.Cryptography;
using PeerChat.Server;

internal static class GroupRosterDatabaseTest {
    static void Require(bool condition,string message){if(!condition)throw new Exception(message);}
    public static int Main(string[] args){try{
        Require(args.Length==1,"Temporary database path required.");
        using(var ownerKey=new ECDsaCng(256))using(var memberKey=new ECDsaCng(256))using(var strangerKey=new ECDsaCng(256)){
            const string legacy="abcdefabcdefabcdefabcdefabcdefab";
            byte[] creation=GroupRosterProofTest.CreateProof(ownerKey,legacy,1);
            var proof=GroupRosterProof.Parse(Convert.ToBase64String(creation));
            byte[] memberPublic=memberKey.Key.Export(CngKeyBlobFormat.EccPublicBlob),strangerPublic=strangerKey.Key.Export(CngKeyBlobFormat.EccPublicBlob);
            string memberId,strangerId;using(var sha=SHA256.Create()){memberId=BitConverter.ToString(sha.ComputeHash(memberPublic)).Replace("-","").ToLowerInvariant();strangerId=BitConverter.ToString(sha.ComputeHash(strangerPublic)).Replace("-","").ToLowerInvariant();}
            using(var database=new AccountDatabase(args[0])){
                byte[] salt=new byte[16],hash=new byte[32];
                Require(database.CreateAccountWithDevice("owner",salt,hash,proof.OwnerIdentity)==1,"Owner fixture account mismatch.");
                Require(database.CreateAccountWithDevice("member",salt,hash,memberId)==2,"Member fixture account mismatch.");
                Require(database.CreateAccountWithDevice("stranger",salt,hash,strangerId)==3,"Stranger fixture account mismatch.");
                var people=database.PeopleSearch("me",1);
                Require(people.Count==1&&Convert.ToInt64(people[0]["accountId"])==2&&Convert.ToString(people[0]["username"])=="member","Public prefix search did not find the expected account.");
                Require(people[0].Count==2&&!people[0].ContainsKey("publicIdentity")&&!people[0].ContainsKey("online"),"Public people search exposed device or presence data.");
                Require(database.PeopleSearch("2",1).Count==1,"Exact account ID search failed.");
                Require(database.PeopleSearch("m",1).Count==0,"One-letter username enumeration was permitted.");
                Require(database.SetBlocked(1,3,true)=="blocked"&&database.PeopleSearch("str",1).Count==0,"Blocked account remained discoverable.");
                var roster=database.RegisterGroup(1,proof.OwnerIdentity,proof);
                Require(roster!=null&&(long)roster["rosterGeneration"]==1,"Signed new-group registration failed.");
                Require(database.RegisterGroup(2,memberId,proof)==null,"Other account registered owner's group.");
                string group=proof.ServerGroupId;
                Require(database.GroupRostersForDevice(1,proof.OwnerIdentity).Count==1&&database.GroupRostersForDevice(2,memberId).Count==0,"New roster was not limited to the authenticated owner device.");
                Require(!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId),"Invited/unknown device obtained group-key relay access.");
                Require(database.GroupInvite(group,3,strangerId,2,memberId,1)==null,"Non-member invited a device.");
                roster=database.GroupInvite(group,1,proof.OwnerIdentity,2,memberId,1);
                Require(roster!=null&&(long)roster["rosterGeneration"]==2,"Owner invitation did not advance the roster/key epoch.");
                Require(!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId),"Invitation granted member relay access before acceptance.");
                Require(database.GroupRespond(group,2,memberId,true,1)==null,"Stale invitation acceptance succeeded.");
                roster=database.GroupRespond(group,2,memberId,true,2);
                Require(roster!=null&&(long)roster["rosterGeneration"]==3,"Member acceptance did not advance epoch.");
                Require(database.GroupMemberAccounts(group).Count==2&&database.GroupMemberAccounts(group).Contains(1)&&database.GroupMemberAccounts(group).Contains(2),"Roster-change notification targets omitted an authorized member.");
                Require(database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId),"Authenticated current members could not rendezvous for key handoff.");
                Require(database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId,3),"Current roster generation was rejected for group control.");
                Require(!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId,2),"Stale roster generation authorized group control.");
                Require(!database.GroupRelayAuthorized("ffffffffffffffffffffffffffffffff",1,proof.OwnerIdentity,2,memberId,3),"Wrong Group ID authorized group control.");
                Require(!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,3,strangerId),"Non-member obtained group-key relay access.");
                Require(!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,strangerId),"Wrong device obtained group-key relay access.");
                Require(database.GroupRemove(group,1,proof.OwnerIdentity,2,memberId,1)==null,"Stale roster mutation succeeded.");
                roster=database.GroupRemove(group,1,proof.OwnerIdentity,2,memberId,3);
                Require(roster!=null&&(long)roster["rosterGeneration"]==4,"Member removal did not advance epoch.");
                Require(database.GroupMemberAccounts(group).Count==1&&database.GroupMemberAccounts(group)[0]==1,"Removed account remained in current roster notification targets.");
                Require(!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId),"Removed member retained group-key relay authorization.");
                roster=database.GroupInvite(group,1,proof.OwnerIdentity,3,strangerId,4);
                Require(roster!=null&&(long)roster["rosterGeneration"]==5,"Second invitation did not advance epoch.");
                roster=database.GroupRespond(group,3,strangerId,false,5);
                Require(roster!=null&&Convert.ToString(roster["state"])=="declined","Invitation decline failed.");
                roster=database.GroupRoster(group,1,proof.OwnerIdentity);
                Require(roster!=null&&(long)roster["rosterGeneration"]==6,"Invitation decline did not advance epoch.");
                Require(!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,3,strangerId),"Declined device gained group relay access.");
                var invite=database.GroupInviteAccount(group,1,proof.OwnerIdentity,2,6);
                Require(invite!=null&&Convert.ToString(invite["state"])=="pending","Account-scoped non-friend invitation failed.");
                string inviteId=Convert.ToString(invite["inviteId"]);
                Require(database.GroupInviteAccount(group,1,proof.OwnerIdentity,2,6)!=null&&Convert.ToString(database.GroupInviteAccount(group,1,proof.OwnerIdentity,2,6)["inviteId"])==inviteId,"Repeated invitation did not reuse the pending request.");
                Require(database.GroupInvitesFor(2).Count==1&&database.GroupInvitesFor(2)[0].Count==5,"Invite inbox leaked device or key state.");
                Require(database.GroupRespondAccount(inviteId,3,strangerId,true)==null,"Other account accepted the invitation.");
                Require(database.GroupRespondAccount(inviteId,2,strangerId,true)==null,"Wrong device accepted the invitation.");
                Require(database.GroupRespondAccount(inviteId,2,memberId,false)!=null&&!database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId),"Decline authorized membership.");
                Require(database.GroupRespondAccount(inviteId,2,memberId,true)==null,"Declined invitation was replayed.");
                invite=database.GroupInviteAccount(group,1,proof.OwnerIdentity,2,6);string renewedId=Convert.ToString(invite["inviteId"]);
                Require(renewedId!=inviteId&&database.GroupRevokeAccountInvite(renewedId,1,proof.OwnerIdentity),"Reissued invitation could not be revoked.");
                Require(database.GroupRespondAccount(renewedId,2,memberId,true)==null,"Revoked invitation was accepted.");
                invite=database.GroupInviteAccount(group,1,proof.OwnerIdentity,2,6);
                roster=database.GroupRespondAccount(Convert.ToString(invite["inviteId"]),2,memberId,true);
                Require(roster!=null&&(long)roster["rosterGeneration"]==7&&database.GroupRelayAuthorized(group,1,proof.OwnerIdentity,2,memberId,7),"Accepted account invite did not authorize the authenticated device at a new generation.");
                Require(database.GroupRostersForDevice(2,memberId).Count==1&&database.GroupRostersForDevice(3,strangerId).Count==0,"Group-state recovery leaked roster data to a non-member.");
                Require(database.GroupMemberAccounts(group).Count==2,"Accepted account invite did not expose both members for roster-change notification.");
                Require(!database.Friends(1,2),"Group acceptance silently created friendship.");
            }
        }
        Console.WriteLine("PASS: signed new-group registration, authenticated membership mutations, device-bound relay authorization, stale-generation rejection, and removal revocation.");return 0;
    }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.Message);return 1;}}
}
