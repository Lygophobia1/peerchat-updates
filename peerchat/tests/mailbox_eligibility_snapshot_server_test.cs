using System;
using System.Collections.Generic;
using System.IO;
using PeerChat.Server;

// Isolated database contract fixture. The signed key registration path is
// exercised in encrypted_mailbox_server_test; this checks snapshot authority.
internal static class MailboxEligibilitySnapshotServerTest {
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    static void Reject(Action action,string message){bool failed=false;try{action();}catch(InvalidOperationException){failed=true;}Check(failed,message);}
    static string Id(int value){return value.ToString("x64");}
    static List<Dictionary<string,object>> Rows(Dictionary<string,object> result){return (List<Dictionary<string,object>>)result["candidates"];}
    static Dictionary<string,object> Find(Dictionary<string,object> result,string device){foreach(var row in Rows(result))if((string)row["device"]==device)return row;throw new Exception("Missing frozen device");}
    public static int Main(string[] args){try{
        Check(args.Length==2,"Expected isolated temporary and canonical mailbox fixture directories.");
        byte[] identity=File.ReadAllBytes(Path.Combine(args[1],"recipient-public.bin"));
        byte[] signedBinding=File.ReadAllBytes(Path.Combine(args[1],"binding.bin"));
        string path=Path.Combine(args[0],"eligibility-snapshot.sqlite"),sender=Id(1),foreign=Id(2),bound=MailboxProtocol.Digest(identity),online=Id(4),late=Id(5);
        var binding=MailboxProtocol.Binding.Parse(signedBinding);
        Check(binding.Account==8&&binding.Device==bound&&binding.Allowed,"Canonical recipient binding is not for fixture account/device.");
        string snapshotId;
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();
            db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'sender',X'00',X'00',1),(8,'recipient',X'00',X'00',1),(9,'foreign',X'00',X'00',1);"+
                "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+sender+"',1),(8,'"+bound+"',1),(8,'"+online+"',1),(9,'"+foreign+"',1);"+
                "INSERT INTO friendships(account_low,account_high,created_utc) VALUES(7,8,1),(7,9,1);");
            Check(db.RegisterMailboxKey(8,bound,identity,signedBinding)=="accepted","Signed recipient binding registration failed.");
            db.CreateSession("recipient-live-session",8,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
            Check(db.TouchPresence(8,online,"",0,"",0,"", "",session:"recipient-live-session")!=null,"Authenticated presence seed failed.");
            Reject(()=>db.MailboxEligibilitySnapshot(9,foreign,8),"Foreign account could snapshot recipient.");
            Reject(()=>db.MailboxEligibilitySnapshot(7,foreign,8),"Sender account could substitute foreign device.");
            Reject(()=>db.MailboxEligibilitySnapshot(8,bound,8),"Self mailbox snapshot was permitted.");
            var issued=db.MailboxEligibilitySnapshot(7,sender,8);
            Check((string)issued["state"]=="ready"&&Convert.ToInt32(issued["snapshotVersion"])==1,"Snapshot did not issue.");
            Check(Convert.ToInt64(issued["senderAccountId"])==7&&(string)issued["senderDevice"]==sender&&Convert.ToInt64(issued["recipientAccountId"])==8,"Snapshot sender scope omitted.");
            Check(Rows(issued).Count==2&&String.CompareOrdinal(Rows(issued)[0]["device"].ToString(),Rows(issued)[1]["device"].ToString())<0,"Snapshot candidate union/order incorrect.");
            Check(!(bool)Find(issued,bound)["online"]&&(string)Find(issued,bound)["binding"]==Convert.ToBase64String(signedBinding),"Signed binding candidate missing.");
            Check((bool)Find(issued,online)["online"]&&(string)Find(issued,online)["binding"]=="","Authenticated live-only candidate missing.");
            Check(Convert.ToInt64(issued["freshUntilUtcMs"])-Convert.ToInt64(issued["issuedUtcMs"])==120000,"Queue freshness interval incorrect.");
            Check(Convert.ToInt64(issued["validateUntilUtcMs"])-Convert.ToInt64(issued["issuedUtcMs"])==600000,"Validation retention interval incorrect.");
            snapshotId=(string)issued["snapshotId"];
            // Reconnects and simultaneous client refreshes must not spend the
            // bounded proof-retention quota on identical still-fresh audiences.
            for(int refresh=0;refresh<70;refresh++){
                var reused=db.MailboxEligibilitySnapshot(7,sender,8);
                Check((string)reused["state"]=="ready","Identical refresh exhausted snapshot quota.");
                Check((string)reused["snapshotId"]==snapshotId&&
                    Convert.ToInt64(reused["snapshotSequence"])==Convert.ToInt64(issued["snapshotSequence"])&&
                    Convert.ToInt64(reused["freshUntilUtcMs"])==Convert.ToInt64(issued["freshUntilUtcMs"]),
                    "Identical refresh replaced immutable snapshot or extended freshness.");
            }
            // Preserve the real quota for changed audiences, while allowing an
            // already retained unchanged proof to be read even at saturation.
            for(int n=0;n<63;n++)db.Execute("INSERT INTO mailbox_eligibility_snapshots(id,sender_account,sender_device,recipient_account,issued,fresh_until,validate_until,digest) SELECT 'quota"+n+"',sender_account,sender_device,recipient_account,issued,1,validate_until,digest FROM mailbox_eligibility_snapshots WHERE id='"+snapshotId+"';");
            Check((string)db.MailboxEligibilitySnapshot(7,sender,8)["snapshotId"]==snapshotId,"Quota blocked reuse of unchanged current proof.");
            db.Execute("UPDATE mailbox_eligibility_snapshots SET fresh_until="+(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()+20000)+" WHERE id='"+snapshotId+"';");
            Check((string)db.MailboxEligibilitySnapshot(7,sender,8)["state"]=="snapshot_quota_exceeded","Near-expiry snapshot reused or quota bypassed.");
            db.Execute("DELETE FROM mailbox_eligibility_snapshots WHERE id LIKE 'quota%';");
            var refreshed=db.MailboxEligibilitySnapshot(7,sender,8);
            Check((string)refreshed["snapshotId"]!=snapshotId&&Convert.ToInt64(refreshed["snapshotSequence"])>Convert.ToInt64(issued["snapshotSequence"]),"Near-expiry proof was extended in place.");
            Reject(()=>db.ValidateMailboxEligibility(9,foreign,8,snapshotId),"Foreign sender read snapshot.");
            Reject(()=>db.ValidateMailboxEligibility(7,foreign,8,snapshotId),"Foreign device read snapshot.");
            Check((string)db.ValidateMailboxEligibility(7,sender,9,snapshotId)["state"]=="snapshot_unavailable","Wrong recipient read snapshot.");
            db.Execute("INSERT INTO devices(account_id,public_identity,created_utc) VALUES(8,'"+late+"',1);"+
                "INSERT INTO mailbox_recipient_keys(device,key_id,account_id,generation,state,allowed,proof) VALUES('"+late+"','"+Id(71).Substring(0,32)+"',8,1,'active',1,X'0405');"+
                "UPDATE mailbox_recipient_keys SET state='revoked' WHERE device='"+bound+"';");
            var validated=db.ValidateMailboxEligibility(7,sender,8,snapshotId);
            var changed=db.MailboxEligibilitySnapshot(7,sender,8);
            Check((string)changed["snapshotId"]!=(string)refreshed["snapshotId"]&&
                (string)changed["eligibilityDigest"]!=(string)refreshed["eligibilityDigest"],"Changed/revoked audience reused an obsolete proof.");
            Check(Rows(validated).Count==2&&!(bool)Find(validated,bound)["eligible"]&&(string)Find(validated,bound)["binding"]=="","Revoked original binding remained eligible.");
            Check((bool)Find(validated,online)["eligible"],"Existing authenticated online device vanished.");
            Check(Convert.ToInt64(validated["snapshotSequence"])==Convert.ToInt64(issued["snapshotSequence"])&&(string)validated["eligibilityDigest"]==(string)issued["eligibilityDigest"],"Frozen snapshot evidence changed.");
            db.Execute("DELETE FROM devices WHERE account_id=8 AND public_identity='"+online+"';");
            validated=db.ValidateMailboxEligibility(7,sender,8,snapshotId);
            Check(!(bool)Find(validated,online)["eligible"]&&Rows(validated).Count==2,"Removed device remained eligible or late device was added.");
        }
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();
            Check((string)db.ValidateMailboxEligibility(7,sender,8,snapshotId)["state"]=="ready","Unexpired snapshot was not durable across restart.");
            db.Execute("UPDATE mailbox_eligibility_snapshots SET validate_until=1 WHERE id='"+snapshotId+"';");
            Check((string)db.ValidateMailboxEligibility(7,sender,8,snapshotId)["state"]=="snapshot_unavailable","Expired snapshot survived validation after restart.");
            Check((string)db.ValidateMailboxEligibility(7,sender,8,Id(99).Substring(0,32))["state"]=="snapshot_unavailable","Unknown token leaked authority.");
            // All 33 are independently authenticated recipient-owned leases;
            // returning only 32 would falsely claim a complete target set.
            db.CreateSession("overflow-live-session",8,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
            for(int i=100;i<133;i++){string peer=Id(i);db.AddDevice(8,peer);db.TouchPresence(8,peer,"",0,"",0,"","",session:"overflow-live-session");}
            Check((string)db.MailboxEligibilitySnapshot(7,sender,8)["state"]=="too_many_eligible_devices","More than 32 candidates was silently truncated.");
            db.Execute("INSERT INTO account_blocks(blocker_id,blocked_id,created_utc) VALUES(8,7,1);");
            Reject(()=>db.MailboxEligibilitySnapshot(7,sender,8),"Blocked sender obtained new snapshot.");
            Reject(()=>db.ValidateMailboxEligibility(7,sender,8,snapshotId),"Blocked sender revalidated old snapshot.");
            db.Execute("DELETE FROM account_blocks WHERE blocker_id=8 AND blocked_id=7;");
            db.Execute("DELETE FROM friendships WHERE account_low=7 AND account_high=8;");
            Reject(()=>db.MailboxEligibilitySnapshot(7,sender,8),"Former friend obtained new snapshot.");
            Reject(()=>db.ValidateMailboxEligibility(7,sender,8,snapshotId),"Former friend revalidated old snapshot.");
        }
        Console.WriteLine("PASS: authenticated snapshot sender/device/friend scope, sorted union, immutable original set, revocation/removal, restart/expiry, 33-device fail-closed overflow.");
        return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
