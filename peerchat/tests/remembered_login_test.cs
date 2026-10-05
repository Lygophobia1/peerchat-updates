using System;
using System.IO;
using System.Collections.Generic;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;

static class RememberedLoginTest {
    sealed class TestPush:IPushProvider {public int Calls;public PushResult Result=PushResult.Sent;public PushResult Wake(string token){Interlocked.Increment(ref Calls);return Result;}}
    static void Check(bool ok,string reason){if(!ok)throw new Exception(reason);}
    static string Hash(byte[] data){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(data)).Replace("-","").ToLowerInvariant();}
    static string Hash(string value){return Hash(Encoding.UTF8.GetBytes(value));}
    static Dictionary<string,object> Fields(string type,params string[] pairs){var r=new Dictionary<string,object>{{"version",1},{"type",type}};for(int i=0;i<pairs.Length;i+=2)r.Add(pairs[i],pairs[i+1]);return r;}
    static Dictionary<string,object> Call(DiscoveryServer server,Dictionary<string,object> r){return (Dictionary<string,object>)typeof(DiscoveryServer).GetMethod("Handle",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,new object[]{r,"127.0.0.1",0});}
    static Dictionary<string,object> Proof(Dictionary<string,object> challenge,ECDsaCng key,string type,params string[] pairs){
        byte[] input=Convert.FromBase64String((string)challenge["signatureInput"]);
        var r=Fields(type,pairs);r["challengeId"]=challenge["challengeId"];r["publicKey"]=Convert.ToBase64String(key.Key.Export(CngKeyBlobFormat.EccPublicBlob));r["signature"]=Convert.ToBase64String(key.SignData(input));return r;
    }
    static string Value(Dictionary<string,object> r,string name){Check((bool)r["ok"],"Rejected "+name);return (string)r[name];}
    static void Denied(Dictionary<string,object> r,string reason){Check(!(bool)r["ok"],reason);}
    static int Main(string[] args){try{
        Directory.CreateDirectory(args[0]);string path=Path.Combine(args[0],"auth.sqlite");
        var config=new ServerConfiguration{DataDirectory=args[0],DatabasePath=path,CertificatePath=args[1],CertificatePassword=args[2]};config.Normalize();
        var server=new DiscoveryServer(config);
        var db=(AccountDatabase)typeof(DiscoveryServer).GetField("accounts",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(server);
        using(var key=new ECDsaCng(256))using(var wrong=new ECDsaCng(256)){
            string device=Hash(key.Key.Export(CngKeyBlobFormat.EccPublicBlob));
            string session=Value(Call(server,Fields("register","username","remember_test","password","fixture-only-password","publicIdentity",device)),"sessionToken");
            var challenge=Call(server,Fields("remember_enroll_challenge","sessionToken",session,"publicIdentity",device));
            Denied(Call(server,Proof(challenge,wrong,"remember_enroll","sessionToken",session,"publicIdentity",device)),"Wrong signer accepted");
            challenge=Call(server,Fields("remember_enroll_challenge","sessionToken",session,"publicIdentity",device));
            var enrolled=Call(server,Proof(challenge,key,"remember_enroll","sessionToken",session,"publicIdentity",device));string credential=Value(enrolled,"credential");long account=Convert.ToInt64(enrolled["accountId"]);
            Denied(Call(server,Proof(challenge,key,"remember_enroll","sessionToken",session,"publicIdentity",device)),"Replay accepted");
            Check(db.RememberedAccount(Hash(credential),device)==account,"Credential binding missing");
            using(var reopened=new AccountDatabase(path))Check(reopened.RememberedAccount(Hash(credential),device)==account,"DB reopen lost credential");
            string boundSession="";
            for(int i=0;i<32;i++){
                challenge=Call(server,Fields("remember_challenge","credential",credential,"publicIdentity",device,"purpose","resume"));
                Check(Convert.FromBase64String((string)challenge["signatureInput"]).Length<512,"Unbounded signing request");
                var request=Proof(challenge,key,"remember_resume","credential",credential,"publicIdentity",device);
                var resumed=Call(server,request);boundSession=Value(resumed,"sessionToken");
                Check(Convert.ToInt64(resumed["accountId"])==account&&db.SessionDevice(Hash(boundSession))==device,"Wrong account/device resumed");
                Denied(Call(server,request),"Resume replay accepted");
                Denied(Call(server,Fields("presence","sessionToken",boundSession,"publicIdentity",Hash(wrong.Key.Export(CngKeyBlobFormat.EccPublicBlob)))),"Bound session changed device");
                Check((bool)Call(server,Fields("presence","sessionToken",boundSession,"publicIdentity",device))["ok"],"Bound presence failed");
                var bootstrap=Fields("messaging_bootstrap","sessionToken",boundSession,"publicIdentity",device);bootstrap["bootstrapVersion"]=1;
                var snapshot=Call(server,bootstrap);
                Check((bool)snapshot["ok"]&&Convert.ToInt64(snapshot["accountId"])==account&&(string)snapshot["registeredPublicIdentity"]==device,"Bootstrap account/device binding failed");
                Check((bool)snapshot["presenceLive"]&&(bool)snapshot["relayOnlyPresence"]&&db.IsPresent(account),"Bootstrap did not establish real presence");
                var friendship=(Dictionary<string,object>)snapshot["friendState"];
                Check(friendship.ContainsKey("friends")&&friendship.ContainsKey("incoming")&&!snapshot.ContainsKey("sessionToken")&&!snapshot.ContainsKey("credential")&&!snapshot.ContainsKey("history"),"Bootstrap shape leaked optional/sensitive state");
                bootstrap["publicIdentity"]=Hash(wrong.Key.Export(CngKeyBlobFormat.EccPublicBlob));Denied(Call(server,bootstrap),"Bootstrap accepted wrong device");
                bootstrap["publicIdentity"]=device;bootstrap["bootstrapVersion"]=2;Denied(Call(server,bootstrap),"Bootstrap accepted unsupported version");
                bootstrap.Remove("bootstrapVersion");Denied(Call(server,bootstrap),"Bootstrap accepted missing version");
            }
            db.Execute("UPDATE sessions SET expires_utc=0 WHERE token_hash='"+Hash(boundSession)+"';");
            Denied(Call(server,Fields("session_info","sessionToken",boundSession)),"Expired access session accepted");
            challenge=Call(server,Fields("remember_challenge","credential",credential,"publicIdentity",device,"purpose","resume"));
            boundSession=Value(Call(server,Proof(challenge,key,"remember_resume","credential",credential,"publicIdentity",device)),"sessionToken");
            Denied(Call(server,Fields("push_register","sessionToken",session,"publicIdentity",device,"provider","fcm","pushToken","fixture-token")),"Unbound account token registered push");
            Check((bool)Call(server,Fields("push_register","sessionToken",boundSession,"publicIdentity",device,"provider","fcm","pushToken","fixture-token"))["ok"],"Push registration failed");
            Denied(Call(server,Fields("push_register","sessionToken",boundSession,"publicIdentity",device,"provider","fcm","pushToken",new string('x',2049))),"Oversized token accepted");
            Check((bool)Call(server,Fields("push_register","sessionToken",boundSession,"publicIdentity",device,"provider","fcm","pushToken","replacement-token"))["ok"],"Push replacement failed");
            using(var reopened=new AccountDatabase(path))Check(reopened.PushToken(account,device)=="replacement-token","Push reopen lost registration");
            var vendor=new TestPush();var dispatcher=new PushWakeDispatcher(db,vendor);
            Check(dispatcher.Hint(account,device),"Wake not admitted");Check(!dispatcher.Hint(account,device),"Duplicate wake not bounded");
            for(int i=0;i<100&&vendor.Calls==0;i++)Thread.Sleep(10);Check(vendor.Calls==1,"Provider not invoked once");
            vendor.Result=PushResult.InvalidToken;var invalidDispatcher=new PushWakeDispatcher(db,vendor);Check(invalidDispatcher.Hint(account,device),"Invalid-token probe not admitted");
            for(int i=0;i<100&&db.PushToken(account,device)!=null;i++)Thread.Sleep(10);Check(db.PushToken(account,device)==null,"Invalid token not retired");
            db.RegisterPush(account,device,"restored-token");
            challenge=Call(server,Fields("remember_challenge","credential",credential,"publicIdentity",device,"purpose","revoke"));
            Check((bool)Call(server,Proof(challenge,key,"remember_revoke","credential",credential,"publicIdentity",device))["ok"],"Revoke failed");
            Denied(Call(server,Fields("session_info","sessionToken",boundSession)),"Revoked bound session accepted");
            Check(db.PushToken(account,device)==null,"Revocation retained push registration");
            Denied(Call(server,Fields("remember_challenge","credential",credential,"publicIdentity",device,"purpose","resume")),"Revoked credential accepted");
            Check((bool)Call(server,Fields("session_info","sessionToken",session))["ok"],"Windows-style account token changed");
            challenge=Call(server,Fields("remember_enroll_challenge","sessionToken",session,"publicIdentity",device));
            string second=Value(Call(server,Proof(challenge,key,"remember_enroll","sessionToken",session,"publicIdentity",device)),"credential");
            challenge=Call(server,Fields("remember_enroll_challenge","sessionToken",session,"publicIdentity",device));
            string third=Value(Call(server,Proof(challenge,key,"remember_enroll","sessionToken",session,"publicIdentity",device)),"credential");
            Denied(Call(server,Fields("remember_challenge","credential",second,"publicIdentity",device,"purpose","resume")),"Replaced credential accepted");
            challenge=Call(server,Fields("remember_challenge","credential",third,"publicIdentity",device,"purpose","resume"));
            string logoutSession=Value(Call(server,Proof(challenge,key,"remember_resume","credential",third,"publicIdentity",device)),"sessionToken");
            Check((bool)Call(server,Fields("logout","sessionToken",logoutSession))["ok"],"Bound logout failed");
            Denied(Call(server,Fields("remember_challenge","credential",third,"publicIdentity",device,"purpose","resume")),"Logout kept credential usable");
            var isolated=new RememberedDeviceProofs();var expired=isolated.Issue(account,device,"resume",Hash(third));expired.Expires=0;
            Check(!isolated.Consume(expired.Id,account,device,"resume",Hash(third),key.Key.Export(CngKeyBlobFormat.EccPublicBlob),key.SignData(expired.Input)),"Expired proof accepted");
            var wrongAction=isolated.Issue(account,device,"resume",Hash(third));
            Check(!isolated.Consume(wrongAction.Id,account,device,"revoke",Hash(third),key.Key.Export(CngKeyBlobFormat.EccPublicBlob),key.SignData(wrongAction.Input)),"Wrong purpose accepted");
            dispatcher.Stop();invalidDispatcher.Stop();Check(!dispatcher.Hint(account,device),"Stopped push dispatcher admitted work");
            // Device removal must not turn a formerly bound token into an account bearer token.
            db.ClearPresence(account,device);db.Execute("DELETE FROM devices WHERE public_identity='"+device+"';");
            Check(db.Authenticate(Hash(boundSession))==0,"Deleted device resurrected session");
        }
        db.Dispose();
        Console.WriteLine("PASS: production auth dispatch, opaque device credential, original-message CNG proof, replay/wrong signer/device rejection, 32 resumes, durable reopen, replacement/revocation, unchanged account sessions.");return 0;
    }catch(Exception e){Console.Error.WriteLine("FAIL: "+e.GetType().Name+": "+e.Message);return 1;}}
}
