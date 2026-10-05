using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using PeerChat.Server;

internal static class FcmProviderTest {
    static void Require(bool ok,string message){if(!ok)throw new Exception(message);}
    static byte[] Decode(string value){value=value.Replace('-','+').Replace('_','/');return Convert.FromBase64String(value.PadRight((value.Length+3)/4*4,'='));}
    public static int Main(){try{
        var json=new JavaScriptSerializer();int oauth=0,sends=0,status=200;
        using(var rsa=new RSACng(2048)){
            var provider=new FcmPushProvider("peerchat-development","test@peerchat-development.iam.gserviceaccount.com",
                input=>rsa.SignData(input,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1),
                (url,bearer,body)=>{
                    if(bearer==null){
                        Require(url=="https://oauth2.googleapis.com/token","OAuth endpoint");oauth++;
                        string jwt=Uri.UnescapeDataString(body.Substring(body.IndexOf("&assertion=",StringComparison.Ordinal)+11));
                        var parts=jwt.Split('.');Require(parts.Length==3,"JWT structure");
                        Require(rsa.VerifyData(Encoding.ASCII.GetBytes(parts[0]+"."+parts[1]),Decode(parts[2]),HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1),"Original-input RS256 signature");
                        var claims=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(Decode(parts[1])));
                        Require((string)claims["scope"]=="https://www.googleapis.com/auth/firebase.messaging","OAuth scope");
                        return new FcmHttpResponse{Status=200,Body="{\"access_token\":\"test-only-access-token\",\"expires_in\":3600}"};
                    }
                    sends++;Require(url=="https://fcm.googleapis.com/v1/projects/peerchat-development/messages:send","Send endpoint");
                    var envelope=json.Deserialize<Dictionary<string,object>>(body);
                    var message=(Dictionary<string,object>)envelope["message"];
                    var data=(Dictionary<string,object>)message["data"];
                    Require(data.Count==2&&(string)data["event"]=="peerchat_pending"&&(string)data["version"]=="1","Opaque wake only");
                    Require(!message.ContainsKey("notification"),"No plaintext notification payload");
                    return new FcmHttpResponse{Status=status,Body=status==404?"{\"error\":{\"details\":[{\"@type\":\"type.googleapis.com/google.firebase.fcm.v1.FcmError\",\"errorCode\":\"UNREGISTERED\"}]}}":"{}"};
                });
            Require(provider.Wake("test-only-token")==PushResult.Sent,"Send succeeds");
            Require(provider.Wake("test-only-token")==PushResult.Sent&&oauth==1&&sends==2,"OAuth cache");
            status=404;Require(provider.Wake("test-only-token")==PushResult.InvalidToken,"Retired token");
            status=500;Require(provider.Wake("test-only-token")==PushResult.Unavailable,"Transient provider failure");
            status=401;Require(provider.Wake("test-only-token")==PushResult.Unavailable,"Unauthorized fails closed");
            status=200;Require(provider.Wake("test-only-token")==PushResult.Sent&&oauth==2,"Authorization refresh");
            Require(provider.Wake("")==PushResult.Unavailable,"Empty destination rejected");
        }
        Require(ReliableIoDiagnostics.ExpectedDisconnect(new EndOfStreamException(),"events"),"Event EOF classified");
        Require(!ReliableIoDiagnostics.ExpectedDisconnect(new IOException(),"events"),"Unknown I/O not downgraded");
        Require(!ReliableIoDiagnostics.ExpectedDisconnect(new SocketException((int)SocketError.ConnectionReset),"presence"),"Presence reset not normal event close");
        Require(ReliableIoDiagnostics.RequestKind("credential-content")=="request","Bounded diagnostic kind");
        Console.WriteLine("PASS FCM OAuth/signature/cache/opaque-payload/failure and I/O classification");return 0;
    }catch(Exception error){Console.Error.WriteLine("FAIL "+error.GetType().Name+": "+error.Message);return 1;}}
}
