// PeerChat discovery service, Milestones 1-2.
// Purpose: authenticate accounts, publish public device identities/presence, and own friendship metadata.
// Inputs: a TLS-protected, length-limited JSON request per connection. Outputs: JSON responses.
// Security: passwords are PBKDF2-SHA256 hashes; session tokens are random and stored only hashed.
// The service never accepts private device keys or message contents. Ordinary sockets are owned by
// short-lived workers; authenticated event subscriptions remain owned until the client disconnects.
using System;
using System.Collections.Generic;
using System.Data;
using System.Globalization;
using System.Linq;
using System.IO;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Security;
using System.Net.Sockets;
using System.IO.Pipes;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Runtime.InteropServices;
using PeerChat.ServerShared;
using PeerChat;

namespace PeerChat.Server {
    static class Sqlite {
#if PEERCHAT_PORTABLE
        static Sqlite(){PortablePlatform.InitializeSqlite();}
#endif
        [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] internal static extern int sqlite3_open16(string filename, out IntPtr database);
        [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] internal static extern int sqlite3_close(IntPtr database);
        [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] internal static extern int sqlite3_exec(IntPtr database,
#if PEERCHAT_PORTABLE
        [MarshalAs(UnmanagedType.LPUTF8Str)]
#endif
        string sql, IntPtr callback, IntPtr argument, out IntPtr error);
        [DllImport("winsqlite3.dll", CallingConvention=CallingConvention.Cdecl)] internal static extern void sqlite3_free(IntPtr value);
    }

