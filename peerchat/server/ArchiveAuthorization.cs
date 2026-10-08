using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace PeerChat.Server {
// Public, signed authorization metadata only. No history, transport, timer,
// private key, archive plaintext, or second persistence owner.
internal static class ArchiveAuthorizationProof {
    internal const int MaxRecords=64,MaxDevices=32;
    internal static void Check(bool ok){if(!ok)throw new InvalidOperationException("Invalid archive authorization metadata.");}
    internal sealed class Reader {
        readonly byte[] wire;int offset=4;
        internal Reader(byte[] bytes,int maximum){
            Check(bytes!=null&&bytes.Length>=4&&bytes.Length<=maximum&&bytes[0]=='P'&&bytes[1]=='T'&&bytes[2]==1&&bytes[3]==31);wire=bytes;
        }
        internal ulong Number(){Check(wire.Length-offset>=8);ulong n=0;for(int i=0;i<8;i++)n|=(ulong)wire[offset++]<<(8*i);return n;}
        internal long Positive(){ulong n=Number();Check(n>0&&n<(ulong)long.MaxValue);return (long)n;}
        internal byte[] Bytes(int maximum){ulong size=Number();Check(size<=(ulong)maximum&&size<=(ulong)(wire.Length-offset));var b=new byte[(int)size];Buffer.BlockCopy(wire,offset,b,0,b.Length);offset+=b.Length;return b;}
        internal string Text(int maximum){return new UTF8Encoding(false,true).GetString(Bytes(maximum));}
        internal void Domain(string expected){Check(Text(64)==expected&&Number()==1);}
        internal void End(){Check(offset==wire.Length);}
    }
    internal sealed class Descriptor {
        internal long Account,Issued;
        internal string Device;
        internal byte[] Public,Signature,Payload,Wire;
        internal static Descriptor Parse(byte[] wire){
            var outer=new Reader(wire,1024);var d=new Descriptor{Wire=(byte[])wire.Clone()};
            d.Payload=outer.Bytes(768);d.Signature=outer.Bytes(64);outer.End();
            var r=new Reader(d.Payload,768);r.Domain("PeerChat/ArchiveDeviceDescriptor/v1");
            d.Account=r.Positive();d.Device=r.Text(64);d.Public=r.Bytes(72);d.Issued=r.Positive();r.End();
            Check(MailboxProtocol.Id(d.Device,64)&&MailboxProtocol.Verify(d.Payload,d.Signature,d.Public,d.Device));return d;
        }
    }
    internal sealed class Grant {
        internal long Account,Generation,Issued,Capabilities;
        internal string Issuer,Device,IssuanceId;
        internal bool Revoked;
        internal byte[] Public,Signature,Payload,Wire;
        internal static Grant Parse(byte[] wire,byte[] issuerPublic){
            var outer=new Reader(wire,2048);var g=new Grant{Wire=(byte[])wire.Clone()};
            g.Payload=outer.Bytes(1792);g.Signature=outer.Bytes(64);outer.End();
            var r=new Reader(g.Payload,1792);r.Domain("PeerChat/ArchiveAuthorization/v1");
            g.Account=r.Positive();g.Issuer=r.Text(64);g.Device=r.Text(64);g.Public=r.Bytes(72);
            ulong capabilities=r.Number();Check(capabilities<=3);g.Capabilities=(long)capabilities;
            g.Generation=r.Positive();g.Issued=r.Positive();g.IssuanceId=r.Text(32);
            ulong revoked=r.Number();Check(revoked<=1);g.Revoked=revoked==1;r.End();
            Check(MailboxProtocol.Id(g.Issuer,64)&&MailboxProtocol.Id(g.Device,64)&&MailboxProtocol.Id(g.IssuanceId,32)&&
                g.Public.Length==72&&MailboxProtocol.Digest(g.Public)==g.Device&&(g.Revoked?g.Capabilities==0:g.Capabilities>0)&&
                MailboxProtocol.Verify(g.Payload,g.Signature,issuerPublic,g.Issuer));return g;
        }
    }
}
sealed partial class AccountDatabase {
    bool archiveAuthorizationInitialized;
    internal void InitializeArchiveAuthorization(){lock(gate){if(archiveAuthorizationInitialized)return;Execute(
        "CREATE TABLE IF NOT EXISTS archive_device_descriptors(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,device TEXT NOT NULL,proof BLOB NOT NULL,PRIMARY KEY(account_id,device));"+
        "CREATE TABLE IF NOT EXISTS archive_device_grants(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,issuer TEXT NOT NULL,device TEXT NOT NULL,generation INTEGER NOT NULL,capabilities INTEGER NOT NULL,revoked INTEGER NOT NULL,disabled INTEGER NOT NULL DEFAULT 0,issuance_id TEXT NOT NULL,proof BLOB NOT NULL,PRIMARY KEY(account_id,issuer,device));"+
        "CREATE TRIGGER IF NOT EXISTS archive_grants_device_removed AFTER DELETE ON devices BEGIN UPDATE archive_device_grants SET disabled=1 WHERE account_id=OLD.account_id AND (issuer=OLD.public_identity OR device=OLD.public_identity); END;");archiveAuthorizationInitialized=true;}}
    string ArchivePredicate(long account,string device){return "account_id="+account+" AND device="+Quote(device);}
    internal string RegisterArchiveDescriptor(long account,string device,byte[] wire){lock(gate){
        var d=ArchiveAuthorizationProof.Descriptor.Parse(wire);
        if(d.Account!=account||d.Device!=device||!OwnsDevice(account,device))return "not_authorized";
        if(d.Issued>MailboxNow()+300000)return "invalid_timestamp";
        InitializeArchiveAuthorization();
        Execute("BEGIN IMMEDIATE;");try {
            string predicate=ArchivePredicate(account,device);
            var existing=MailboxBlob("SELECT proof FROM archive_device_descriptors WHERE "+predicate);
            if(existing!=null){
                var old=ArchiveAuthorizationProof.Descriptor.Parse(existing);
                if(!old.Public.SequenceEqual(d.Public)){Execute("ROLLBACK;");return "identity_conflict";}
                // Descriptor time is not an authorization generation. Keep one
                // immutable public identity; reconnect is idempotent.
                Execute("COMMIT;");return "accepted";
            }
            if(ScalarLong(database,"SELECT count(*) FROM archive_device_descriptors WHERE account_id="+account)>=ArchiveAuthorizationProof.MaxDevices){Execute("ROLLBACK;");return "capacity";}
            Execute("INSERT INTO archive_device_descriptors(account_id,device,proof) VALUES("+account+","+Quote(device)+","+MailboxData(wire)+");");
            Execute("COMMIT;");return "accepted";
        }catch{try{Execute("ROLLBACK;");}catch{}throw;}
    }}
    ArchiveAuthorizationProof.Descriptor ArchiveDescriptor(long account,string device){
        var wire=MailboxBlob("SELECT proof FROM archive_device_descriptors WHERE "+ArchivePredicate(account,device));
        return wire==null?null:ArchiveAuthorizationProof.Descriptor.Parse(wire);
    }
    internal string RegisterArchiveGrant(long account,string issuer,byte[] wire){lock(gate){
        if(!OwnsDevice(account,issuer))return "not_authorized";
        InitializeArchiveAuthorization();var authority=ArchiveDescriptor(account,issuer);
        if(authority==null)return "issuer_descriptor_required";
        var grant=ArchiveAuthorizationProof.Grant.Parse(wire,authority.Public);
        if(grant.Account!=account||grant.Issuer!=issuer)return "not_authorized";
        if(grant.Issued>MailboxNow()+300000)return "invalid_timestamp";
        var target=ArchiveDescriptor(account,grant.Device);
        if(target==null||!target.Public.SequenceEqual(grant.Public)||(!grant.Revoked&&!OwnsDevice(account,grant.Device)))return "target_not_authorized";
        // Server admission proves device/account/signature, NOT client consent
        // to this authority. Every endpoint independently pins the authorizer.
        Execute("BEGIN IMMEDIATE;");try {
            string predicate="account_id="+account+" AND issuer="+Quote(issuer)+" AND device="+Quote(grant.Device);
            var existing=MailboxBlob("SELECT proof FROM archive_device_grants WHERE "+predicate);
            long generation=ScalarLong(database,"SELECT generation FROM archive_device_grants WHERE "+predicate);
            if(existing!=null&&existing.SequenceEqual(wire)){
                bool disabled=ScalarLong(database,"SELECT disabled FROM archive_device_grants WHERE "+predicate)!=0;
                Execute("COMMIT;");return disabled?"device_removed_reenrollment_required":"accepted";
            }
            if(generation==long.MaxValue||grant.Generation!=generation+1){Execute("ROLLBACK;");return "stale_or_conflicting_generation";}
            if(existing==null&&ScalarLong(database,"SELECT count(*) FROM archive_device_grants WHERE account_id="+account)>=ArchiveAuthorizationProof.MaxRecords){Execute("ROLLBACK;");return "capacity";}
            Execute("INSERT OR REPLACE INTO archive_device_grants(account_id,issuer,device,generation,capabilities,revoked,disabled,issuance_id,proof) VALUES("+
                account+","+Quote(issuer)+","+Quote(grant.Device)+","+grant.Generation+","+grant.Capabilities+","+(grant.Revoked?1:0)+",0,"+Quote(grant.IssuanceId)+","+MailboxData(wire)+");");
            Execute("COMMIT;");return "accepted";
        }catch{try{Execute("ROLLBACK;");}catch{}throw;}
    }}
    internal List<Dictionary<string,object>> ArchiveDescriptors(long account,string device){lock(gate){
        ArchiveAuthorizationProof.Check(OwnsDevice(account,device));InitializeArchiveAuthorization();
        return MailboxRows("SELECT device,proof FROM archive_device_descriptors a WHERE account_id="+account+
            " AND EXISTS(SELECT 1 FROM devices d WHERE d.account_id=a.account_id AND d.public_identity=a.device) ORDER BY device LIMIT 32",32,"device","descriptor");
    }}
    internal List<Dictionary<string,object>> ArchiveGrants(long account,string device){lock(gate){
        ArchiveAuthorizationProof.Check(OwnsDevice(account,device));InitializeArchiveAuthorization();
        // Tombstones remain visible and durable. Never delete a revoked grant
        // and then allow its old generation to become 'first use' again.
        var rows=MailboxRows("SELECT issuer||':'||device,proof FROM archive_device_grants WHERE account_id="+account+
            " ORDER BY issuer,device LIMIT 64",64,"grantId","grant");
        foreach(var row in rows){string id=Convert.ToString(row["grantId"]);string issuer=id.Substring(0,64),subject=id.Substring(65);
            row["currentlyEligible"]=OwnsDevice(account,issuer)&&OwnsDevice(account,subject)&&ScalarLong(database,"SELECT count(*) FROM archive_device_grants WHERE account_id="+account+" AND issuer="+Quote(issuer)+" AND device="+Quote(subject)+" AND disabled=0 AND revoked=0")==1;}
        return rows;
    }}
    internal bool ArchiveAuthorized(long account,string issuer,string device,long generation,long capability){lock(gate){
        if(account<=0||generation<=0||capability<1||capability>3||!OwnsDevice(account,issuer)||!OwnsDevice(account,device))return false;
        InitializeArchiveAuthorization();
        return ScalarLong(database,"SELECT count(*) FROM archive_device_grants WHERE account_id="+account+" AND issuer="+Quote(issuer)+" AND device="+Quote(device)+
            " AND generation="+generation+" AND revoked=0 AND disabled=0 AND (capabilities & "+capability+")="+capability)==1;
    }}
}
sealed partial class DiscoveryServer {
    Dictionary<string,object> ArchiveAuthorizationRequest(long account,string device,string type,Dictionary<string,object> request){
        if(!ValidIdentity(device)||!accounts.OwnsDevice(account,device))return Error("archive_device_not_authorized");
        if(type=="archive_authorization_state")return Ok(new Dictionary<string,object>{
            {"archiveVersion",1},{"accountId",account},{"serverEpoch",accounts.Live.Epoch},
            {"devices",accounts.ArchiveDescriptors(account,device)},{"grants",accounts.ArchiveGrants(account,device)}});
        byte[] wire;
        try{wire=MailboxProtocol.Decode(Text(request,"proof"),type=="archive_descriptor_register"?1024:2048);}
        catch(FormatException){return Error("archive_invalid_proof");}
        catch(InvalidOperationException){return Error("archive_invalid_proof");}
        string result;
        try{result=type=="archive_descriptor_register"?accounts.RegisterArchiveDescriptor(account,device,wire):accounts.RegisterArchiveGrant(account,device,wire);}
        catch(InvalidOperationException){return Error("archive_invalid_proof_or_storage_failure");}
        catch(ArgumentException){return Error("archive_invalid_proof");}
        if(result!="accepted")return Error("archive_"+result);
        // Public metadata notification only, never a grant of UI/native trust.
        Notify(account,new Dictionary<string,object>{{"event","archive_authorization_changed"}});
        return Ok(new Dictionary<string,object>{{"state","accepted"},{"archiveVersion",1}});
    }
}
}
