using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Linq;
using PeerChatDesktop;
internal static class MailboxLifecycleTest {
    static void Check(bool value){if(!value)throw new Exception("Mailbox lifecycle assertion failed");}
    static void Main(string[] args){
        if(args.Length==1){NativeRestart(args[0]);return;}
        Check(!MailboxExecution.CanStart(8,0,false));
        Check(!MailboxExecution.CanStart(8,7,false));
        Check(!MailboxExecution.CanStart(0,0,false));
        Check(!MailboxExecution.CanStart(8,8,true));
        Check(MailboxExecution.CanStart(8,8,false));
        Check(MailboxExecution.RouteNeedsReevaluation(new CoreEvent{kind=17,connected=false}));
        Check(!MailboxExecution.RouteNeedsReevaluation(new CoreEvent{kind=17,connected=true}));
        Check(!MailboxExecution.RouteNeedsReevaluation(new CoreEvent{kind=14,connected=false}));
        foreach(bool policy in new[]{false,true}){
            var execution=new MailboxExecution((op,bytes,id,allowed)=>{Check(op==2&&allowed==policy);return new byte[]{1};});
            Check(execution.PendingUploads(policy).SequenceEqual(new byte[]{1}));
        }
        for(int cycle=0;cycle<100;cycle++){
            var entered=new ManualResetEventSlim();var release=new ManualResetEventSlim();
            int calls=0;
            var execution=new MailboxExecution((op,bytes,id,allowed)=>{
                Interlocked.Increment(ref calls);Check(op==7);
                entered.Set();Check(release.Wait(2000));return new byte[]{1};
            });
            var active=Task.Run(()=>{
                try{execution.Service(null,"",false,(peer,inserted)=>{});throw new Exception("Cancellation ignored");}
                catch(OperationCanceledException){}
            });
            Check(entered.Wait(2000));execution.Stop();release.Set();
            Check(active.Wait(2000));Check(calls==1);
            try{execution.Service(null,"",false,(peer,inserted)=>{});throw new Exception("New work accepted after stop");}
            catch(OperationCanceledException){}
            Check(calls==1);entered.Dispose();release.Dispose();
        }
        foreach(var bytes in new[]{new byte[0],new byte[]{0,0},new byte[]{0,0,0,4,1,2}}){
            try{DiscoveryClient.ReadFrame(new MemoryStream(bytes));throw new Exception("Partial/EOF accepted");}
            catch(IOException error){Check(!error.Message.Contains("mailbox protocol"));}
        }
        try{using(var invalid=new MailboxExecution.Record(new byte[]{0,0,0,0},4)){}throw new Exception("Malformed object accepted");}
        catch(InvalidOperationException){}
        Console.WriteLine("PASS: account-context ordering, 100 active stop/drain/restart cycles, stopped work rejected, EOF/partial transport errors distinct from malformed records.");
    }
    static void NativeRestart(string profile){
        byte[] identity=null;
        for(int cycle=0;cycle<10;cycle++)using(var core=new CoreAdapter()){
            core.Start(profile,0,"Mailbox lifecycle fixture");
            try{core.Mailbox(7);throw new Exception("Missing account context accepted");}
            catch(InvalidOperationException){}
            Check(core.Poll().Any(e=>e.text=="Mailbox account context is not ready"));
            Check(core.SetAuthenticatedFriends(8,1,null));
            byte[] current=core.Mailbox(7);Check(current.Length==72);
            Check(identity==null||identity.SequenceEqual(current));identity=current;
            Check(!core.Poll().Any(e=>e.text=="Invalid mailbox protocol object"));
        }
        Console.WriteLine("PASS: actual native missing-context classification, serialized authenticated publication and ten profile close/reopen cycles with unchanged identity.");
    }
}
