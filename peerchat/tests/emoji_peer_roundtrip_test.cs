using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using PeerChatDesktop;
class EmojiPeerRoundtripTest {
    static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
    static int Main(string[] args){try{Run(Path.Combine(args[0],Guid.NewGuid().ToString("N")));return 0;}catch(Exception e){Console.Error.WriteLine(e);return 1;}}
    static void Run(string root){
        Directory.CreateDirectory(root);string a="",b="";string text="Unicode fixture\n😀 👩🏽‍💻 👨‍👩‍👧‍👦 🇺🇸 🇨🇦 1️⃣ ❤️ 🏳️‍🌈\n日本語 العربية e\u0301";
        using(var left=new CoreAdapter())using(var right=new CoreAdapter()){
            ulong la=0,ra=0;bool lr=false,rr=false;var lreceived=new HashSet<string>();var rreceived=new HashSet<string>();
            left.Start(Path.Combine(root,"left"),0,"Fixture left");right.Start(Path.Combine(root,"right"),0,"Fixture right");
            Action poll=()=>{foreach(var e in left.Poll()??new CoreEvent[0]){if(e.kind==0)a=e.peer;if(e.kind==18)la=(ulong)e.attempt;if(e.kind==2)Check(left.PinInitialAuthenticatedFriendIdentity(e.peer,(ulong)e.attempt,1),"left pin");if(e.kind==3)lr=true;if(e.kind==5&&e.messages!=null)foreach(var m in e.messages){if(m.sender==b&&m.text==text)lreceived.Add(m.id);}}foreach(var e in right.Poll()??new CoreEvent[0]){if(e.kind==0)b=e.peer;if(e.kind==18)ra=(ulong)e.attempt;if(e.kind==2)Check(right.PinInitialAuthenticatedFriendIdentity(e.peer,(ulong)e.attempt,1),"right pin");if(e.kind==3)rr=true;if(e.kind==5&&e.messages!=null)foreach(var m in e.messages){if(m.sender==a&&m.text==text)rreceived.Add(m.id);}}};
            Action<CoreAdapter,CoreAdapter,ulong,ulong> forward=(from,to,outgoing,incoming)=>{var bytes=new byte[65536];for(int n=0;n<8;n++){int count=from.TakeRelayOutput(outgoing,bytes,0);if(count<=0)break;Check(to.RelayReceive(incoming,bytes,count),"relay input");}};
            Action pump=()=>{if(la>0&&ra>0){forward(left,right,la,ra);forward(right,left,ra,la);}poll();};
            Action<Func<bool>> wait=condition=>{var clock=Stopwatch.StartNew();while(clock.ElapsedMilliseconds<60000){pump();if(condition())return;Thread.Sleep(5);}throw new Exception("Unicode peer fixture timeout");};
            wait(()=>a.Length==64&&b.Length==64);
            Check(left.SetAuthenticatedFriends(71,1,new[]{new FriendConversationBinding{AccountId=72,CurrentPeerId=b,KnownPeerIds=new List<string>{b},ActiveFriend=true}})&&right.SetAuthenticatedFriends(72,1,new[]{new FriendConversationBinding{AccountId=71,CurrentPeerId=a,KnownPeerIds=new List<string>{a},ActiveFriend=true}}),"friend context");
            left.SetNetworkContext(1,"unicode-fixture");right.SetNetworkContext(1,"unicode-fixture");left.UpdateCandidates(b,"",0,"",0,true,1,true);right.UpdateCandidates(a,"",0,"",0,true,1,true);wait(()=>la>0&&ra>0);Check(left.RelayReady(la,String.CompareOrdinal(a,b)>0)&&right.RelayReady(ra,String.CompareOrdinal(b,a)>0),"roles");wait(()=>lr&&rr);
            Check(left.Send(b,text)&&right.Send(a,text),"send");wait(()=>lreceived.Count>0&&rreceived.Count>0);
            Check(lreceived.Count==1&&rreceived.Count==1,"duplicate live delivery");
            left.RelayClosed(la,"Unicode reconnect fixture");right.RelayClosed(ra,"Unicode reconnect fixture");la=ra=0;lr=rr=false;poll();la=ra=0;
            left.SetNetworkContext(2,"unicode-reconnect");right.SetNetworkContext(2,"unicode-reconnect");left.UpdateCandidates(b,"",0,"",0,true,2,true);right.UpdateCandidates(a,"",0,"",0,true,2,true);
            Check(left.Send(b,text+" reconnect")&&right.Send(a,text+" reconnect"),"queued reconnect send");wait(()=>la>0&&ra>0);Check(left.RelayReady(la,String.CompareOrdinal(a,b)>0)&&right.RelayReady(ra,String.CompareOrdinal(b,a)>0),"reconnect roles");wait(()=>lr&&rr);
            var settle=Stopwatch.StartNew();while(settle.ElapsedMilliseconds<500){pump();Thread.Sleep(5);}
        }
        foreach(bool first in new[]{true,false})using(var node=new CoreAdapter()){
            node.Start(Path.Combine(root,first?"left":"right"),0,"Restart fixture");string identity="";var watch=Stopwatch.StartNew();while(identity.Length==0&&watch.ElapsedMilliseconds<10000){foreach(var e in node.Poll()??new CoreEvent[0])if(e.kind==0)identity=e.peer;Thread.Sleep(5);}Check(identity==(first?a:b),"restart identity");
            node.History(first?b:a,long.MaxValue);MessageData[] history=null;watch.Restart();while(history==null&&watch.ElapsedMilliseconds<10000){foreach(var e in node.Poll()??new CoreEvent[0])if(e.kind==6)history=e.messages;Thread.Sleep(5);}Check(history!=null&&history.Count(m=>m.text==text)==2&&history.Count(m=>m.text==text+" reconnect")==2,"durable exact history and deduplication");
        }
        Console.WriteLine("PASS: packaged native mutual-TLS DM Unicode in both directions, exact protected history after restart, stable identities and no duplicate rows. Isolated host relay-byte fixture, not physical Android.");
    }
}
