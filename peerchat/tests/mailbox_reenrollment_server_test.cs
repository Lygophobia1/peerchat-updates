using System;
using System.IO;
using System.Linq;
using System.Collections.Generic;
using PeerChat.Server;
internal static class MailboxReenrollmentServerTest {
    static void Check(bool value){if(!value)throw new Exception("Mailbox account recreation assertion failed");}
    static void Reject(Action operation){bool rejected=false;try{operation();}catch(InvalidOperationException){rejected=true;}Check(rejected);}
    static byte[] FileBytes(string root,string name){return File.ReadAllBytes(Path.Combine(root,name));}
    public static int Main(string[] args){try{
        Check(args.Length==1);string root=args[0],path=Path.Combine(root,"recreated-linux-accounts.sqlite");
        byte[] identity=FileBytes(root,"recipient-public.bin"),sender=FileBytes(root,"sender-public.bin");
        byte[] old=FileBytes(root,"old-binding19.bin"),enrollment=FileBytes(root,"new-binding4.bin");
        byte[] wrongGeneration=FileBytes(root,"new-binding4-invalid-generation.bin");
        byte[] oldEnvelope=FileBytes(root,"old-envelope19.bin"),newEnvelope=FileBytes(root,"new-envelope4.bin"),receipt=FileBytes(root,"new-receipt4.bin");
        string phone=MailboxProtocol.Digest(identity),origin=MailboxProtocol.Digest(sender);
        var historical=MailboxProtocol.Binding.Parse(old);var target=MailboxProtocol.Binding.Parse(enrollment);
        Check(historical.Account==19&&historical.Generation==2&&target.Account==4&&target.Generation==1);
        Check(historical.Device==target.Device&&historical.Public.SequenceEqual(target.Public)&&historical.Key!=target.Key);
        Check(MailboxProtocol.Verify(target.Original,target.Signature,identity,phone));
        string legacyPath=Path.Combine(root,"original-windows-accounts.sqlite");
        using(var legacy=new AccountDatabase(legacyPath)){
            legacy.InitializeMailbox();
            legacy.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(19,'synthetic-original-Jonah',X'00',X'00',1),(5,'synthetic-original-contact',X'00',X'00',1);"+
                "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(19,'"+phone+"',1),(5,'"+origin+"',1);INSERT INTO friendships(account_low,account_high,created_utc) VALUES(5,19,1);");
            Check(legacy.RegisterMailboxKey(19,phone,identity,FileBytes(root,"old-genesis-binding19.bin"))=="accepted");
            Check(legacy.RegisterMailboxKey(19,phone,identity,old)=="accepted");
            Check(legacy.StoreMailbox(5,origin,sender,oldEnvelope)=="stored");
            Check(legacy.FetchMailbox(19,phone).Count==1);Reject(()=>legacy.FetchMailbox(4,phone));
        }
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();
            db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(4,'synthetic-JonahPhone',X'00',X'00',1),(5,'synthetic-Sammy',X'00',X'00',1),(19,'unrelated-recreated-account',X'00',X'00',1);"+
                "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(4,'"+phone+"',1),(5,'"+origin+"',1);INSERT INTO friendships(account_low,account_high,created_utc) VALUES(4,5,1);");
            Check(db.OwnsDevice(4,phone)&&!db.OwnsDevice(19,phone));
            Check(db.MailboxKeys(5,origin,4).Count==0);
            Check(db.RegisterMailboxKey(4,phone,identity,old)=="not_authorized");
            Check(db.RegisterMailboxKey(4,phone,identity,wrongGeneration)=="invalid_generation");
            Check(db.MailboxKeys(5,origin,4).Count==0);
            Check(db.StoreMailbox(5,origin,sender,newEnvelope)=="recipient_key_unavailable");
            Check(db.RegisterMailboxKey(4,phone,sender,enrollment)=="not_authorized");
            Check(db.RegisterMailboxKey(4,phone,identity,enrollment)=="accepted");
            Check(db.RegisterMailboxKey(4,phone,identity,enrollment)=="accepted");
            var advertised=db.MailboxKeys(5,origin,4);Check(advertised.Count==1&&Convert.FromBase64String((string)advertised[0]["binding"]).SequenceEqual(enrollment));
            Check(db.StoreMailbox(5,origin,sender,oldEnvelope)=="not_authorized");
            Check(db.StoreMailbox(5,origin,sender,newEnvelope)=="stored");
            Check(db.RegisterMailboxKey(4,phone,identity,wrongGeneration)=="key_conflict_or_revoked");
            Check(db.FetchMailbox(4,phone).Count==1);
            Reject(()=>db.FetchMailbox(19,phone));
        }
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();var pending=db.FetchMailbox(4,phone);
            Check(pending.Count==1&&Convert.FromBase64String((string)pending[0]["envelope"]).SequenceEqual(newEnvelope));
            Check(db.FetchMailbox(4,phone).Count==1);
            Check(db.AckMailbox(19,phone,identity,receipt)=="not_authorized");
            Check(db.FetchMailbox(4,phone).Count==1);
            Check(db.AckMailbox(4,phone,identity,receipt)=="acknowledged");
            Check(db.AckMailbox(4,phone,identity,receipt)=="acknowledged");
            Check(db.FetchMailbox(4,phone).Count==0&&db.MailboxReceipts(5,origin).Count==1);
        }
        using(var legacy=new AccountDatabase(legacyPath)){
            legacy.InitializeMailbox();var retained=legacy.FetchMailbox(19,phone);
            Check(retained.Count==1&&Convert.FromBase64String((string)retained[0]["envelope"]).SequenceEqual(oldEnvelope));
            Check(legacy.MailboxReceipts(5,origin).Count==0);Reject(()=>legacy.FetchMailbox(4,phone));
        }
        Check(FileBytes(root,"old-binding19.bin").SequenceEqual(old));
        Console.WriteLine("PASS: recreated Linux-style account4/device authorization; old account19 binding rejected; same physical public key with new signed enrollment ID accepted via unchanged registration protocol; wrong signer/generation/conflicting proof rejected; account19 ciphertext/ACK denied; durable account4 store/fetch/restart/ACK. ISOLATED FIXTURE, no phone migration.");return 0;
    }catch(Exception failure){Console.Error.WriteLine(failure);return 1;}}
}
