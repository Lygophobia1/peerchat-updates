using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace PeerChat.Server {
// Application persistence only: no socket, session, timer, route or reconnect
// ownership. Called under the existing authenticated server request dispatch.
sealed partial class AccountDatabase {
    internal void InitializeMailbox(){lock(gate){Execute("PRAGMA synchronous=FULL;"+
        "CREATE TABLE IF NOT EXISTS mailbox_recipient_keys(device TEXT NOT NULL,key_id TEXT NOT NULL,account_id INTEGER NOT NULL,generation INTEGER NOT NULL,state TEXT NOT NULL,allowed INTEGER NOT NULL,proof BLOB NOT NULL,PRIMARY KEY(device,key_id),UNIQUE(device,generation));"+
        "CREATE TABLE IF NOT EXISTS encrypted_mailbox(sender_device TEXT NOT NULL,message_id TEXT NOT NULL,recipient_device TEXT NOT NULL,sender_account INTEGER NOT NULL,recipient_account INTEGER NOT NULL,key_id TEXT NOT NULL,generation INTEGER NOT NULL,digest TEXT NOT NULL,envelope BLOB,receipt BLOB,created INTEGER NOT NULL,expires INTEGER NOT NULL,PRIMARY KEY(sender_device,message_id,recipient_device));"+
        "CREATE INDEX IF NOT EXISTS mailbox_recipient ON encrypted_mailbox(recipient_device,created,message_id);"+
        "CREATE INDEX IF NOT EXISTS mailbox_sender_receipts ON encrypted_mailbox(sender_device,expires);"+
        "CREATE TABLE IF NOT EXISTS mailbox_eligibility_snapshots(sequence INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT NOT NULL UNIQUE,sender_account INTEGER NOT NULL,sender_device TEXT NOT NULL,recipient_account INTEGER NOT NULL,issued INTEGER NOT NULL,fresh_until INTEGER NOT NULL,validate_until INTEGER NOT NULL,digest TEXT NOT NULL);"+
        "CREATE TABLE IF NOT EXISTS mailbox_eligibility_members(snapshot_id TEXT NOT NULL,device TEXT NOT NULL,was_online INTEGER NOT NULL,binding BLOB,PRIMARY KEY(snapshot_id,device));"+
        "CREATE INDEX IF NOT EXISTS mailbox_eligibility_expiry ON mailbox_eligibility_snapshots(validate_until);");PurgeMailbox();PurgeEligibilitySnapshots();}}
    byte[] MailboxBlob(string sql){IntPtr statement=IntPtr.Zero;try{
        if(SqliteNative.prepare(database,sql,out statement)!=0)throw new InvalidOperationException("Mailbox database read failed.");
        int result=SqliteNative.step(statement);if(result==101)return null;
        if(result!=100)throw new InvalidOperationException("Mailbox database read failed.");
        var bytes=SqliteNative.columnBlob(statement,0);if(bytes.Length>MailboxProtocol.MaxWire)throw new InvalidOperationException("Mailbox database record exceeds limit.");return bytes;
    }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}}
    static string MailboxData(byte[] bytes){return "X'"+Hex(bytes)+"'";}
    static long MailboxNow(){return DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();}
    void PurgeMailbox(){long now=MailboxNow();Execute("DELETE FROM encrypted_mailbox WHERE expires<="+now+" OR NOT EXISTS(SELECT 1 FROM devices d WHERE d.account_id=encrypted_mailbox.recipient_account AND d.public_identity=encrypted_mailbox.recipient_device) OR NOT EXISTS(SELECT 1 FROM devices d WHERE d.account_id=encrypted_mailbox.sender_account AND d.public_identity=encrypted_mailbox.sender_device);");}
    internal string RegisterMailboxKey(long account,string device,byte[] identity,byte[] wire){lock(gate){
        var binding=MailboxProtocol.Binding.Parse(wire);
        if(binding.Account!=account||binding.Device!=device||!OwnsDevice(account,device)||!MailboxProtocol.Verify(binding.Original,binding.Signature,identity,device))return "not_authorized";
        if(binding.Created>MailboxNow()+300000)return "invalid_timestamp";
        Execute("BEGIN IMMEDIATE;");try {
            string predicate="device="+Quote(device)+" AND key_id="+Quote(binding.Key);
            var same=MailboxBlob("SELECT proof FROM mailbox_recipient_keys WHERE "+predicate);
            if(same!=null){string state=ScalarText(database,"SELECT state FROM mailbox_recipient_keys WHERE "+predicate);Execute("COMMIT;");return same.SequenceEqual(wire)&&state=="active"?"accepted":"key_conflict_or_revoked";}
            long generation=ScalarLong(database,"SELECT COALESCE(MAX(generation),0) FROM mailbox_recipient_keys WHERE device="+Quote(device));
            string predecessor=ScalarText(database,"SELECT key_id FROM mailbox_recipient_keys WHERE device="+Quote(device)+" AND state='active'");
            if(generation==0?binding.Generation!=1||binding.Predecessor.Length!=0:binding.Generation!=generation+1||binding.Predecessor!=predecessor){Execute("ROLLBACK;");return "invalid_generation";}
            Execute("UPDATE mailbox_recipient_keys SET state='retired' WHERE device="+Quote(device)+" AND state='active';");
            Execute("INSERT INTO mailbox_recipient_keys(device,key_id,account_id,generation,state,allowed,proof) VALUES("+Quote(device)+","+Quote(binding.Key)+","+account+","+binding.Generation+",'active',"+(binding.Allowed?1:0)+","+MailboxData(wire)+");");
            Execute("COMMIT;");return "accepted";
        }catch{try{Execute("ROLLBACK;");}catch{}throw;}
    }}
    internal List<Dictionary<string,object>> MailboxKeys(long account,string device,long recipient){lock(gate){
        if(!OwnsDevice(account,device)||recipient==account||!Friends(account,recipient)||Blocked(account,recipient))throw new InvalidOperationException("Mailbox access not authorized.");
        return MailboxRows("SELECT device,proof FROM mailbox_recipient_keys k WHERE account_id="+recipient+" AND state='active' AND allowed=1 AND EXISTS(SELECT 1 FROM devices d WHERE d.account_id=k.account_id AND d.public_identity=k.device) ORDER BY device LIMIT 32",32,"device","binding");
    }}
    const long EligibilityFreshMilliseconds=120000;
    const long EligibilityValidationMilliseconds=600000;
    const int EligibilityMaximumDevices=32;
    sealed class EligibilityMember {
        internal string Device;
        internal bool Online;
        internal byte[] Binding;
    }
    void PurgeEligibilitySnapshots(){
        long now=MailboxNow();
        Execute("BEGIN IMMEDIATE;");try{
            Execute("DELETE FROM mailbox_eligibility_members WHERE snapshot_id IN (SELECT id FROM mailbox_eligibility_snapshots WHERE validate_until<="+now+");"+
                "DELETE FROM mailbox_eligibility_snapshots WHERE validate_until<="+now+";"+
                "DELETE FROM mailbox_eligibility_members WHERE NOT EXISTS(SELECT 1 FROM mailbox_eligibility_snapshots s WHERE s.id=mailbox_eligibility_members.snapshot_id);");
            Execute("COMMIT;");
        }catch{try{Execute("ROLLBACK;");}catch{}throw;}
    }
    bool MailboxFriendScope(long account,string device,long recipient){
        return account>0&&recipient>0&&recipient!=account&&OwnsDevice(account,device)&&Friends(account,recipient)&&!Blocked(account,recipient);
    }
    HashSet<string> AuthenticatedRecipientPresence(long recipient){
        var result=new HashSet<string>(StringComparer.Ordinal);
        foreach(var lease in Live.Snapshot(recipient)){
            // A bare remembered presence row or an expired/revoked session is not
            // an authenticated recipient device eligibility claim.
            if(lease.Account==recipient&&MailboxProtocol.Id(lease.Identity,64)&&OwnsDevice(recipient,lease.Identity)&&
               !String.IsNullOrEmpty(lease.Session)&&Authenticate(lease.Session)==recipient&&SessionAllowsDevice(lease.Session,lease.Identity))result.Add(lease.Identity);
        }
        return result;
    }
    List<EligibilityMember> EligibilityBindings(long recipient){
        var result=new List<EligibilityMember>();IntPtr statement=IntPtr.Zero;
        try{
            string sql="SELECT k.device,k.proof FROM mailbox_recipient_keys k WHERE k.account_id="+recipient+
                " AND k.state='active' AND k.allowed=1 AND EXISTS(SELECT 1 FROM devices d WHERE d.account_id=k.account_id AND d.public_identity=k.device) ORDER BY k.device LIMIT 33";
            if(SqliteNative.prepare(database,sql,out statement)!=0)throw new InvalidOperationException("Eligibility read failed.");
            int step;while((step=SqliteNative.step(statement))==100){
                byte[] proof=SqliteNative.columnBlob(statement,1);
                if(proof.Length>4096||result.Count>=EligibilityMaximumDevices)throw new InvalidOperationException("Eligibility exceeds bound.");
                result.Add(new EligibilityMember{Device=SqliteNative.columnText(statement,0),Binding=proof});
            }
            if(step!=101)throw new InvalidOperationException("Eligibility read failed.");
            return result;
        }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
    }
    List<EligibilityMember> CurrentEligibility(long recipient){
        var members=EligibilityBindings(recipient).ToDictionary(x=>x.Device,x=>x,StringComparer.Ordinal);
        foreach(string device in AuthenticatedRecipientPresence(recipient)){
            EligibilityMember row;if(!members.TryGetValue(device,out row))members.Add(device,new EligibilityMember{Device=device,Online=true});
            else row.Online=true;
            if(members.Count>EligibilityMaximumDevices)throw new InvalidOperationException("Eligibility exceeds bound.");
        }
        return members.Values.OrderBy(x=>x.Device,StringComparer.Ordinal).ToList();
    }
    static string EligibilityDigest(IEnumerable<EligibilityMember> members){
        var canonical=new StringBuilder();foreach(var member in members){
            canonical.Append(member.Device).Append('|').Append(member.Online?'1':'0').Append('|');
            if(member.Binding!=null)canonical.Append(MailboxProtocol.Digest(member.Binding));
            canonical.Append('\n');
        }
        return MailboxProtocol.Digest(Encoding.UTF8.GetBytes(canonical.ToString()));
    }
    static List<Dictionary<string,object>> EligibilityResponse(IEnumerable<EligibilityMember> members,bool validated){
        var result=new List<Dictionary<string,object>>();foreach(var member in members){
            var row=new Dictionary<string,object>{{"device",member.Device},{"online",member.Online},{"binding",member.Binding==null?"":Convert.ToBase64String(member.Binding)}};
            if(validated)row["eligible"]=member.Online||member.Binding!=null;
            result.Add(row);
        }return result;
    }
    internal Dictionary<string,object> MailboxEligibilitySnapshot(long account,string device,long recipient){lock(gate){
        if(!MailboxFriendScope(account,device,recipient))throw new InvalidOperationException("Mailbox access not authorized.");
        PurgeEligibilitySnapshots();
        // Never return a truncated candidate set. The caller must not mistake a
        // partial page for a complete pre-send frozen-target upper bound.
        List<EligibilityMember> members;
        try{members=CurrentEligibility(recipient);}catch(InvalidOperationException e){
            if(e.Message=="Eligibility exceeds bound.")return new Dictionary<string,object>{{"state","too_many_eligible_devices"}};
            throw;
        }
        long now=MailboxNow();
        string digest=EligibilityDigest(members);
        // Repeated authenticated refreshes of an unchanged audience reuse the
        // immutable proof, not its lifetime. Re-evaluate authorization and the
        // complete audience above first; keep enough freshness for older clients
        // that refresh on a fixed 60-second cadence plus bounded scheduling delay.
        string reuse="sender_account="+account+" AND sender_device="+Quote(device)+
            " AND recipient_account="+recipient+" AND digest="+Quote(digest)+
            " AND fresh_until>="+(now+90000)+" AND validate_until>"+now;
        string existing=ScalarText(database,"SELECT id FROM mailbox_eligibility_snapshots WHERE "+reuse+" ORDER BY sequence DESC LIMIT 1");
        if(!String.IsNullOrEmpty(existing)){
            string where="id="+Quote(existing);
            return new Dictionary<string,object>{{"state","ready"},{"snapshotVersion",1},{"snapshotId",existing},
                {"snapshotSequence",ScalarLong(database,"SELECT sequence FROM mailbox_eligibility_snapshots WHERE "+where)},
                {"senderAccountId",account},{"senderDevice",device},{"recipientAccountId",recipient},
                {"issuedUtcMs",ScalarLong(database,"SELECT issued FROM mailbox_eligibility_snapshots WHERE "+where)},
                {"freshUntilUtcMs",ScalarLong(database,"SELECT fresh_until FROM mailbox_eligibility_snapshots WHERE "+where)},
                {"validateUntilUtcMs",ScalarLong(database,"SELECT validate_until FROM mailbox_eligibility_snapshots WHERE "+where)},
                {"eligibilityDigest",digest},{"candidates",EligibilityResponse(members,false)}};
        }
        if(ScalarLong(database,"SELECT count(*) FROM mailbox_eligibility_snapshots")>=4096||
           ScalarLong(database,"SELECT count(*) FROM mailbox_eligibility_snapshots WHERE sender_account="+account+" AND sender_device="+Quote(device))>=64)
            return new Dictionary<string,object>{{"state","snapshot_quota_exceeded"}};
        long freshUntil=now+EligibilityFreshMilliseconds,validateUntil=now+EligibilityValidationMilliseconds;
        string id=Guid.NewGuid().ToString("N");
        Execute("BEGIN IMMEDIATE;");try{
            Execute("INSERT INTO mailbox_eligibility_snapshots(id,sender_account,sender_device,recipient_account,issued,fresh_until,validate_until,digest) VALUES("+
                Quote(id)+","+account+","+Quote(device)+","+recipient+","+now+","+freshUntil+","+validateUntil+","+Quote(digest)+");");
            long sequence=ScalarLong(database,"SELECT last_insert_rowid()");
            foreach(var member in members)Execute("INSERT INTO mailbox_eligibility_members(snapshot_id,device,was_online,binding) VALUES("+
                Quote(id)+","+Quote(member.Device)+","+(member.Online?1:0)+","+(member.Binding==null?"NULL":MailboxData(member.Binding))+");");
            Execute("COMMIT;");
            return new Dictionary<string,object>{{"state","ready"},{"snapshotVersion",1},{"snapshotId",id},{"snapshotSequence",sequence},
                {"senderAccountId",account},{"senderDevice",device},{"recipientAccountId",recipient},{"issuedUtcMs",now},{"freshUntilUtcMs",freshUntil},{"validateUntilUtcMs",validateUntil},
                {"eligibilityDigest",digest},{"candidates",EligibilityResponse(members,false)}};
        }catch{try{Execute("ROLLBACK;");}catch{}throw;}
    }}
    internal Dictionary<string,object> ValidateMailboxEligibility(long account,string device,long recipient,string id){lock(gate){
        if(!MailboxProtocol.Id(id,32)||!MailboxFriendScope(account,device,recipient))throw new InvalidOperationException("Mailbox access not authorized.");
        PurgeEligibilitySnapshots();
        string where="id="+Quote(id)+" AND sender_account="+account+" AND sender_device="+Quote(device)+" AND recipient_account="+recipient;
        if(ScalarLong(database,"SELECT count(*) FROM mailbox_eligibility_snapshots WHERE "+where)!=1)
            return new Dictionary<string,object>{{"state","snapshot_unavailable"}};
        var originally=new List<EligibilityMember>();IntPtr statement=IntPtr.Zero;
        try{
            if(SqliteNative.prepare(database,"SELECT device,was_online,binding FROM mailbox_eligibility_members WHERE snapshot_id="+Quote(id)+" ORDER BY device",out statement)!=0)throw new InvalidOperationException("Eligibility read failed.");
            int step;while((step=SqliteNative.step(statement))==100){
                if(originally.Count>=EligibilityMaximumDevices)throw new InvalidOperationException("Eligibility exceeds bound.");
                byte[] proof=SqliteNative.columnBlob(statement,2);
                originally.Add(new EligibilityMember{Device=SqliteNative.columnText(statement,0),Online=SqliteNative.columnLong(statement,1)==1,Binding=proof.Length==0?null:proof});
            }if(step!=101)throw new InvalidOperationException("Eligibility read failed.");
        }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
        string originalDigest=ScalarText(database,"SELECT digest FROM mailbox_eligibility_snapshots WHERE "+where);
        if(originalDigest!=EligibilityDigest(originally))return new Dictionary<string,object>{{"state","snapshot_unavailable"}};
        var live=AuthenticatedRecipientPresence(recipient);
        var current=new List<EligibilityMember>();foreach(var original in originally){
            bool owned=OwnsDevice(recipient,original.Device);
            byte[] binding=null;
            if(owned&&original.Binding!=null){
                if(ScalarLong(database,"SELECT count(*) FROM mailbox_recipient_keys WHERE account_id="+recipient+" AND device="+Quote(original.Device)+" AND state='active' AND allowed=1 AND proof="+MailboxData(original.Binding))==1)binding=original.Binding;
            }
            current.Add(new EligibilityMember{Device=original.Device,Online=owned&&live.Contains(original.Device),Binding=binding});
        }
        return new Dictionary<string,object>{{"state","ready"},{"snapshotVersion",1},{"snapshotId",id},{"snapshotSequence",ScalarLong(database,"SELECT sequence FROM mailbox_eligibility_snapshots WHERE "+where)},
            {"senderAccountId",account},{"senderDevice",device},{"recipientAccountId",recipient},{"issuedUtcMs",ScalarLong(database,"SELECT issued FROM mailbox_eligibility_snapshots WHERE "+where)},
            {"freshUntilUtcMs",ScalarLong(database,"SELECT fresh_until FROM mailbox_eligibility_snapshots WHERE "+where)},
            {"validateUntilUtcMs",ScalarLong(database,"SELECT validate_until FROM mailbox_eligibility_snapshots WHERE "+where)},
            {"eligibilityDigest",originalDigest},{"candidates",EligibilityResponse(current,true)}};
    }}
    internal string RevokeMailboxKey(long account,string device,byte[] identity,byte[] wire){lock(gate){
        var revoke=MailboxProtocol.Revocation.Parse(wire);
        if(revoke.Account!=account||revoke.Device!=device||!OwnsDevice(account,device)||!MailboxProtocol.Verify(revoke.Original,revoke.Signature,identity,device))return "not_authorized";
        string predicate="account_id="+account+" AND device="+Quote(device)+" AND key_id="+Quote(revoke.Key)+" AND generation="+revoke.Generation;
        if(ScalarLong(database,"SELECT count(*) FROM mailbox_recipient_keys WHERE "+predicate)!=1)return "unknown_key";
        Execute("BEGIN IMMEDIATE;");try {
            Execute("UPDATE mailbox_recipient_keys SET state='revoked' WHERE "+predicate+";");
            Execute("DELETE FROM encrypted_mailbox WHERE recipient_account="+account+" AND recipient_device="+Quote(device)+" AND key_id="+Quote(revoke.Key)+" AND envelope IS NOT NULL;");
            Execute("COMMIT;");return "revoked";
        }catch{try{Execute("ROLLBACK;");}catch{}throw;}
    }}
    // Client storage consent is signed into both the registration and envelope.
    // A server-session credential alone cannot substitute the recipient key.
    internal string StoreMailbox(long account,string device,byte[] identity,byte[] wire){lock(gate){
        var e=MailboxProtocol.Envelope.Parse(wire);long now=MailboxNow();PurgeMailbox();
        if(e.Sender!=account||e.SenderDevice!=device||!OwnsDevice(account,device)||!OwnsDevice(e.Recipient,e.RecipientDevice)||!Friends(account,e.Recipient)||Blocked(account,e.Recipient)||!MailboxProtocol.Verify(e.Original,e.Signature,identity,device))return "not_authorized";
        if(e.Created>now+300000||e.Expires<=now)return "expired_or_invalid_timestamp";
        string destination="device="+Quote(e.RecipientDevice)+" AND key_id="+Quote(e.Key)+" AND account_id="+e.Recipient+" AND generation="+e.Generation;
        string keyState=ScalarText(database,"SELECT state FROM mailbox_recipient_keys WHERE "+destination+" AND allowed=1");
        if(keyState!="active"&&keyState!="retired")return "recipient_key_unavailable";
        string predicate="sender_device="+Quote(device)+" AND message_id="+Quote(e.Message)+" AND recipient_device="+Quote(e.RecipientDevice);
        string digest=MailboxProtocol.Digest(wire),old=ScalarText(database,"SELECT digest FROM encrypted_mailbox WHERE "+predicate);
        if(old!=null)return old==digest?"stored":"conflicting_message";
        if(keyState!="active")return "recipient_key_retired";
        Execute("BEGIN IMMEDIATE;");try {
            long count=ScalarLong(database,"SELECT count(*) FROM encrypted_mailbox WHERE recipient_device="+Quote(e.RecipientDevice)+" AND envelope IS NOT NULL");
            long bytes=ScalarLong(database,"SELECT COALESCE(SUM(length(envelope)),0) FROM encrypted_mailbox WHERE recipient_device="+Quote(e.RecipientDevice));
            long totalCount=ScalarLong(database,"SELECT count(*) FROM encrypted_mailbox"),total=ScalarLong(database,"SELECT COALESCE(SUM(length(envelope)+COALESCE(length(receipt),0)),0) FROM encrypted_mailbox");
            if(count>=256||bytes+wire.Length>8*1024*1024||totalCount>=4096||total+wire.Length>64*1024*1024){Execute("ROLLBACK;");return "quota_exceeded";}
            Execute("INSERT INTO encrypted_mailbox(sender_device,message_id,recipient_device,sender_account,recipient_account,key_id,generation,digest,envelope,created,expires) VALUES("+Quote(device)+","+Quote(e.Message)+","+Quote(e.RecipientDevice)+","+account+","+e.Recipient+","+Quote(e.Key)+","+e.Generation+","+Quote(digest)+","+MailboxData(wire)+","+e.Created+","+e.Expires+");");
            Execute("COMMIT;");return "stored";
        }catch{try{Execute("ROLLBACK;");}catch{}throw;}
    }}
    List<Dictionary<string,object>> MailboxRows(string sql,int maximum,string idLabel,string wireLabel,bool includeCursor=false,bool receipt=false){
        var rows=new List<Dictionary<string,object>>();IntPtr statement=IntPtr.Zero;int bytes=0;
        try{if(SqliteNative.prepare(database,sql,out statement)!=0)throw new InvalidOperationException("Mailbox database read failed.");
            int result;while((result=SqliteNative.step(statement))==100){
                var wire=SqliteNative.columnBlob(statement,1);if(wire.Length>MailboxProtocol.MaxWire||rows.Count>=maximum)throw new InvalidOperationException("Mailbox response exceeds limit.");
                // Fits the existing bounded server frame; cursorless repeated fetch
                // returns remaining rows after real recipient ACK removes payloads.
                if(rows.Count>0&&bytes+wire.Length>64000)break;bytes+=wire.Length;
                var item=new Dictionary<string,object>{{idLabel,SqliteNative.columnText(statement,0)},{wireLabel,Convert.ToBase64String(wire)}};
                if(includeCursor){item["cursor"]=SqliteNative.columnText(statement,2);if(receipt)item["recipientDevice"]=SqliteNative.columnText(statement,3);}
                rows.Add(item);
            }if(result!=100&&result!=101)throw new InvalidOperationException("Mailbox database read failed.");return rows;
        }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
    }
    // The cursor is scoped by the authenticated account and destination device in
    // the query. It names a position, never an authorization grant or ACK.
    internal List<Dictionary<string,object>> FetchMailbox(long account,string device,string cursor=""){lock(gate){
        if(!OwnsDevice(account,device))throw new InvalidOperationException("Mailbox access not authorized.");PurgeMailbox();
        string after="";
        if(cursor.Length>0){var parts=cursor.Split(':');long created;
            if(parts.Length!=5||parts[0]!=account.ToString()||parts[1]!=device||!Int64.TryParse(parts[2],out created)||created<=0||parts[2]!=created.ToString()||!MailboxProtocol.Id(parts[3],32)||!MailboxProtocol.Id(parts[4],64))throw new InvalidOperationException("Invalid mailbox cursor.");
            after=" AND (m.created>"+created+" OR (m.created="+created+" AND (m.message_id>"+Quote(parts[3])+" OR (m.message_id="+Quote(parts[3])+" AND m.sender_device>"+Quote(parts[4])+"))))";
        }
        return MailboxRows("SELECT message_id,envelope,'"+account+":"+device+":'||CAST(created AS TEXT)||':'||message_id||':'||sender_device FROM encrypted_mailbox m WHERE recipient_account="+account+" AND recipient_device="+Quote(device)+" AND envelope IS NOT NULL AND EXISTS(SELECT 1 FROM mailbox_recipient_keys k WHERE k.device=m.recipient_device AND k.key_id=m.key_id AND k.state IN ('active','retired')) AND EXISTS(SELECT 1 FROM friendships f WHERE f.account_low=MIN(m.sender_account,m.recipient_account) AND f.account_high=MAX(m.sender_account,m.recipient_account)) AND NOT EXISTS(SELECT 1 FROM account_blocks b WHERE (b.blocker_id=m.sender_account AND b.blocked_id=m.recipient_account) OR (b.blocker_id=m.recipient_account AND b.blocked_id=m.sender_account))"+after+" ORDER BY created,message_id,sender_device LIMIT 64",64,"messageId","envelope",true);
    }}
    internal string AckMailbox(long account,string device,byte[] identity,byte[] wire){lock(gate){
        var ack=MailboxProtocol.Receipt.Parse(wire);PurgeMailbox();
        if(ack.Recipient!=account||ack.Device!=device||!OwnsDevice(account,device)||!MailboxProtocol.Verify(ack.Original,ack.Signature,identity,device))return "not_authorized";
        string predicate="recipient_account="+account+" AND recipient_device="+Quote(device)+" AND sender_device="+Quote(ack.SenderDevice)+" AND message_id="+Quote(ack.Message)+" AND digest="+Quote(ack.DigestHex);
        if(ScalarLong(database,"SELECT count(*) FROM encrypted_mailbox WHERE "+predicate)!=1)return "unknown_envelope";
        var prior=MailboxBlob("SELECT receipt FROM encrypted_mailbox WHERE "+predicate);
        // The durable signed receipt is the only source of Delivered. A duplicate
        // valid signature may differ (ECDSA randomness); retain the first proof.
        if(prior!=null&&prior.Length>0)return "acknowledged";
        Execute("UPDATE encrypted_mailbox SET receipt="+MailboxData(wire)+",envelope=NULL,expires="+(MailboxNow()+MailboxProtocol.MaxTtl)+" WHERE "+predicate+";");return "acknowledged";
    }}
    internal List<Dictionary<string,object>> MailboxReceipts(long account,string device,string afterMessage="",string cursor=null){lock(gate){
        if(!OwnsDevice(account,device))throw new InvalidOperationException("Mailbox access not authorized.");PurgeMailbox();
        string after;
        if(cursor!=null){after="";if(cursor.Length>0){var parts=cursor.Split(':');
            if(parts.Length!=4||parts[0]!=account.ToString()||parts[1]!=device||!MailboxProtocol.Id(parts[2],32)||!MailboxProtocol.Id(parts[3],64))throw new InvalidOperationException("Invalid receipt cursor.");
            after=" AND (message_id>"+Quote(parts[2])+" OR (message_id="+Quote(parts[2])+" AND recipient_device>"+Quote(parts[3])+"))";
        }}else{
            if(afterMessage.Length>0&&!MailboxProtocol.Id(afterMessage,32))throw new InvalidOperationException("Invalid receipt cursor.");
            after=" AND message_id>"+Quote(afterMessage);
        }
        return MailboxRows("SELECT message_id,receipt,'"+account+":"+device+":'||message_id||':'||recipient_device,recipient_device FROM encrypted_mailbox WHERE sender_account="+account+" AND sender_device="+Quote(device)+after+" AND receipt IS NOT NULL ORDER BY message_id,recipient_device LIMIT 64",64,"messageId","receipt",true,true);
    }}
    internal long MailboxReceiptSender(MailboxProtocol.Receipt receipt){lock(gate){
        return ScalarLong(database,"SELECT sender_account FROM encrypted_mailbox WHERE sender_device="+Quote(receipt.SenderDevice)+" AND recipient_device="+Quote(receipt.Device)+" AND message_id="+Quote(receipt.Message)+" AND digest="+Quote(receipt.DigestHex)+" AND receipt IS NOT NULL");
    }}
}
}
