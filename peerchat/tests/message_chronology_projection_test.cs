using System;
using System.Linq;
using PeerChatDesktop;

class MessageChronologyProjectionTest {
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static MessageData Message(string id,string sender,long time,long logical,long sequence){return new MessageData{id=id,sender=sender,time=time,logical=logical,sequence=sequence,text="Message "+id,delivery=0};}
    static string Order(ConversationModel c){return String.Join(",",c.Messages.Where(m=>m.Attachment==null).Select(m=>m.Data.id));}
    [STAThread]static int Main(){try{
        var chat=new ChatViewModel();chat.Apply(new CoreEvent{kind=0,peer="local"});
        var newest=Message("z","peer",300,1,1);var old=Message("o","peer",100,100,5);
        var a=Message("a","peer",200,90,3);var b=Message("b","peer",200,2,2);
        chat.Apply(new CoreEvent{kind=5,peer="peer",messages=new[]{newest}});
        chat.Apply(new CoreEvent{kind=6,peer="peer",messages=new[]{b,old,a}});
        var c=chat.Get("peer");Check(Order(c)=="o,a,b,z","History/live merge is arrival or logical ordered");
        Check(c.Preview==newest.text,"Late old history replaced preview");
        chat.Apply(new CoreEvent{kind=5,peer="peer",messages=new[]{old,a,b,newest}});
        Check(Order(c)=="o,a,b,z"&&c.Messages.Count==4,"Live/history overlap duplicated messages");
        chat.Apply(new CoreEvent{kind=7,messages=new[]{new MessageData{id="z"}}});
        Check(newest.time==300&&newest.logical==1&&newest.sequence==1,"Delivery rewrote original fields");
        var restored=new ChatViewModel();restored.Apply(new CoreEvent{kind=6,peer="peer",messages=new[]{a,newest,b,old}});
        Check(Order(restored.Get("peer"))==Order(c),"Restart/history order differs from live projection");
        bool reused;chat.BindFriend(2,"Peer","peer",new[]{"peer"},out reused);
        chat.Apply(new CoreEvent{kind=6,peer="other",messages=new[]{Message("earlier","other",50,200,8)}});
        var merged=chat.BindFriend(2,"Peer","other",new[]{"peer","other"},out reused);
        Check(Order(merged)=="earlier,o,a,b,z"&&merged.Preview==newest.text,"Account/device merge changed chronological order or preview");
        // A page request targets one device. The oldest visible row from a
        // different device must not become that device's history cursor.
        chat.BindFriend(3,"Paged friend","device-a",new[]{"device-a","device-b"},out reused);
        var pageA=Enumerable.Range(101,100).Select(n=>Message("a"+n,"device-a",n,n,n)).ToArray();
        chat.Apply(new CoreEvent{kind=6,peer="device-a",messages=pageA});
        chat.Apply(new CoreEvent{kind=6,peer="device-b",messages=new[]{Message("b50","device-b",50,50,50)}});
        var paged=chat.Get("device-a");
        Check(paged.HistoryCursor("device-a")==101,"Another device's earlier row skipped device A's next page");
        Check(paged.HistoryCursor("device-b")==50,"Device B lost its independent page cursor");
        chat.Apply(new CoreEvent{kind=5,peer="device-a",messages=new[]{Message("late-live","device-a",1,201,201)}});
        Check(paged.HistoryCursor("device-a")==101,"Late live arrival changed the requested history page");
        chat.Apply(new CoreEvent{kind=6,peer="device-a",messages=Enumerable.Range(1,100).Select(n=>Message("older"+n,"device-a",n,n,300+n)).ToArray()});
        Check(paged.HistoryCursor("device-a")==301,"Device A's second page did not advance its own cursor");
        chat.Apply(new CoreEvent{kind=6,peer="device-a",text="history_cleared",messages=new MessageData[0]});
        Check(paged.HistoryCursor("device-a")==long.MaxValue,"History deletion retained a stale page cursor");
        chat.Apply(new CoreEvent{kind=9,groups=new[]{new GroupData{id="group",name="Group",members=new GroupMemberData[0],invites=new GroupMemberData[0]}}});
        chat.Apply(new CoreEvent{kind=10,peer="group",groupEvents=new[]{new GroupEventData{id="z",sender="peer",time=300,logical=1,text="Newest"}}});
        chat.Apply(new CoreEvent{kind=11,peer="group",groupEvents=new[]{new GroupEventData{id="o",sender="peer",time=100,logical=99,text="Old"}}});
        Check(chat.GroupChats["group"].Messages.First().Data.id=="o"&&chat.GroupChats["group"].Preview.EndsWith("\nNewest",StringComparison.Ordinal),"Group preview ignored chronological order");
        Console.WriteLine("PASS: Windows chronology, stable equal-time ties, independent device-page cursors, late/history/live/restart/device merge previews, preserved timestamp/logical/cursor and group projection");return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
