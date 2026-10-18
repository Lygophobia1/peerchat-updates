using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using PeerChat.Server;
using PeerChat.ServerShared;
#if PEERCHAT_PORTABLE
using ECDsaCng=PortableTestKey;
#endif

internal static class DeviceManagementServerTest {
    static void Check(bool value,string why){if(!value)throw new Exception(why);}
    static string Hash(byte[] value){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(value)).Replace("-","").ToLowerInvariant();}
    static string Hash(string value){return Hash(Encoding.UTF8.GetBytes(value));}
    static string Identity(ECDsaCng key){return Hash(key.Key.Export(CngKeyBlobFormat.EccPublicBlob));}
    static Dictionary<string,object> Fields(string type,params string[] fields){var result=new Dictionary<string,object>{{"version",1},{"type",type}};for(int i=0;i<fields.Length;i+=2)result.Add(fields[i],fields[i+1]);return result;}
    static Dictionary<string,object> Call(DiscoveryServer server,Dictionary<string,object> request){return (Dictionary<string,object>)typeof(DiscoveryServer).GetMethod("Handle",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,new object[]{request,"127.0.0.1",0});}
    static Dictionary<string,object> Sign(Dictionary<string,object> challenge,ECDsaCng key,string type,params string[] fields){Check((bool)challenge["ok"],"Challenge failed: "+(challenge.ContainsKey("error")?challenge["error"]:"unknown"));var r=Fields(type,fields);r["challengeId"]=challenge["challengeId"];r["publicKey"]=Convert.ToBase64String(key.Key.Export(CngKeyBlobFormat.EccPublicBlob));r["signature"]=Convert.ToBase64String(key.SignData(Convert.FromBase64String((string)challenge["signatureInput"])));return r;}
    static void Denied(Dictionary<string,object> result,string why){Check(!(bool)result["ok"],why);}
    static string Value(Dictionary<string,object> result,string field){Check((bool)result["ok"],"RPC rejected: "+(result.ContainsKey("error")?result["error"]:"unknown"));return (string)result[field];}
    static string Bind(DiscoveryServer server,string accountSession,ECDsaCng key){string device=Identity(key);Check((bool)Call(server,Fields("presence","sessionToken",accountSession,"publicIdentity",device))["ok"],"Presence setup failed");var challenge=Call(server,Fields("remember_enroll_challenge","sessionToken",accountSession,"publicIdentity",device));string credential=Value(Call(server,Sign(challenge,key,"remember_enroll","sessionToken",accountSession,"publicIdentity",device)),"credential");challenge=Call(server,Fields("remember_challenge","credential",credential,"publicIdentity",device,"purpose","resume"));return Value(Call(server,Sign(challenge,key,"remember_resume","credential",credential,"publicIdentity",device)),"sessionToken");}
    static Dictionary<string,object> Inventory(DiscoveryServer s,string token,string actor){return Call(s,Fields("device_inventory","sessionToken",token,"publicIdentity",actor));}
    static string Revision(Dictionary<string,object> inventory){Check((bool)inventory["ok"],"Inventory rejected");return Convert.ToInt64(inventory["authorityRevision"]).ToString();}
    static Dictionary<string,object> Row(Dictionary<string,object> inventory,string device){return ((List<Dictionary<string,object>>)inventory["devices"]).Single(r=>(string)r["publicIdentity"]==device);}
    static void ResetRateLimit(DiscoveryServer server){((IDictionary)typeof(DiscoveryServer).GetField("deviceManagementStepUps",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server)).Clear();}
    static Dictionary<string,object> Challenge(DiscoveryServer s,string token,string actor,string target,string revision,string password="fixture-password"){ResetRateLimit(s);return Call(s,Fields("device_revoke_challenge","sessionToken",token,"publicIdentity",actor,"targetPublicIdentity",target,"expectedRevision",revision,"password",password));}
    static Dictionary<string,object> Revoke(Dictionary<string,object> c,ECDsaCng key,string token,string target,string revision){return Sign(c,key,"device_revoke","sessionToken",token,"publicIdentity",Identity(key),"targetPublicIdentity",target,"expectedRevision",revision);}
    static void Reject(Action action,string why){bool rejected=false;try{action();}catch(InvalidOperationException){rejected=true;}Check(rejected,why);}
    public static int Main(string[] args){try{
        Directory.CreateDirectory(args[0]);var config=new ServerConfiguration{DataDirectory=args[0],DatabasePath=Path.Combine(args[0],"management.sqlite"),CertificatePath=args[1],CertificatePassword=args[2]};config.Normalize();
        var server=new DiscoveryServer(config);var db=(AccountDatabase)typeof(DiscoveryServer).GetField("accounts",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server);
        using(var actorKey=new ECDsaCng(256))using(var targetKey=new ECDsaCng(256))using(var foreignKey=new ECDsaCng(256)){
            string actor=Identity(actorKey),target=Identity(targetKey),foreign=Identity(foreignKey);
            string passwordSession=Value(Call(server,Fields("register","username","manager","password","fixture-password","publicIdentity",actor)),"sessionToken");
            string actorSession=Bind(server,passwordSession,actorKey),targetSession=Bind(server,passwordSession,targetKey);
            string foreignPassword=Value(Call(server,Fields("register","username","foreign","password","fixture-password","publicIdentity",foreign)),"sessionToken"),foreignSession=Bind(server,foreignPassword,foreignKey);
            long account=db.Authenticate(Hash(actorSession)),other=db.Authenticate(Hash(foreignSession));
            Denied(Inventory(server,passwordSession,actor),"Unbound password session listed devices");Denied(Inventory(server,actorSession,target),"Actor identity substituted");
            var inventory=Inventory(server,actorSession,actor);Check(((List<Dictionary<string,object>>)inventory["devices"]).Count==2,"Inventory account isolation failed");string revision=Revision(inventory);
            Check((bool)Row(inventory,actor)["currentDevice"]&&!(bool)Row(inventory,target)["currentDevice"],"Current device wrong");
            Check((string)Row(inventory,target)["platform"]=="unavailable","Fabricated platform");
            var metadata=Fields("device_metadata","sessionToken",actorSession,"publicIdentity",actor,"platform","Windows","appVersion","4.0.2-fixture");var reported=Call(server,metadata);Check((string)Row(reported,actor)["platform"]=="Windows"&&(string)Row(reported,target)["platform"]=="unavailable","Self-report incorrectly changed another device");Check(Revision(Call(server,metadata))==Revision(reported),"Repeated metadata caused revision/write");metadata["targetPublicIdentity"]=target;Denied(Call(server,metadata),"Self-report targeted another device");revision=Revision(reported);
            var rename=Fields("device_rename","sessionToken",actorSession,"publicIdentity",actor,"targetPublicIdentity",target,"expectedRevision",revision,"name","Phone e\u0301");
            var renamed=Call(server,rename);Check((string)Row(renamed,target)["name"]=="Phone \u00e9","Rename NFC mismatch: "+BitConverter.ToString(Encoding.UTF8.GetBytes((string)Row(renamed,target)["name"])));Denied(Call(server,rename),"Stale concurrent rename accepted");revision=Revision(renamed);
            rename["expectedRevision"]=revision;rename["name"]="bad\nname";Denied(Call(server,rename),"Control name accepted");rename["name"]="Valid";rename["targetPublicIdentity"]=foreign;Denied(Call(server,rename),"Cross-account rename accepted");
            ContactNames(server,db,account,other,foreignSession,target);
            Denied(Challenge(server,actorSession,actor,target,revision,"bad-password"),"Wrong password accepted");Denied(Challenge(server,actorSession,actor,actor,revision),"Self revoke accepted");Denied(Challenge(server,actorSession,actor,foreign,revision),"Cross-account revoke accepted");
            var challenge=Challenge(server,actorSession,actor,target,revision);
            string canonical=Encoding.ASCII.GetString(Convert.FromBase64String((string)challenge["signatureInput"]));Check(canonical.StartsWith("PeerChat device management v1|revoke|"+account+"|"+actor+"|"+target+"|"+revision+"|"),"Canonical scope mismatch");Check(Convert.ToInt64(challenge["expiresUtc"])-Convert.ToInt64(challenge["issuedUtc"])==60,"Freshness not bounded");
            var forged=Sign(challenge,foreignKey,"device_revoke","sessionToken",actorSession,"publicIdentity",actor,"targetPublicIdentity",target,"expectedRevision",revision);Denied(Call(server,forged),"Wrong signer accepted");Denied(Call(server,Revoke(challenge,actorKey,actorSession,target,revision)),"Consumed failed challenge replay accepted");
            challenge=Challenge(server,actorSession,actor,target,revision);var pending=(IDictionary)typeof(DiscoveryServer).GetField("deviceManagementChallenges",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server);object item=pending[(string)challenge["challengeId"]];item.GetType().GetField("Expires",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(item,0L);Denied(Call(server,Revoke(challenge,actorKey,actorSession,target,revision)),"Expired challenge accepted");
            challenge=Challenge(server,actorSession,actor,target,revision);var changed=Revoke(challenge,actorKey,actorSession,actor,revision);Denied(Call(server,changed),"Target substitution accepted");
            challenge=Challenge(server,actorSession,actor,target,revision);var request=Revoke(challenge,actorKey,actorSession,target,revision);
            var result=Call(server,request);Check((bool)result["ok"],"Scoped revoke failed");Check((string)Row(result,target)["state"]=="revoked"&&!(bool)Row(result,target)["online"],"Revocation state missing");
            Check(db.OwnsDevice(account,target)&&!db.ActiveDevice(account,target),"Ownership/provenance destroyed or target active");Check(db.Authenticate(Hash(actorSession))==account&&db.Authenticate(Hash(foreignSession))==other,"Unrelated proof-bound sessions invalidated");Check(db.Authenticate(Hash(targetSession))==0&&db.Authenticate(Hash(passwordSession))==0,"Target/legacy sessions survived");
            Check((bool)Call(server,request)["ok"],"Exact accepted response-loss retry failed");request["targetPublicIdentity"]=actor;Denied(Call(server,request),"Modified accepted operation inherited success");request["targetPublicIdentity"]=target;
            Check(!db.AddDevice(account,target),"Revoked identity re-added");string relogin=Value(Call(server,Fields("login","username","manager","password","fixture-password")),"sessionToken");Denied(Call(server,Fields("presence","sessionToken",relogin,"publicIdentity",target)),"Password login bypassed tombstone");Denied(Call(server,Fields("remember_enroll_challenge","sessionToken",relogin,"publicIdentity",target)),"Automatic reenrollment allowed");
            db.SendFriendRequest(account,other);db.RespondToRequest(other,account,true);var friends=(List<Dictionary<string,object>>)db.FriendState(other)["friends"];Check(((List<string>)friends.Single()["revokedPublicIdentities"]).Contains(target),"Contact revocation not projected");Check(!friends.Single().ContainsKey("name"),"Private inventory leaked");
            using(var reopened=new AccountDatabase(config.DatabasePath)){Check(!reopened.ActiveDevice(account,target)&&reopened.OwnsDevice(account,target),"Restart lost tombstone/provenance");var persisted=reopened.DeviceInventory(account,actor,Hash(actorSession));Check((string)Row(persisted,target)["name"]=="Phone \u00e9","Restart lost name");var contact=((List<Dictionary<string,object>>)reopened.FriendState(other,true)["friends"]).Single();Check(((Dictionary<string,string>)contact["deviceNames"])[target]=="Phone \u00e9","Restart lost contact display label");Check(((List<string>)contact["revokedPublicIdentities"]).Contains(target),"Retained display label revived revoked identity");}
            db.Dispose();
        }
        PreserveMailbox(args[0],args[3]);
        Console.WriteLine("PASS device manager: scoped auth/signature/step-up/freshness/replay, CAS names, isolation, durable tombstone, session retirement, no reenrollment, queued ciphertext preservation, active authorization.");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
    static void ContactNames(DiscoveryServer server,AccountDatabase db,long account,long other,string session,string target){
        Check(!db.Search("manager",other).ContainsKey("deviceNames"),"Public search leaked names");
        Check(db.Suggestions("man",other,5).All(p=>!p.ContainsKey("deviceNames")),"Suggestions leaked names");
        db.SendFriendRequest(account,other);
        Check(((List<Dictionary<string,object>>)db.FriendState(other,true)["incoming"]).All(p=>!p.ContainsKey("deviceNames")),"Pending request leaked names");
        db.RespondToRequest(other,account,true);
        var legacy=Call(server,Fields("friend_state","sessionToken",session));
        Check(!((List<Dictionary<string,object>>)legacy["friends"]).Single().ContainsKey("deviceNames"),"Legacy/bootstrap response changed without opt-in");
        var request=Fields("friend_state","sessionToken",session);request["deviceNamesVersion"]=1;
        var response=Call(server,request);var row=((List<Dictionary<string,object>>)response["friends"]).Single();
        var names=(Dictionary<string,string>)row["deviceNames"];Check(names.Count==1&&names[target]=="Phone \u00e9","Accepted contact did not receive exact account/device label");
        Check(!row.ContainsKey("platform")&&!row.ContainsKey("version")&&!row.ContainsKey("devices")&&!row.ContainsKey("credential"),"Private inventory leaked with labels");
        var serialize=typeof(DiscoveryServer).GetMethod("SerializeOrdinaryResponse",BindingFlags.NonPublic|BindingFlags.Instance);
        string wire=(string)serialize.Invoke(server,new object[]{"friend_state",response});Check(wire.Contains("deviceNames"),"Wire serializer dropped names");
        var expanded=new Dictionary<string,object>{{"deviceNamesVersion",1},{"padding",new string('x',17000)}};
        Check(((string)serialize.Invoke(server,new object[]{"friend_state",expanded})).Length>16384,"Negotiated response retained old small-frame bound");
        bool legacyBound=false;try{serialize.Invoke(server,new object[]{"friend_state",new Dictionary<string,object>{{"padding",new string('x',17000)}}});}catch(TargetInvocationException e){legacyBound=e.InnerException is InvalidOperationException;}Check(legacyBound,"Legacy frame bound changed");
        db.Execute("UPDATE device_authority SET name='not-base64' WHERE public_identity='"+target+"';");
        var damaged=Call(server,request);Check((bool)damaged["ok"]&&((Dictionary<string,string>)((List<Dictionary<string,object>>)damaged["friends"]).Single()["deviceNames"]).Count==0,"Corrupt cosmetic label blocked contact state");
        db.Execute("UPDATE device_authority SET name='"+Convert.ToBase64String(Encoding.UTF8.GetBytes("Phone \u00e9"))+"' WHERE public_identity='"+target+"';");
        db.RemoveFriend(other,account);Check(((List<Dictionary<string,object>>)Call(server,request)["friends"]).Count==0,"Removed contact still received names");
        db.SendFriendRequest(account,other);db.RespondToRequest(other,account,true);db.SetBlocked(other,account,true);
        var blocked=Call(server,request);Check(((List<Dictionary<string,object>>)blocked["friends"]).Count==0&&((List<Dictionary<string,object>>)blocked["blocked"]).All(p=>!p.ContainsKey("deviceNames")),"Blocked contact received names");
        db.SetBlocked(other,account,false);
        Console.WriteLine("PASS contact labels: negotiated only, Unicode, accepted friendship, search/pending/removal/block privacy, no private inventory.");
    }
    static void PreserveMailbox(string root,string fixture){
        byte[] binding=File.ReadAllBytes(Path.Combine(fixture,"binding.bin")),envelope=File.ReadAllBytes(Path.Combine(fixture,"envelope.bin")),senderPublic=File.ReadAllBytes(Path.Combine(fixture,"sender-public.bin")),recipientPublic=File.ReadAllBytes(Path.Combine(fixture,"recipient-public.bin"));
        var descriptor=MailboxProtocol.Binding.Parse(binding);var message=MailboxProtocol.Envelope.Parse(envelope);string actor=new string('a',64),token=new string('b',64),path=Path.Combine(root,"mailbox-retention.sqlite");
        using(var db=new AccountDatabase(path)){
            db.InitializeMailbox();db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'sender',X'00',X'00',1),(8,'recipient',X'00',X'00',1);INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+message.SenderDevice+"',1),(8,'"+descriptor.Device+"',1),(8,'"+actor+"',1);INSERT INTO friendships VALUES(7,8,1);");
            Check(db.RegisterMailboxKey(8,descriptor.Device,recipientPublic,binding)=="accepted","Fixture signed key failed");Check(db.StoreMailbox(7,message.SenderDevice,senderPublic,envelope)=="stored","Fixture signed envelope failed");
            db.RememberDevice(8,actor,new string('c',64));db.CreateDeviceSession(token,8,actor,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds(),new string('c',64));
            var snapshot=db.MailboxEligibilitySnapshot(7,message.SenderDevice,8);string snapshotId=(string)snapshot["snapshotId"];
            Check(db.RevokeManagedDevice(8,actor,token,descriptor.Device,0,new string('d',64),new string('e',64))==null,"Fixture authority revoke failed");
            Reject(()=>db.FetchMailbox(8,descriptor.Device),"Revoked recipient fetched ciphertext");Check(db.StoreMailbox(7,message.SenderDevice,senderPublic,envelope)=="not_authorized","New delivery reached revoked target");
            var validated=db.ValidateMailboxEligibility(7,message.SenderDevice,8,snapshotId);var frozen=(List<Dictionary<string,object>>)validated["candidates"];Check(frozen.Count==1&&!(bool)frozen[0]["eligible"],"Frozen manifest silently dropped/revived revoked target");
            db.InitializeMailbox(); // Calls actual production purge; ownership row retained, so ciphertext survives.
            var dbHandle=(IntPtr)typeof(AccountDatabase).GetField("database",BindingFlags.NonPublic|BindingFlags.Instance).GetValue(db);IntPtr statement;Check(SqliteNative.prepare(dbHandle,"SELECT count(*) FROM encrypted_mailbox WHERE envelope IS NOT NULL",out statement)==0,"Count prepare");try{Check(SqliteNative.step(statement)==100&&SqliteNative.columnLong(statement,0)==1,"Revocation/purge destroyed queued ciphertext");}finally{SqliteNative.finalize(statement);}
            Check(!db.ArchiveAuthorized(8,actor,descriptor.Device,1,1),"Revoked archive participant authorized");
        }
        using(var reopened=new AccountDatabase(path)){reopened.InitializeMailbox();Check(reopened.OwnsDevice(8,descriptor.Device)&&!reopened.ActiveDevice(8,descriptor.Device),"Reopen lost retained provenance");}
    }
}
