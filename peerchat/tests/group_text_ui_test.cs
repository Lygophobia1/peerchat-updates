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
        var incoming=new GroupEventData{id="received",account=2,sender="member",time=2000,logical=2,sequence=5,text="synthetic"};
        model.Apply(new CoreEvent{kind=10,peer=first.id,groupEvents=new[]{sent,incoming}});
        var group=model.GroupChats[first.id];Require(group.Messages.Count==2&&model.GroupChats[second.id].Messages.Count==0,"Group messages mixed across targets.");
        Require(group.Messages[0].Text.StartsWith("Owner\n")&&group.Messages[1].Text.StartsWith("Member\n"),"Sender identity missing.");
        Require(!group.Messages[0].Stamp.Contains("Delivered")&&group.Messages[0].Stamp.Contains("Awaiting"),"Local echo incorrectly claimed E2E delivery.");
        Require(group.Messages[1].ShortTime.Length>0&&group.Unread==1,"Incoming timestamp/unread projection failed.");
        model.Apply(new CoreEvent{kind=10,peer=first.id,groupEvents=new[]{incoming}});Require(group.Messages.Count==2&&group.Unread==1,"Repeated event displayed/notified twice.");
        model.Apply(new CoreEvent{kind=12,peer=first.id,detail="sent",text="Server accepted (not E2E delivery)"});Require(group.Messages[0].Stamp.Contains("not E2E"),"Server receipt presentation conflated with E2E ACK.");
        var reloaded=new ChatViewModel{OwnId="owner",Groups=true};reloaded.Apply(new CoreEvent{kind=9,groups=new[]{first}});reloaded.Apply(new CoreEvent{kind=11,peer=first.id,groupEvents=new[]{sent,incoming}});
        Require(reloaded.GroupChats[first.id].Messages.Count==2&&reloaded.GroupChats[first.id].Unread==0&&reloaded.GroupChats[first.id].Messages[0].Data.sequence==4,"History reload/cursor or replay notification failed.");
        Console.WriteLine("PASS: group sender/timestamp/unread/history projection, logical-group separation, idempotent UI display and honest delivery labels.");return 0;
    }catch(Exception e){Console.Error.WriteLine("FAIL: "+e.Message);return 1;}}
}