    // SQLite owns durable account state. All writes use a process-wide lock because this starter
    // service deliberately uses one embedded database rather than introducing another service.
    sealed partial class AccountDatabase : IDisposable {
        readonly object gate = new object(); readonly IntPtr database; internal readonly ServerPresence Live=new ServerPresence();
        public AccountDatabase(string path) {
            string fullPath=Path.GetFullPath(path);Console.WriteLine("Discovery account database: "+fullPath);
            if (Sqlite.sqlite3_open16(fullPath, out database) != 0){if(database!=IntPtr.Zero)Sqlite.sqlite3_close(database);throw new InvalidOperationException("Cannot open server database.");}
            try{Execute("PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; PRAGMA foreign_keys=ON;" +
                "CREATE TABLE IF NOT EXISTS accounts(id INTEGER PRIMARY KEY, username TEXT NOT NULL COLLATE NOCASE UNIQUE, password_salt BLOB NOT NULL, password_hash BLOB NOT NULL, discoverability TEXT NOT NULL DEFAULT 'public', created_utc INTEGER NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS devices(id INTEGER PRIMARY KEY, account_id INTEGER NOT NULL REFERENCES accounts(id), public_identity TEXT NOT NULL UNIQUE, created_utc INTEGER NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS sessions(token_hash TEXT PRIMARY KEY, account_id INTEGER NOT NULL REFERENCES accounts(id), expires_utc INTEGER NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS remembered_devices(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, public_identity TEXT NOT NULL REFERENCES devices(public_identity) ON DELETE CASCADE, credential_hash TEXT NOT NULL UNIQUE, PRIMARY KEY(account_id,public_identity));" +
                "CREATE TABLE IF NOT EXISTS device_sessions(token_hash TEXT PRIMARY KEY REFERENCES sessions(token_hash) ON DELETE CASCADE, public_identity TEXT NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS device_push(account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE, public_identity TEXT NOT NULL REFERENCES devices(public_identity) ON DELETE CASCADE, provider TEXT NOT NULL, token TEXT NOT NULL, expires_utc INTEGER NOT NULL, PRIMARY KEY(account_id,public_identity));" +
                "CREATE TABLE IF NOT EXISTS presence(device_id INTEGER PRIMARY KEY REFERENCES devices(id), seen_utc INTEGER NOT NULL, endpoint_host TEXT, endpoint_port INTEGER, public_endpoint_host TEXT, public_endpoint_port INTEGER, public_candidate_source TEXT, observed_classification TEXT);" +
                "CREATE TABLE IF NOT EXISTS friend_requests(sender_id INTEGER NOT NULL REFERENCES accounts(id), recipient_id INTEGER NOT NULL REFERENCES accounts(id), state TEXT NOT NULL, created_utc INTEGER NOT NULL, updated_utc INTEGER NOT NULL, PRIMARY KEY(sender_id,recipient_id));" +
                "CREATE INDEX IF NOT EXISTS friend_requests_recipient_state ON friend_requests(recipient_id,state,updated_utc);" +
                "CREATE TABLE IF NOT EXISTS friendships(account_low INTEGER NOT NULL REFERENCES accounts(id), account_high INTEGER NOT NULL REFERENCES accounts(id), created_utc INTEGER NOT NULL, PRIMARY KEY(account_low,account_high));" +
                "CREATE TABLE IF NOT EXISTS account_blocks(blocker_id INTEGER NOT NULL REFERENCES accounts(id), blocked_id INTEGER NOT NULL REFERENCES accounts(id), created_utc INTEGER NOT NULL, PRIMARY KEY(blocker_id,blocked_id));" +
                "CREATE TABLE IF NOT EXISTS group_rosters(group_id TEXT PRIMARY KEY, legacy_group_id TEXT NOT NULL, owner_account_id INTEGER NOT NULL, owner_identity TEXT NOT NULL, distributor_identity TEXT NOT NULL, roster_generation INTEGER NOT NULL, creation_event_id TEXT NOT NULL DEFAULT '', creation_proof TEXT NOT NULL DEFAULT '', created_utc INTEGER NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS group_roster_devices(group_id TEXT NOT NULL REFERENCES group_rosters(group_id) ON DELETE CASCADE, account_id INTEGER NOT NULL, public_identity TEXT NOT NULL, role TEXT NOT NULL, state TEXT NOT NULL, PRIMARY KEY(group_id,account_id,public_identity));" +
                "CREATE INDEX IF NOT EXISTS group_roster_members_by_device ON group_roster_devices(public_identity,state,group_id);" +
                "CREATE TABLE IF NOT EXISTS group_device_identity_proofs(public_identity TEXT PRIMARY KEY REFERENCES devices(public_identity) ON DELETE CASCADE,account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,signed_proof TEXT NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS group_device_proofs(proof_id TEXT PRIMARY KEY, group_id TEXT NOT NULL REFERENCES group_rosters(group_id) ON DELETE CASCADE, account_id INTEGER NOT NULL, actor_identity TEXT NOT NULL, target_identity TEXT NOT NULL, applied_generation INTEGER NOT NULL, revoked INTEGER NOT NULL, signed_proof TEXT NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS group_device_epochs(proof_id TEXT PRIMARY KEY REFERENCES group_device_proofs(proof_id) ON DELETE CASCADE,before_roster TEXT NOT NULL,after_roster TEXT NOT NULL);" +
                "CREATE TABLE IF NOT EXISTS group_invites(group_id TEXT NOT NULL REFERENCES group_rosters(group_id) ON DELETE CASCADE, recipient_account_id INTEGER NOT NULL REFERENCES accounts(id), invite_id TEXT NOT NULL UNIQUE, inviter_account_id INTEGER NOT NULL REFERENCES accounts(id), inviter_identity TEXT NOT NULL, state TEXT NOT NULL, created_utc INTEGER NOT NULL, updated_utc INTEGER NOT NULL, PRIMARY KEY(group_id,recipient_account_id));" +
                "CREATE INDEX IF NOT EXISTS group_invites_recipient_state ON group_invites(recipient_account_id,state,updated_utc);");
            // Existing Milestone 1 databases predate privacy controls. Add the non-sensitive
            // default once; future Friends Only/Hidden settings use this same column.
            try { Execute("ALTER TABLE accounts ADD COLUMN discoverability TEXT NOT NULL DEFAULT 'public';"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            // Endpoint candidates are deliberately optional. Older Milestone 1 databases receive
            // these nullable columns without changing their account/device identities.
            try { Execute("ALTER TABLE presence ADD COLUMN endpoint_host TEXT;"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            try { Execute("ALTER TABLE presence ADD COLUMN endpoint_port INTEGER;"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            try { Execute("ALTER TABLE presence ADD COLUMN public_endpoint_host TEXT;"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            try { Execute("ALTER TABLE presence ADD COLUMN public_endpoint_port INTEGER;"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            try { Execute("ALTER TABLE presence ADD COLUMN public_candidate_source TEXT;"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            try { Execute("ALTER TABLE presence ADD COLUMN observed_classification TEXT;"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            try { Execute("ALTER TABLE group_rosters ADD COLUMN creation_event_id TEXT NOT NULL DEFAULT '';"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            try { Execute("ALTER TABLE group_rosters ADD COLUMN creation_proof TEXT NOT NULL DEFAULT '';"); } catch(InvalidOperationException error){if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw;}
            // Early group-roster databases predate the distributor field. CREATE TABLE IF NOT
            // EXISTS does not upgrade them; registration would otherwise fail on its first INSERT.
            try { Execute("ALTER TABLE group_rosters ADD COLUMN distributor_identity TEXT NOT NULL DEFAULT '';"); }
            catch(InvalidOperationException error) { if(error.Message.IndexOf("duplicate column name",StringComparison.OrdinalIgnoreCase)<0)throw; }
            Execute("UPDATE group_rosters SET distributor_identity=owner_identity WHERE distributor_identity='';");
            }catch{Sqlite.sqlite3_close(database);throw;}
        }
        public void Execute(string sql) { lock (gate) { IntPtr error; if (Sqlite.sqlite3_exec(database, sql, IntPtr.Zero, IntPtr.Zero, out error) != 0) { string message=error==IntPtr.Zero?"SQLite error":Marshal.PtrToStringAnsi(error); if(error!=IntPtr.Zero)Sqlite.sqlite3_free(error); throw new InvalidOperationException(message); } } }
        // Parameter values are hex/base64url or validated identifiers, never interpolated raw user input.
        static string Quote(string value) { return "'" + value.Replace("'", "''") + "'"; }
        static string Hex(byte[] value) { var b=new StringBuilder(value.Length*2); foreach(byte x in value)b.Append(x.ToString("x2")); return b.ToString(); }
        static string ScalarText(IntPtr database, string sql) { IntPtr statement=IntPtr.Zero; try { if(SqliteNative.prepare(database,sql,out statement)!=0)return null; return SqliteNative.step(statement)==100?SqliteNative.columnText(statement,0):null; } finally { if(statement!=IntPtr.Zero)SqliteNative.finalize(statement); } }
        static long ScalarLong(IntPtr database, string sql) { var value=ScalarText(database,sql); long parsed; return Int64.TryParse(value,out parsed)?parsed:0; }
        public bool UsernameExists(string username) { lock(gate)return ScalarLong(database,"SELECT count(*) FROM accounts WHERE username="+Quote(username))>0; }
        // An account is only useful for discovery if it owns a public device identity.  Keep both
        // inserts in one transaction so a reused device key cannot leave behind an account that
        // cannot be found (or a partial account after a database error).
        public long CreateAccountWithDevice(string username, byte[] salt, byte[] hash, string publicIdentity) { lock(gate) { Execute("BEGIN IMMEDIATE;"); try { Execute("INSERT INTO accounts(username,password_salt,password_hash,created_utc) VALUES("+Quote(username)+",X'"+Hex(salt)+"',X'"+Hex(hash)+"',"+UnixNow()+");"); long id=ScalarLong(database,"SELECT last_insert_rowid();"); Execute("INSERT INTO devices(account_id,public_identity,created_utc) VALUES("+id+","+Quote(publicIdentity)+","+UnixNow()+");"); Execute("COMMIT;"); return id; } catch { try { Execute("ROLLBACK;"); } catch {} throw; } } }
        public bool DeviceIdentityExists(string publicIdentity) { lock(gate) return ScalarLong(database,"SELECT count(*) FROM devices WHERE public_identity="+Quote(publicIdentity))>0; }
        public string PrimaryDeviceIdentity(long accountId) { lock(gate) return ScalarText(database,"SELECT public_identity FROM devices WHERE account_id="+accountId+" ORDER BY id LIMIT 1")??""; }
        public bool PasswordMatches(string username, Func<byte[], byte[], bool> verifier, out long accountId) { lock(gate) { IntPtr s=IntPtr.Zero; accountId=0; try { if(SqliteNative.prepare(database,"SELECT id,password_salt,password_hash FROM accounts WHERE username="+Quote(username),out s)!=0||SqliteNative.step(s)!=100)return false; accountId=SqliteNative.columnLong(s,0); return verifier(SqliteNative.columnBlob(s,1),SqliteNative.columnBlob(s,2)); } finally { if(s!=IntPtr.Zero)SqliteNative.finalize(s); } } }
        public bool AddDevice(long accountId,string publicIdentity) { lock(gate){long owner=ScalarLong(database,"SELECT account_id FROM devices WHERE public_identity="+Quote(publicIdentity));if(owner!=0)return owner==accountId;Execute("INSERT INTO devices(account_id,public_identity,created_utc) VALUES("+accountId+","+Quote(publicIdentity)+","+UnixNow()+");");return true;} }
        public bool OwnsDevice(long accountId,string publicIdentity){lock(gate)return ScalarLong(database,"SELECT count(*) FROM devices WHERE account_id="+accountId+" AND public_identity="+Quote(publicIdentity))>0;}
        public void CreateSession(string tokenHash,long accountId,long expires) { lock(gate) Execute("DELETE FROM sessions WHERE expires_utc<"+UnixNow()+"; INSERT INTO sessions(token_hash,account_id,expires_utc) VALUES("+Quote(tokenHash)+","+accountId+","+expires+");"); }
        public long Authenticate(string tokenHash) { lock(gate)return ScalarLong(database,"SELECT s.account_id FROM sessions s WHERE s.token_hash="+Quote(tokenHash)+" AND s.expires_utc>="+UnixNow()+" AND (NOT EXISTS(SELECT 1 FROM device_sessions b WHERE b.token_hash=s.token_hash) OR EXISTS(SELECT 1 FROM device_sessions b JOIN remembered_devices r ON r.account_id=s.account_id AND r.public_identity=b.public_identity WHERE b.token_hash=s.token_hash))"); }
        public void RevokeSession(string tokenHash) { lock(gate) Execute("DELETE FROM sessions WHERE token_hash="+Quote(tokenHash)+";"); }
        public string SessionDevice(string tokenHash) { lock(gate)return ScalarText(database,"SELECT public_identity FROM device_sessions WHERE token_hash="+Quote(tokenHash))??""; }
        public bool SessionAllowsDevice(string tokenHash,string device) { var bound=SessionDevice(tokenHash);return bound.Length==0||bound==device; }
        public long RememberedAccount(string hash,string device) { lock(gate)return ScalarLong(database,"SELECT r.account_id FROM remembered_devices r JOIN devices d ON d.account_id=r.account_id AND d.public_identity=r.public_identity WHERE r.credential_hash="+Quote(hash)+" AND r.public_identity="+Quote(device)); }
        // Replace/revoke atomically; no raw credential is stored. Device-bound
        // sessions cannot survive credential revocation or device/account deletion.
        public void RememberDevice(long account,string device,string hash) { lock(gate){
            if(!OwnsDevice(account,device))throw new InvalidOperationException("Device is not owned by account.");
            Execute("BEGIN IMMEDIATE;");try {
                RevokeRememberedDevice(account,device);
                Execute("INSERT INTO remembered_devices(account_id,public_identity,credential_hash) VALUES("+account+","+Quote(device)+","+Quote(hash)+");COMMIT;");
            }catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        public void RevokeRememberedDevice(long account,string device) { lock(gate)Execute("DELETE FROM sessions WHERE account_id="+account+" AND token_hash IN (SELECT token_hash FROM device_sessions WHERE public_identity="+Quote(device)+"); DELETE FROM remembered_devices WHERE account_id="+account+" AND public_identity="+Quote(device)+"; DELETE FROM device_push WHERE account_id="+account+" AND public_identity="+Quote(device)+";"); }
        public bool RegisterPush(long account,string device,string token) { lock(gate){
            if(!OwnsDevice(account,device)||ScalarLong(database,"SELECT count(*) FROM remembered_devices WHERE account_id="+account+" AND public_identity="+Quote(device))!=1)return false;
            Execute("DELETE FROM device_push WHERE expires_utc<"+UnixNow()+"; INSERT INTO device_push(account_id,public_identity,provider,token,expires_utc) VALUES("+account+","+Quote(device)+",'fcm',"+Quote(token)+","+(UnixNow()+30*86400)+") ON CONFLICT(account_id,public_identity) DO UPDATE SET token=excluded.token,provider=excluded.provider,expires_utc=excluded.expires_utc;");return true;
        }}
        public string PushToken(long account,string device) {lock(gate)return ScalarText(database,"SELECT p.token FROM device_push p JOIN remembered_devices r ON r.account_id=p.account_id AND r.public_identity=p.public_identity JOIN devices d ON d.public_identity=p.public_identity AND d.account_id=p.account_id WHERE p.account_id="+account+" AND p.public_identity="+Quote(device)+" AND p.expires_utc>="+UnixNow());}
        public void RemovePush(long account,string device,string expectedToken=null) {lock(gate)Execute("DELETE FROM device_push WHERE account_id="+account+" AND public_identity="+Quote(device)+(expectedToken==null?"":" AND token="+Quote(expectedToken))+";");}
        public void CreateDeviceSession(string tokenHash,long account,string device,long expires,string credentialHash) { lock(gate){
            if(RememberedAccount(credentialHash,device)!=account)throw new InvalidOperationException("Remembered device revoked.");
            Execute("BEGIN IMMEDIATE;");try {CreateSession(tokenHash,account,expires);Execute("INSERT INTO device_sessions(token_hash,public_identity) VALUES("+Quote(tokenHash)+","+Quote(device)+");COMMIT;");}catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        public ServerPresence.Device TouchPresence(long accountId,string publicIdentity,string endpointHost,int endpointPort,string publicHost,int publicPort,string publicSource,string observedClassification,string owner="",bool replace=false,string session="") {
            lock(gate){if(!OwnsDevice(accountId,publicIdentity))return null;
                var current=Live.Get(publicIdentity);
                if(owner.Length==0){owner=current==null?"presence-rpc":current.Owner;replace=current==null;}
                return Live.Observe(accountId,publicIdentity,owner,replace,endpointHost,endpointPort,publicHost,publicPort,publicSource,observedClassification,session);
            }
        }
        public void ClearPresence(long accountId,string publicIdentity,string owner=null) { lock(gate)Live.Remove(accountId,publicIdentity,owner); }
        public Dictionary<string,object> Search(string username,long viewerAccount) { lock(gate) { IntPtr s=IntPtr.Zero; try { string sql="SELECT a.id,a.username,COALESCE((SELECT d2.public_identity FROM presence p2 JOIN devices d2 ON d2.id=p2.device_id WHERE d2.account_id=a.id ORDER BY p2.seen_utc DESC LIMIT 1),(SELECT d3.public_identity FROM devices d3 WHERE d3.account_id=a.id ORDER BY d3.id LIMIT 1)),COALESCE((SELECT max(p3.seen_utc) FROM presence p3 JOIN devices d3 ON d3.id=p3.device_id WHERE d3.account_id=a.id),0) FROM accounts a WHERE a.id<>"+viewerAccount+" AND a.discoverability='public' AND a.username="+Quote(username)+" AND NOT EXISTS (SELECT 1 FROM account_blocks b WHERE (b.blocker_id="+viewerAccount+" AND b.blocked_id=a.id) OR (b.blocker_id=a.id AND b.blocked_id="+viewerAccount+")) ORDER BY a.id LIMIT 1"; if(SqliteNative.prepare(database,sql,out s)!=0||SqliteNative.step(s)!=100)return null; long seen=SqliteNative.columnLong(s,3); return LiveProfile(new Dictionary<string,object>{{"accountId",SqliteNative.columnLong(s,0)},{"username",SqliteNative.columnText(s,1)},{"publicIdentity",SqliteNative.columnText(s,2)},{"online",false}}); } finally { if(s!=IntPtr.Zero)SqliteNative.finalize(s); } } }
        // accounts.username is UNIQUE COLLATE NOCASE, so this prefix query uses the account index.
        // Discovery returns only the public identity reference required for later friend actions.
        public List<Dictionary<string,object>> Suggestions(string prefix,long excludeAccount,int limit){lock(gate){var list=new List<Dictionary<string,object>>();IntPtr s=IntPtr.Zero;try{string sql="SELECT a.id,a.username,COALESCE((SELECT d2.public_identity FROM presence p2 JOIN devices d2 ON d2.id=p2.device_id WHERE d2.account_id=a.id ORDER BY p2.seen_utc DESC LIMIT 1),(SELECT d3.public_identity FROM devices d3 WHERE d3.account_id=a.id ORDER BY d3.id LIMIT 1)),COALESCE((SELECT max(p3.seen_utc) FROM presence p3 JOIN devices d3 ON d3.id=p3.device_id WHERE d3.account_id=a.id),0) FROM accounts a WHERE a.id<>"+excludeAccount+" AND a.discoverability='public' AND a.username LIKE "+Quote(prefix+"%")+" ESCAPE '\\' AND NOT EXISTS (SELECT 1 FROM account_blocks b WHERE (b.blocker_id="+excludeAccount+" AND b.blocked_id=a.id) OR (b.blocker_id=a.id AND b.blocked_id="+excludeAccount+")) ORDER BY a.username,a.id LIMIT "+limit;if(SqliteNative.prepare(database,sql,out s)!=0)return list;while(SqliteNative.step(s)==100){long seen=SqliteNative.columnLong(s,3);list.Add(LiveProfile(new Dictionary<string,object>{{"accountId",SqliteNative.columnLong(s,0)},{"username",SqliteNative.columnText(s,1)},{"publicIdentity",SqliteNative.columnText(s,2)},{"online",false}}));}return list;}finally{if(s!=IntPtr.Zero)SqliteNative.finalize(s);}}}
        // Shared Find People lookup exposes only account names and lease-derived
        // availability, never device identity or relationship authority. The caller
        // must obtain separately authorized details for any later operation.
        public List<Dictionary<string,object>> PeopleSearch(string query,long viewerAccount){lock(gate){
            var results=new List<Dictionary<string,object>>();if(String.IsNullOrEmpty(query)||query.Length>32||query.Any(c=>!(Char.IsLetterOrDigit(c)||c=='-'||c=='_')))return results;
            long exactId;bool byId=Int64.TryParse(query,out exactId)&&exactId>0;
            if(query.Length<2&&!byId)return results;
            string match="(a.username LIKE "+Quote(query+"%")+" OR a.id="+(byId?exactId:0)+")";
            string sql="SELECT a.id,a.username,COALESCE((SELECT max(p.seen_utc) FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id),0) FROM accounts a WHERE a.id<>"+viewerAccount+" AND a.discoverability='public' AND "+match+" AND NOT EXISTS (SELECT 1 FROM account_blocks b WHERE (b.blocker_id="+viewerAccount+" AND b.blocked_id=a.id) OR (b.blocker_id=a.id AND b.blocked_id="+viewerAccount+")) ORDER BY CASE WHEN a.id="+(byId?exactId:0)+" OR a.username="+Quote(query)+" COLLATE NOCASE THEN 0 ELSE 1 END,a.username COLLATE NOCASE,a.id LIMIT 15";
            IntPtr statement=IntPtr.Zero;try{if(SqliteNative.prepare(database,sql,out statement)!=0)throw new InvalidOperationException("Could not search public accounts.");while(SqliteNative.step(statement)==100)results.Add(new Dictionary<string,object>{{"accountId",SqliteNative.columnLong(statement,0)},{"username",SqliteNative.columnText(statement,1)},{"online",IsPresent(SqliteNative.columnLong(statement,0))}});return results;}finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
        }}
        static long Low(long a,long b){return Math.Min(a,b);} static long High(long a,long b){return Math.Max(a,b);}
        bool Exists(long accountId){return ScalarLong(database,"SELECT count(*) FROM accounts WHERE id="+accountId)>0;}
        bool Blocked(long a,long b){return ScalarLong(database,"SELECT count(*) FROM account_blocks WHERE (blocker_id="+a+" AND blocked_id="+b+") OR (blocker_id="+b+" AND blocked_id="+a+")")>0;}
        // Used by the notification layer only to decide whether a presence update is
        // appropriate for a subscriber. It never exposes friendship data to the client.
        public bool Friends(long a,long b){return ScalarLong(database,"SELECT count(*) FROM friendships WHERE account_low="+Low(a,b)+" AND account_high="+High(a,b))>0;}
        public bool RelayAuthorized(long a,long b){lock(gate)return Friends(a,b)||ScalarLong(database,"SELECT count(*) FROM friend_requests WHERE state='pending' AND ((sender_id="+a+" AND recipient_id="+b+") OR (sender_id="+b+" AND recipient_id="+a+"))")>0;}
        static bool GroupId(string value){return value!=null&&value.Length==32&&value.All(c=>(c>='0'&&c<='9')||(c>='a'&&c<='f'));}
        static bool GroupPeerId(string value){return value!=null&&value.Length==64&&value.All(c=>(c>='0'&&c<='9')||(c>='a'&&c<='f'));}
        string GroupRole(string group,long account,string device,string state="member"){
            if(String.IsNullOrEmpty(device))return null;
            return ScalarText(database,"SELECT role FROM group_roster_devices WHERE group_id="+Quote(group)+" AND account_id="+account+" AND public_identity="+Quote(device)+" AND state="+Quote(state));
        }
        public bool GroupRelayAuthorized(string group,long a,string aDevice,long b,string bDevice,long generation=0){lock(gate){
            return GroupId(group)&&a!=b&&(generation==0||generation>0&&ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group))==generation)&&OwnsDevice(a,aDevice)&&OwnsDevice(b,bDevice)&&
                GroupRole(group,a,aDevice)!=null&&GroupRole(group,b,bDevice)!=null;
        }}
        public bool GroupMessageAuthorized(string group,long generation,long account,string device){lock(gate){return GroupId(group)&&generation>0&&GroupPeerId(device)&&OwnsDevice(account,device)&&GroupRole(group,account,device)!=null&&ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group))==generation;}}
        public void WithGroupMessageAuthorization(string group,long generation,long sender,string senderDevice,long recipient,string recipientDevice,Action admit){lock(gate){if(GroupMessageAuthorized(group,generation,sender,senderDevice)&&GroupMessageAuthorized(group,generation,recipient,recipientDevice))admit();}}
        public bool GroupRosterMember(string group,long account,string device){lock(gate){return GroupId(group)&&OwnsDevice(account,device)&&GroupRole(group,account,device)!=null;}}
        public bool WithGroupRosterAuthorization(string group,long account,string device,Action<Dictionary<string,object>> action){lock(gate){
            if(!GroupRosterMember(group,account,device))return false;
            action(GroupRoster(group,account,device));return true;
        }}
        public Dictionary<string,object> RegisterGroup(long account,string device,GroupRosterProof proof){lock(gate){
            if(proof==null||proof.OwnerAccountId!=account||proof.OwnerIdentity!=device||!OwnsDevice(account,device))return null;
            string group=proof.ServerGroupId;long existing=ScalarLong(database,"SELECT owner_account_id FROM group_rosters WHERE group_id="+Quote(group));
            if(existing!=0){if(existing!=account||ScalarText(database,"SELECT owner_identity FROM group_rosters WHERE group_id="+Quote(group))!=device||ScalarText(database,"SELECT legacy_group_id FROM group_rosters WHERE group_id="+Quote(group))!=proof.LegacyGroupId||ScalarText(database,"SELECT creation_event_id FROM group_rosters WHERE group_id="+Quote(group))!=proof.CreateEventId)return null;// Backfill only an exact verified registration retry; never invent a root for legacy history.
                string stored=GroupCreationProof(group);
                if(String.IsNullOrEmpty(stored))Execute("UPDATE group_rosters SET creation_proof="+Quote(proof.Encoded)+" WHERE group_id="+Quote(group)+" AND creation_proof='';");
                return GroupRoster(group,account,device);}
            Execute("BEGIN IMMEDIATE;");try{
                Execute("INSERT INTO group_rosters(group_id,legacy_group_id,owner_account_id,owner_identity,distributor_identity,roster_generation,creation_event_id,creation_proof,created_utc) VALUES("+Quote(group)+","+Quote(proof.LegacyGroupId)+","+account+","+Quote(device)+","+Quote(device)+",1,"+Quote(proof.CreateEventId)+","+Quote(proof.Encoded)+","+UnixNow()+");");
                Execute("INSERT INTO group_roster_devices(group_id,account_id,public_identity,role,state) VALUES("+Quote(group)+","+account+","+Quote(device)+",'owner','member');");
                Execute("COMMIT;");return GroupRoster(group,account,device);
            }catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        // Gated application prerequisite, not an RPC or membership operation.
        // Issuing work must be tied to the currently authenticated device lease,
        // not merely an account token or a previously captured presence object.
        bool CurrentGroupDeviceSession(string tokenHash,string device,string leaseEpoch,long leaseGeneration,out long account){
            account=0;if(leaseEpoch!=Live.Epoch||!GroupRosterProof.Id(tokenHash,64)||!GroupRosterProof.Id(device,64)||leaseGeneration<=0)return false;
            account=Authenticate(tokenHash);var lease=Live.Get(device);
            return account>0&&OwnsDevice(account,device)&&SessionAllowsDevice(tokenHash,device)&&lease!=null&&
                lease.Account==account&&lease.Identity==device&&lease.Session==tokenHash&&lease.Generation==leaseGeneration;
        }
        internal bool PublishGroupDeviceIdentityForSession(string tokenHash,string device,string leaseEpoch,long leaseGeneration,GroupDeviceIdentityProof proof){lock(gate){
            long account;if(!CurrentGroupDeviceSession(tokenHash,device,leaseEpoch,leaseGeneration,out account))return false;
            return PublishGroupDeviceIdentity(account,device,proof);
        }}
        internal string ReadGroupDeviceIdentityForSession(string tokenHash,string device,string leaseEpoch,long leaseGeneration,string target){lock(gate){
            long account;if(!CurrentGroupDeviceSession(tokenHash,device,leaseEpoch,leaseGeneration,out account))return null;
            return ReadGroupDeviceIdentity(account,device,target);
        }}
        internal Dictionary<string,object> ApplyGroupDeviceProofForSession(string tokenHash,string device,string leaseEpoch,long leaseGeneration,GroupDeviceProof proof){lock(gate){
            long account;if(!CurrentGroupDeviceSession(tokenHash,device,leaseEpoch,leaseGeneration,out account))return null;
            return ApplyGroupDeviceProof(account,device,proof);
        }}
        // Read-only RPC derives the currently owned lease, then revalidates it
        // through the same strict account/device/session admission. An account
        // token alone or an obsolete lease cannot retrieve recovery evidence.
        internal Dictionary<string,object> ReadGroupDeviceRecoveryForCurrentSession(string tokenHash,string device,string group,long afterGeneration){lock(gate){
            if(!GroupRosterProof.Id(tokenHash,64)||!GroupRosterProof.Id(device,64)||!GroupId(group)||afterGeneration<=0)return null;
            var lease=Live.Get(device);if(lease==null)return null;
            return ReadGroupDeviceRecoveryForSession(tokenHash,device,Live.Epoch,lease.Generation,group,afterGeneration);
        }}
        // Snapshots were captured
        // atomically with the original verified proof, never reconstructed from
        // today's membership. Ordinary roster gaps require signed-ledger recovery.
        internal Dictionary<string,object> ReadGroupDeviceRecoveryForSession(string tokenHash,string device,string leaseEpoch,long leaseGeneration,string group,long afterGeneration){lock(gate){
            long account;if(!CurrentGroupDeviceSession(tokenHash,device,leaseEpoch,leaseGeneration,out account)||
                !GroupId(group)||GroupRole(group,account,device)==null||afterGeneration<=0)return null;
            long current=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group));
            if(afterGeneration>current)return null;
            var rows=new List<Dictionary<string,object>>();var json=new JavaScriptSerializer{MaxJsonLength=65536};
            long next=afterGeneration;string reason="none";int bytes=256;IntPtr statement=IntPtr.Zero;
            try{
                if(SqliteNative.prepare(database,"SELECT p.signed_proof,p.applied_generation,e.before_roster,e.after_roster FROM group_device_proofs p LEFT JOIN group_device_epochs e ON e.proof_id=p.proof_id WHERE p.group_id="+Quote(group)+" AND p.applied_generation>"+afterGeneration+" ORDER BY p.applied_generation LIMIT 32",out statement)!=0)throw new InvalidOperationException("Cannot read group recovery evidence.");
                while(SqliteNative.step(statement)==100){
                    var proof=GroupDeviceProof.Parse(SqliteNative.columnText(statement,0));long applied=SqliteNative.columnLong(statement,1);
                    if(proof.GroupId!=group||proof.Generation+1!=applied||applied>current)throw new InvalidOperationException("Stored group recovery proof is invalid.");
                    if(applied!=next+1){reason="signed_ledger_gap";break;}
                    string before=SqliteNative.columnText(statement,2),after=SqliteNative.columnText(statement,3);
                    if(String.IsNullOrEmpty(before)||String.IsNullOrEmpty(after)){reason="epoch_evidence_unavailable";break;}
                    var a=json.Deserialize<Dictionary<string,object>>(before);var b=json.Deserialize<Dictionary<string,object>>(after);
                    if(Convert.ToString(a["groupId"])!=group||Convert.ToString(b["groupId"])!=group||
                        Convert.ToString(a["legacyGroupId"])!=proof.LegacyGroupId||Convert.ToString(b["legacyGroupId"])!=proof.LegacyGroupId||
                        Convert.ToInt64(a["rosterGeneration"])!=proof.Generation||Convert.ToInt64(b["rosterGeneration"])!=applied)
                        throw new InvalidOperationException("Stored group recovery snapshot is invalid.");
                    var row=new Dictionary<string,object>{{"before",a},{"after",b},{"proof",proof.Encoded}};
                    int cost=Encoding.UTF8.GetByteCount(json.Serialize(row))+1;
                    if(bytes+cost>8192){reason=rows.Count==0?"epoch_evidence_too_large":"page_limit";break;}
                    bytes+=cost;rows.Add(row);next=applied;
                }
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
            if(next!=current&&reason=="none")reason="signed_ledger_gap";
            return new Dictionary<string,object>{{"groupId",group},{"fromGeneration",afterGeneration},{"nextGeneration",next},{"currentGeneration",current},{"complete",next==current},{"reason",reason},{"transitions",rows}};
        }}
        string GroupDeviceEpochSnapshot(string group,long account,string device){
            var snapshot=GroupRoster(group,account,device,false);
            if(snapshot==null)throw new InvalidOperationException("Cannot capture authenticated group epoch.");
            snapshot.Remove("deviceApprovals");snapshot.Remove("deviceApprovalsIncluded");snapshot.Remove("deviceApprovalsTruncated");
            var members=(List<Dictionary<string,object>>)snapshot["members"];
            foreach(var member in members)member.Remove("username");
            if(members.Count>256)throw new InvalidOperationException("Group epoch exceeds verified replica bound.");
            return new JavaScriptSerializer{MaxJsonLength=65536}.Serialize(snapshot);
        }
        public bool PublishGroupDeviceIdentity(long account,string device,GroupDeviceIdentityProof proof){lock(gate){
            if(proof==null||proof.Account!=account||proof.Device!=device||!OwnsDevice(account,device))return false;
            Execute("INSERT INTO group_device_identity_proofs(public_identity,account_id,signed_proof) VALUES("+Quote(device)+","+account+","+Quote(proof.Encoded)+") ON CONFLICT(public_identity) DO NOTHING;");
            return ReadGroupDeviceIdentity(account,device,device)!=null;
        }}
        public string ReadGroupDeviceIdentity(long account,string requestingDevice,string targetDevice){lock(gate){
            if(!OwnsDevice(account,requestingDevice)||!OwnsDevice(account,targetDevice))return null;
            string encoded=ScalarText(database,"SELECT signed_proof FROM group_device_identity_proofs WHERE account_id="+account+" AND public_identity="+Quote(targetDevice));
            if(encoded==null)return null;
            var proof=GroupDeviceIdentityProof.Parse(encoded);
            if(proof.Account!=account||proof.Device!=targetDevice)throw new InvalidOperationException("Stored group device identity evidence is invalid.");
            return proof.Encoded;
        }}
        // Internal production boundary: RPC/UI activation waits for signed replica
        // reconciliation and current-key provisioning. Never admits account login alone.
        public Dictionary<string,object> ApplyGroupDeviceProof(long account,string device,GroupDeviceProof proof){lock(gate){
            if(proof==null||proof.Account!=account||proof.Actor!=device||!OwnsDevice(account,device)||
               !OwnsDevice(account,proof.Target)||GroupRole(proof.GroupId,account,device)==null||
               ScalarText(database,"SELECT legacy_group_id FROM group_rosters WHERE group_id="+Quote(proof.GroupId))!=proof.LegacyGroupId)return null;
            long current=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(proof.GroupId));
            long applied=ScalarLong(database,"SELECT applied_generation FROM group_device_proofs WHERE proof_id="+Quote(proof.Id));
            if(applied!=0)return current==applied?GroupRoster(proof.GroupId,account,device):null;
            if(current!=proof.Generation)return null;
            string role=GroupRole(proof.GroupId,account,device),targetRole=GroupRole(proof.GroupId,account,proof.Target);
            long count=ScalarLong(database,"SELECT count(*) FROM group_roster_devices WHERE group_id="+Quote(proof.GroupId)+" AND account_id="+account+" AND state='member'");
            if(proof.Revoke?(targetRole==null||count<=1):(targetRole!=null||count>=8||
                ScalarLong(database,"SELECT count(*) FROM group_roster_devices WHERE group_id="+Quote(proof.GroupId)+" AND public_identity="+Quote(proof.Target))!=0))return null;
            Execute("BEGIN IMMEDIATE;");try{
                string beforeEpoch=GroupDeviceEpochSnapshot(proof.GroupId,account,device);
                if(proof.Revoke){
                    Execute("DELETE FROM group_roster_devices WHERE group_id="+Quote(proof.GroupId)+" AND account_id="+account+" AND public_identity="+Quote(proof.Target)+";");
                    Execute("UPDATE group_rosters SET owner_identity="+Quote(device)+" WHERE group_id="+Quote(proof.GroupId)+" AND owner_identity="+Quote(proof.Target)+" AND owner_account_id="+account+";");
                }else Execute("INSERT INTO group_roster_devices(group_id,account_id,public_identity,role,state) VALUES("+Quote(proof.GroupId)+","+account+","+Quote(proof.Target)+","+Quote(role)+",'member');");
                Execute("UPDATE group_rosters SET roster_generation=roster_generation+1,distributor_identity="+Quote(device)+" WHERE group_id="+Quote(proof.GroupId)+";");
                Execute("INSERT INTO group_device_proofs(proof_id,group_id,account_id,actor_identity,target_identity,applied_generation,revoked,signed_proof) VALUES("+Quote(proof.Id)+","+Quote(proof.GroupId)+","+account+","+Quote(device)+","+Quote(proof.Target)+","+(current+1)+","+(proof.Revoke?1:0)+","+Quote(proof.Encoded)+");");
                string afterEpoch=GroupDeviceEpochSnapshot(proof.GroupId,account,device);
                Execute("INSERT INTO group_device_epochs(proof_id,before_roster,after_roster) VALUES("+Quote(proof.Id)+","+Quote(beforeEpoch)+","+Quote(afterEpoch)+");");
                Execute("COMMIT;");return GroupRoster(proof.GroupId,account,device);
            }catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        // Public, content-free root evidence; never grants current membership or historical keys.
        // The original creator may have been revoked, so compare immutable registration fields.
        string GroupCreationProof(string group){
            string encoded=ScalarText(database,"SELECT creation_proof FROM group_rosters WHERE group_id="+Quote(group));
            if(String.IsNullOrEmpty(encoded))return "";
            var proof=GroupRosterProof.Parse(encoded);
            if(proof.ServerGroupId!=group||proof.LegacyGroupId!=ScalarText(database,"SELECT legacy_group_id FROM group_rosters WHERE group_id="+Quote(group))||
               proof.CreateEventId!=ScalarText(database,"SELECT creation_event_id FROM group_rosters WHERE group_id="+Quote(group))||
               proof.OwnerAccountId!=ScalarLong(database,"SELECT owner_account_id FROM group_rosters WHERE group_id="+Quote(group)))
                throw new InvalidOperationException("Stored group creation evidence is invalid.");
            return proof.Encoded;
        }
        // Public signed evidence only. A receipt is not membership or a key grant;
        // native consecutive-roster admission remains mandatory.
        List<Dictionary<string,object>> GroupDeviceApprovals(string group,string legacy,long generation,out bool truncated){
            var result=new List<Dictionary<string,object>>();IntPtr statement=IntPtr.Zero;int bytes=2;
            truncated=ScalarLong(database,"SELECT COUNT(*) FROM group_device_proofs WHERE group_id="+Quote(group))>32;
            try{
                if(SqliteNative.prepare(database,"SELECT proof_id,account_id,actor_identity,target_identity,applied_generation,revoked,signed_proof FROM group_device_proofs WHERE group_id="+Quote(group)+" ORDER BY applied_generation DESC LIMIT 32",out statement)!=0)throw new InvalidOperationException("Cannot read group device evidence.");
                while(SqliteNative.step(statement)==100){
                    var proof=GroupDeviceProof.Parse(SqliteNative.columnText(statement,6));long applied=SqliteNative.columnLong(statement,4);
                    if(proof.Id!=SqliteNative.columnText(statement,0)||proof.Account!=SqliteNative.columnLong(statement,1)||
                       proof.Actor!=SqliteNative.columnText(statement,2)||proof.Target!=SqliteNative.columnText(statement,3)||
                       proof.GroupId!=group||proof.LegacyGroupId!=legacy||proof.Generation+1!=applied||applied>generation||
                       (proof.Revoke?1:0)!=SqliteNative.columnLong(statement,5))throw new InvalidOperationException("Stored group device evidence is invalid.");
                    // Base64 and fixed JSON keys are ASCII. Reserve more than the
                    // exact row overhead; never let evidence consume the RPC frame.
                    int cost=proof.Encoded.Length+128;
                    if(bytes+cost>8192){truncated=true;break;}
                    bytes+=cost;result.Add(new Dictionary<string,object>{{"appliedGeneration",applied},{"proof",proof.Encoded}});
                }
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
            result.Reverse();return result;
        }
        public Dictionary<string,object> GroupRoster(string group,long account,string device,bool includeDeviceApprovals=true){lock(gate){
            if(!GroupId(group)||!OwnsDevice(account,device)||GroupRole(group,account,device)==null&&GroupRole(group,account,device,"invited")==null)return null;
            long generation=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group));
            var members=new List<Dictionary<string,object>>();IntPtr statement=IntPtr.Zero;
            try{if(SqliteNative.prepare(database,"SELECT r.account_id,r.public_identity,r.role,r.state,a.username FROM group_roster_devices r JOIN accounts a ON a.id=r.account_id WHERE r.group_id="+Quote(group)+" ORDER BY r.account_id,r.public_identity",out statement)!=0)throw new InvalidOperationException("Cannot read group roster.");
                while(SqliteNative.step(statement)==100)members.Add(new Dictionary<string,object>{{"accountId",SqliteNative.columnLong(statement,0)},{"publicIdentity",SqliteNative.columnText(statement,1)},{"role",SqliteNative.columnText(statement,2)},{"state",SqliteNative.columnText(statement,3)},{"username",SqliteNative.columnText(statement,4)}});
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
            var legacy=ScalarText(database,"SELECT legacy_group_id FROM group_rosters WHERE group_id="+Quote(group));
            bool truncated=false;bool included=includeDeviceApprovals&&GroupRole(group,account,device)!=null;
            var approvals=included?GroupDeviceApprovals(group,legacy,generation,out truncated):new List<Dictionary<string,object>>();
            return new Dictionary<string,object>{{"groupId",group},{"legacyGroupId",legacy},{"rosterGeneration",generation},{"distributorIdentity",ScalarText(database,"SELECT distributor_identity FROM group_rosters WHERE group_id="+Quote(group))},{"members",members},{"creationProof",GroupCreationProof(group)},{"deviceApprovalsIncluded",included},{"deviceApprovalsTruncated",truncated},{"deviceApprovals",approvals}};
        }}
        public List<Dictionary<string,object>> GroupRostersForDevice(long account,string device){lock(gate){
            var result=new List<Dictionary<string,object>>();if(!OwnsDevice(account,device))return result;
            var ids=new List<string>();IntPtr statement=IntPtr.Zero;
            try{if(SqliteNative.prepare(database,"SELECT group_id FROM group_roster_devices WHERE account_id="+account+" AND public_identity="+Quote(device)+" AND state='member' ORDER BY group_id LIMIT 200",out statement)!=0)throw new InvalidOperationException("Cannot list group rosters.");
                while(SqliteNative.step(statement)==100)ids.Add(SqliteNative.columnText(statement,0));
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
            foreach(string id in ids){var roster=GroupRoster(id,account,device,false);if(roster!=null)result.Add(roster);}return result;
        }}
        public List<long> GroupMemberAccounts(string group){lock(gate){
            var members=new List<long>();if(!GroupId(group))return members;IntPtr statement=IntPtr.Zero;
            try{if(SqliteNative.prepare(database,"SELECT DISTINCT account_id FROM group_roster_devices WHERE group_id="+Quote(group)+" AND state='member' ORDER BY account_id",out statement)!=0)throw new InvalidOperationException("Cannot read group members.");
                while(SqliteNative.step(statement)==100)members.Add(SqliteNative.columnLong(statement,0));return members;
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
        }}
        public Dictionary<string,object> GroupInvite(string group,long actor,string actorDevice,long target,string targetDevice,long expectedGeneration){lock(gate){
            if(!GroupId(group)||!GroupPeerId(actorDevice)||!GroupPeerId(targetDevice)||actor==target||!OwnsDevice(actor,actorDevice)||!OwnsDevice(target,targetDevice)||expectedGeneration!=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group)))return null;
            string role=GroupRole(group,actor,actorDevice);if(role!="owner"&&role!="admin")return null;
            if(ScalarLong(database,"SELECT count(*) FROM group_roster_devices WHERE group_id="+Quote(group)+" AND account_id="+target+" AND public_identity="+Quote(targetDevice))!=0)return null;
            Execute("BEGIN IMMEDIATE;");try{
                Execute("INSERT INTO group_roster_devices(group_id,account_id,public_identity,role,state) VALUES("+Quote(group)+","+target+","+Quote(targetDevice)+",'member','invited');");
                Execute("UPDATE group_rosters SET roster_generation=roster_generation+1,distributor_identity="+Quote(actorDevice)+" WHERE group_id="+Quote(group)+";");
                Execute("COMMIT;");return GroupRoster(group,actor,actorDevice);
            }catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        // Invitation is account-scoped metadata. The recipient's device identity
        // is supplied by its own authenticated session only when it accepts.
        public Dictionary<string,object> GroupInviteAccount(string group,long actor,string actorDevice,long target,long expectedGeneration){lock(gate){
            if(!GroupId(group)||!GroupPeerId(actorDevice)||actor==target||!Exists(target)||Blocked(actor,target)||
               !OwnsDevice(actor,actorDevice)||expectedGeneration!=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group)))return null;
            string role=GroupRole(group,actor,actorDevice);if(role!="owner"&&role!="admin")return null;
            if(ScalarLong(database,"SELECT count(*) FROM group_roster_devices WHERE group_id="+Quote(group)+" AND account_id="+target+" AND state='member'")!=0)return new Dictionary<string,object>{{"state","already_member"},{"groupId",group}};
            string existingState=ScalarText(database,"SELECT state FROM group_invites WHERE group_id="+Quote(group)+" AND recipient_account_id="+target);
            if(existingState=="pending")return new Dictionary<string,object>{{"state","already_pending"},{"groupId",group},{"inviteId",ScalarText(database,"SELECT invite_id FROM group_invites WHERE group_id="+Quote(group)+" AND recipient_account_id="+target)}};
            string inviteId=Guid.NewGuid().ToString("N"),now=UnixNow().ToString();
            Execute("INSERT INTO group_invites(group_id,recipient_account_id,invite_id,inviter_account_id,inviter_identity,state,created_utc,updated_utc) VALUES("+Quote(group)+","+target+","+Quote(inviteId)+","+actor+","+Quote(actorDevice)+",'pending',"+now+","+now+") ON CONFLICT(group_id,recipient_account_id) DO UPDATE SET invite_id=excluded.invite_id,inviter_account_id=excluded.inviter_account_id,inviter_identity=excluded.inviter_identity,state='pending',created_utc=excluded.created_utc,updated_utc=excluded.updated_utc;");
            return new Dictionary<string,object>{{"state","pending"},{"groupId",group},{"inviteId",inviteId}};
        }}
        public List<Dictionary<string,object>> GroupInvitesFor(long recipient){lock(gate){
            var results=new List<Dictionary<string,object>>();IntPtr statement=IntPtr.Zero;
            try{string sql="SELECT i.invite_id,i.group_id,i.inviter_account_id,a.username,i.created_utc FROM group_invites i JOIN accounts a ON a.id=i.inviter_account_id WHERE i.recipient_account_id="+recipient+" AND i.state='pending' ORDER BY i.updated_utc DESC,i.invite_id LIMIT 50";
                if(SqliteNative.prepare(database,sql,out statement)!=0)throw new InvalidOperationException("Cannot read group invitations.");
                while(SqliteNative.step(statement)==100)results.Add(new Dictionary<string,object>{{"inviteId",SqliteNative.columnText(statement,0)},{"groupId",SqliteNative.columnText(statement,1)},{"inviterAccountId",SqliteNative.columnLong(statement,2)},{"inviterUsername",SqliteNative.columnText(statement,3)},{"createdUtc",SqliteNative.columnLong(statement,4)}});
                return results;
            }finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
        }}
        public Dictionary<string,object> GroupRespondAccount(string inviteId,long account,string device,bool accept){lock(gate){
            if(!GroupId(inviteId)||!GroupPeerId(device)||!OwnsDevice(account,device))return null;
            string group=ScalarText(database,"SELECT group_id FROM group_invites WHERE invite_id="+Quote(inviteId)+" AND recipient_account_id="+account+" AND state='pending'");if(group==null)return null;
            long inviter=ScalarLong(database,"SELECT inviter_account_id FROM group_invites WHERE invite_id="+Quote(inviteId));
            string inviterDevice=ScalarText(database,"SELECT inviter_identity FROM group_invites WHERE invite_id="+Quote(inviteId));
            string role=GroupRole(group,inviter,inviterDevice);if(role!="owner"&&role!="admin")return null;
            if(accept&&(Blocked(account,inviter)||ScalarLong(database,"SELECT count(*) FROM group_roster_devices WHERE group_id="+Quote(group)+" AND account_id="+account+" AND state='member'")!=0))return null;
            Execute("BEGIN IMMEDIATE;");try{
                Execute("UPDATE group_invites SET state="+Quote(accept?"accepted":"declined")+",updated_utc="+UnixNow()+" WHERE invite_id="+Quote(inviteId)+" AND state='pending';");
                if(accept){Execute("INSERT INTO group_roster_devices(group_id,account_id,public_identity,role,state) VALUES("+Quote(group)+","+account+","+Quote(device)+",'member','member');");Execute("UPDATE group_rosters SET roster_generation=roster_generation+1,distributor_identity=owner_identity WHERE group_id="+Quote(group)+";");}
                Execute("COMMIT;");return accept?GroupRoster(group,account,device):new Dictionary<string,object>{{"state","declined"},{"groupId",group}};
            }catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        public bool GroupRevokeAccountInvite(string inviteId,long actor,string actorDevice){lock(gate){
            if(!GroupId(inviteId)||!GroupPeerId(actorDevice)||!OwnsDevice(actor,actorDevice))return false;
            string group=ScalarText(database,"SELECT group_id FROM group_invites WHERE invite_id="+Quote(inviteId)+" AND state='pending'");if(group==null)return false;
            string role=GroupRole(group,actor,actorDevice);if(role!="owner"&&role!="admin")return false;
            Execute("UPDATE group_invites SET state='revoked',updated_utc="+UnixNow()+" WHERE invite_id="+Quote(inviteId)+" AND state='pending';");return true;
        }}
        public long GroupInviteRecipient(string inviteId){lock(gate)return GroupId(inviteId)?ScalarLong(database,"SELECT recipient_account_id FROM group_invites WHERE invite_id="+Quote(inviteId)):0;}
        public Dictionary<string,object> GroupRespond(string group,long account,string device,bool accept,long expectedGeneration){lock(gate){
            if(!GroupId(group)||String.IsNullOrEmpty(device)||!OwnsDevice(account,device)||GroupRole(group,account,device,"invited")==null||expectedGeneration!=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group)))return null;
            Execute("BEGIN IMMEDIATE;");try{
                if(accept){Execute("UPDATE group_roster_devices SET state='member' WHERE group_id="+Quote(group)+" AND account_id="+account+" AND public_identity="+Quote(device)+" AND state='invited';");Execute("UPDATE group_rosters SET roster_generation=roster_generation+1,distributor_identity=owner_identity WHERE group_id="+Quote(group)+";");}
                else {Execute("DELETE FROM group_roster_devices WHERE group_id="+Quote(group)+" AND account_id="+account+" AND public_identity="+Quote(device)+" AND state='invited';");Execute("UPDATE group_rosters SET roster_generation=roster_generation+1 WHERE group_id="+Quote(group)+";");}
                Execute("COMMIT;");return accept?GroupRoster(group,account,device):new Dictionary<string,object>{{"groupId",group},{"state","declined"}};
            }catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        public Dictionary<string,object> GroupRemove(string group,long actor,string actorDevice,long target,string targetDevice,long expectedGeneration){lock(gate){
            if(!GroupId(group)||String.IsNullOrEmpty(actorDevice)||String.IsNullOrEmpty(targetDevice)||actor==target||!OwnsDevice(actor,actorDevice)||!OwnsDevice(target,targetDevice)||expectedGeneration!=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group)))return null;
            string role=GroupRole(group,actor,actorDevice),targetRole=GroupRole(group,target,targetDevice);
            if(targetRole==null||targetRole=="owner"||role!="owner"&&!(role=="admin"&&targetRole=="member"))return null;
            Execute("BEGIN IMMEDIATE;");try{Execute("DELETE FROM group_roster_devices WHERE group_id="+Quote(group)+" AND account_id="+target+";");Execute("UPDATE group_rosters SET roster_generation=roster_generation+1,distributor_identity="+Quote(actorDevice)+" WHERE group_id="+Quote(group)+";");Execute("COMMIT;");return GroupRoster(group,actor,actorDevice);}catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        public Dictionary<string,object> GroupLeave(string group,long account,string device,long expectedGeneration){lock(gate){
            if(!GroupId(group)||String.IsNullOrEmpty(device)||!OwnsDevice(account,device)||GroupRole(group,account,device)==null||GroupRole(group,account,device)=="owner"||expectedGeneration!=ScalarLong(database,"SELECT roster_generation FROM group_rosters WHERE group_id="+Quote(group)))return null;
            Execute("BEGIN IMMEDIATE;");try{Execute("DELETE FROM group_roster_devices WHERE group_id="+Quote(group)+" AND account_id="+account+";");Execute("UPDATE group_rosters SET roster_generation=roster_generation+1,distributor_identity=owner_identity WHERE group_id="+Quote(group)+";");Execute("COMMIT;");return new Dictionary<string,object>{{"groupId",group},{"state","left"}};}catch{try{Execute("ROLLBACK;");}catch{}throw;}
        }}
        void MakeFriends(long a,long b){Execute("INSERT OR IGNORE INTO friendships(account_low,account_high,created_utc) VALUES("+Low(a,b)+","+High(a,b)+","+UnixNow()+");");}
        // Every state transition is serialized and transactional. The authenticated account ID is
        // supplied by the server session lookup, never by a client-provided sender field.
        public string SendFriendRequest(long sender,long recipient){lock(gate){if(sender==recipient)return "self_request";if(!Exists(recipient))return "not_found";Execute("BEGIN IMMEDIATE;");try{if(Blocked(sender,recipient)){Execute("ROLLBACK;");return "blocked";}if(Friends(sender,recipient)){Execute("ROLLBACK;");return "already_friends";}if(ScalarLong(database,"SELECT count(*) FROM friend_requests WHERE sender_id="+sender+" AND recipient_id="+recipient+" AND state='pending'")>0){Execute("ROLLBACK;");return "already_pending";}if(ScalarLong(database,"SELECT count(*) FROM friend_requests WHERE sender_id="+recipient+" AND recipient_id="+sender+" AND state='pending'")>0){MakeFriends(sender,recipient);Execute("DELETE FROM friend_requests WHERE (sender_id="+sender+" AND recipient_id="+recipient+") OR (sender_id="+recipient+" AND recipient_id="+sender+");");Execute("COMMIT;");return "accepted_crossed";}long now=UnixNow();Execute("INSERT INTO friend_requests(sender_id,recipient_id,state,created_utc,updated_utc) VALUES("+sender+","+recipient+",'pending',"+now+","+now+") ON CONFLICT(sender_id,recipient_id) DO UPDATE SET state='pending',updated_utc=excluded.updated_utc;");Execute("COMMIT;");return "pending";}catch{try{Execute("ROLLBACK;");}catch{}throw;}}}
        public string RespondToRequest(long recipient,long sender,bool accept){lock(gate){Execute("BEGIN IMMEDIATE;");try{if(ScalarLong(database,"SELECT count(*) FROM friend_requests WHERE sender_id="+sender+" AND recipient_id="+recipient+" AND state='pending'")==0){Execute("ROLLBACK;");return "not_found";}if(accept){if(Blocked(sender,recipient)){Execute("ROLLBACK;");return "blocked";}MakeFriends(sender,recipient);Execute("DELETE FROM friend_requests WHERE sender_id="+sender+" AND recipient_id="+recipient+";");Execute("COMMIT;");return "friends";}Execute("UPDATE friend_requests SET state='rejected',updated_utc="+UnixNow()+" WHERE sender_id="+sender+" AND recipient_id="+recipient+";");Execute("COMMIT;");return "rejected";}catch{try{Execute("ROLLBACK;");}catch{}throw;}}}
        public string CancelRequest(long sender,long recipient){lock(gate){int before=(int)ScalarLong(database,"SELECT count(*) FROM friend_requests WHERE sender_id="+sender+" AND recipient_id="+recipient+" AND state='pending'");if(before==0)return "not_found";Execute("DELETE FROM friend_requests WHERE sender_id="+sender+" AND recipient_id="+recipient+" AND state='pending';");return "cancelled";}}
        public string RemoveFriend(long account,long other){lock(gate){int before=(int)ScalarLong(database,"SELECT count(*) FROM friendships WHERE account_low="+Low(account,other)+" AND account_high="+High(account,other));if(before==0)return "not_found";Execute("DELETE FROM friendships WHERE account_low="+Low(account,other)+" AND account_high="+High(account,other)+";");return "removed";}}
        public string SetBlocked(long account,long other,bool blocked){lock(gate){if(account==other)return "self_block";if(!Exists(other))return "not_found";if(blocked){Execute("BEGIN IMMEDIATE;");try{Execute("INSERT OR IGNORE INTO account_blocks(blocker_id,blocked_id,created_utc) VALUES("+account+","+other+","+UnixNow()+");");Execute("DELETE FROM friendships WHERE account_low="+Low(account,other)+" AND account_high="+High(account,other)+"; DELETE FROM friend_requests WHERE (sender_id="+account+" AND recipient_id="+other+") OR (sender_id="+other+" AND recipient_id="+account+");");Execute("COMMIT;");return "blocked";}catch{try{Execute("ROLLBACK;");}catch{}throw;}}Execute("DELETE FROM account_blocks WHERE blocker_id="+account+" AND blocked_id="+other+";");return "unblocked";}}
        List<Dictionary<string,object>> Profiles(string predicate,bool includeEndpoint){var list=new List<Dictionary<string,object>>();IntPtr s=IntPtr.Zero;try{string sql="SELECT a.id,a.username,COALESCE((SELECT d2.public_identity FROM presence p2 JOIN devices d2 ON d2.id=p2.device_id WHERE d2.account_id=a.id ORDER BY p2.seen_utc DESC LIMIT 1),(SELECT d3.public_identity FROM devices d3 WHERE d3.account_id=a.id ORDER BY d3.id LIMIT 1)),COALESCE((SELECT max(seen_utc) FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id),0),COALESCE((SELECT p.endpoint_host FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id ORDER BY p.seen_utc DESC LIMIT 1),''),COALESCE((SELECT p.endpoint_port FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id ORDER BY p.seen_utc DESC LIMIT 1),0),COALESCE((SELECT p.public_endpoint_host FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id ORDER BY p.seen_utc DESC LIMIT 1),''),COALESCE((SELECT p.public_endpoint_port FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id ORDER BY p.seen_utc DESC LIMIT 1),0),COALESCE((SELECT p.public_candidate_source FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id ORDER BY p.seen_utc DESC LIMIT 1),''),COALESCE((SELECT p.observed_classification FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id ORDER BY p.seen_utc DESC LIMIT 1),'') FROM accounts a WHERE "+predicate+" ORDER BY a.username";if(SqliteNative.prepare(database,sql,out s)!=0)return list;while(SqliteNative.step(s)==100){long seen=SqliteNative.columnLong(s,3);var profile=new Dictionary<string,object>{{"accountId",SqliteNative.columnLong(s,0)},{"username",SqliteNative.columnText(s,1)},{"publicIdentity",SqliteNative.columnText(s,2)},{"online",seen>=UnixNow()-90}};if(includeEndpoint){profile["endpointHost"]=SqliteNative.columnText(s,4)??"";profile["endpointPort"]=SqliteNative.columnLong(s,5);profile["publicEndpointHost"]=SqliteNative.columnText(s,6)??"";profile["publicEndpointPort"]=SqliteNative.columnLong(s,7);profile["candidateSeenUtc"]=seen;profile["publicCandidateSource"]=SqliteNative.columnText(s,8)??"";profile["observedAddressClassification"]=SqliteNative.columnText(s,9)??"";}list.Add(LiveProfile(profile,includeEndpoint));}return list;}finally{if(s!=IntPtr.Zero)SqliteNative.finalize(s);}}
        public Dictionary<string,object> FriendState(long account){lock(gate){return new Dictionary<string,object>{{"incoming",Profiles("a.id IN (SELECT sender_id FROM friend_requests WHERE recipient_id="+account+" AND state='pending')",false)},{"outgoing",Profiles("a.id IN (SELECT recipient_id FROM friend_requests WHERE sender_id="+account+" AND state='pending')",false)},{"friends",Profiles("a.id IN (SELECT CASE WHEN account_low="+account+" THEN account_high ELSE account_low END FROM friendships WHERE account_low="+account+" OR account_high="+account+")",true)},{"blocked",Profiles("a.id IN (SELECT blocked_id FROM account_blocks WHERE blocker_id="+account+")",false)}};}}
        public Dictionary<string,object> Statistics(){lock(gate)return new Dictionary<string,object>{{"accounts",ScalarLong(database,"SELECT count(*) FROM accounts")},{"sessions",ScalarLong(database,"SELECT count(*) FROM sessions WHERE expires_utc>="+UnixNow())},{"present",Live.Snapshot(0).Count(d=>OwnsDevice(d.Account,d.Identity))}};}
        static string Like(string value){return value.Replace("\\","\\\\").Replace("%","\\%").Replace("_","\\_");}
        // Local admin browsing is deliberately paged and returns only non-secret operational
        // metadata.  The server retains no separate login timestamp, so lastActiveUtc is the
        // last authenticated presence observation rather than an inferred login time.
        public Dictionary<string,object> AdminAccounts(string query,int offset,int limit){lock(gate){
            query=(query??"").Trim();if(query.Length>64||query.Any(Char.IsControl))throw new InvalidOperationException("Invalid account search.");offset=Math.Max(0,Math.Min(offset,100000));limit=Math.Max(1,Math.Min(limit,50));
            string where="1=1",order; if(query.Length==0)order="last_active DESC,a.created_utc DESC,a.id DESC";else{string exact=Quote(query),prefix=Quote(Like(query)+"%"),contains=Quote("%"+Like(query)+"%");where="a.username LIKE "+contains+" ESCAPE '\\'";order="CASE WHEN a.username="+exact+" COLLATE NOCASE THEN 0 WHEN a.username LIKE "+prefix+" ESCAPE '\\' THEN 1 ELSE 2 END,a.username COLLATE NOCASE,a.id";}
            string sql="SELECT a.id,a.username,a.created_utc,COALESCE((SELECT max(p.seen_utc) FROM presence p JOIN devices d ON d.id=p.device_id WHERE d.account_id=a.id),0) AS last_active,(SELECT count(*) FROM devices d WHERE d.account_id=a.id) AS device_count FROM accounts a WHERE "+where+" ORDER BY "+order+" LIMIT "+(limit+1)+" OFFSET "+offset;var rows=new List<Dictionary<string,object>>();IntPtr statement=IntPtr.Zero;try{if(SqliteNative.prepare(database,sql,out statement)!=0)throw new InvalidOperationException("Could not search accounts.");long onlineAfter=UnixNow()-90;while(SqliteNative.step(statement)==100){long active=SqliteNative.columnLong(statement,3);rows.Add(new Dictionary<string,object>{{"accountId",SqliteNative.columnLong(statement,0)},{"username",SqliteNative.columnText(statement,1)??""},{"createdUtc",SqliteNative.columnLong(statement,2)},{"lastActiveUtc",active},{"online",IsPresent(SqliteNative.columnLong(statement,0))},{"deviceCount",SqliteNative.columnLong(statement,4)}});}bool more=rows.Count>limit;if(more)rows.RemoveAt(rows.Count-1);return new Dictionary<string,object>{{"accounts",rows},{"offset",offset},{"limit",limit},{"hasMore",more}};}finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}
        }}
        public Dictionary<string,object> AdminLookup(string username){lock(gate){if(String.IsNullOrWhiteSpace(username))return null;var page=AdminAccounts(username,0,2);var rows=(List<Dictionary<string,object>>)page["accounts"];return rows.Count==1&&String.Equals((string)rows[0]["username"],username,StringComparison.OrdinalIgnoreCase)?rows[0]:null;}}
        List<long> RelatedAccounts(long accountId){var result=new List<long>();IntPtr statement=IntPtr.Zero;try{string sql="SELECT DISTINCT related FROM (SELECT CASE WHEN account_low="+accountId+" THEN account_high ELSE account_low END AS related FROM friendships WHERE account_low="+accountId+" OR account_high="+accountId+" UNION SELECT CASE WHEN sender_id="+accountId+" THEN recipient_id ELSE sender_id END AS related FROM friend_requests WHERE sender_id="+accountId+" OR recipient_id="+accountId+" UNION SELECT CASE WHEN blocker_id="+accountId+" THEN blocked_id ELSE blocker_id END AS related FROM account_blocks WHERE blocker_id="+accountId+" OR blocked_id="+accountId+")";if(SqliteNative.prepare(database,sql,out statement)!=0)return result;while(SqliteNative.step(statement)==100)result.Add(SqliteNative.columnLong(statement,0));return result;}finally{if(statement!=IntPtr.Zero)SqliteNative.finalize(statement);}}
        public Dictionary<string,object> AdminDeleteAccount(long accountId){lock(gate){string username=Username(accountId);if(String.IsNullOrEmpty(username))return null;var related=RelatedAccounts(accountId);Execute("BEGIN IMMEDIATE;");try{Execute("DELETE FROM sessions WHERE account_id="+accountId+"; DELETE FROM presence WHERE device_id IN (SELECT id FROM devices WHERE account_id="+accountId+"); DELETE FROM group_invites WHERE recipient_account_id="+accountId+" OR inviter_account_id="+accountId+"; DELETE FROM friend_requests WHERE sender_id="+accountId+" OR recipient_id="+accountId+"; DELETE FROM friendships WHERE account_low="+accountId+" OR account_high="+accountId+"; DELETE FROM account_blocks WHERE blocker_id="+accountId+" OR blocked_id="+accountId+"; DELETE FROM devices WHERE account_id="+accountId+"; DELETE FROM accounts WHERE id="+accountId+";");Execute("COMMIT;");return new Dictionary<string,object>{{"accountId",accountId},{"username",username},{"relatedAccounts",related}};}catch{try{Execute("ROLLBACK;");}catch{}throw;}}}
        public bool IsPresent(long accountId){lock(gate)return Live.Snapshot(accountId).Any(d=>OwnsDevice(accountId,d.Identity));}
        Dictionary<string,object> LiveProfile(Dictionary<string,object> profile,bool endpoints=false){
            long account=Convert.ToInt64(profile["accountId"]);var devices=Live.Snapshot(account).Where(d=>OwnsDevice(account,d.Identity)).ToList();
            var selected=devices.FirstOrDefault();profile["online"]=selected!=null;profile["serverEpoch"]=Live.Epoch;profile["presenceRevision"]=Live.Revision;
            profile["devicesOnline"]=devices.Select(d=>(object)new Dictionary<string,object>{{"publicIdentity",d.Identity},{"generation",d.Generation},{"revision",d.Revision}}).ToArray();
            if(selected!=null&&profile.ContainsKey("publicIdentity"))profile["publicIdentity"]=selected.Identity;
            if(endpoints){profile["endpointHost"]=selected==null?"":selected.Host;profile["endpointPort"]=selected==null?0:selected.Port;profile["publicEndpointHost"]=selected==null?"":selected.PublicHost;profile["publicEndpointPort"]=selected==null?0:selected.PublicPort;profile["candidateSeenUtc"]=selected==null?0:UnixNow();profile["publicCandidateSource"]=selected==null?"":selected.Source;profile["observedAddressClassification"]=selected==null?"":selected.Classification;}
            return profile;
        }
        public string Username(long accountId){lock(gate)return ScalarText(database,"SELECT username FROM accounts WHERE id="+accountId);}
        static long UnixNow(){return DateTimeOffset.UtcNow.ToUnixTimeSeconds();}
        bool disposed;public void Dispose(){lock(gate){if(disposed)return;if(Sqlite.sqlite3_close(database)!=0)throw new InvalidOperationException("SQLite close failed: outstanding statement or backup");disposed=true;}}
    }
    static class SqliteNative {
        [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int sqlite3_prepare_v2(IntPtr d,
#if PEERCHAT_PORTABLE
        [MarshalAs(UnmanagedType.LPUTF8Str)]
#endif
        string q,int n,out IntPtr s,IntPtr tail);
        [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int sqlite3_step(IntPtr s);
        [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int sqlite3_finalize(IntPtr s);
        [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern long sqlite3_column_int64(IntPtr s,int c);
        [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr sqlite3_column_text(IntPtr s,int c);
        [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern IntPtr sqlite3_column_blob(IntPtr s,int c);
        [DllImport("winsqlite3.dll",CallingConvention=CallingConvention.Cdecl)] internal static extern int sqlite3_column_bytes(IntPtr s,int c);
        internal static int prepare(IntPtr d,string q,out IntPtr s){return sqlite3_prepare_v2(d,q,-1,out s,IntPtr.Zero);} internal static int step(IntPtr s){return sqlite3_step(s);} internal static void finalize(IntPtr s){sqlite3_finalize(s);} internal static long columnLong(IntPtr s,int c){return sqlite3_column_int64(s,c);} internal static string columnText(IntPtr s,int c){var p=sqlite3_column_text(s,c);
#if PEERCHAT_PORTABLE
        return p==IntPtr.Zero?null:Marshal.PtrToStringUTF8(p);
#else
        return p==IntPtr.Zero?null:Marshal.PtrToStringAnsi(p);
#endif
} internal static byte[] columnBlob(IntPtr s,int c){int n=sqlite3_column_bytes(s,c);var p=sqlite3_column_blob(s,c);var b=new byte[n];if(n>0)Marshal.Copy(p,b,0,n);return b;}
    }
    // One-shot proof challenges, not a session/reconnect owner. Original-message
    // SHA256/ECDSA preserves the accepted device signature representation.
    internal sealed class RememberedDeviceProofs {
        internal sealed class Challenge {
            public string Id, Device, Action, Binding;
            public long Account, Expires;
            public byte[] Input { get { return Encoding.ASCII.GetBytes("PeerChat remembered device v1|"+Action+"|"+Account.ToString(CultureInfo.InvariantCulture)+"|"+Device+"|"+Id+"|"+Binding); } }
        }
        readonly object gate=new object();readonly Dictionary<string,Challenge> pending=new Dictionary<string,Challenge>();
        public Challenge Issue(long account,string device,string action,string binding) {
            lock(gate){long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();foreach(var id in pending.Where(p=>p.Value.Expires<now).Select(p=>p.Key).ToArray())pending.Remove(id);
                if(pending.Count>=1024||pending.Values.Count(p=>p.Account==account&&p.Device==device)>=4)throw new InvalidOperationException("Proof challenge limit.");
                byte[] bytes=new byte[32];using(var rng=RandomNumberGenerator.Create())rng.GetBytes(bytes);
                var value=new Challenge{Id=BitConverter.ToString(bytes).Replace("-","").ToLowerInvariant(),Account=account,Device=device,Action=action,Binding=binding,Expires=now+60};pending.Add(value.Id,value);return value;
            }
        }
        public bool Consume(string id,long account,string device,string action,string binding,byte[] publicKey,byte[] signature) {
            Challenge value;lock(gate){if(!pending.TryGetValue(id,out value))return false;pending.Remove(id);}
            if(value.Expires<DateTimeOffset.UtcNow.ToUnixTimeSeconds()||value.Account!=account||value.Device!=device||value.Action!=action||value.Binding!=binding||publicKey==null||publicKey.Length!=72||signature==null||signature.Length!=64)return false;
            try{using(var sha=SHA256.Create()){
                if(BitConverter.ToString(sha.ComputeHash(publicKey)).Replace("-","").ToLowerInvariant()!=device)return false;
#if PEERCHAT_PORTABLE
                return PortablePlatform.VerifyHash(publicKey,sha.ComputeHash(value.Input),signature);
#else
                using(var key=CngKey.Import(publicKey,CngKeyBlobFormat.EccPublicBlob))using(var verifier=new ECDsaCng(key))return verifier.VerifyHash(sha.ComputeHash(value.Input),signature);
#endif
            }}catch(CryptographicException){return false;}
        }
    }

    // Provider receives only a destination token and fixed opaque hint. No
    // message, identity, account or relationship data reaches a push vendor.
    internal enum PushResult { Sent, Unavailable, InvalidToken }
    internal interface IPushProvider { PushResult Wake(string destinationToken); }
    internal sealed class NullPushProvider:IPushProvider { public PushResult Wake(string destinationToken){return PushResult.Unavailable;} }
    internal sealed class FcmHttpResponse {public int Status;public string Body;}
    internal static class ReliableIoDiagnostics {
        public static string RequestKind(string type){return new[]{"events","relay_attach","push_hint","push_register","push_remove","presence","messaging_bootstrap","login","logout","remember_resume"}.Contains(type)?type:"request";}
        public static bool ExpectedDisconnect(Exception error,string kind){
            if(kind!="events"&&kind!="relay_attach")return false;
            var socket=error as SocketException??error.InnerException as SocketException;
            return error is EndOfStreamException||socket!=null&&(socket.SocketErrorCode==SocketError.ConnectionReset||socket.SocketErrorCode==SocketError.ConnectionAborted||socket.SocketErrorCode==SocketError.OperationAborted||socket.SocketErrorCode==SocketError.Shutdown);
        }
    }
    // FCM HTTP v1 + OAuth service-account JWT, using platform RSA/SHA256, not
    // custom cryptography. Transport is injectable for deterministic host tests.
    internal sealed class FcmPushProvider:IPushProvider {
        readonly string project,email;readonly Func<byte[],byte[]> sign;
        readonly Func<string,string,string,FcmHttpResponse> http;
        readonly object gate=new object();string accessToken;long tokenExpires;
        readonly JavaScriptSerializer json=new JavaScriptSerializer{MaxJsonLength=65536};
        const string TokenUrl="https://oauth2.googleapis.com/token";
        public FcmPushProvider(string project,string email,Func<byte[],byte[]> sign,Func<string,string,string,FcmHttpResponse> http){
            if(!System.Text.RegularExpressions.Regex.IsMatch(project??"","^[a-z][a-z0-9-]{4,62}$")||String.IsNullOrEmpty(email)||email.Length>254||!email.EndsWith(".iam.gserviceaccount.com",StringComparison.Ordinal)||sign==null||http==null)throw new InvalidOperationException("Invalid FCM provider configuration.");
            this.project=project;this.email=email;this.sign=sign;this.http=http;
        }
        public static IPushProvider Create(ServerConfiguration config){
            if(!config.FcmEnabled)return new NullPushProvider();
            try{
                if(!Path.IsPathRooted(config.FcmServiceAccountPath)||new FileInfo(config.FcmServiceAccountPath).Length>65536)throw new InvalidOperationException();
                var fields=new JavaScriptSerializer{MaxJsonLength=65536}.Deserialize<Dictionary<string,object>>(
#if PEERCHAT_PORTABLE
                    PortablePlatform.ReadSecret(config.FcmServiceAccountPath)
#else
                    File.ReadAllText(config.FcmServiceAccountPath)
#endif
);
                if((string)fields["type"]!="service_account"||(string)fields["project_id"]!=config.FcmProjectId||(string)fields["token_uri"]!=TokenUrl)throw new InvalidOperationException();
                string pem=(string)fields["private_key"];if(!pem.StartsWith("-----BEGIN PRIVATE KEY-----",StringComparison.Ordinal)||!pem.TrimEnd().EndsWith("-----END PRIVATE KEY-----",StringComparison.Ordinal))throw new InvalidOperationException();
                byte[] key=Convert.FromBase64String(pem.Replace("-----BEGIN PRIVATE KEY-----","").Replace("-----END PRIVATE KEY-----",""));
#if PEERCHAT_PORTABLE
                Func<byte[],byte[]> signer=input=>PortablePlatform.RsaSign(key,input);
#else
                Func<byte[],byte[]> signer=input=>{using(var imported=CngKey.Import(key,CngKeyBlobFormat.Pkcs8PrivateBlob))using(var rsa=new RSACng(imported))return rsa.SignData(input,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);};
#endif
                // Validate key/type before accepting enabled configuration.
                signer(Encoding.ASCII.GetBytes("PeerChat FCM provider startup validation"));
                return new FcmPushProvider(config.FcmProjectId,(string)fields["client_email"],signer,Post);
            }catch{throw new InvalidOperationException("FCM is enabled but its external service-account configuration is invalid or unavailable.");}
        }
        static string B64(byte[] bytes){return Convert.ToBase64String(bytes).TrimEnd('=').Replace('+','-').Replace('/','_');}
        string AccessToken(){lock(gate){
            long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();if(accessToken!=null&&tokenExpires>now+60)return accessToken;
            var claims=new Dictionary<string,object>{{"iss",email},{"scope","https://www.googleapis.com/auth/firebase.messaging"},{"aud",TokenUrl},{"iat",now},{"exp",now+3600}};
            string unsigned=B64(Encoding.UTF8.GetBytes("{\"alg\":\"RS256\",\"typ\":\"JWT\"}"))+"."+B64(Encoding.UTF8.GetBytes(json.Serialize(claims)));
            string assertion=unsigned+"."+B64(sign(Encoding.ASCII.GetBytes(unsigned)));
            var response=http(TokenUrl,null,"grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion="+Uri.EscapeDataString(assertion));
            if(response.Status!=200)return null;
            var fields=json.Deserialize<Dictionary<string,object>>(response.Body);object value,seconds;
            if(!fields.TryGetValue("access_token",out value)||!(value is string)||String.IsNullOrEmpty((string)value)||((string)value).Length>8192||!fields.TryGetValue("expires_in",out seconds))return null;
            int expiry=Convert.ToInt32(seconds);if(expiry<120||expiry>3600)return null;
            accessToken=(string)value;tokenExpires=now+expiry;return accessToken;
        }}
        public PushResult Wake(string destinationToken){try{
            if(String.IsNullOrEmpty(destinationToken)||destinationToken.Length>2048)return PushResult.Unavailable;
            string auth=AccessToken();if(auth==null)return PushResult.Unavailable;
            var body=new Dictionary<string,object>{{"message",new Dictionary<string,object>{{"token",destinationToken},{"data",new Dictionary<string,string>{{"event","peerchat_pending"},{"version","1"}}},{"android",new Dictionary<string,object>{{"priority","HIGH"},{"ttl","60s"},{"collapse_key","peerchat_pending"}}}}}};
            var response=http("https://fcm.googleapis.com/v1/projects/"+project+"/messages:send",auth,json.Serialize(body));
            if(response.Status==200)return PushResult.Sent;
            if(response.Status==401){lock(gate){accessToken=null;tokenExpires=0;}return PushResult.Unavailable;}
            var fields=json.Deserialize<Dictionary<string,object>>(response.Body);object error,details;
            if(fields.TryGetValue("error",out error)){var e=error as Dictionary<string,object>;if(e!=null&&e.TryGetValue("details",out details))foreach(var item in details as System.Collections.IEnumerable??new object[0]){
                var d=item as Dictionary<string,object>;object type,code;if(d!=null&&d.TryGetValue("@type",out type)&&(string)type=="type.googleapis.com/google.firebase.fcm.v1.FcmError"&&d.TryGetValue("errorCode",out code)&&(string)code=="UNREGISTERED")return PushResult.InvalidToken;
            }}return PushResult.Unavailable;
        }catch{return PushResult.Unavailable;}}
        static FcmHttpResponse Post(string url,string bearer,string body){
            var request=(HttpWebRequest)WebRequest.Create(url);request.Method="POST";request.AllowAutoRedirect=false;request.Timeout=10000;request.ReadWriteTimeout=10000;
            request.ContentType=bearer==null?"application/x-www-form-urlencoded":"application/json";
            if(bearer!=null)request.Headers[HttpRequestHeader.Authorization]="Bearer "+bearer;
            byte[] bytes=Encoding.UTF8.GetBytes(body);request.ContentLength=bytes.Length;using(var stream=request.GetRequestStream())stream.Write(bytes,0,bytes.Length);
            HttpWebResponse response;try{response=(HttpWebResponse)request.GetResponse();}catch(WebException error){response=error.Response as HttpWebResponse;if(response==null)throw;}
            using(response)using(var stream=response.GetResponseStream())using(var output=new MemoryStream()){
                byte[] buffer=new byte[4096];int read;while((read=stream.Read(buffer,0,buffer.Length))>0){if(output.Length+read>65536)throw new InvalidDataException("FCM response exceeds bound.");output.Write(buffer,0,read);}
                return new FcmHttpResponse{Status=(int)response.StatusCode,Body=Encoding.UTF8.GetString(output.ToArray())};
            }
        }
    }
    internal sealed class PushWakeDispatcher {
        readonly AccountDatabase accounts;readonly IPushProvider provider;readonly object gate=new object();
        readonly Dictionary<string,long> last=new Dictionary<string,long>();int pending;bool stopped;
        readonly Action<long,string,PushResult> report;
        public PushWakeDispatcher(AccountDatabase accounts,IPushProvider provider,Action<long,string,PushResult> report=null){this.accounts=accounts;this.provider=provider;this.report=report;}
        public bool Hint(long account,string device) {
            string token;
            lock(gate){if(stopped||provider is NullPushProvider)return false;token=accounts.PushToken(account,device);if(token==null)return false;long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();foreach(var key in last.Where(p=>p.Value<now-30).Select(p=>p.Key).ToArray())last.Remove(key);
                long previous;if(pending>=32||last.Count>=4096||(last.TryGetValue(device,out previous)&&previous>=now-30))return false;
                last[device]=now;pending++;
            }
            ThreadPool.QueueUserWorkItem(_=>{try{
                lock(gate){if(stopped||accounts.PushToken(account,device)!=token)return;}
                var result=provider.Wake(token);
                if(report!=null)try{report(account,device,result);}catch{}
                if(result==PushResult.InvalidToken)lock(gate){if(!stopped)accounts.RemovePush(account,device,token);}
            }catch{/* Push failure is never message/transport failure; no token logging. */}
            finally{lock(gate){pending--;Monitor.PulseAll(gate);}}});return true;
        }
        // After Stop returns, queued callbacks cannot touch the account DB.
        public void Stop(){lock(gate){stopped=true;last.Clear();}}
        public bool Drain(int milliseconds){var clock=System.Diagnostics.Stopwatch.StartNew();lock(gate){while(pending!=0){int left=milliseconds-(int)clock.ElapsedMilliseconds;if(left<=0)return false;Monitor.Wait(gate,left);}return true;}}
    }

        sealed partial class DiscoveryServer {
        readonly object workerGate=new object();
        readonly HashSet<TcpClient> workerClients=new HashSet<TcpClient>();
        int workerCount;
        NamedPipeServerStream managementStream;
        void QueueWorker(Action work,TcpClient client=null){
            lock(workerGate){if(stopping){if(client!=null)client.Close();return;}workerCount++;if(client!=null)workerClients.Add(client);}
            ThreadPool.QueueUserWorkItem(_=>{
                try{work();}catch(Exception error){if(!stopping)Log("WARNING","Server worker ended: "+error.GetType().Name);}
                finally{lock(workerGate){if(client!=null){workerClients.Remove(client);client.Close();}workerCount--;Monitor.PulseAll(workerGate);}}
            });
        }
        internal void RequestStop(){
            lock(workerGate){if(stopping)return;stopping=true;}
            Log("INFO","Graceful shutdown requested.");try{if(listener!=null)listener.Stop();}catch{}
        }
        void DrainWorkers(){
            if(listener!=null)listener.Stop();pushWakes.Stop();
            lock(workerGate){foreach(var client in workerClients)try{client.Close();}catch{}if(managementStream!=null)try{managementStream.Dispose();}catch{}}
            // No database disposal until all application workers have retired.
            // Relay pairing is bounded to 15 seconds; closing sockets unblocks IO.
            var clock=System.Diagnostics.Stopwatch.StartNew();
            lock(workerGate){while(workerCount!=0){int left=20000-(int)clock.ElapsedMilliseconds;
                if(left<=0)throw new TimeoutException("Server workers did not drain; database deliberately left open for process termination.");Monitor.Wait(workerGate,left);}}
            if(!pushWakes.Drain(10000))throw new TimeoutException("Push workers did not drain; database deliberately left open.");
            lock(eventGate){eventClients.Clear();onlineAccounts.Clear();}
            CloseAttachmentCache();accounts.Dispose();certificate.Dispose();Log("INFO","Server shutdown complete; workers drained and SQLite closed.");
        }
        public void Run(){
            startedUtc=DateTime.UtcNow;listener=new TcpListener(IPAddress.Any,config.Port);
            try{
                listener.Start(64);Log("INFO","PeerChat Server "+VersionInfo.Display+" listener started on 0.0.0.0:"+config.Port+" (TLS enabled).");
                using(var sha=SHA256.Create())Log("INFO","Server certificate SHA-256: "+BitConverter.ToString(sha.ComputeHash(certificate.RawData)).Replace("-",""));
                Log("INFO",config.TurnEnabled?"TURN media credential issuer enabled.":"TURN media credential issuer disabled.");
                QueueWorker(PresenceMonitor);if(!String.IsNullOrEmpty(managementPipe))QueueWorker(ManagementLoop);
                while(!stopping){try{var client=listener.AcceptTcpClient();QueueWorker(()=>Serve(client),client);}
                    catch(SocketException){if(!stopping)throw;}catch(ObjectDisposedException){if(!stopping)throw;}}
            }finally{RequestStop();DrainWorkers();}
        }
        readonly RememberedDeviceProofs rememberedProofs=new RememberedDeviceProofs();
        readonly PushWakeDispatcher pushWakes;
        const int ProtocolVersion=1, MaxRequestBytes=4096, MaxRelayPayloadBytes=64*1024, TurnCredentialRateWindowSeconds=60, TurnCredentialRateLimit=6; readonly ServerConfiguration config; readonly AccountDatabase accounts; readonly JavaScriptSerializer json=new JavaScriptSerializer{MaxJsonLength=16384}; readonly X509Certificate2 certificate; TcpListener listener; volatile bool stopping; int connectedClients; DateTime startedUtc; string managementPipe; readonly object eventGate=new object(); readonly List<EventClient> eventClients=new List<EventClient>(); readonly Dictionary<string,long> supersededEventSessions=new Dictionary<string,long>(StringComparer.Ordinal); readonly HashSet<long> onlineAccounts=new HashSet<long>(); readonly Dictionary<long,string> onlineCandidateSignatures=new Dictionary<long,string>();
        const int EventHeartbeatSeconds=20, EventLeaseSeconds=75; static readonly System.Diagnostics.Stopwatch leaseClock=System.Diagnostics.Stopwatch.StartNew();
        readonly GroupCiphertextFanout groupFanout;
        readonly GroupMediaRooms groupRooms;
        void BroadcastGroupRooms(string groupId){
            List<GroupFanoutSession> sessions;lock(eventGate)sessions=eventClients.Select(GroupSession).ToList();
            foreach(var session in sessions){
                bool changed;
                var result=groupRooms.Apply(session,new Dictionary<string,object>{{"groupId",groupId},{"requestId",Guid.NewGuid().ToString("N")},{"operation","snapshot"}},out changed);
                object snapshot;if(result.TryGetValue("snapshot",out snapshot)&&session.Current())
                    try{session.Send(new Dictionary<string,object>{{"type","group_room_snapshot"},{"snapshot",snapshot}});}catch{}
            }
        }
        void HandleGroupRooms(EventClient client,Dictionary<string,object> request){
            var session=GroupSession(client);
            if(Text(request,"type")=="group_media"){
                List<GroupFanoutSession> sessions;lock(eventGate)sessions=eventClients.Select(GroupSession).ToList();
                session.Send(groupRooms.Signal(session,request,sessions));return;
            }
            bool changed;var result=groupRooms.Apply(session,request,out changed);
            session.Send(result);if(changed)BroadcastGroupRooms(Text(request,"groupId"));
        }
        readonly JavaScriptSerializer groupJson=new JavaScriptSerializer{MaxJsonLength=GroupCiphertextFanout.MaxFrame};
        GroupFanoutSession GroupSession(EventClient client){return new GroupFanoutSession{Account=client.AccountId,Device=client.PublicIdentity,Binding=client.Binding,Current=()=>client.Active&&leaseClock.ElapsedMilliseconds-client.LastHeartbeatUtc<EventLeaseSeconds*1000L&&accounts.Authenticate(client.SessionTokenHash)==client.AccountId&&accounts.OwnsDevice(client.AccountId,client.PublicIdentity),Send=fields=>{lock(client.Gate)WriteFrame(client.Tls,Encoding.UTF8.GetBytes(groupJson.Serialize(fields)));}};}
        void HandleGroupChannel(EventClient client,Dictionary<string,object> request){
            var origin=GroupSession(client);Dictionary<string,object> result;
            string group=Text(request,"groupId"),message=Text(request,"messageId");
            try{
                if(Text(request,"type")=="group_ack")result=groupFanout.Ack(origin,group,message,Text(request,"state"));
                else {
                    long generation;string encoded=Text(request,"envelope");object recipients;
                    if(request.TryGetValue("recipients",out recipients)||encoded==null||encoded.Length>87720||!Int64.TryParse(Text(request,"rosterGeneration"),out generation))throw new InvalidDataException();
                    List<GroupFanoutSession> sessions;lock(eventGate)sessions=eventClients.Select(GroupSession).ToList();
                    byte[] envelope=Convert.FromBase64String(encoded);
                    if(envelope.Length<254||Encoding.ASCII.GetString(envelope,54,32)!=message)throw new InvalidDataException();
                    result=groupFanout.Send(origin,group,generation,envelope,sessions);
                }
            }catch{result=new Dictionary<string,object>{{"type","group_delivery"},{"groupId",group??""},{"messageId",message??""},{"state","malformed_request"}};}
            if(!result.ContainsKey("messageId")||String.IsNullOrEmpty(Convert.ToString(result["messageId"])))result["messageId"]=message??"";
            origin.Send(result);
        }
        sealed class EventClient : IDisposable { public readonly TcpClient Client; public readonly SslStream Tls; public readonly long AccountId; public readonly string PublicIdentity,SessionTokenHash; public readonly bool ClientHeartbeats; public readonly object Gate=new object(); public readonly string Binding=Guid.NewGuid().ToString("N"); public volatile bool Active=true;long lastHeartbeatUtc; public EventClient(TcpClient client,SslStream tls,long accountId,string identity,string tokenHash,bool clientHeartbeats){Client=client;Tls=tls;AccountId=accountId;PublicIdentity=identity;SessionTokenHash=tokenHash;ClientHeartbeats=clientHeartbeats;Heartbeat();} public long LastHeartbeatUtc{get{return Interlocked.Read(ref lastHeartbeatUtc);}} public void Heartbeat(){Interlocked.Exchange(ref lastHeartbeatUtc,leaseClock.ElapsedMilliseconds);} public void Dispose(){Active=false;try{Tls.Dispose();}catch{}try{Client.Close();}catch{}} }
        // Never persisted: this is permission to exchange one temporary reliable-route hint, not
        // a presence record, friendship mutation, or peer-trust decision.
        sealed class DirectProbe { public string Id,InitiatorDevice,RecipientDevice,InitiatorHost,RecipientHost;public long Initiator,Recipient,Expires;public int InitiatorPort,RecipientPort;public bool Consented,InitiatorSubmitted,RecipientSubmitted; }
        sealed class RelayEndpoint {
            public readonly TcpClient Client;public readonly SslStream Tls;public readonly long AccountId,TargetAccountId,RosterGeneration;public readonly string DeviceId,TargetDeviceId,Binding,Scope,GroupId,SessionTokenHash;public readonly object WriteGate=new object();public readonly TaskCompletionSource<bool> Ready=new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            public readonly string DiagnosticId=Guid.NewGuid().ToString("N").Substring(0,12);public string PairDiagnosticId;
            public RelayEndpoint Peer;public string SessionId;public bool Incoming;
            public readonly ArchiveRelayAuthorization Archive;
            public string ArchiveKey {get{return Archive==null?"":Archive.Key;}}
            public RelayEndpoint(TcpClient client,SslStream tls,long account,string device,long target,string targetDevice,string scope,string groupId,long rosterGeneration,string tokenHash,ArchiveRelayAuthorization archive=null){Client=client;Tls=tls;AccountId=account;DeviceId=device;TargetAccountId=target;TargetDeviceId=targetDevice;Scope=scope;GroupId=groupId;RosterGeneration=rosterGeneration;SessionTokenHash=tokenHash;Archive=archive;Binding=scope+":"+groupId+":"+rosterGeneration+":"+account+":"+device+">"+target+":"+targetDevice+(archive==null?"":":"+archive.Key);}
        }
        readonly object relayGate=new object();readonly List<RelayEndpoint> waitingRelays=new List<RelayEndpoint>();readonly List<RelayEndpoint> relayEndpoints=new List<RelayEndpoint>();readonly HashSet<string> activeRelayBindings=new HashSet<string>(StringComparer.Ordinal);
        readonly object directProbeGate=new object();readonly Dictionary<string,DirectProbe> directProbes=new Dictionary<string,DirectProbe>(StringComparer.Ordinal);const int DirectProbeLifetimeSeconds=60;
        sealed class TurnCredentialRate { public long WindowUtc; public int Count; }
        readonly object turnCredentialGate=new object();readonly Dictionary<string,TurnCredentialRate> turnCredentialRates=new Dictionary<string,TurnCredentialRate>(StringComparer.Ordinal);
        readonly object peopleSearchGate=new object();readonly Dictionary<long,Queue<long>> peopleSearchTimes=new Dictionary<long,Queue<long>>();
        bool PermitPeopleSearch(long accountId){lock(peopleSearchGate){long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();Queue<long> times;if(!peopleSearchTimes.TryGetValue(accountId,out times)){times=new Queue<long>();peopleSearchTimes[accountId]=times;}while(times.Count>0&&times.Peek()<=now-60)times.Dequeue();if(times.Count>=20)return false;times.Enqueue(now);return true;}}
        // A certificate can be loaded from a PFX for ordinary deployment or from CurrentUser\My
        // during local development. Neither path ever exposes its private key to clients.
        public DiscoveryServer(ServerConfiguration c,IPushProvider pushProvider=null){config=c;managementPipe=c.ManagementPipe;accounts=new AccountDatabase(c.DatabasePath);accounts.InitializeMailbox();certificate=LoadCertificate(c);groupFanout=new GroupCiphertextFanout(accounts);groupRooms=new GroupMediaRooms(accounts);pushWakes=new PushWakeDispatcher(accounts,pushProvider??FcmPushProvider.Create(c),(account,device,result)=>Log("DEBUG","Opaque push attempt account="+account+" device="+device.Substring(0,12)+" result="+result+" (provider acceptance is not message delivery)."));Log("INFO",c.FcmEnabled?"FCM opaque wake provider configured.":"FCM opaque wake provider disabled (no delivery claimed).");}
        static X509Certificate2 LoadCertificate(ServerConfiguration config){
#if PEERCHAT_PORTABLE
            if(!OperatingSystem.IsWindows()){
                if(!String.IsNullOrEmpty(config.CertificateThumbprint)||!Path.IsPathRooted(config.CertificatePath))throw new InvalidOperationException("Linux requires an explicit PFX file, not a Windows certificate store.");
                PortablePlatform.ValidateSecretFile(config.CertificatePath,16*1024*1024);
                string password=String.IsNullOrEmpty(config.CertificatePasswordFile)?config.CertificatePassword:PortablePlatform.ReadSecret(config.CertificatePasswordFile,4096).TrimEnd('\r','\n');
                return X509CertificateLoader.LoadPkcs12FromFile(config.CertificatePath,password,X509KeyStorageFlags.EphemeralKeySet);
            }
#endif
            if(!String.IsNullOrWhiteSpace(config.CertificateThumbprint)){var store=new X509Store(StoreName.My,StoreLocation.CurrentUser);store.Open(OpenFlags.ReadOnly);try{var found=store.Certificates.Find(X509FindType.FindByThumbprint,config.CertificateThumbprint.Replace(" ",""),false);if(found.Count==0||!found[0].HasPrivateKey)throw new InvalidOperationException("Configured certificate thumbprint was not found with a private key in CurrentUser\\My.");return found[0];}finally{store.Close();}}
            return new X509Certificate2(config.CertificatePath,config.CertificatePassword,X509KeyStorageFlags.UserKeySet|X509KeyStorageFlags.PersistKeySet);
        }
        static bool ValidUsername(string value){if(String.IsNullOrEmpty(value)||value.Length<3||value.Length>32)return false;foreach(char c in value)if(!(Char.IsLetterOrDigit(c)||c=='-'||c=='_'))return false;return true;}
        static bool ValidIdentity(string value){return !String.IsNullOrEmpty(value)&&value.Length>=32&&value.Length<=2048;}
        static bool ValidEndpoint(string host,int port){if(String.IsNullOrEmpty(host))return port==0;if(host.Length>253||port<1||port>65535)return false;return !host.Any(c=>Char.IsWhiteSpace(c)||Char.IsControl(c));}
        static bool IsLocalInterfaceAddress(string host){IPAddress address;if(!IPAddress.TryParse(host,out address))return false;try{return NetworkInterface.GetAllNetworkInterfaces().Where(n=>n.OperationalStatus==OperationalStatus.Up).SelectMany(n=>n.GetIPProperties().UnicastAddresses).Any(u=>u.Address.Equals(address));}catch{return false;}}
        static bool IsUsablePublicIpv4(string host,out string classification){classification="Server-observed Internet address";IPAddress address;if(!IPAddress.TryParse(host,out address)||address.AddressFamily!=AddressFamily.InterNetwork){classification=String.IsNullOrEmpty(host)?"Missing": "Not an IPv4 address";return false;}byte[] b=address.GetAddressBytes();if(address.Equals(IPAddress.Loopback)||b[0]==127){classification="Loopback — rejected as public candidate";return false;}if(address.Equals(IPAddress.Any)||b[0]==0){classification="Unspecified — rejected as public candidate";return false;}if(b[0]==10||(b[0]==172&&b[1]>=16&&b[1]<=31)||(b[0]==192&&b[1]==168)){classification="Private RFC1918 — rejected as public candidate";return false;}if(b[0]==169&&b[1]==254){classification="Link-local — rejected as public candidate";return false;}if(b[0]==100&&b[1]>=64&&b[1]<=127){classification="Shared address space — rejected as public candidate";return false;}if(b[0]>=224){classification="Multicast/reserved — rejected as public candidate";return false;}return true;}
        static string RandomToken(){var b=new byte[32];using(var r=RandomNumberGenerator.Create())r.GetBytes(b);return Convert.ToBase64String(b).TrimEnd('=').Replace('+','-').Replace('/','_');}
        static string TokenHash(string token){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(token))).Replace("-","").ToLowerInvariant();}
        static string DeviceHint(string identity){using(var hash=SHA256.Create())return BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(identity??""))).Replace("-","").ToLowerInvariant().Substring(0,16);}
        static byte[] PasswordHash(string password,byte[] salt){using(var kdf=new Rfc2898DeriveBytes(password,salt,310000,HashAlgorithmName.SHA256))return kdf.GetBytes(32);}
        // Do not use ordinary equality here: its early exit can leak how much of a stored password
        // verifier matched. .NET Framework lacks the public FixedTimeEquals API available on newer runtimes.
        static bool ConstantTimeEquals(byte[] a,byte[] b){if(a==null||b==null||a.Length!=b.Length)return false;int difference=0;for(int i=0;i<a.Length;i++)difference|=a[i]^b[i];return difference==0;}
        bool AdmitTurnCredentialIssue(long accountId,string device){long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();string key=accountId+":"+DeviceHint(device);lock(turnCredentialGate){foreach(var expired in turnCredentialRates.Where(x=>now-x.Value.WindowUtc>=TurnCredentialRateWindowSeconds).Select(x=>x.Key).ToArray())turnCredentialRates.Remove(expired);TurnCredentialRate rate;if(!turnCredentialRates.TryGetValue(key,out rate)){turnCredentialRates[key]=new TurnCredentialRate{WindowUtc=now,Count=1};return true;}if(rate.Count>=TurnCredentialRateLimit)return false;++rate.Count;return true;}}
        Dictionary<string,object> MediaTurnCredentials(long accountId,string device){
            if(!config.TurnEnabled)return Error("turn_unavailable");
            if(!ValidIdentity(device)||!accounts.OwnsDevice(accountId,device))return Error("media_identity_invalid");
            if(!AdmitTurnCredentialIssue(accountId,device))return Error("turn_credential_rate_limited");
            long expiry=DateTimeOffset.UtcNow.AddSeconds(config.TurnCredentialTtlSeconds).ToUnixTimeSeconds();
            string username=expiry+":"+accountId+":"+DeviceHint(device)+":"+RandomToken();
            string credential;using(var hmac=new HMACSHA1(Encoding.UTF8.GetBytes(config.TurnRestSharedSecret)))credential=Convert.ToBase64String(hmac.ComputeHash(Encoding.UTF8.GetBytes(username)));
            Log("DEBUG","Issued temporary TURN/UDP credentials for account "+accountId+"; expiry="+expiry+".");
            return Ok(new Dictionary<string,object>{{"host",config.TurnPublicHost},{"port",config.TurnUdpPort},{"transport","udp"},{"username",username},{"password",credential},{"expiresUtc",expiry}});
        }
        void Broadcast(long accountId,long other,string eventName,bool includeOther=true){var payload=Encoding.UTF8.GetBytes(json.Serialize(new Dictionary<string,object>{{"ok",true},{"version",ProtocolVersion},{"rpcVersion",1},{"type","friend_event"},{"event",eventName},{"accountId",other}}));List<EventClient> dead=new List<EventClient>();lock(eventGate){foreach(var subscriber in eventClients){if(subscriber.AccountId!=accountId&&(!includeOther||subscriber.AccountId!=other))continue;try{lock(subscriber.Gate)WriteFrame(subscriber.Tls,payload);}catch{dead.Add(subscriber);}}foreach(var client in dead)eventClients.Remove(client);}foreach(var client in dead)client.Dispose();}
        void Notify(long accountId,Dictionary<string,object> fields){fields["ok"]=true;fields["version"]=ProtocolVersion;fields["type"]="friend_event";List<EventClient> dead=new List<EventClient>();var payload=Encoding.UTF8.GetBytes(json.Serialize(fields));lock(eventGate){foreach(var subscriber in eventClients.Where(value=>value.AccountId==accountId)){try{lock(subscriber.Gate)WriteFrame(subscriber.Tls,payload);}catch{dead.Add(subscriber);}}foreach(var client in dead)eventClients.Remove(client);}foreach(var client in dead)client.Dispose();}
        void PurgeDirectProbes(){long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();lock(directProbeGate){foreach(var id in directProbes.Where(value=>value.Value.Expires<=now||!accounts.Friends(value.Value.Initiator,value.Value.Recipient)).Select(value=>value.Key).ToArray())directProbes.Remove(id);}}
        void InvalidateDirectProbes(long account,long other=0){List<DirectProbe> removed;lock(directProbeGate){removed=directProbes.Values.Where(p=>(p.Initiator==account||p.Recipient==account)&&(other==0||p.Initiator==other||p.Recipient==other)).ToList();foreach(var p in removed)directProbes.Remove(p.Id);}foreach(var p in removed)Notify(p.Initiator==account?p.Recipient:p.Initiator,new Dictionary<string,object>{{"event","p2p_cancelled"},{"requestId",p.Id}});}
        void BroadcastPresence(long accountId,string publicIdentity,string endpointHost,int endpointPort,string publicHost="",int publicPort=0,bool online=true){
            var device=accounts.Live.Get(publicIdentity??"");online=accounts.IsPresent(accountId);
            var fields=new Dictionary<string,object>{{"ok",true},{"version",ProtocolVersion},{"rpcVersion",1},{"type","friend_event"},{"event","presence_changed"},{"accountId",accountId},{"publicIdentity",publicIdentity??""},{"endpointHost",device==null?"":device.Host},{"endpointPort",device==null?0:device.Port},{"publicEndpointHost",device==null?"":device.PublicHost},{"publicEndpointPort",device==null?0:device.PublicPort},{"online",online},{"deviceOnline",device!=null},{"serverEpoch",accounts.Live.Epoch},{"presenceRevision",accounts.Live.Revision},{"deviceGeneration",device==null?0:device.Generation}};
            string signature=publicIdentity+"|"+online+"|"+accounts.Live.Revision;var dead=new List<EventClient>();
            lock(eventGate){string previous;if(onlineCandidateSignatures.TryGetValue(accountId,out previous)&&previous==signature)return;onlineCandidateSignatures[accountId]=signature;if(online)onlineAccounts.Add(accountId);else onlineAccounts.Remove(accountId);
                var payload=Encoding.UTF8.GetBytes(json.Serialize(fields));foreach(var subscriber in eventClients){if(subscriber.AccountId==accountId||!accounts.Friends(accountId,subscriber.AccountId))continue;try{lock(subscriber.Gate)WriteFrame(subscriber.Tls,payload);}catch{dead.Add(subscriber);}}foreach(var client in dead)eventClients.Remove(client);
            }foreach(var client in dead)client.Dispose();
        }
        bool IsSupersededEventSession(string tokenHash){long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();lock(eventGate){foreach(var stale in supersededEventSessions.Where(value=>value.Value<=now).Select(value=>value.Key).ToArray())supersededEventSessions.Remove(stale);return supersededEventSessions.ContainsKey(tokenHash);}}
        void EndEventSubscription(EventClient subscription){lock(eventGate){subscription.Active=false;eventClients.Remove(subscription);}accounts.ClearPresence(subscription.AccountId,subscription.PublicIdentity,subscription.Binding);BroadcastPresence(subscription.AccountId,subscription.PublicIdentity,"",0,"",0,false);subscription.Dispose();foreach(string group in groupRooms.Disconnect(subscription.Binding))BroadcastGroupRooms(group);}
        void ReplaceEventSubscription(EventClient subscription){List<EventClient> replaced;lock(eventGate){long expiry=DateTimeOffset.UtcNow.AddHours(config.SessionHours).ToUnixTimeSeconds();foreach(var old in eventClients.Where(value=>value.AccountId==subscription.AccountId&&value.PublicIdentity==subscription.PublicIdentity&&value.SessionTokenHash!=subscription.SessionTokenHash).ToArray())supersededEventSessions[old.SessionTokenHash]=expiry;replaced=eventClients.Where(value=>value.AccountId==subscription.AccountId&&value.PublicIdentity==subscription.PublicIdentity).ToList();foreach(var old in replaced){old.Active=false;eventClients.Remove(old);}eventClients.Add(subscription);}foreach(var old in replaced)old.Dispose();}
        void RegisterEventSubscription(EventClient subscription){
            try {lock(eventGate){
                ReplaceEventSubscription(subscription);
                accounts.TouchPresence(subscription.AccountId,subscription.PublicIdentity,"",0,"",0,"Relay-only presence","Authenticated event-channel lease",subscription.Binding,true,subscription.SessionTokenHash);
                var ready=Encoding.UTF8.GetBytes(groupJson.Serialize(Ok(new Dictionary<string,object>{{"type","events_ready"},{"serverEpoch",accounts.Live.Epoch},{"presenceRevision",accounts.Live.Revision},{"presenceSnapshot",accounts.FriendState(subscription.AccountId)["friends"]}})));
                if(ready.Length>100000)throw new InvalidDataException("Presence snapshot exceeds bounded event frame");
                lock(subscription.Gate)WriteFrame(subscription.Tls,ready);
            }}catch{EndEventSubscription(subscription);throw;}
        }
        void RetireEventSession(string tokenHash){List<EventClient> removed;lock(eventGate){removed=eventClients.Where(value=>value.SessionTokenHash==tokenHash).ToList();foreach(var value in removed)eventClients.Remove(value);}foreach(var value in removed){accounts.ClearPresence(value.AccountId,value.PublicIdentity,value.Binding);EndEventSubscription(value);BroadcastPresence(value.AccountId,value.PublicIdentity,"",0,"",0,false);}}
        void PresenceMonitor(){while(!stopping){Thread.Sleep(1000);if(stopping)break;MaintainAttachmentCache();foreach(var device in accounts.Live.Expire((account,identity)=>{var current=accounts.Live.Get(identity);return accounts.OwnsDevice(account,identity)&&(current==null||accounts.Authenticate(current.Session)==account);}))BroadcastPresence(device.Account,device.Identity,"",0,"",0,false);}}
        // Consent is account/device-bound and endpoint-free. A candidate is retained only until
        // both exact participants submit after acceptance; no database or presence row is used.
        Dictionary<string,object> DirectProbeRequest(long account,string device,long target,string targetDevice){PurgeDirectProbes();if(account==target||!ValidIdentity(device)||!ValidIdentity(targetDevice)||!accounts.OwnsDevice(account,device)||!accounts.OwnsDevice(target,targetDevice)||!accounts.Friends(account,target))return Error("p2p_not_authorized");var probe=new DirectProbe{Id=RandomToken(),Initiator=account,InitiatorDevice=device,Recipient=target,RecipientDevice=targetDevice,Expires=DateTimeOffset.UtcNow.AddSeconds(DirectProbeLifetimeSeconds).ToUnixTimeSeconds()};lock(directProbeGate){foreach(var id in directProbes.Where(x=>x.Value.Initiator==account&&x.Value.Recipient==target&&x.Value.InitiatorDevice==device&&x.Value.RecipientDevice==targetDevice).Select(x=>x.Key).ToArray())directProbes.Remove(id);directProbes[probe.Id]=probe;}Notify(target,new Dictionary<string,object>{{"event","p2p_request"},{"requestId",probe.Id},{"accountId",account},{"publicIdentity",device},{"targetPublicIdentity",targetDevice},{"expiresUtc",probe.Expires}});return Ok(new Dictionary<string,object>{{"requestId",probe.Id},{"expiresUtc",probe.Expires},{"state","pending"}});}
        Dictionary<string,object> DirectProbeEnd(long account,string device,string requestId,bool accept){PurgeDirectProbes();DirectProbe probe;lock(directProbeGate){if(!directProbes.TryGetValue(requestId,out probe)||probe.Recipient!=account||probe.RecipientDevice!=device||!accounts.Friends(probe.Initiator,probe.Recipient))return Error("p2p_request_invalid");if(!accept){directProbes.Remove(requestId);Notify(probe.Initiator,new Dictionary<string,object>{{"event","p2p_declined"},{"requestId",requestId}});return Ok(new Dictionary<string,object>{{"state","declined"}});}probe.Consented=true;}Notify(probe.Initiator,new Dictionary<string,object>{{"event","p2p_accepted"},{"requestId",requestId},{"expiresUtc",probe.Expires}});return Ok(new Dictionary<string,object>{{"state","accepted"},{"expiresUtc",probe.Expires}});}
        Dictionary<string,object> DirectProbeCandidate(long account,string device,string requestId,string host,int port){PurgeDirectProbes();if(String.IsNullOrEmpty(host)||!ValidEndpoint(host,port)||port==0)return Error("invalid_p2p_candidate");DirectProbe p;bool ready;lock(directProbeGate){if(!directProbes.TryGetValue(requestId,out p)||!p.Consented||!accounts.Friends(p.Initiator,p.Recipient))return Error("p2p_request_invalid");if(account==p.Initiator&&device==p.InitiatorDevice){p.InitiatorHost=host;p.InitiatorPort=port;p.InitiatorSubmitted=true;}else if(account==p.Recipient&&device==p.RecipientDevice){p.RecipientHost=host;p.RecipientPort=port;p.RecipientSubmitted=true;}else return Error("p2p_request_invalid");ready=p.InitiatorSubmitted&&p.RecipientSubmitted;}if(!ready)return Ok(new Dictionary<string,object>{{"state","waiting_for_peer"},{"requestId",requestId}});bool initiator=account==p.Initiator;Notify(initiator?p.Recipient:p.Initiator,new Dictionary<string,object>{{"event","p2p_candidates_ready"},{"requestId",requestId},{"peerHost",initiator?p.InitiatorHost:p.RecipientHost},{"peerPort",initiator?p.InitiatorPort:p.RecipientPort},{"peerPublicIdentity",device},{"expiresUtc",p.Expires}});return Ok(new Dictionary<string,object>{{"state","candidates_ready"},{"requestId",requestId},{"peerHost",initiator?p.RecipientHost:p.InitiatorHost},{"peerPort",initiator?p.RecipientPort:p.InitiatorPort},{"peerPublicIdentity",initiator?p.RecipientDevice:p.InitiatorDevice},{"expiresUtc",p.Expires}});}
        Dictionary<string,object> DirectProbeCancel(long account,string device,string requestId){DirectProbe p;lock(directProbeGate){if(!directProbes.TryGetValue(requestId,out p)||((p.Initiator!=account||p.InitiatorDevice!=device)&&(p.Recipient!=account||p.RecipientDevice!=device)))return Error("p2p_request_invalid");directProbes.Remove(requestId);}Notify(account==p.Initiator?p.Recipient:p.Initiator,new Dictionary<string,object>{{"event","p2p_cancelled"},{"requestId",requestId}});return Ok(new Dictionary<string,object>{{"state","cancelled"}});}
        Dictionary<string,object> Handle(Dictionary<string,object> request,string observedHost="",int observedPort=0) {
            object version; object kind; if(!request.TryGetValue("version",out version)||Convert.ToInt32(version)!=ProtocolVersion||!request.TryGetValue("type",out kind))return Error("unsupported_request"); string type=kind as string; string username=Text(request,"username"),password=Text(request,"password"),identity=Text(request,"publicIdentity"),token=Text(request,"sessionToken");
            if(type=="register") { if(!ValidUsername(username)||String.IsNullOrEmpty(password)||password.Length>256||!ValidIdentity(identity)){Log("WARNING","Rejected malformed account registration.");return Error("invalid_registration");} if(accounts.UsernameExists(username)){Log("WARNING","Rejected duplicate account registration.");return Error("registration_failed");} if(accounts.DeviceIdentityExists(identity)){Log("WARNING","Rejected account registration because this public device identity is already registered.");return Error("device_already_registered");} var salt=new byte[16];using(var rng=RandomNumberGenerator.Create())rng.GetBytes(salt); long id;try{id=accounts.CreateAccountWithDevice(username,salt,PasswordHash(password,salt),identity);Log("INFO","Account created: "+username+".");}catch(Exception error){Log("ERROR","Account registration storage failure: "+error.GetType().Name);return Error("registration_failed");}return Session(id); }
            if(type=="login") { if(!ValidUsername(username)||String.IsNullOrEmpty(password)||password.Length>256)return Error("login_failed");long id;bool ok=accounts.PasswordMatches(username,(salt,hash)=>ConstantTimeEquals(PasswordHash(password,salt),hash),out id);Log(ok?"INFO":"WARNING",ok?"Account authentication succeeded.":"Account authentication failed.");return ok?Session(id):Error("login_failed"); }
            if(type=="remember_challenge"||type=="remember_resume"||type=="remember_revoke")return RememberedRequest(request,type,identity);
            long accountId=accounts.Authenticate(TokenHash(token)); if(accountId==0)return Error("session_invalid");
            if(!String.IsNullOrEmpty(identity)&&!accounts.SessionAllowsDevice(TokenHash(token),identity))return Error("device_session_mismatch");
            if(type=="archive_descriptor_register"||type=="archive_grant_register"||type=="archive_authorization_state")return ArchiveAuthorizationRequest(accountId,identity,type,request);
            if(type=="remember_enroll_challenge"||type=="remember_enroll")return EnrollRemembered(request,type,accountId,identity,TokenHash(token));
            if(type=="messaging_bootstrap"){
                object bootstrapVersion;
                if(!request.TryGetValue("bootstrapVersion",out bootstrapVersion)||!(bootstrapVersion is int)||Convert.ToInt32(bootstrapVersion)!=1)return Error("bootstrap_version_unsupported");
                if(!ValidIdentity(identity)||!accounts.OwnsDevice(accountId,identity))return Error("device_account_mismatch");
                var state=accounts.FriendState(accountId);
                var result=Ok(new Dictionary<string,object>{{"bootstrapVersion",1},{"accountId",accountId},{"username",accounts.Username(accountId)??""},{"registeredPublicIdentity",identity},{"relayOnlyPresence",true},{"presenceLive",true},{"friendState",state}});
                // Bound before publishing presence. No history, message content,
                // credentials, group keys or TLS/session secrets in this snapshot.
                var boundedJson=new JavaScriptSerializer{MaxJsonLength=16384};
                try{if(Encoding.UTF8.GetByteCount(boundedJson.Serialize(result))>16384)return Error("bootstrap_too_large");}catch(InvalidOperationException){return Error("bootstrap_too_large");}
                var presence=Handle(new Dictionary<string,object>{{"version",1},{"type","presence"},{"sessionToken",token},{"publicIdentity",identity},{"directCandidates",false}},observedHost,observedPort);
                return (bool)presence["ok"]?result:presence;
            }
            if(type=="push_register"||type=="push_remove"){
                if(!ValidIdentity(identity)||accounts.SessionDevice(TokenHash(token))!=identity)return Error("push_device_session_required");
                if(type=="push_remove"){accounts.RemovePush(accountId,identity);return Ok();}
                string pushToken=Text(request,"pushToken");if(Text(request,"provider")!="fcm"||pushToken==null||pushToken.Length<1||pushToken.Length>2048||pushToken.Any(c=>c<33||c>126))return Error("push_invalid");
                return accounts.RegisterPush(accountId,identity,pushToken)?Ok():Error("push_not_authorized");
            }
            if(type=="push_hint"){
                long target;string targetDevice=Text(request,"targetPublicIdentity");
                if(!ValidIdentity(identity)||!accounts.OwnsDevice(accountId,identity)||!Int64.TryParse(Text(request,"targetAccountId"),out target)||target==accountId||!ValidIdentity(targetDevice)||!accounts.OwnsDevice(target,targetDevice)||!accounts.Friends(accountId,target))return Error("push_not_authorized");
                pushWakes.Hint(target,targetDevice);return Ok();
            }
            if(type=="logout"){InvalidateDirectProbes(accountId);string tokenHash=TokenHash(token);string bound=accounts.SessionDevice(tokenHash);if(bound.Length>0){accounts.RevokeRememberedDevice(accountId,bound);RetireRememberedRoutes(accountId,bound);}accounts.RevokeSession(tokenHash);RetireEventSession(tokenHash);foreach(var lease in accounts.Live.Snapshot(accountId).Where(d=>d.Session==tokenHash)){accounts.ClearPresence(accountId,lease.Identity,lease.Owner);BroadcastPresence(accountId,lease.Identity,"",0,"",0,false);}Log("INFO","Account session signed out.");return Ok();}
            if(type=="presence") { if(IsSupersededEventSession(TokenHash(token)))return Error("session_superseded");bool directCandidates=Flag(request,"directCandidates",false);int endpointPort=0,publicPort=0;string endpointHost=Text(request,"endpointHost")??"",publicHost=Text(request,"publicEndpointHost")??"";if(!ValidIdentity(identity))return Error("invalid_presence");if(directCandidates){if(!Int32.TryParse(Text(request,"endpointPort"),out endpointPort)||!ValidEndpoint(endpointHost,endpointPort))return Error("invalid_presence");if(!Int32.TryParse(Text(request,"publicEndpointPort"),out publicPort))publicPort=0;}else{endpointHost="";endpointPort=0;publicHost="";publicPort=0;}string observedClassification="Not evaluated";string publicSource; if(!directCandidates){publicSource="Relay-only presence";}else if(String.IsNullOrEmpty(publicHost)){if(IsUsablePublicIpv4(observedHost,out observedClassification)){publicHost=observedHost;publicPort=endpointPort;publicSource="Server-observed Internet address";}else if(observedClassification.StartsWith("Loopback",StringComparison.Ordinal)&&IsLocalInterfaceAddress(endpointHost)){string configuredClassification;if(IsUsablePublicIpv4(config.SelfHostedPublicP2PHost,out configuredClassification)){publicHost=config.SelfHostedPublicP2PHost;publicPort=endpointPort;publicSource="Configured self-hosted P2P fallback";}else{publicHost="";publicPort=0;publicSource="No usable configured self-hosted P2P address";}}else{publicHost="";publicPort=0;publicSource="Observed address rejected";}}else{string suppliedClassification;if(!IsUsablePublicIpv4(publicHost,out suppliedClassification))return Error("invalid_public_endpoint");publicSource="Authenticated explicit public candidate";}if(!ValidEndpoint(publicHost,publicPort))return Error("invalid_public_endpoint");if(!accounts.AddDevice(accountId,identity))return Error("device_account_mismatch");accounts.TouchPresence(accountId,identity,endpointHost,endpointPort,publicHost,publicPort,publicSource,observedClassification,session:TokenHash(token));BroadcastPresence(accountId,identity,endpointHost,endpointPort,publicHost,publicPort);Log("DEBUG","Presence refreshed for account "+accountId+" ("+accounts.Username(accountId)+") direct candidates="+directCandidates+"; public candidate source: "+publicSource+".");return Ok(new Dictionary<string,object>{{"accountId",accountId},{"username",accounts.Username(accountId)??""},{"registeredPublicIdentity",identity},{"endpointHost",endpointHost},{"endpointPort",endpointPort},{"publicEndpointHost",publicHost},{"publicEndpointPort",publicPort},{"publicCandidateSource",publicSource},{"observedAddressClassification",observedClassification},{"directCandidatesEnabled",directCandidates}}); }
            if(type=="session_info")return Ok(new Dictionary<string,object>{{"accountId",accountId},{"username",accounts.Username(accountId)??""},{"publicIdentity",accounts.PrimaryDeviceIdentity(accountId)},{"relayOnlyPresence",true}});
            if(type=="media_turn_credentials")return MediaTurnCredentials(accountId,identity);
            if(type=="search") { if(!ValidUsername(username)){Log("WARNING","Rejected malformed username search.");return Error("invalid_search");}var found=accounts.Search(username,accountId);Log("DEBUG",found==null?"Username search returned no result.":"Username search completed.");return found==null?Error("not_found"):Ok(found); }
            if(type=="suggest") { if(String.IsNullOrEmpty(username)||username.Length>32||username.Any(c=>!(Char.IsLetterOrDigit(c)||c=='-'||c=='_')))return Error("invalid_search");var suggestions=accounts.Suggestions(username,accountId,15);Log("DEBUG","Username suggestions completed: "+suggestions.Count+" result(s).");return Ok(new Dictionary<string,object>{{"suggestions",suggestions}}); }
            if(type=="people_search") { string query=Text(request,"query");long searchAccountId;if(String.IsNullOrEmpty(query)||query.Length>32||query.Any(c=>!(Char.IsLetterOrDigit(c)||c=='-'||c=='_'))||query.Length<2&&!Int64.TryParse(query,out searchAccountId))return Error("invalid_search");if(!PermitPeopleSearch(accountId))return Error("search_rate_limited");return Ok(new Dictionary<string,object>{{"accounts",accounts.PeopleSearch(query,accountId)}}); }
            if(type=="friend_state")return Ok(accounts.FriendState(accountId));
            if(type=="group_register"){
                GroupRosterProof proof;try{proof=GroupRosterProof.Parse(Text(request,"createProof"));}catch{return Error("invalid_group_proof");}
                if(proof.OwnerAccountId!=accountId)return Error("group_owner_account_mismatch");
                if(proof.OwnerIdentity!=identity||!accounts.OwnsDevice(accountId,identity))return Error("group_owner_device_mismatch");
                try { var roster=accounts.RegisterGroup(accountId,identity,proof);return roster==null?Error("group_registration_conflict"):Ok(roster); }
                catch(InvalidOperationException) { Log("WARNING","Group registration database operation failed.");return Error("group_database_error"); }
            }
            if(type=="group_roster"){
                var roster=accounts.GroupRoster(Text(request,"groupId"),accountId,identity);return roster==null?Error("group_not_authorized"):Ok(roster);
            }
            if(type=="group_device_recovery"){
                long after;if(!ValidIdentity(identity)||!GroupRosterProof.Id(Text(request,"groupId"),32)||
                    !Int64.TryParse(Text(request,"afterGeneration"),NumberStyles.None,CultureInfo.InvariantCulture,out after)||after<=0)return Error("invalid_group_recovery_request");
                try{var evidence=accounts.ReadGroupDeviceRecoveryForCurrentSession(TokenHash(token),identity,Text(request,"groupId"),after);
                    return evidence==null?Error("group_not_authorized"):Ok(evidence);
                }catch(InvalidOperationException){Log("WARNING","Group recovery database operation failed.");return Error("group_database_error");}
            }
            if(type=="group_state")return Ok(new Dictionary<string,object>{{"groups",accounts.GroupRostersForDevice(accountId,identity)}});
            if(type=="group_invite_account"){
                long target,generation;if(!Int64.TryParse(Text(request,"targetAccountId"),out target)||!Int64.TryParse(Text(request,"rosterGeneration"),out generation)||generation<1)return Error("invalid_group_request");
                var invite=accounts.GroupInviteAccount(Text(request,"groupId"),accountId,identity,target,generation);if(invite==null)return Error("group_invite_denied");
                if(Convert.ToString(invite["state"])=="pending")Notify(target,new Dictionary<string,object>{{"event","group_invite_changed"}});
                return Ok(invite);
            }
            if(type=="group_invite_state")return Ok(new Dictionary<string,object>{{"invites",accounts.GroupInvitesFor(accountId)}});
            if(type=="group_invite_respond"){
                string inviteId=Text(request,"inviteId"),action=Text(request,"action");if(action!="accept"&&action!="decline")return Error("invalid_group_request");
                var result=accounts.GroupRespondAccount(inviteId,accountId,identity,action=="accept");if(result==null)return Error("group_invite_denied");
                string group=Convert.ToString(result["groupId"]);if(action=="accept"){RetireGroupRelays(group);NotifyGroupRosterChanged(group);}
                Notify(accountId,new Dictionary<string,object>{{"event","group_invite_changed"}});
                return Ok(result);
            }
            if(type=="group_invite_revoke"){
                string inviteId=Text(request,"inviteId");long recipient=accounts.GroupInviteRecipient(inviteId);
                if(!accounts.GroupRevokeAccountInvite(inviteId,accountId,identity))return Error("group_invite_denied");
                if(recipient>0)Notify(recipient,new Dictionary<string,object>{{"event","group_invite_changed"}});
                return Ok(new Dictionary<string,object>{{"state","revoked"}});
            }
            if(type=="group_invite"||type=="group_remove"){
                long target,generation;string targetDevice=Text(request,"targetPublicIdentity");
                if(!Int64.TryParse(Text(request,"targetAccountId"),out target)||!Int64.TryParse(Text(request,"rosterGeneration"),out generation)||generation<1)return Error("invalid_group_request");
                var roster=type=="group_invite"?accounts.GroupInvite(Text(request,"groupId"),accountId,identity,target,targetDevice,generation):accounts.GroupRemove(Text(request,"groupId"),accountId,identity,target,targetDevice,generation);
                if(roster==null)return Error("group_not_authorized");
                RetireGroupRelays(Text(request,"groupId"));
                NotifyGroupRosterChanged(Text(request,"groupId"));
                if(type=="group_remove")Notify(target,new Dictionary<string,object>{{"event","group_roster_changed"},{"groupId",Text(request,"groupId")}});
                return Ok(roster);
            }
            if(type=="group_accept"||type=="group_decline"){
                long generation;if(!Int64.TryParse(Text(request,"rosterGeneration"),out generation)||generation<1)return Error("invalid_group_request");
                var roster=accounts.GroupRespond(Text(request,"groupId"),accountId,identity,type=="group_accept",generation);
                if(roster!=null){RetireGroupRelays(Text(request,"groupId"));NotifyGroupRosterChanged(Text(request,"groupId"));Notify(accountId,new Dictionary<string,object>{{"event","group_roster_changed"},{"groupId",Text(request,"groupId")}});}
                return roster==null?Error("group_not_authorized"):Ok(roster);
            }
            if(type=="group_leave"){
                long generation;if(!Int64.TryParse(Text(request,"rosterGeneration"),out generation)||generation<1)return Error("invalid_group_request");
                var roster=accounts.GroupLeave(Text(request,"groupId"),accountId,identity,generation);
                if(roster!=null){RetireGroupRelays(Text(request,"groupId"));NotifyGroupRosterChanged(Text(request,"groupId"));Notify(accountId,new Dictionary<string,object>{{"event","group_roster_changed"},{"groupId",Text(request,"groupId")}});}
                return roster==null?Error("group_not_authorized"):Ok(roster);
            }
            if(type=="p2p_request"){long target;return Int64.TryParse(Text(request,"targetAccountId"),out target)?DirectProbeRequest(accountId,identity,target,Text(request,"targetPublicIdentity")):Error("invalid_account");}
            if(type=="p2p_consent"){return DirectProbeEnd(accountId,identity,Text(request,"requestId"),Flag(request,"accept",false));}
            if(type=="p2p_candidates"){int port;return Int32.TryParse(Text(request,"endpointPort"),out port)?DirectProbeCandidate(accountId,identity,Text(request,"requestId"),Text(request,"endpointHost"),port):Error("invalid_p2p_candidate");}
            if(type=="p2p_cancel"||type=="p2p_complete")return DirectProbeCancel(accountId,identity,Text(request,"requestId"));
            long other; if(!Int64.TryParse(Text(request,"accountId"),out other)||other<1)return Error("invalid_account");
            if(type=="friend_request"){var result=accounts.SendFriendRequest(accountId,other);Log(result=="pending"||result=="accepted_crossed"?"INFO":"WARNING","Friend request result: "+result+".");if(result=="pending"||result=="accepted_crossed")Broadcast(accountId,other,result=="accepted_crossed"?"friendship_changed":"friend_request");return result=="pending"||result=="accepted_crossed"?Ok(new Dictionary<string,object>{{"state",result}}):Error(result);}
            if(type=="friend_respond"){bool accept=String.Equals(Text(request,"action"),"accept",StringComparison.Ordinal);var result=accounts.RespondToRequest(accountId,other,accept);Log(result=="friends"||result=="rejected"?"INFO":"WARNING","Friend request response result: "+result+".");if(result=="friends"||result=="rejected")Broadcast(accountId,other,accept?"friendship_changed":"friend_request_updated");return result=="friends"||result=="rejected"?Ok(new Dictionary<string,object>{{"state",result}}):Error(result);}
            if(type=="friend_cancel"){var result=accounts.CancelRequest(accountId,other);if(result=="cancelled")Broadcast(accountId,other,"friend_request_updated");return result=="cancelled"?Ok(new Dictionary<string,object>{{"state",result}}):Error(result);}
            if(type=="friend_remove"){var result=accounts.RemoveFriend(accountId,other);if(result=="removed"){InvalidateDirectProbes(accountId,other);Broadcast(accountId,other,"friendship_changed");}return result=="removed"?Ok(new Dictionary<string,object>{{"state",result}}):Error(result);}
            if(type=="friend_block"||type=="friend_unblock"){var result=accounts.SetBlocked(accountId,other,type=="friend_block");if(result=="blocked"||result=="unblocked"){if(result=="blocked")InvalidateDirectProbes(accountId,other);Broadcast(accountId,other,"friendship_changed");}return result=="blocked"||result=="unblocked"?Ok(new Dictionary<string,object>{{"state",result}}):Error(result);}
            return Error("unsupported_request");
        }
        Dictionary<string,object> Session(long accountId){string token=RandomToken();accounts.CreateSession(TokenHash(token),accountId,DateTimeOffset.UtcNow.AddHours(config.SessionHours).ToUnixTimeSeconds());return Ok(new Dictionary<string,object>{{"sessionToken",token},{"accountId",accountId},{"username",accounts.Username(accountId)??""},{"expiresInSeconds",config.SessionHours*3600},{"relayOnlyPresence",true},{"messagingBootstrapVersion",1}});}
        static bool ValidCredential(string value){return value!=null&&value.Length==43&&value.All(c=>Char.IsLetterOrDigit(c)||c=='-'||c=='_');}
        Dictionary<string,object> ProofChallenge(long account,string device,string action,string binding){
            try{var proof=rememberedProofs.Issue(account,device,action,binding);return Ok(new Dictionary<string,object>{{"challengeId",proof.Id},{"signatureInput",Convert.ToBase64String(proof.Input)},{"accountId",account},{"publicIdentity",device},{"purpose",action}});}
            catch(InvalidOperationException){return Error("remember_rate_limited");}
        }
        bool VerifyRemembered(Dictionary<string,object> request,long account,string device,string action,string binding){
            try {string id=Text(request,"challengeId"),key=Text(request,"publicKey"),sig=Text(request,"signature");if(id==null||key==null||sig==null||id.Length!=64||key.Length>100||sig.Length>90)return false;
                return rememberedProofs.Consume(id,account,device,action,binding,Convert.FromBase64String(key),Convert.FromBase64String(sig));
            }catch(FormatException){return false;}
        }
        Dictionary<string,object> EnrollRemembered(Dictionary<string,object> request,string type,long account,string device,string sessionHash){
            if(!ValidIdentity(device)||!accounts.OwnsDevice(account,device))return Error("remember_device_mismatch");
            if(type=="remember_enroll_challenge")return ProofChallenge(account,device,"enroll",sessionHash);
            if(!VerifyRemembered(request,account,device,"enroll",sessionHash))return Error("remember_proof_invalid");
            string credential=RandomToken();accounts.RememberDevice(account,device,TokenHash(credential));RetireRememberedRoutes(account,device);
            return Ok(new Dictionary<string,object>{{"credential",credential},{"accountId",account},{"publicIdentity",device}});
        }
        Dictionary<string,object> RememberedRequest(Dictionary<string,object> request,string type,string device){
            string credential=Text(request,"credential");if(!ValidCredential(credential)||!ValidIdentity(device))return Error("remember_invalid");
            string hash=TokenHash(credential);long account=accounts.RememberedAccount(hash,device);if(account==0)return Error("remember_revoked");
            string action=type=="remember_challenge"?Text(request,"purpose"):(type=="remember_revoke"?"revoke":"resume");
            if(action!="resume"&&action!="revoke")return Error("remember_invalid");
            if(type=="remember_challenge")return ProofChallenge(account,device,action,hash);
            if(!VerifyRemembered(request,account,device,action,hash))return Error("remember_proof_invalid");
            if(action=="revoke"){accounts.RevokeRememberedDevice(account,device);RetireRememberedRoutes(account,device);return Ok();}
            string token=RandomToken();try{accounts.CreateDeviceSession(TokenHash(token),account,device,DateTimeOffset.UtcNow.AddHours(config.SessionHours).ToUnixTimeSeconds(),hash);}catch(InvalidOperationException){return Error("remember_revoked");}
            return Ok(new Dictionary<string,object>{{"sessionToken",token},{"accountId",account},{"username",accounts.Username(account)??""},{"expiresInSeconds",config.SessionHours*3600},{"relayOnlyPresence",true},{"messagingBootstrapVersion",1}});
        }
        static string Text(Dictionary<string,object> r,string key){object value;return r.TryGetValue(key,out value)?value as string:null;} static bool Flag(Dictionary<string,object> r,string key,bool fallback){object value;if(!r.TryGetValue(key,out value)||value==null)return fallback;try{return Convert.ToBoolean(value);}catch{return fallback;}} static Dictionary<string,object> Ok(Dictionary<string,object> data=null){var result=new Dictionary<string,object>{{"ok",true},{"version",ProtocolVersion},{"rpcVersion",1}};if(data!=null)foreach(var p in data)result[p.Key]=p.Value;return result;} static Dictionary<string,object> Error(string code){return new Dictionary<string,object>{{"ok",false},{"version",ProtocolVersion},{"error",code}};}
        readonly object logGate=new object();
        // Operational logs stay in the configured Logs directory. Rotate a single old file so a
        // long-running home server cannot grow an unbounded log beside its executable.
        void Log(string level,string text){string line=DateTime.UtcNow.ToString("o")+" ["+level+"] "+text;Console.WriteLine(line);try{lock(logGate){Directory.CreateDirectory(config.LogDirectory);string file=Path.Combine(config.LogDirectory,"peerchat-server.log");if(File.Exists(file)&&new FileInfo(file).Length>1024*1024){string previous=file+".1";if(File.Exists(previous))File.Delete(previous);File.Move(file,previous);}File.AppendAllText(file,line+Environment.NewLine);}}catch{/* Logging must never prevent the server from accepting clients. */}}

        static string DecodeAdminText(string value){try{return String.IsNullOrEmpty(value)?null:Encoding.UTF8.GetString(Convert.FromBase64String(value));}catch{return null;}}
        void ManagementLoop(){while(!stopping){try{using(var pipe=new NamedPipeServerStream(managementPipe,PipeDirection.InOut,4,PipeTransmissionMode.Byte,PipeOptions.None)){lock(workerGate){if(stopping)return;managementStream=pipe;}pipe.WaitForConnection();var encoding=new UTF8Encoding(false);var reader=new StreamReader(pipe,encoding,false,256,true);var writer=new StreamWriter(pipe,encoding,256,true);var command=reader.ReadLine()??"";Dictionary<string,object> response;if(command=="STATUS")response=Status();else if(command=="STOP"){response=new Dictionary<string,object>{{"ok",true}};}else if(command.StartsWith("ACCOUNT_LIST ",StringComparison.Ordinal)){var parts=command.Substring("ACCOUNT_LIST ".Length).Split(' ');int offset,limit;response=parts.Length==3&&Int32.TryParse(parts[0],out offset)&&Int32.TryParse(parts[1],out limit)?Ok(accounts.AdminAccounts(DecodeAdminText(parts[2]),offset,limit)):Error("invalid_account_search");}else if(command.StartsWith("ACCOUNT_LOOKUP ",StringComparison.Ordinal)){var account=accounts.AdminLookup(DecodeAdminText(command.Substring("ACCOUNT_LOOKUP ".Length)));response=account==null?Error("account_not_found"):Ok(account);}else if(command.StartsWith("DELETE_ACCOUNT ",StringComparison.Ordinal)){long id;response=Int64.TryParse(command.Substring("DELETE_ACCOUNT ".Length),out id)&&id>0?AdminDeleteAccount(id):Error("invalid_account_id");}else response=Error("unsupported_admin_command");writer.WriteLine(json.Serialize(response));writer.Flush();if(command=="STOP")RequestStop();}}catch(Exception error){if(!stopping)Log("WARNING","Local admin pipe error: "+error.GetType().Name);}}}
        Dictionary<string,object> Status(){var data=accounts.Statistics();data["ok"]=true;data["running"]=!stopping;data["uptimeSeconds"]=(long)(DateTime.UtcNow-startedUtc).TotalSeconds;data["port"]=config.Port;data["address"]="0.0.0.0";data["tls"]=true;data["connectedClients"]=Interlocked.CompareExchange(ref connectedClients,0,0);data["turnEnabled"]=config.TurnEnabled;data["turnHost"]=config.TurnEnabled?config.TurnPublicHost:"";data["turnUdpPort"]=config.TurnEnabled?config.TurnUdpPort:0;data["turnCredentialTtlSeconds"]=config.TurnEnabled?config.TurnCredentialTtlSeconds:0;return data;}

        // TLS is a byte stream, not a message transport. A four-byte big-endian length prefix
        // keeps partial reads from being mistaken for an entire JSON request.
        static byte[] ReadFrame(Stream stream,int maximum){var header=ReadExact(stream,4);if(header==null)return null;int length=(header[0]<<24)|(header[1]<<16)|(header[2]<<8)|header[3];return length<1||length>maximum?null:ReadExact(stream,length);}
        static byte[] ReadExact(Stream stream,int length){var bytes=new byte[length];int offset=0;while(offset<length){int read=stream.Read(bytes,offset,length-offset);if(read==0)return null;offset+=read;}return bytes;}
        // Poll/Available observes encrypted socket bytes, not plaintext already
        // buffered inside SslStream. One outstanding framed read also consumes
        // buffered TLS data while the owner checks its lease/shutdown deadline.
        static async Task<byte[]> ReadFrameAsync(Stream stream,int maximum){
            var header=new byte[4];int offset=0;
            while(offset<4){int n=await stream.ReadAsync(header,offset,4-offset).ConfigureAwait(false);if(n==0)return null;offset+=n;}
            int length=(header[0]<<24)|(header[1]<<16)|(header[2]<<8)|header[3];if(length<1||length>maximum)return null;
            var frame=new byte[length];offset=0;
            while(offset<length){int n=await stream.ReadAsync(frame,offset,length-offset).ConfigureAwait(false);if(n==0)return null;offset+=n;}
            return frame;
        }
        static void WriteFrame(Stream stream,byte[] bytes){var header=new[]{(byte)(bytes.Length>>24),(byte)(bytes.Length>>16),(byte)(bytes.Length>>8),(byte)bytes.Length};stream.Write(header,0,header.Length);stream.Write(bytes,0,bytes.Length);}
        bool PairRelay(RelayEndpoint endpoint){
            lock(relayGate){
                if(activeRelayBindings.Contains(endpoint.Binding))return false;
                activeRelayBindings.Add(endpoint.Binding);
                RelayEndpoint peer=waitingRelays.FirstOrDefault(value=>value.Scope==endpoint.Scope&&value.ArchiveKey==endpoint.ArchiveKey&&value.GroupId==endpoint.GroupId&&value.RosterGeneration==endpoint.RosterGeneration&&value.AccountId==endpoint.TargetAccountId&&value.DeviceId==endpoint.TargetDeviceId&&value.TargetAccountId==endpoint.AccountId&&value.TargetDeviceId==endpoint.DeviceId);
                if(peer==null){waitingRelays.Add(endpoint);return true;}
                waitingRelays.Remove(peer);string session=RandomToken();endpoint.Peer=peer;peer.Peer=endpoint;endpoint.SessionId=peer.SessionId=session;
                endpoint.PairDiagnosticId=peer.PairDiagnosticId=Guid.NewGuid().ToString("N").Substring(0,12);
                endpoint.Incoming=String.CompareOrdinal(endpoint.DeviceId,peer.DeviceId)>0;peer.Incoming=!endpoint.Incoming;endpoint.Ready.TrySetResult(true);peer.Ready.TrySetResult(true);return true;
            }
        }
        void RemoveRelay(RelayEndpoint endpoint){lock(relayGate){waitingRelays.Remove(endpoint);relayEndpoints.Remove(endpoint);activeRelayBindings.Remove(endpoint.Binding);}try{if(endpoint.Peer!=null)endpoint.Peer.Client.Close();}catch{}endpoint.Ready.TrySetCanceled();}
        void RetireRememberedRoutes(long account,string device){
            List<RelayEndpoint> relays;lock(relayGate)relays=relayEndpoints.Where(value=>value.AccountId==account&&value.DeviceId==device&&accounts.Authenticate(value.SessionTokenHash)==0).ToList();
            foreach(var endpoint in relays)try{endpoint.Client.Close();}catch{}
            List<EventClient> events;lock(eventGate)events=eventClients.Where(value=>value.AccountId==account&&value.PublicIdentity==device&&accounts.Authenticate(value.SessionTokenHash)==0).ToList();
            foreach(var subscription in events){accounts.ClearPresence(account,device,subscription.Binding);EndEventSubscription(subscription);BroadcastPresence(account,device,"",0,"",0,false);}
        }
        void RetireGroupRelays(string groupId){
            List<RelayEndpoint> stale;
            lock(relayGate)stale=relayEndpoints.Where(value=>value.Scope=="group_key"&&value.GroupId==groupId).ToList();
            foreach(var endpoint in stale)try{endpoint.Client.Close();}catch{}
        }
        void NotifyGroupRosterChanged(string groupId){groupRooms.Reconcile(groupId);BroadcastGroupRooms(groupId);foreach(long member in accounts.GroupMemberAccounts(groupId))Notify(member,new Dictionary<string,object>{{"event","group_roster_changed"},{"groupId",groupId}});}
        void RetireDeletedAccount(long accountId){List<EventClient> events;lock(eventGate){onlineAccounts.Remove(accountId);onlineCandidateSignatures.Remove(accountId);events=eventClients.Where(value=>value.AccountId==accountId).ToList();foreach(var value in events)eventClients.Remove(value);}foreach(var value in events)value.Dispose();List<RelayEndpoint> relays;lock(relayGate)relays=relayEndpoints.Where(value=>value.AccountId==accountId||value.TargetAccountId==accountId).ToList();foreach(var value in relays){try{value.Client.Close();}catch{}}}
        Dictionary<string,object> AdminDeleteAccount(long accountId){bool online=accounts.IsPresent(accountId);var deleted=accounts.AdminDeleteAccount(accountId);if(deleted==null)return Error("account_not_found");var related=deleted["relatedAccounts"] as List<long>;if(related!=null)foreach(var other in related)Broadcast(other,accountId,"friendship_changed",false);RetireDeletedAccount(accountId);Log("WARNING","Local administrator permanently deleted server account "+accountId+" ("+deleted["username"]+").");return Ok(new Dictionary<string,object>{{"state","account_deleted"},{"accountId",accountId},{"username",deleted["username"]},{"disconnectedActiveSession",online}});}
        void ServeRelay(TcpClient client,SslStream tls,Dictionary<string,object> request){
            client.NoDelay=true; // Opaque relay ACK/control pacing; payload and TLS semantics unchanged.
            string tokenHash=TokenHash(Text(request,"sessionToken"));long accountId=accounts.Authenticate(tokenHash),targetAccountId;string device=Text(request,"publicIdentity"),targetDevice=Text(request,"targetPublicIdentity");
            if(accountId==0){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("session_invalid"))));return;}
            string scope=Text(request,"purpose")??"friend",groupId=Text(request,"groupId")??"";long rosterGeneration=0;
            if(!Int64.TryParse(Text(request,"targetAccountId"),out targetAccountId)||targetAccountId<1||
               (scope=="archive_history"?accountId!=targetAccountId:accountId==targetAccountId)||
               device==targetDevice||!ValidIdentity(device)||!ValidIdentity(targetDevice)||!accounts.SessionAllowsDevice(tokenHash,device)||!accounts.OwnsDevice(accountId,device)||!accounts.OwnsDevice(targetAccountId,targetDevice)){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("relay_identity_invalid"))));return;}
            if(scope=="group_key"&&(!Int64.TryParse(Text(request,"rosterGeneration"),out rosterGeneration)||rosterGeneration<1)){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("group_roster_generation_invalid"))));return;}
            var archive=scope=="archive_history"?ReadArchiveRelay(request):null;
            string requestedRoster=Text(request,"rosterGeneration");
            bool authorized=scope=="friend"&&groupId.Length==0&&rosterGeneration==0&&accounts.RelayAuthorized(accountId,targetAccountId)
                ||scope=="group_key"&&accounts.GroupRelayAuthorized(groupId,accountId,device,targetAccountId,targetDevice,rosterGeneration)
                ||scope=="archive_history"&&groupId.Length==0&&(String.IsNullOrEmpty(requestedRoster)||requestedRoster=="0")&&
                    accounts.ArchiveRelayAuthorized(accountId,device,targetDevice,archive);
            if(!authorized){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("relay_not_authorized"))));return;}
            // An authorized route request is demand, not friendship/presence.
            // The recipient's existing session owner still authenticates peer TLS.
            var demand=new Dictionary<string,object>{{"event","relay_demand"},{"accountId",accountId},{"publicIdentity",device},{"targetPublicIdentity",targetDevice},{"purpose",scope},{"groupId",groupId},{"rosterGeneration",rosterGeneration}};
            if(archive!=null)archive.AddFields(demand);
            // Supplement native sender-owned retry/outbox; this is not a mailbox.
            // A hint carries no TLS records or plaintext and cannot authorize delivery.
            var endpoint=new RelayEndpoint(client,tls,accountId,device,targetAccountId,targetDevice,scope,groupId,rosterGeneration,tokenHash,archive);lock(relayGate)relayEndpoints.Add(endpoint);
            ulong diagnosticAttempt;if(!UInt64.TryParse(Text(request,"diagnosticAttempt"),NumberStyles.None,CultureInfo.InvariantCulture,out diagnosticAttempt))diagnosticAttempt=0;
            if(!PairRelay(endpoint)){lock(relayGate)relayEndpoints.Remove(endpoint);Log("DEBUG","relay_lifecycle id="+endpoint.DiagnosticId+" attempt="+diagnosticAttempt+" scope="+scope+" stage=claim reason=duplicate_attachment winner_preserved=true");WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("relay_duplicate_attachment"))));return;}
            string retirement="channel_closed";Task<byte[]> pendingRelayRead=null;
            try{
                Log("DEBUG","relay_lifecycle id="+endpoint.DiagnosticId+" attempt="+diagnosticAttempt+" scope="+scope+" local="+device+" target="+targetDevice+" stage=claimed role="+(archive==null?"peer":device==archive.Reader?"reader":"source"));
                // Only accepted reader demand recruits an archive source. A
                // duplicate or source echo cannot start another attachment.
                if(archive==null||device==archive.Reader){Notify(targetAccountId,demand);pushWakes.Hint(targetAccountId,targetDevice);}
                // Observe cancellation while waiting for the other endpoint.
                // Retain this single read for forwarding after pairing: never
                // create concurrent reads or accept payload before readiness.
                pendingRelayRead=ReadFrameAsync(tls,43+MaxRelayPayloadBytes);
                var timeout=Task.Delay(TimeSpan.FromSeconds(15));
                int completed=Task.WaitAny(new Task[]{endpoint.Ready.Task,pendingRelayRead,timeout});
                if(completed==1){retirement="disconnected_or_payload_before_pair";return;}
                if(completed==2){retirement="pair_timeout";WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("relay_pair_timeout"))));return;}
                endpoint.Ready.Task.GetAwaiter().GetResult();
                if(scope=="archive_history"&&!CurrentArchiveRelay(endpoint))return;
                var readyFields=new Dictionary<string,object>{{"type","relay_ready"},{"sessionId",endpoint.SessionId},{"incoming",endpoint.Incoming},{"purpose",scope},{"groupId",groupId},{"rosterGeneration",rosterGeneration}};
                if(archive!=null)archive.AddFields(readyFields);
                var ready=Encoding.UTF8.GetBytes(json.Serialize(Ok(readyFields)));lock(endpoint.WriteGate)WriteFrame(tls,ready);
                Log("DEBUG","relay_lifecycle id="+endpoint.DiagnosticId+" pair="+endpoint.PairDiagnosticId+" scope="+scope+" stage=paired incoming="+endpoint.Incoming);
                byte[] session=Encoding.ASCII.GetBytes(endpoint.SessionId);
                while(!stopping){var frame=pendingRelayRead==null?ReadFrame(tls,session.Length+MaxRelayPayloadBytes):pendingRelayRead.GetAwaiter().GetResult();pendingRelayRead=null;if(frame==null||frame.Length<=session.Length)return;for(int i=0;i<session.Length;i++)if(frame[i]!=session[i])return;var peer=endpoint.Peer;if(peer==null||peer.SessionId!=endpoint.SessionId)return;if(scope=="group_key"&&(accounts.Authenticate(endpoint.SessionTokenHash)!=accountId||accounts.Authenticate(peer.SessionTokenHash)!=targetAccountId||!accounts.GroupRelayAuthorized(groupId,accountId,device,targetAccountId,targetDevice,rosterGeneration)))return;if(scope=="archive_history"&&!CurrentArchiveRelay(endpoint))return;lock(peer.WriteGate)WriteFrame(peer.Tls,frame);}
            }finally{RemoveRelay(endpoint);if(pendingRelayRead!=null)pendingRelayRead.ContinueWith(t=>{var ignored=t.Exception;},TaskContinuationOptions.OnlyOnFaulted);Log("DEBUG","relay_lifecycle id="+endpoint.DiagnosticId+" pair="+(endpoint.PairDiagnosticId??"none")+" scope="+scope+" stage=cleanup reason="+retirement+" ownership_released=true");}
        }
        void Serve(TcpClient client){Interlocked.Increment(ref connectedClients);string correlation=Guid.NewGuid().ToString("N").Substring(0,12),kind="unknown",stage="tls_handshake";long diagnosticAccount=0;Log("DEBUG","Discovery client connected request_id="+correlation+".");using(client)using(var tls=new SslStream(client.GetStream(),false)){try{tls.AuthenticateAsServer(certificate,false,System.Security.Authentication.SslProtocols.Tls12|System.Security.Authentication.SslProtocols.Tls13,false);stage="request_read";var bytes=ReadFrame(tls,MaxRequestBytes);if(bytes==null)return;var request=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(bytes));string requestType=Text(request,"type");kind=ReliableIoDiagnostics.RequestKind(requestType);string diagnosticToken=Text(request,"sessionToken");if(!String.IsNullOrEmpty(diagnosticToken))diagnosticAccount=accounts.Authenticate(TokenHash(diagnosticToken));
                if(requestType=="rpc_open"){stage="rpc";string host="";var ep=client.Client.RemoteEndPoint as IPEndPoint;if(ep!=null)host=ep.Address.ToString();ServeRpc(tls,request,host);return;} if(requestType=="relay_attach"){stage="relay";ServeRelay(client,tls,request);return;}
                if(requestType=="mailbox_rpc"){stage="mailbox";ServeMailboxRpc(tls,request);return;}
                // Event subscriptions are authenticated with the same short-lived session token
                // as ordinary requests. The server sends notifications only; clients fetch the
                // authoritative friend snapshot after each notification. This keeps the channel
                // useful for presence and request updates without making it a second data store.
                if(requestType=="events"){
                    stage="event_channel";
                    string eventTokenHash=TokenHash(Text(request,"sessionToken"));long eventAccount=accounts.Authenticate(eventTokenHash);string eventIdentity=Text(request,"publicIdentity");
                    if(eventAccount==0||!ValidIdentity(eventIdentity)||!accounts.SessionAllowsDevice(eventTokenHash,eventIdentity)||!accounts.OwnsDevice(eventAccount,eventIdentity)){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("session_invalid"))));return;}
                    if(IsSupersededEventSession(eventTokenHash)){WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(Error("event_session_superseded"))));return;}

                    bool clientHeartbeats=Flag(request,"clientHeartbeats",false);tls.ReadTimeout=5000;tls.WriteTimeout=1000;var subscription=new EventClient(client,tls,eventAccount,eventIdentity,eventTokenHash,clientHeartbeats);RegisterEventSubscription(subscription);BroadcastPresence(eventAccount,eventIdentity,"",0,"",0);Log("DEBUG","Authenticated discovery event channel opened.");
                    // Presence is a client-renewed lease. An open-but-half-dead TCP connection
                    // cannot keep the account online because the server never renews it itself.
Task<byte[]> pendingEventRead=null;try{long nextLegacyLease=leaseClock.ElapsedMilliseconds+30000;while(!stopping){long now=leaseClock.ElapsedMilliseconds;if(subscription.ClientHeartbeats&&now-subscription.LastHeartbeatUtc>=EventLeaseSeconds*1000L)break;if(!subscription.ClientHeartbeats&&now>=nextLegacyLease){accounts.TouchPresence(eventAccount,eventIdentity,"",0,"",0,"Relay-only presence","Legacy authenticated event-channel lease",subscription.Binding,false);BroadcastPresence(eventAccount,eventIdentity,"",0,"",0);nextLegacyLease=now+30000;}if(pendingEventRead==null)pendingEventRead=ReadFrameAsync(tls,GroupCiphertextFanout.MaxFrame);if(pendingEventRead.Wait(500)){var eventFrame=pendingEventRead.GetAwaiter().GetResult();pendingEventRead=null;if(eventFrame==null)break;var heartbeat=groupJson.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(eventFrame));if((Text(heartbeat,"type")??"").StartsWith("mailbox_",StringComparison.Ordinal)){HandleMailboxChannel(subscription,heartbeat);continue;}if(Text(heartbeat,"type")=="group_room"||Text(heartbeat,"type")=="group_media"){HandleGroupRooms(subscription,heartbeat);continue;}if(Text(heartbeat,"type")=="group_send"||Text(heartbeat,"type")=="group_ack"){HandleGroupChannel(subscription,heartbeat);continue;}if(Text(heartbeat,"type")=="event_goodbye"){accounts.ClearPresence(eventAccount,eventIdentity,subscription.Binding);BroadcastPresence(eventAccount,eventIdentity,"",0,"",0,false);break;}if(Text(heartbeat,"type")!="event_heartbeat"||accounts.Authenticate(subscription.SessionTokenHash)!=eventAccount)break;if(accounts.TouchPresence(eventAccount,eventIdentity,"",0,"",0,"Relay-only presence","Authenticated event-channel lease",subscription.Binding,false)==null)break;subscription.Heartbeat();}}}
                    finally{EndEventSubscription(subscription);if(pendingEventRead!=null)pendingEventRead.ContinueWith(t=>{var ignored=t.Exception;},TaskContinuationOptions.OnlyOnFaulted);Log("DEBUG","Discovery event channel closed.");}
                    return;
                }
                string observedHost="";try{var remote=client.Client.RemoteEndPoint as IPEndPoint;if(remote!=null)observedHost=remote.Address.ToString();}catch{}stage="request_handle";var response=Encoding.UTF8.GetBytes(json.Serialize(Handle(request,observedHost,0)));stage="response_write";WriteFrame(tls,response);}catch(Exception error){bool expected=stopping||ReliableIoDiagnostics.ExpectedDisconnect(error,kind);Log(expected?"DEBUG":"WARNING",(expected?"Reliable channel disconnected":"Client request failed")+": "+error.GetType().Name+" request_id="+correlation+" request="+kind+" stage="+stage+" authenticated_account="+diagnosticAccount);}finally{Interlocked.Decrement(ref connectedClients);Log("DEBUG","Discovery client disconnected request_id="+correlation+".");}}}
        public static int Main(string[] args){try{if(args.Length==1&&args[0]=="--version"){Console.WriteLine(VersionInfo.Display);return 0;}string configPath=ServerConfiguration.DefaultConfigPath;for(int i=0;i+1<args.Length;i++)if(args[i]=="--config")configPath=args[++i];
#if PEERCHAT_PORTABLE
            if(!OperatingSystem.IsWindows()&&(!Array.Exists(args,x=>x=="--config")||!Path.IsPathRooted(configPath)||!File.Exists(configPath)))throw new InvalidOperationException("Linux requires --config with an existing absolute configuration path.");
#endif
            var c=ServerConfiguration.Load(configPath);for(int i=0;i+1<args.Length;i++){if(args[i]=="--port")c.Port=Int32.Parse(args[++i],CultureInfo.InvariantCulture);else if(args[i]=="--database")c.DatabasePath=args[++i];else if(args[i]=="--certificate")c.CertificatePath=args[++i];else if(args[i]=="--certificate-password")c.CertificatePassword=args[++i];else if(args[i]=="--certificate-thumbprint")c.CertificateThumbprint=args[++i];else if(args[i]=="--management-pipe")c.ManagementPipe=args[++i];}c.Validate();var server=new DiscoveryServer(c);
#if PEERCHAT_PORTABLE
            using(var signals=PortablePlatform.ShutdownSignals(server.RequestStop)){server.Run();}
#else
            ConsoleCancelEventHandler cancel=(sender,e)=>{e.Cancel=true;server.RequestStop();};Console.CancelKeyPress+=cancel;try{server.Run();}finally{Console.CancelKeyPress-=cancel;}
#endif
return 0;}catch(Exception error){Console.Error.WriteLine("PeerChat server startup failed: "+error.GetType().Name+": "+error.Message);return 1;}}
    }
}
