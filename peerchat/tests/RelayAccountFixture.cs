using System;
using System.Collections.Generic;
using System.Text;
using System.Web.Script.Serialization;
using PeerChatDesktop;

// TEST ONLY. Used as a child of the JVM harness. Tokens travel only over its
// private stdout pipe consumed by that harness; never print this child directly.
class RelayAccountFixture {
    static void Check(bool value){if(!value)throw new InvalidOperationException("Host relay account fixture rejected");}
    static int Main(string[] args){try {
        if(args.Length!=5)return 2;
        int port=Int32.Parse(args[1]);string suffix=Guid.NewGuid().ToString("N").Substring(0,12);
        var a=new DiscoveryClient(args[0],port,args[2]);var b=new DiscoveryClient(args[0],port,args[2]);
        string userA="jna_"+suffix,passwordA=Guid.NewGuid().ToString("N");
        string userB="jnb_"+suffix,passwordB=Guid.NewGuid().ToString("N");
        a.Register(userA,passwordA,args[3]);
        b.Register(userB,passwordB,args[4]);
        a.ReportPresence(args[3],"",0,"",0,false);b.ReportPresence(args[4],"",0,"",0,false);
        Check(a.SendFriendRequest(b.AccountId)=="pending");Check(b.RespondToFriendRequest(a.AccountId,true)=="friends");
        var c=new DiscoveryClient(args[0],port,args[2]);string cid=new string('c',64);
        c.Register("jnc_"+suffix,Guid.NewGuid().ToString("N"),cid);
        System.Net.Sockets.TcpClient socket;using(var channel=c.OpenAuthenticatedStream(out socket,true))using(socket){
            var json=new JavaScriptSerializer();
            var request=new Dictionary<string,object>{{"version",1},{"type","relay_attach"},{"sessionToken",c.SessionToken},{"publicIdentity",cid},{"targetAccountId",a.AccountId.ToString()},{"targetPublicIdentity",args[3]}};
            DiscoveryClient.WriteFrame(channel,Encoding.UTF8.GetBytes(json.Serialize(request)));
            var result=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(DiscoveryClient.ReadFrame(channel)));
            Check(!(bool)result["ok"]&&Convert.ToString(result["error"])=="relay_not_authorized");
        }
        c.Logout();
        Console.WriteLine(a.AccountId);Console.WriteLine(a.SessionToken);
        Console.WriteLine(b.AccountId);Console.WriteLine(b.SessionToken);
        // Isolated disposable fixture credentials, private child pipe only.
        // Used to prove fresh login, not token reuse, after native runtime loss.
        Console.WriteLine(userA);Console.WriteLine(passwordA);
        Console.WriteLine(userB);Console.WriteLine(passwordB);
        return 0;
    }catch(Exception error){Console.Error.WriteLine("Host relay fixture failed: "+error.GetType().Name);return 1;}}
}
