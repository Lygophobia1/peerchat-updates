using System;
using System.Collections.Generic;
using System.Linq;

namespace PeerChat.Server {
// Application persistence only: no socket, session, timer, route or reconnect
// ownership. Called under the existing authenticated server request dispatch.
sealed partial class AccountDatabase {
    internal void InitializeMailbox(){lock(gate){Execute("PRAGMA synchronous=FULL;"+
        "CREATE TABLE IF NOT EXISTS mailbox_recipient_keys(device TEXT NOT NULL,key_id TEXT NOT NULL,account_id INTEGER NOT NULL,generation INTEGER NOT NULL,state TEXT NOT NULL,allowed INTEGER NOT NULL,proof BLOB NOT NULL,PRIMARY KEY(device,key_id),UNIQUE(device,generation));"+
        "CREATE TABLE IF NOT EXISTS encrypted_mailbox(sender_device TEXT NOT NULL,message_id TEXT NOT NULL,recipient_device TEXT NOT NULL,sender_account INTEGER NOT NULL,recipient_account INTEGER NOT NULL,key_id TEXT NOT NULL,generation INTEGER NOT NULL,digest TEXT NOT NULL,envelope BLOB,receipt BLOB,created INTEGER NOT NULL,expires INTEGER NOT NULL,PRIMARY KEY(sender_device,message_id,recipient_device));"+
        "CREATE INDEX IF NOT EXISTS mailbox_recipient ON encrypted_mailbox(recipient_device,created,message_id);"+
        "CREATE INDEX IF NOT EXISTS mailbox_sender_receipts ON encrypted_mailbox(sender_device,expires);");PurgeMailbox();}}
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
    List<Dictionary<string,object>> MailboxRows(string sql,int maximum,string idLabel,string wireLabel){
        var rows=new List<Dictionary<string,object>>();IntPtr statement=IntPtr.Zero;int bytes=0;
        try{if(SqliteNative.prepare(database,sql,out statement)!=0)throw new InvalidOperationException("Mailbox database read failed.");
            int result;while((result=SqliteNative.step(statement))==100){
                var wire=SqliteNative.columnBlob(statement,1);if(wire.Length>MailboxProtocol.MaxWire||rows.Count>=maximum)throw new InvalidOperationException("Mailbox response exceeds limit.");
                // Fits the existing bounded server frame; cursorless repeated fetch
                // returns remaining rows after real recipient ACK removes payloads.
                if(rows.Count>0&&bytes+wire.Length>64000)break;bytes+=wire.Length;
                rows.Add(new Dictionary<string,object>{{idLabel,SqliteNative.columnText(statement,0)},{wireLabel,Convert.ToBase64String(wire)}});
            }if(result!=100&&result!=101)throw new InvalidOperationException("Mailbox database read failed.");return rows;
        }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
    }
    internal List<Dictionary<string,object>> FetchMailbox(long account,string device){lock(gate){
        if(!OwnsDevice(account,device))throw new InvalidOperationException("Mailbox access not authorized.");PurgeMailbox();
        return MailboxRows("SELECT message_id,envelope FROM encrypted_mailbox m WHERE recipient_account="+account+" AND recipient_device="+Quote(device)+" AND envelope IS NOT NULL AND EXISTS(SELECT 1 FROM mailbox_recipient_keys k WHERE k.device=m.recipient_device AND k.key_id=m.key_id AND k.state IN ('active','retired')) AND EXISTS(SELECT 1 FROM friendships f WHERE f.account_low=MIN(m.sender_account,m.recipient_account) AND f.account_high=MAX(m.sender_account,m.recipient_account)) AND NOT EXISTS(SELECT 1 FROM account_blocks b WHERE (b.blocker_id=m.sender_account AND b.blocked_id=m.recipient_account) OR (b.blocker_id=m.recipient_account AND b.blocked_id=m.sender_account)) ORDER BY created,message_id LIMIT 64",64,"messageId","envelope");
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
    internal List<Dictionary<string,object>> MailboxReceipts(long account,string device,string afterMessage=""){lock(gate){
        if(!OwnsDevice(account,device))throw new InvalidOperationException("Mailbox access not authorized.");PurgeMailbox();
        if(afterMessage.Length>0&&!MailboxProtocol.Id(afterMessage,32))throw new InvalidOperationException("Invalid receipt cursor.");
        return MailboxRows("SELECT message_id,receipt FROM encrypted_mailbox WHERE sender_account="+account+" AND sender_device="+Quote(device)+" AND message_id>"+Quote(afterMessage)+" AND receipt IS NOT NULL ORDER BY message_id LIMIT 64",64,"messageId","receipt");
    }}
    internal long MailboxReceiptSender(MailboxProtocol.Receipt receipt){lock(gate){
        return ScalarLong(database,"SELECT sender_account FROM encrypted_mailbox WHERE sender_device="+Quote(receipt.SenderDevice)+" AND recipient_device="+Quote(receipt.Device)+" AND message_id="+Quote(receipt.Message)+" AND digest="+Quote(receipt.DigestHex)+" AND receipt IS NOT NULL");
    }}
}
}
