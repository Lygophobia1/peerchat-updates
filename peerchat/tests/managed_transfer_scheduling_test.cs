using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using PeerChatDesktop;
class ManagedTransferSchedulingTest {
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    static int Main(string[] args){
        try{Run(args[0],args[1]=="event");return 0;}catch(Exception e){Console.Error.WriteLine(e.GetType().Name+": "+e.Message);return 1;}
    }
    static void Run(string root,bool eventDriven){
        Directory.CreateDirectory(root);
        using(var left=new CoreAdapter())using(var right=new CoreAdapter())using(var stop=new CancellationTokenSource()){
            string a="",b="";ulong la=0,ra=0;bool lr=false,rr=false;Thread writerA=null,writerB=null;Exception forwardingError=null;
            left.Start(Path.Combine(root,"left"),0,"Fixture left");right.Start(Path.Combine(root,"right"),0,"Fixture right");
            Action poll=()=>{foreach(var e in left.Poll()??new CoreEvent[0]){if(e.kind==0)a=e.peer;if(e.kind==18)la=(ulong)e.attempt;if(e.kind==2)Check(left.PinInitialAuthenticatedFriendIdentity(e.peer,(ulong)e.attempt,1),"left pin rejected");if(e.kind==3)lr=true;}foreach(var e in right.Poll()??new CoreEvent[0]){if(e.kind==0)b=e.peer;if(e.kind==18)ra=(ulong)e.attempt;if(e.kind==2)Check(right.PinInitialAuthenticatedFriendIdentity(e.peer,(ulong)e.attempt,1),"right pin rejected");if(e.kind==3)rr=true;}};
            Action<CoreAdapter,CoreAdapter,ulong,ulong,int> forward=(from,to,outgoing,incoming,wait)=>{var bytes=new byte[65536];for(int n=0;n<(wait==0?8:1);n++){int count=from.TakeRelayOutput(outgoing,bytes,wait);if(count<=0)break;Check(to.RelayReceive(incoming,bytes,count),"relay input rejected");}};
            Action pump=()=>{if(la>0&&ra>0&&writerA==null){forward(left,right,la,ra,0);forward(right,left,ra,la,0);}poll();};
            Action<Func<bool>,int> waitFor=(condition,delay)=>{var clock=Stopwatch.StartNew();while(clock.ElapsedMilliseconds<120000){pump();if(condition())return;Thread.Sleep(delay);}throw new Exception("isolated transfer timed out");};
            waitFor(()=>a.Length==64&&b.Length==64,5);
            Check(left.SetAuthenticatedFriends(71,1,new[]{new FriendConversationBinding{AccountId=72,CurrentPeerId=b,KnownPeerIds=new List<string>{b},ActiveFriend=true}})&&right.SetAuthenticatedFriends(72,1,new[]{new FriendConversationBinding{AccountId=71,CurrentPeerId=a,KnownPeerIds=new List<string>{a},ActiveFriend=true}}),"friend context rejected");
            left.SetNetworkContext(1,"managed-scheduling-fixture");right.SetNetworkContext(1,"managed-scheduling-fixture");left.UpdateCandidates(b,"",0,"",0,true,1,true);right.UpdateCandidates(a,"",0,"",0,true,1,true);waitFor(()=>la>0&&ra>0,5);Check(left.RelayReady(la,String.CompareOrdinal(a,b)>0)&&right.RelayReady(ra,String.CompareOrdinal(b,a)>0),"role rejected");waitFor(()=>lr&&rr,5);
            try{
                if(eventDriven){
                    writerA=new Thread(()=>{try{while(!stop.IsCancellationRequested)forward(left,right,la,ra,50);}catch(Exception e){forwardingError=e;}});
                    writerB=new Thread(()=>{try{while(!stop.IsCancellationRequested)forward(right,left,ra,la,50);}catch(Exception e){forwardingError=e;}});
                    writerA.Start();writerB.Start();
                }
                var bytes=new byte[10*1024*1024];new Random(148).NextBytes(bytes);string source=Path.Combine(root,"payload.bin"),destination=Path.Combine(root,"received");File.WriteAllBytes(source,bytes);Directory.CreateDirectory(destination);
                string id=left.FileTransfer(1,b,path:source);waitFor(()=>right.FileTransfers().Any(r=>r.id==id&&r.state==2),eventDriven?25:5);right.FileTransfer(2,id:id,path:destination);
                var elapsed=Stopwatch.StartNew();waitFor(()=>{Check(forwardingError==null,"forwarding failed");return right.FileTransfers().Any(r=>r.id==id&&r.state==4)&&left.FileTransfers().Any(r=>r.id==id&&r.state==4);},eventDriven?25:5);elapsed.Stop();
                var received=right.FileTransfers().Single(r=>r.id==id);Check(received.done==bytes.Length&&File.ReadAllBytes(received.path).SequenceEqual(bytes),"final integrity failed");
                Console.WriteLine("MANAGED_PACKAGED_HOST mode="+(eventDriven?"blocking-forwarders":"5ms-observer-pump")+" bytes="+bytes.Length+" accepted_to_complete_ms="+elapsed.Elapsed.TotalMilliseconds.ToString("0.0",System.Globalization.CultureInfo.InvariantCulture)+" payload_mbps="+(bytes.Length*.008/elapsed.Elapsed.TotalMilliseconds).ToString("0.00",System.Globalization.CultureInfo.InvariantCulture)+" integrity=verified no_sockets_or_pi=true");
                Console.WriteLine(left.FileTransfer(11,id:id));
            }finally{stop.Cancel();if(writerA!=null)Check(writerA.Join(2000),"left forwarder not stopped");if(writerB!=null)Check(writerB.Join(2000),"right forwarder not stopped");}
        }
    }
}
