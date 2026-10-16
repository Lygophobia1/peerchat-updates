using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;
internal static class EncryptedMailboxServerTest {
    // Hold only the disposable test provider, never a production FCM request.
    // This proves queued recipient wake work is independent of sender teardown.
    sealed class HeldPush : IPushProvider {
        internal readonly ManualResetEvent Started=new ManualResetEvent(false),Release=new ManualResetEvent(false),Completed=new ManualResetEvent(false);
        public PushResult Wake(string token){Started.Set();if(!Release.WaitOne(10000))return PushResult.Unavailable;Completed.Set();return PushResult.Sent;}
    }
    static void Check(bool ok){if(!ok)throw new Exception("Encrypted mailbox server assertion failed.");}
    static void Reject(Action action){bool rejected=false;try{action();}catch{rejected=true;}Check(rejected);}
    static void Seed(AccountDatabase db,string a,string b){db.InitializeMailbox();db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'synthetic-sender',X'00',X'00',1),(8,'synthetic-recipient',X'00',X'00',1);"+
        "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+a+"',1),(8,'"+b+"',1);INSERT INTO friendships(account_low,account_high,created_utc) VALUES(7,8,1);");}
    static void Policy(string root,byte[] sender,byte[] recipient,byte[] binding,byte[] envelope){
        string a=MailboxProtocol.Digest(sender),b=MailboxProtocol.Digest(recipient);
        using(var db=new AccountDatabase(Path.Combine(root,"quota-mailbox.sqlite"))){
            Seed(db,a,b);Check(db.RegisterMailboxKey(8,b,recipient,binding)=="accepted");
            for(int i=0;i<257;i++)Check(db.StoreMailbox(7,a,sender,File.ReadAllBytes(Path.Combine(root,"quota","small-"+i+".bin")))==(i<256?"stored":"quota_exceeded"));
            Check(db.FetchMailbox(8,b).Count==64);
            // Advance only this disposable fixture's durable expiry metadata;
            // no system clock/policy changes or production server DB touched.
            db.Execute("UPDATE encrypted_mailbox SET expires=1;");Check(db.FetchMailbox(8,b).Count==0);
            bool quota=false;for(int i=0;i<129;i++){string result=db.StoreMailbox(7,a,sender,File.ReadAllBytes(Path.Combine(root,"quota","large-"+i+".bin")));if(result=="quota_exceeded"){quota=true;break;}Check(result=="stored");}
            Check(quota);Check(db.FetchMailbox(8,b).Count==1); // bounded large-wire response
            db.Execute("UPDATE encrypted_mailbox SET expires=1;");Check(db.FetchMailbox(8,b).Count==0);
            Check(db.StoreMailbox(7,a,sender,envelope)=="stored");
            Check(db.RegisterMailboxKey(8,b,recipient,File.ReadAllBytes(Path.Combine(root,"rotated-binding.bin")))=="accepted");
            Check(db.FetchMailbox(8,b).Count==1);Check(db.StoreMailbox(7,a,sender,envelope)=="stored"); // exact old-generation retry
            Check(db.StoreMailbox(7,a,sender,File.ReadAllBytes(Path.Combine(root,"quota","small-0.bin")))=="recipient_key_retired");
            Check(db.StoreMailbox(7,a,sender,File.ReadAllBytes(Path.Combine(root,"rotated-envelope.bin")))=="stored");
            Check(db.RevokeMailboxKey(8,b,recipient,File.ReadAllBytes(Path.Combine(root,"revocation.bin")))=="revoked");Check(db.FetchMailbox(8,b).Count==1);
            db.Execute("DELETE FROM devices WHERE account_id=8;");Check(db.StoreMailbox(7,a,sender,envelope)=="not_authorized");Reject(()=>db.FetchMailbox(8,b));
            db.InitializeMailbox(); // orphan cleanup after device removal
        }
        Console.WriteLine("PASS: 256-row/8-MiB quotas, bounded large responses, expiry cleanup, rotation/current-generation enforcement, retired exact retry, revocation and device removal.");
    }
    static void Network(string root,string certificate,string password,byte[] sender,byte[] recipient,byte[] binding,byte[] envelope,byte[] receipt){
        string path=Path.Combine(root,"network-mailbox.sqlite"),a=MailboxProtocol.Digest(sender),b=MailboxProtocol.Digest(recipient);
        var probe=new TcpListener(IPAddress.Loopback,0);probe.Start();int port=((IPEndPoint)probe.LocalEndpoint).Port;probe.Stop();
        var config=new ServerConfiguration{DataDirectory=root,DatabasePath=path,LogDirectory=Path.Combine(root,"network-logs"),Port=port,CertificatePath=certificate,CertificatePassword=password};
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'synthetic-sender',X'00',X'00',1),(8,'synthetic-recipient',X'00',X'00',1);"+
                "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+a+"',1),(8,'"+b+"',1);INSERT INTO friendships(account_low,account_high,created_utc) VALUES(7,8,1);");
            string pin;using(var cert=new X509Certificate2(certificate,password))using(var sha=SHA256.Create())pin=BitConverter.ToString(sha.ComputeHash(cert.RawData)).Replace("-","");
            var sending=new DiscoveryClient("localhost",port,pin);var receiving=new DiscoveryClient("localhost",port,pin);
            foreach(var pair in new[]{Tuple.Create(sending,7L),Tuple.Create(receiving,8L)}){pair.Item1.SessionToken=Guid.NewGuid().ToString("N");pair.Item1.AccountId=pair.Item2;
                using(var sha=SHA256.Create())db.CreateSession(BitConverter.ToString(sha.ComputeHash(System.Text.Encoding.UTF8.GetBytes(pair.Item1.SessionToken))).Replace("-","").ToLowerInvariant(),pair.Item2,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());}
            // Recipient authorization and push registration are durable device
            // state, not children of the sender's authenticated session.
            db.RememberDevice(8,b,"synthetic-remembered-hash");
            Check(db.RegisterPush(8,b,"synthetic-push-token"));
            var push=new HeldPush();
            var server=new DiscoveryServer(config,push);Exception error=null;var worker=new Thread(()=>{try{server.Run();}catch(Exception failure){error=failure;}}){IsBackground=true};worker.Start();
            try {
                var end=DateTime.UtcNow.AddSeconds(10);bool ready=false;
                while(DateTime.UtcNow<end&&!ready){try{using(var tcp=new TcpClient()){tcp.Connect("localhost",port);ready=true;}}catch(SocketException){Thread.Sleep(20);}}
                Check(ready);sending.StartEventChannel(a,e=>{});receiving.StartEventChannel(b,e=>{});
                end=DateTime.UtcNow.AddSeconds(10);while(DateTime.UtcNow<end&&(!sending.EventChannelConnected||!receiving.EventChannelConnected))Thread.Sleep(20);
                Check(sending.EventChannelConnected&&receiving.EventChannelConnected);
                var registration=receiving.Mailbox("mailbox_register",b,new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(recipient)},{"binding",Convert.ToBase64String(binding)}});Check((string)registration["state"]=="accepted");
                // Recipient is offline before storage. Keep its exact authenticated
                // device/key registration; closing presence is not revocation.
                receiving.StopEventChannel();Check(!receiving.EventChannelConnected);
                var upload=new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(sender)},{"envelope",Convert.ToBase64String(envelope)}};
                Check((string)sending.Mailbox("mailbox_store",a,upload)["state"]=="stored");Check((string)sending.Mailbox("mailbox_store",a,upload)["state"]=="stored");
                Check(push.Started.WaitOne(5000));
                Action<string> snapshot=stage=>{
                    Check(db.FetchMailbox(8,b).Count==1);
                    var publicBindings=db.MailboxKeys(7,a,8);
                    Check(publicBindings.Count==1&&Convert.FromBase64String((string)publicBindings[0]["binding"]).SequenceEqual(binding));
                    Check(db.PushToken(8,b)=="synthetic-push-token");
                    Check(db.MailboxReceipts(7,a).Count==0);
                    Console.WriteLine("TRACE: "+stage+" sender_account=7 recipient_account=8 ciphertext=retained recipient_binding=unchanged fetch=eligible recipient_push=registered receipt=pending");
                };
                snapshot("before_sender_disconnect");
                // Sender is gone after Stored: no event channel, presence lease,
                // authenticated server session, or peer transport is available.
                sending.StopEventChannel();sending.Logout();
                snapshot("after_sender_event_disconnect_and_logout");
                db.ClearPresence(7,a);
                // The device remains authorized while the presence lease and
                // authenticated session disappear. Exercise normal purge too.
                db.InitializeMailbox();
                snapshot("after_sender_presence_cleanup_and_mailbox_purge");
                Check(!db.IsPresent(7));
                push.Release.Set();
                Check(push.Completed.WaitOne(5000));
                Check(!sending.EventChannelConnected);
                // Recipient reconnects only after the sender is fully logged out.
                receiving.StartEventChannel(b,e=>{});end=DateTime.UtcNow.AddSeconds(10);
                while(DateTime.UtcNow<end&&!receiving.EventChannelConnected)Thread.Sleep(20);
                Check(receiving.EventChannelConnected);
                registration=receiving.Mailbox("mailbox_register",b,new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(recipient)},{"binding",Convert.ToBase64String(binding)}});
                Check((string)registration["state"]=="accepted");
                // A failed pinned connection cannot consume pending ciphertext.
                var interrupted=new DiscoveryClient("localhost",port,new string('0',64)){SessionToken=receiving.SessionToken};
                Reject(()=>interrupted.Mailbox("mailbox_fetch",b,new Dictionary<string,object>()));
                Check(db.FetchMailbox(8,b).Count==1);
                var fetched=receiving.Mailbox("mailbox_fetch",b,new Dictionary<string,object>());
                var rows=(System.Collections.IEnumerable)fetched["envelopes"];var values=rows.Cast<object>().Cast<Dictionary<string,object>>().ToArray();Check(values.Length==1&&Convert.FromBase64String((string)values[0]["envelope"]).SequenceEqual(envelope));
                string fetchCursor=(string)fetched["nextCursor"];Check(fetchCursor==(string)values[0]["cursor"]);
                var fetchedTail=receiving.Mailbox("mailbox_fetch",b,new Dictionary<string,object>{{"cursor",fetchCursor}});
                Check(((System.Collections.IEnumerable)fetchedTail["envelopes"]).Cast<object>().Count()==0&&(string)fetchedTail["nextCursor"]=="");
                var fetchedAgain=receiving.Mailbox("mailbox_fetch",b,new Dictionary<string,object>());
                var replay=((System.Collections.IEnumerable)fetchedAgain["envelopes"]).Cast<object>().Cast<Dictionary<string,object>>().ToArray();
                Check(replay.Length==1&&Convert.FromBase64String((string)replay[0]["envelope"]).SequenceEqual(envelope));
                Check(db.MailboxReceipts(7,a).Count==0);
                // RPC must not replace the active event channel/device session.
                Check(receiving.EventChannelConnected);
                var ack=new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(recipient)},{"receipt",Convert.ToBase64String(receipt)}};
                Check((string)receiving.Mailbox("mailbox_ack",b,ack)["state"]=="acknowledged");Check((string)receiving.Mailbox("mailbox_ack",b,ack)["state"]=="acknowledged");
                Check(db.FetchMailbox(8,b).Count==0);
                // Reopen the sender's application session only AFTER recipient
                // ACK/cleanup. It reconciles the durable proof, not a live ACK.
                sending.SessionToken=Guid.NewGuid().ToString("N");sending.AccountId=7;
                using(var sha=SHA256.Create())db.CreateSession(BitConverter.ToString(sha.ComputeHash(System.Text.Encoding.UTF8.GetBytes(sending.SessionToken))).Replace("-","").ToLowerInvariant(),7,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
                var proofs=sending.Mailbox("mailbox_receipts",a,new Dictionary<string,object>());var proofRows=((System.Collections.IEnumerable)proofs["receipts"]).Cast<object>().Cast<Dictionary<string,object>>().ToArray();Check(proofRows.Length==1);
                string proofCursor=(string)proofs["nextCursor"];Check(proofCursor==(string)proofRows[0]["cursor"]&&(string)proofRows[0]["recipientDevice"]==b);
                var proofTail=sending.Mailbox("mailbox_receipts",a,new Dictionary<string,object>{{"cursor",proofCursor}});
                Check(((System.Collections.IEnumerable)proofTail["receipts"]).Cast<object>().Count()==0&&(string)proofTail["nextCursor"]=="");
                Reject(()=>sending.Mailbox("mailbox_fetch",b,new Dictionary<string,object>()));
                var wrong=new DiscoveryClient("localhost",port,new string('0',64)){SessionToken=sending.SessionToken};Reject(()=>wrong.Mailbox("mailbox_fetch",a,new Dictionary<string,object>()));
                Console.WriteLine("PASS: pinned TLS recipient offline during store, sender logged out, recipient reconnect/same-binding registration, failed fetch retains ciphertext, repeated fetch and signed ACK; recipient cleanup precedes sender reopen/receipt reconciliation; duplicate/wrong-device/pin rejection.");
            }finally{push.Release.Set();sending.StopEventChannel();receiving.StopEventChannel();typeof(DiscoveryServer).GetMethod("RequestStop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,null);Check(worker.Join(6000));if(error!=null)throw error;}
        }
    }
    public static int Main(string[] args){try{
        Check(args.Length==1||args.Length==3);string root=args[0],path=Path.Combine(root,"mailbox-server.sqlite");
        byte[] sender=File.ReadAllBytes(Path.Combine(root,"sender-public.bin")),recipient=File.ReadAllBytes(Path.Combine(root,"recipient-public.bin"));
        byte[] binding=File.ReadAllBytes(Path.Combine(root,"binding.bin")),envelope=File.ReadAllBytes(Path.Combine(root,"envelope.bin")),receipt=File.ReadAllBytes(Path.Combine(root,"receipt.bin"));
        string a=MailboxProtocol.Digest(sender),b=MailboxProtocol.Digest(recipient);
        if(args.Length==3)Network(root,args[1],args[2],sender,recipient,binding,envelope,receipt);
        Policy(root,sender,recipient,binding,envelope);
        var e=MailboxProtocol.Envelope.Parse(envelope);Check(e.Sender==7&&e.Recipient==8&&e.SenderDevice==a&&e.RecipientDevice==b);
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();
            db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'synthetic-sender',X'00',X'00',1),(8,'synthetic-recipient',X'00',X'00',1),(9,'synthetic-outsider',X'00',X'00',1);"+
                "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+a+"',1),(8,'"+b+"',1);"+
                "INSERT INTO friendships(account_low,account_high,created_utc) VALUES(7,8,1);");
            Check(db.RegisterMailboxKey(7,a,recipient,binding)=="not_authorized");
            Check(db.RegisterMailboxKey(8,b,sender,binding)=="not_authorized");
            Check(db.RegisterMailboxKey(8,b,recipient,binding)=="accepted");
            Check(db.RegisterMailboxKey(8,b,recipient,binding)=="accepted");
            var keys=db.MailboxKeys(7,a,8);Check(keys.Count==1&&Convert.FromBase64String((string)keys[0]["binding"]).SequenceEqual(binding));
            Check(db.StoreMailbox(7,a,recipient,envelope)=="not_authorized");
            Check(db.StoreMailbox(7,a,sender,envelope)=="stored");
            Check(db.StoreMailbox(7,a,sender,envelope)=="stored");
            Check(db.FetchMailbox(8,b).Count==1);Check(db.FetchMailbox(7,a).Count==0);
            Reject(()=>db.FetchMailbox(9,b));Reject(()=>db.MailboxKeys(9,a,8));
            var changed=(byte[])envelope.Clone();changed[changed.Length-1]^=1;Check(db.StoreMailbox(7,a,sender,changed)=="not_authorized");
            for(int i=0;i<binding.Length;i++)Reject(()=>MailboxProtocol.Binding.Parse(binding.Take(i).ToArray()));
            for(int i=0;i<envelope.Length;i++)Reject(()=>MailboxProtocol.Envelope.Parse(envelope.Take(i).ToArray()));
            Check(db.MailboxReceipts(7,a).Count==0);
        }
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();Check(db.FetchMailbox(8,b).Count==1);
            Check(db.AckMailbox(8,b,sender,receipt)=="not_authorized");
            Check(db.AckMailbox(8,b,recipient,receipt)=="acknowledged");
            Check(db.AckMailbox(8,b,recipient,receipt)=="acknowledged");
            Check(db.FetchMailbox(8,b).Count==0);var proofs=db.MailboxReceipts(7,a);
            Check(proofs.Count==1&&Convert.FromBase64String((string)proofs[0]["receipt"]).SequenceEqual(receipt));
            Check(db.StoreMailbox(7,a,sender,envelope)=="stored");Check(db.FetchMailbox(8,b).Count==0);
            db.Execute("DELETE FROM friendships;");Reject(()=>db.MailboxKeys(7,a,8));Check(db.StoreMailbox(7,a,sender,envelope)=="not_authorized");
        }
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();Check(db.MailboxReceipts(7,a).Count==1);
            var revoke=File.ReadAllBytes(Path.Combine(root,"revocation.bin"));
            Check(db.RevokeMailboxKey(8,b,sender,revoke)=="not_authorized");
            Check(db.RevokeMailboxKey(8,b,recipient,revoke)=="revoked");
            Check(db.RevokeMailboxKey(8,b,recipient,revoke)=="revoked");
            Check(db.RegisterMailboxKey(8,b,recipient,binding)=="key_conflict_or_revoked");
        }
        foreach(string file in Directory.GetFiles(root,"mailbox-server.sqlite*")){
            byte[] bytes=File.ReadAllBytes(file);Check(System.Text.Encoding.UTF8.GetString(bytes).IndexOf("synthetic\0text",StringComparison.Ordinal)<0);
        }
        Console.WriteLine("PASS: native/managed canonical wire, identity-bound registration, blind durable store/fetch/signed ACK, duplicate/restart/authorization/tamper/truncation and receipt persistence.");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
