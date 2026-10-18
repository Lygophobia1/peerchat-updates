using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using PeerChat.Server;

static class AccountAuthenticationConcurrencyTests {
    static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
    public static int Run(string root,bool expectBlocked){
        Directory.CreateDirectory(root);
        using(var db=new AccountDatabase(Path.Combine(root,"authentication.sqlite"))) {
            long id=db.CreateAccountWithDevice("test-auth",new byte[]{1},new byte[]{2},new string('a',64));
            using(var entered=new ManualResetEvent(false))using(var release=new ManualResetEvent(false))
            using(var lookupDone=new ManualResetEvent(false)) {
                Exception failure=null;bool authenticated=false;long actual=0,lookupMs=0;
                var auth=new Thread(()=>{try{authenticated=db.PasswordMatches("test-auth",(salt,hash)=>{entered.Set();Check(release.WaitOne(5000),"verifier barrier timeout");return salt[0]==1&&hash[0]==2;},out actual);}catch(Exception e){failure=e;}});
                var lookup=new Thread(()=>{try{var watch=Stopwatch.StartNew();Check(db.UsernameExists("test-auth"),"unrelated lookup");lookupMs=watch.ElapsedMilliseconds;}catch(Exception e){failure=e;}finally{lookupDone.Set();}});
                bool blocked;
                try{auth.Start();Check(entered.WaitOne(5000),"verifier not entered");lookup.Start();blocked=!lookupDone.WaitOne(300);}
                finally{release.Set();Check(auth.Join(6000),"auth drain");Check(lookup.Join(6000),"lookup drain");}
                Check(failure==null&&authenticated&&actual==id,"authentication failed");
                Console.WriteLine("AUTH_CONTENTION injected_verifier_barrier_ms=300 unrelated_lookup_ms="+lookupMs+" blocked="+blocked+" scope=isolated_database_not_physical_network");
                Check(blocked==expectBlocked,"unexpected shared lock ownership");
            }
            if(expectBlocked)return 0;
            long rejected;
            Check(!db.PasswordMatches("missing",(salt,hash)=>{throw new Exception("missing verifier invoked");},out rejected)&&rejected==0,"missing account");
            Check(!db.PasswordMatches("test-auth",(salt,hash)=>false,out rejected)&&rejected==0,"bad password");
            // Test-only mutation models credential replacement while expensive verification runs.
            Check(!db.PasswordMatches("test-auth",(salt,hash)=>{
                db.Execute("UPDATE accounts SET password_hash=X'03' WHERE id="+id);
                return true;
            },out rejected)&&rejected==0,"stale credential accepted");
            Check(db.PasswordMatches("test-auth",(salt,hash)=>salt[0]==1&&hash[0]==3,out rejected)&&rejected==id,"current credentials rejected");
        }
        Console.WriteLine("PASS authentication lock isolation, wrong/missing credentials and concurrent credential replacement");return 0;
    }
}
