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
        chat.Apply(new CoreEvent{kind=9,groups=new[]{new GroupData{id="group",name="Group",members=new GroupMemberData[0],invites=new GroupMemberData[0]}}});
        chat.Apply(new CoreEvent{kind=10,peer="group",groupEvents=new[]{new GroupEventData{id="z",sender="peer",time=300,logical=1,text="Newest"}}});
        chat.Apply(new CoreEvent{kind=11,peer="group",groupEvents=new[]{new GroupEventData{id="o",sender="peer",time=100,logical=99,text="Old"}}});
        Check(chat.GroupChats["group"].Messages.First().Data.id=="o"&&chat.GroupChats["group"].Preview.EndsWith("\nNewest",StringComparison.Ordinal),"Group preview ignored chronological order");
        Console.WriteLine("PASS: Windows chronology, stable equal-time ties, late/history/live/restart/device merge previews, preserved timestamp/logical/cursor and group projection");return 0;
    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
