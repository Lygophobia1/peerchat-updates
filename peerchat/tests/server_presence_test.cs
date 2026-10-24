using System;
using PeerChat.Server;
using System.IO;
class ServerPresenceTest {
    static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
    public static int Main(string[] args){try{
        long now=0;var state=new ServerPresence(()=>now);
        var first=state.Observe(1,"a","old",true);Check(first!=null&&state.Snapshot(1).Count==1,"initial snapshot");
        var second=state.Observe(1,"b","other",true);Check(state.Snapshot(1).Count==2,"multi-device aggregation");
        var fresh=state.Observe(1,"a","fresh",true);Check(fresh.Generation>first.Generation,"generation replacement");
        Check(state.Observe(1,"a","old",false)==null,"stale heartbeat");Check(state.Remove(1,"a","old")==null,"stale cleanup");
        Check(state.Remove(1,"b","other")!=null&&state.Snapshot(1).Count==1,"one device logout keeps account online");
        now=74000;Check(state.Get("a")!=null,"early expiry");state.Observe(1,"a","fresh",false);
        now=149001;var expired=state.Expire((a,d)=>true);Check(expired.Count==1&&state.Snapshot(1).Count==0,"unclean loss expiry");
        Check(state.Observe(1,"a","fresh",false)==null,"late heartbeat resurrected expired lease");
        state.Observe(1,"a","new",true);Check(state.Expire((a,d)=>false).Count==1&&state.Get("a")==null,"revocation");
        Check(state.LastIdentity(1)=="a"&&state.Get("a")==null,"offline routing hint must not invent presence");
        for(int account=10;account<4110;account++)state.Observe(account,"bounded"+account,"owner",true);
        Check(state.LastIdentity(1)==null&&state.LastIdentity(4109)=="bounded4109","last-device hints must remain bounded");
        var restart=new ServerPresence(()=>now);Check(restart.Epoch!=state.Epoch&&restart.Snapshot(1).Count==0,"restart epoch");
        var order=new PeerChatDesktop.PresenceSequence();order.Reset(state.Epoch);
        Check(order.Accept(state.Epoch,1,10)&&!order.Accept(state.Epoch,1,9)&&!order.Accept(state.Epoch,1,10),"reordered/duplicate event rejection");
        Check(order.Accept(state.Epoch,2,2),"unrelated account cursor interference");order.Reset(restart.Epoch);
        Check(!order.Accept(state.Epoch,1,100)&&order.Accept(restart.Epoch,1,1),"stale epoch event rejection");
        for(int i=0;i<1000;i++){var lease=state.Observe(2,"c","cycle"+i,true);Check(state.Remove(2,"c",lease.Owner)!=null,"clean logout");}
        state.Observe(3,"race","old",true);now+=76000;
        Check(state.Expire((a,d)=>{state.Observe(a,d,"replacement",true);return false;}).Count==0&&state.Get("race").Owner=="replacement","expiry removed replacement generation");
        if(args.Length>0){
            string path=Path.Combine(args[0],"presence-"+Guid.NewGuid().ToString("N")+".db");Directory.CreateDirectory(args[0]);
            using(var db=new AccountDatabase(path)){
                long account=db.CreateAccountWithDevice("presence-test",new byte[16],new byte[32],new string('a',64));
                db.CreateSession(new string('b',64),account,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
                for(int i=0;i<1000;i++)db.TouchPresence(account,new string('a',64),"",0,"",0,"test","test",session:new string('b',64));
                Check(db.IsPresent(account),"production memory authority");
                var phone=new string('c',64);Check(db.AddDevice(account,phone),"second test device");
                db.TouchPresence(account,phone,"",0,"",0,"test","test",owner:"phone",replace:true,session:new string('b',64));
                db.ClearPresence(account,new string('a',64));db.ClearPresence(account,phone);
                var offline=db.Search("presence-test",99);
                Check(!(bool)offline["online"]&&(string)offline["publicIdentity"]==phone,"sleep changed canonical device to oldest profile");
                db.Execute("DELETE FROM devices WHERE public_identity='"+phone+"';");
                Check((string)db.Search("presence-test",99)["publicIdentity"]==new string('a',64),"removed last device remained selectable");

                using(var reopened=new AccountDatabase(path)){
                    Check(!reopened.IsPresent(account)&&!(bool)reopened.Search("presence-test",99)["online"],"durable presence leaked across epoch");
                    Check(reopened.OwnsDevice(account,new string('a',64)),"durable device lost on presence restart");
                    Check(reopened.AddDevice(account,phone),"recreate isolated phone fixture");
                    reopened.RememberDevice(account,phone,"test-remembered-hash");
                    Check(reopened.RegisterPush(account,phone,"test-only-push-token"),"register isolated push");
                    Check((string)reopened.Search("presence-test",99)["publicIdentity"]==phone&&!reopened.IsPresent(account),"restart must retain eligible offline push route without claiming presence");
                    reopened.RemovePush(account,phone);
                    Check((string)reopened.Search("presence-test",99)["publicIdentity"]==new string('a',64),"removed push route remained preferred after restart");

                }
                var field=typeof(AccountDatabase).GetField("database",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic);
                IntPtr statement;Check(PeerChat.Server.SqliteNative.prepare((IntPtr)field.GetValue(db),"SELECT count(*) FROM main.presence",out statement)==0,"presence row inspection");
                try{Check(PeerChat.Server.SqliteNative.step(statement)==100&&PeerChat.Server.SqliteNative.columnLong(statement,0)==0,"heartbeat persisted live presence");}finally{PeerChat.Server.SqliteNative.finalize(statement);}
            }
        }
        Console.WriteLine("PASS: monotonic presence, initial/multi-device snapshots, stale generation/owner rejection, clean logout, expiry, revocation and restart epoch (1000 cycles).");return 0;
    }catch(Exception error){Console.Error.WriteLine(error.Message);return 1;}}
}
