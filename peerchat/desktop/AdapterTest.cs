using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using PeerChatDesktop;
class AdapterTest {
    static void Require(bool condition,string reason){if(!condition)throw new Exception(reason);}
    static void Until(Func<bool> check,Action pump,string label){var watch=Stopwatch.StartNew();while(watch.ElapsedMilliseconds<20000){pump();if(check())return;Thread.Sleep(30);}throw new Exception("Timeout: "+label);}
    static int Main(){try{
        string root=Path.Combine(Environment.CurrentDirectory,"build","tests","runtime","desktop-"+Guid.NewGuid().ToString("N"));
        var a=new CoreAdapter();var b=new CoreAdapter();var av=new ChatViewModel();var bv=new ChatViewModel();
        Action pump=()=>{foreach(var pair in new[]{Tuple.Create(a,av),Tuple.Create(b,bv)})foreach(var e in pair.Item1.Poll()){if(e.kind==8)throw new Exception(e.text);pair.Item2.Apply(e);if(e.kind==2)pair.Item1.Approve(e.peer,true);}};
        try{
            a.Start(Path.Combine(root,"A"),29431,"Alice");b.Start(Path.Combine(root,"B"),29432,"Bob");
            Until(()=>av.Listening&&bv.Listening,pump,"listeners");string aId=av.OwnId,bId=bv.OwnId;
            string invite=b.Share("127.0.0.1",bv.Port,bv.OwnId);Require(invite.StartsWith("P2PCHAT1:"),"Share format");Require(a.Add("bad",av.OwnId).Length>0,"Invalid ID was accepted");Require(a.Add(invite,av.OwnId)=="","Add Friend");
            Until(()=>av.Conversations.Values.Any(c=>c.Online)&&bv.Conversations.Values.Any(c=>c.Online),pump,"mutual TLS and trust");
            string text="Hello \"friend\" \\ Unicode ☃ https://example.com/\nsecond line";
            Require(a.Send(bId,text),"Send A to B");Require(b.Send(aId,"Reply from Bob"),"Send B to A");
            Until(()=>av.Get(bId).Messages.Count==2&&bv.Get(aId).Messages.Count==2&&av.Get(bId).Messages.Any(m=>m.Outgoing&&m.Data.delivery==2)&&bv.Get(aId).Messages.Any(m=>m.Outgoing&&m.Data.delivery==2),pump,"bidirectional messages and acknowledgements");
            Require(bv.Get(aId).Messages.Any(m=>m.Text==text),"UTF-16/JSON payload changed");
            a.Dispose();b.Dispose();a=new CoreAdapter();b=new CoreAdapter();av=new ChatViewModel();bv=new ChatViewModel();
            a.Start(Path.Combine(root,"A"),29431,"");b.Start(Path.Combine(root,"B"),29432,"");
            Until(()=>av.Listening&&bv.Listening&&av.Conversations.Values.Any(c=>c.Online)&&bv.Conversations.Values.Any(c=>c.Online),pump,"restart and reconnect");
            Require(av.OwnId==aId&&bv.OwnId==bId,"Identity changed");a.History(bId,long.MaxValue);b.History(aId,long.MaxValue);
            Until(()=>av.Get(bId).Messages.Count==2&&bv.Get(aId).Messages.Count==2,pump,"encrypted history reload");
            Console.WriteLine("PASS: WPF adapter + view model: profile, listen, share/add, mutual TLS, Unicode messages both directions, delivery, saved contacts, stable identity, restart, history, reconnect.");
        }finally{a.Dispose();b.Dispose();}return 0;
    }catch(Exception e){Console.Error.WriteLine("FAIL: "+e.Message);return 1;}}
}
