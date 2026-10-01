using System;
using System.Linq;
using PeerChatDesktop;

internal static class GroupTextUiTest {
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
    public static int Main(){try{
        var model=new ChatViewModel{OwnId="owner",Groups=true};
        var first=new GroupData{id="group-one",name="One",members=new[]{new GroupMemberData{account=1,peer="owner",name="Owner"},new GroupMemberData{account=2,peer="member",name="Member"}}};
        var second=new GroupData{id="group-two",name="Two",members=first.members};
        model.Apply(new CoreEvent{kind=9,groups=new[]{first,second}});
        var sent=new GroupEventData{id="sent",account=1,sender="owner",time=1000,logical=1,sequence=4,text="synthetic",status="Awaiting server acceptance (online recipients only)"};
        var roster=new ServerGroupRoster{GroupId="server-one",LegacyGroupId=first.id,RosterGeneration=1};
        roster.Members.Add(new ServerGroupMember{AccountId=1,PeerId="owner",State="member",Username="Owner"});
        roster.Members.Add(new ServerGroupMember{AccountId=8,PeerId="member",State="member",Username="JonahTest2"});
        roster.Members.Add(new ServerGroupMember{AccountId=9,PeerId="other",State="member",Username="Other"});
        model.SetGroupSenderRoster(roster);
        // Ledger labels are not authoritative, even when they imitate another account.
        first.members[1].name="Other";
        var incoming=new GroupEventData{id="received",account=8,sender="member",time=2000,logical=2,sequence=5,text="synthetic"};
        model.Apply(new CoreEvent{kind=10,peer=first.id,groupEvents=new[]{sent,incoming}});
        var group=model.GroupChats[first.id];Require(group.Messages.Count==2&&model.GroupChats[second.id].Messages.Count==0,"Group messages mixed across targets.");
        Require(group.Messages[0].Text.StartsWith("Owner\n")&&group.Messages[1].Text.StartsWith("JonahTest2\n"),"Authoritative sender identity missing.");
        Require(group.Messages[1].GroupSenderAccountId==8&&group.Messages[1].Data.sender=="member"&&group.Messages[1].Data.text=="synthetic","Display projection changed authenticated attribution or body.");
        Require(!group.Messages[0].Stamp.Contains("Delivered")&&group.Messages[0].Stamp.Contains("Awaiting"),"Local echo incorrectly claimed E2E delivery.");
        Require(group.Messages[1].ShortTime.Length>0&&group.Unread==1,"Incoming timestamp/unread projection failed.");
        model.Apply(new CoreEvent{kind=10,peer=first.id,groupEvents=new[]{incoming}});Require(group.Messages.Count==2&&group.Unread==1,"Repeated event displayed/notified twice.");
        model.Apply(new CoreEvent{kind=12,peer=first.id,detail="sent",text="Server accepted (not E2E delivery)"});Require(group.Messages[0].Stamp.Contains("not E2E"),"Server receipt presentation conflated with E2E ACK.");
        var reloaded=new ChatViewModel{OwnId="owner",Groups=true};reloaded.Apply(new CoreEvent{kind=9,groups=new[]{first}});reloaded.Apply(new CoreEvent{kind=11,peer=first.id,groupEvents=new[]{sent,incoming}});
        Require(reloaded.GroupChats[first.id].Messages.Count==2&&reloaded.GroupChats[first.id].Unread==0&&reloaded.GroupChats[first.id].Messages[0].Data.sequence==4,"History reload/cursor or replay notification failed.");
        Require(reloaded.GroupChats[first.id].Messages[1].Text.StartsWith("Account 8\n"),"Unresolved history sender did not use explicit fallback.");
        reloaded.SetGroupSenderRoster(roster);
        Require(reloaded.GroupChats[first.id].Messages[1].Text.StartsWith("JonahTest2\n"),"History label did not refresh when authenticated roster arrived.");
        var other=new GroupEventData{id="other",account=9,sender="other",logical=3,text="JonahTest2"};
        var wrongDevice=new GroupEventData{id="wrong-device",account=8,sender="other",logical=4,text="Other"};
        model.Apply(new CoreEvent{kind=10,peer=first.id,groupEvents=new[]{other,wrongDevice}});
        Require(group.Messages[2].Text.StartsWith("Other\nJonahTest2")&&group.Messages[3].Text.StartsWith("Account 8\n"),"Message/ledger metadata or wrong device spoofed authoritative sender.");
        Require(group.Messages[2].GroupSenderAccountId==9&&group.Messages[3].GroupSenderAccountId==8,"Different account attribution was merged.");
        roster.Members[1].Username="";model.SetGroupSenderRoster(roster);
        Require(group.Messages[1].Text.StartsWith("Account 8\n"),"Missing username did not use fallback.");
        roster.Members[1].Username="Owner";model.SetGroupSenderRoster(roster);
        Require(group.Messages[0].GroupSenderAccountId==1&&group.Messages[1].GroupSenderAccountId==8&&group.Messages[1].Text.StartsWith("Owner\n"),"Duplicate usernames changed account attribution.");
        model.ClearGroupSenderNames();
        Require(group.Messages[0].Text.StartsWith("Account 1\n")&&group.Messages[1].Text.StartsWith("Account 8\n"),"Prior session roster names survived reset.");
        Console.WriteLine("PASS: authoritative account/device sender names, spoof resistance, fallback, local echo, live/history/retry projection and unchanged account identity.");return 0;
    }catch(Exception e){Console.Error.WriteLine("FAIL: "+e.Message);return 1;}}
}
