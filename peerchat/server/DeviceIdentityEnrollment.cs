using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace PeerChat.Server {
    // Public identity evidence only. These rows never confer account, mailbox, or cache access.
    sealed partial class AccountDatabase {
        internal void InitializeDeviceIdentity(){Execute(
            "CREATE TABLE IF NOT EXISTS identity_possessions(device TEXT PRIMARY KEY REFERENCES devices(public_identity) ON DELETE CASCADE,account INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,proof TEXT NOT NULL,key_blob TEXT NOT NULL);"+
            "CREATE TABLE IF NOT EXISTS identity_roots(account INTEGER PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,device TEXT NOT NULL,proof TEXT NOT NULL);"+
            "CREATE TABLE IF NOT EXISTS identity_endorsements(account INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,target TEXT NOT NULL,proof TEXT NOT NULL,PRIMARY KEY(account,target));"+
            "CREATE TABLE IF NOT EXISTS identity_challenges(id TEXT PRIMARY KEY,kind TEXT NOT NULL,account INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,actor TEXT NOT NULL,target TEXT NOT NULL,session TEXT NOT NULL,revision INTEGER NOT NULL,issued INTEGER NOT NULL,expires INTEGER NOT NULL,payload TEXT NOT NULL);");}
        static string IdentityNonce(){byte[] bytes=new byte[32];using(var rng=RandomNumberGenerator.Create())rng.GetBytes(bytes);return Hex(bytes);}
        static void IdentityNumber(Stream stream,long number){if(number<0)throw new InvalidOperationException("device_identity_invalid");for(int i=7;i>=0;i--)stream.WriteByte((byte)((ulong)number>>(i*8)));}
        static void IdentityField(Stream stream,byte[] value){if(value==null)throw new InvalidOperationException("device_identity_invalid");uint size=(uint)value.Length;for(int i=3;i>=0;i--)stream.WriteByte((byte)(size>>(i*8)));stream.Write(value,0,value.Length);}
        static void IdentityText(Stream stream,string value){IdentityField(stream,Encoding.UTF8.GetBytes(value));}
        static byte[] IdentityPayload(string kind,byte[] possession,string authority,byte[] authorityKey,long revision,string issuance,string session,long issued,long expires){
            using(var stream=new MemoryStream()){
                byte[] domain=Encoding.ASCII.GetBytes(kind=="authority"?"PeerChat/account-device-authority/v1\0":"PeerChat/account-device-endorsement/v1\0");stream.Write(domain,0,domain.Length);
                IdentityField(stream,possession);
                if(kind=="endorse"){IdentityText(stream,authority);IdentityField(stream,authorityKey);}
                IdentityNumber(stream,1);if(kind=="endorse")IdentityNumber(stream,1);
                IdentityNumber(stream,revision);IdentityText(stream,issuance);IdentityText(stream,session);IdentityNumber(stream,issued);IdentityNumber(stream,expires);return stream.ToArray();
            }
        }
        string IdentityPossession(string device){return ScalarText(database,"SELECT proof FROM identity_possessions WHERE device="+Quote(device));}
        string IdentityKey(string device){return ScalarText(database,"SELECT key_blob FROM identity_possessions WHERE device="+Quote(device));}
        internal bool RegisterIdentityPossession(long account,string actor,string session,string server,DeviceIdentityPossession proof){lock(gate){
            if(!ManagementActor(account,actor,session)||proof.Account!=account||proof.Device!=actor||proof.Server!=server)throw new InvalidOperationException("device_identity_not_authorized");
            Execute("BEGIN IMMEDIATE;");try{
                if(!ManagementActor(account,actor,session))throw new InvalidOperationException("device_identity_not_authorized");
                string encoded=Convert.ToBase64String(proof.Wire),existing=IdentityPossession(actor);
                if(existing!=null){
                    // ECDSA signatures need not be deterministic. Equivalent verified
                    // self proofs retain the first published evidence and revision.
                    if(IdentityKey(actor)!=Convert.ToBase64String(proof.Public))throw new InvalidOperationException("device_identity_immutable");
                    Execute("COMMIT;");return false;
                }
                if(ScalarLong(database,"SELECT count(*) FROM identity_possessions WHERE account="+account)>=32)throw new InvalidOperationException("device_identity_limit");
                Execute("INSERT INTO identity_possessions(device,account,proof,key_blob) VALUES("+Quote(actor)+","+account+","+Quote(encoded)+","+Quote(Convert.ToBase64String(proof.Public))+");");AdvanceDeviceAuthority(account);Execute("COMMIT;");return true;
            }catch{Execute("ROLLBACK;");throw;}
        }}
        internal byte[] IssueIdentityChallenge(long account,string actor,string target,string session,string server,string kind){lock(gate){
            if(!ManagementActor(account,actor,session)||!MailboxProtocol.Id(server,64))throw new InvalidOperationException("device_identity_session_required");
            Execute("BEGIN IMMEDIATE;");try{
                long now=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                Execute("DELETE FROM identity_challenges WHERE expires<="+now+";");
                if(ScalarLong(database,"SELECT count(*) FROM identity_challenges")>=1024||ScalarLong(database,"SELECT count(*) FROM identity_challenges WHERE account="+account)>=4)throw new InvalidOperationException("device_identity_challenge_limit");
                string root=ScalarText(database,"SELECT device FROM identity_roots WHERE account="+account);
                if(kind=="authority"){
                    if(root!=null||actor!=target)throw new InvalidOperationException("device_identity_authority_exists");
                }else if(kind=="endorse"){
                    if(root!=actor||!ActiveDevice(account,root)||actor==target||ScalarLong(database,"SELECT count(*) FROM identity_endorsements WHERE account="+account+" AND target="+Quote(target))!=0||ScalarLong(database,"SELECT count(*) FROM identity_endorsements WHERE account="+account)>=31)throw new InvalidOperationException("device_identity_endorse_denied");
                }else throw new InvalidOperationException("device_identity_invalid");
                if(!ActiveDevice(account,target))throw new InvalidOperationException("device_identity_target_inactive");
                string encoded=IdentityPossession(target);if(encoded==null)throw new InvalidOperationException("device_identity_possession_missing");
                string key=IdentityKey(actor);if(kind=="endorse"&&key==null)throw new InvalidOperationException("device_identity_possession_missing");
                string nonce=IdentityNonce();long revision=DeviceAuthorityRevision(account),expires=now+60000;
                byte[] payload=IdentityPayload(kind,Convert.FromBase64String(encoded),kind=="endorse"?actor:"",kind=="endorse"?Convert.FromBase64String(key):null,revision,nonce,session,now,expires);
                if(payload.Length>1792)throw new InvalidOperationException("device_identity_limit");
                Execute("INSERT INTO identity_challenges(id,kind,account,actor,target,session,revision,issued,expires,payload) VALUES("+Quote(nonce)+","+Quote(kind)+","+account+","+Quote(actor)+","+Quote(target)+","+Quote(session)+","+revision+","+now+","+expires+","+Quote(Convert.ToBase64String(payload))+");COMMIT;");return payload;
            }catch{Execute("ROLLBACK;");throw;}
        }}
        internal bool CommitIdentity(long account,string actor,string session,string server,string kind,byte[] wire){lock(gate){
            DeviceIdentityAuthority authority=null;DeviceIdentityEndorsement endorsement=null;
            if(kind=="authority")authority=DeviceIdentityAuthority.Parse(wire);else if(kind=="endorse")endorsement=DeviceIdentityEndorsement.Parse(wire);else throw new InvalidOperationException("device_identity_invalid");
            var target=kind=="authority"?authority.Possession:endorsement.Target;
            string nonce=kind=="authority"?authority.Issuance:endorsement.Issuance;
            string bound=kind=="authority"?authority.Session:endorsement.Session;
            long revision=kind=="authority"?authority.Revision:endorsement.Revision;
            long issued=kind=="authority"?authority.Issued:endorsement.Issued;
            long expires=kind=="authority"?authority.Expires:endorsement.Expires;
            string encoded=Convert.ToBase64String(wire);
            if(target.Server!=server||target.Account!=account||bound!=session||!MailboxProtocol.Id(nonce,64)||
                (kind=="authority"?actor!=target.Device:actor!=endorsement.Authority))throw new InvalidOperationException("device_identity_proof_invalid");
            // Durable proof itself is the exact success outcome. It survives challenge deletion/restart.
            string prior=kind=="authority"?ScalarText(database,"SELECT proof FROM identity_roots WHERE account="+account):ScalarText(database,"SELECT proof FROM identity_endorsements WHERE account="+account+" AND target="+Quote(target.Device));
            if(prior!=null){if(prior==encoded&&ManagementActor(account,actor,session)&&ActiveDevice(account,target.Device))return false;throw new InvalidOperationException("device_identity_immutable");}
            Execute("BEGIN IMMEDIATE;");try{
                long now=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();
                if(!ManagementActor(account,actor,session)||!ActiveDevice(account,target.Device)||revision!=DeviceAuthorityRevision(account)||now<issued||now>=expires||IdentityPossession(target.Device)!=Convert.ToBase64String(target.Wire))throw new InvalidOperationException("device_identity_not_authorized");
                if(ScalarLong(database,"SELECT count(*) FROM identity_challenges WHERE id="+Quote(nonce)+" AND kind="+Quote(kind)+" AND account="+account+" AND actor="+Quote(actor)+" AND target="+Quote(target.Device)+" AND session="+Quote(session)+" AND revision="+revision+" AND issued="+issued+" AND expires="+expires+" AND payload="+Quote(Convert.ToBase64String(IdentityPayload(kind,target.Wire,kind=="endorse"?actor:"",kind=="endorse"?Convert.FromBase64String(IdentityKey(actor)??""):null,revision,nonce,session,issued,expires)))+";")!=1)throw new InvalidOperationException("device_identity_challenge_invalid");
                string root=ScalarText(database,"SELECT device FROM identity_roots WHERE account="+account);
                if(kind=="authority"){
                    if(root!=null||actor!=target.Device||authority.Generation!=1)throw new InvalidOperationException("device_identity_authority_exists");
                    Execute("INSERT INTO identity_roots(account,device,proof) VALUES("+account+","+Quote(actor)+","+Quote(encoded)+");");
                }else{
                    if(root!=actor||!ActiveDevice(account,root)||endorsement.Authority!=actor||endorsement.AuthorityGeneration!=1||endorsement.TargetGeneration!=1||!endorsement.AuthorityKey.SequenceEqual(Convert.FromBase64String(IdentityKey(actor)??""))||ScalarLong(database,"SELECT count(*) FROM identity_endorsements WHERE account="+account)>=31)throw new InvalidOperationException("device_identity_endorse_denied");
                    Execute("INSERT INTO identity_endorsements(account,target,proof) VALUES("+account+","+Quote(target.Device)+","+Quote(encoded)+");");
                }
                Execute("DELETE FROM identity_challenges WHERE id="+Quote(nonce)+";");AdvanceDeviceAuthority(account);Execute("COMMIT;");return true;
            }catch{Execute("ROLLBACK;");throw;}
        }}
        internal Dictionary<string,object> IdentitySnapshot(long viewer,string actor,string session,long account,string server){lock(gate){
            if(!ManagementActor(viewer,actor,session)||account<=0||(viewer!=account&&(!Friends(viewer,account)||Blocked(viewer,account))))throw new InvalidOperationException("device_identity_snapshot_denied");
            string root=ScalarText(database,"SELECT device FROM identity_roots WHERE account="+account),rootProof=ScalarText(database,"SELECT proof FROM identity_roots WHERE account="+account)??"";
            bool rootActive=root!=null&&ActiveDevice(account,root);
            var endorsements=new List<Dictionary<string,object>>();var possessions=new List<string>();IntPtr statement=IntPtr.Zero;
            try{
                if(SqliteNative.prepare(database,"SELECT target,proof FROM identity_endorsements WHERE account="+account+" ORDER BY target LIMIT 33",out statement)!=0)throw new InvalidOperationException("device_identity_unavailable");
                while(SqliteNative.step(statement)==100){if(endorsements.Count==32)throw new InvalidOperationException("device_identity_limit");string target=SqliteNative.columnText(statement,0);endorsements.Add(new Dictionary<string,object>{{"proof",SqliteNative.columnText(statement,1)},{"active",rootActive&&ActiveDevice(account,target)}});}
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
            if(viewer==account){try{
                if(SqliteNative.prepare(database,"SELECT device,proof FROM identity_possessions WHERE account="+account+" ORDER BY device LIMIT 33",out statement)!=0)throw new InvalidOperationException("device_identity_unavailable");
                int seen=0;while(SqliteNative.step(statement)==100){if(++seen>32)throw new InvalidOperationException("device_identity_limit");if(ActiveDevice(account,SqliteNative.columnText(statement,0)))possessions.Add(SqliteNative.columnText(statement,1));}
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}}
            return new Dictionary<string,object>{{"deviceIdentityVersion",1},{"serverFingerprint",server},{"accountId",account},{"revision",DeviceAuthorityRevision(account)},{"authorityProof",rootProof},{"authorityActive",rootActive},{"endorsements",endorsements},{"possessionProofs",possessions}};
        }}
    }
    sealed partial class DiscoveryServer {
        string IdentityServerFingerprint(){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(certificate.RawData)).Replace("-","").ToLowerInvariant();}
        Dictionary<string,object> BoundedIdentitySnapshot(long viewer,string actor,string session,long account,string server){
            var result=Ok(accounts.IdentitySnapshot(viewer,actor,session,account,server));
            if(Encoding.UTF8.GetByteCount(new JavaScriptSerializer{MaxJsonLength=150000}.Serialize(result))>131072)throw new InvalidOperationException("device_identity_limit");
            return result;
        }
        Dictionary<string,object> DeviceIdentityRequest(long account,string actor,string session,string type,Dictionary<string,object> request){
            if(!accounts.ManagementActor(account,actor,session))return Error("device_identity_session_required");
            string server=IdentityServerFingerprint();
            try{
                if(type=="device_identity_snapshot"){
                    long target;if(!Int64.TryParse(Text(request,"accountId"),System.Globalization.NumberStyles.None,System.Globalization.CultureInfo.InvariantCulture,out target)||target<=0)return Error("device_identity_invalid");
                    return BoundedIdentitySnapshot(account,actor,session,target,server);
                }
                if(type=="device_identity_register"){
                    var proof=DeviceIdentityPossession.Parse(MailboxProtocol.Decode(Text(request,"proof"),1024));
                    if(accounts.RegisterIdentityPossession(account,actor,session,server,proof))NotifyIdentityChange(account);
                    return BoundedIdentitySnapshot(account,actor,session,account,server);
                }
                if(type=="device_identity_authority_challenge"||type=="device_identity_endorse_challenge"){
                    string kind=type=="device_identity_authority_challenge"?"authority":"endorse";
                    string target=kind=="authority"?actor:Text(request,"targetDevice");
                    if(!MailboxProtocol.Id(target,64))return Error("device_identity_invalid");
                    var payload=accounts.IssueIdentityChallenge(account,actor,target,session,server,kind);
                    return Ok(new Dictionary<string,object>{{"deviceIdentityVersion",1},{"accountId",account},{"serverFingerprint",server},{"payload",Convert.ToBase64String(payload)}});
                }
                if(type=="device_identity_authority_commit"||type=="device_identity_endorse_commit"){
                    string kind=type=="device_identity_authority_commit"?"authority":"endorse";
                    if(accounts.CommitIdentity(account,actor,session,server,kind,MailboxProtocol.Decode(Text(request,"proof"),2048)))NotifyIdentityChange(account);
                    return BoundedIdentitySnapshot(account,actor,session,account,server);
                }
            }catch(FormatException){return Error("device_identity_proof_invalid");}catch(InvalidOperationException e){return Error(e.Message.StartsWith("device_identity_",StringComparison.Ordinal)?e.Message:"device_identity_proof_invalid");}
            return Error("unsupported_request");
        }
        void NotifyIdentityChange(long account){NotifyDeviceManagement(account);foreach(long friend in accounts.ManagedDeviceContacts(account))if(accounts.Friends(account,friend))Notify(friend,new Dictionary<string,object>{{"event","friendship_changed"},{"accountId",account}});}
    }
}
