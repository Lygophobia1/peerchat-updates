using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Security;
using System.Net.Sockets;
using System.Text;
using System.Linq;
using System.Threading;
using System.Web.Script.Serialization;

namespace PeerChatDesktop {
// Physical adapter only. SessionTransportManager decides when this route exists,
// wins, drains, retries, or is replaced. The server-facing TLS stream carries
// framed opaque bytes; the native core still performs the authoritative peer TLS.
public sealed class RelayTransport : IDisposable {
    const int MaximumPayload=64*1024;
    readonly DiscoveryClient discovery;readonly CoreAdapter core;readonly ulong attempt;readonly string localPeer,targetPeer,groupId;readonly long targetAccount,rosterGeneration;readonly Action<ulong> completed;
    readonly JavaScriptSerializer json=new JavaScriptSerializer{MaxJsonLength=16*1024};
    Thread worker;TcpClient tcp;volatile bool stopping;
    readonly ArchiveRelayScope archive;
    public RelayTransport(DiscoveryClient discovery,CoreAdapter core,ulong attempt,string localPeer,long targetAccount,string targetPeer,Action<ulong> completed,string groupId=null,long rosterGeneration=0,ArchiveRelayScope archive=null){this.discovery=discovery;this.core=core;this.attempt=attempt;this.localPeer=localPeer;this.targetAccount=targetAccount;this.targetPeer=targetPeer;this.completed=completed;this.groupId=groupId;this.rosterGeneration=rosterGeneration;this.archive=archive;}
    public void Start(){worker=new Thread(Run){IsBackground=true,Name="PeerChat opaque relay"};worker.Start();}
    static bool ValidSession(string value){if(String.IsNullOrEmpty(value)||value.Length!=43)return false;foreach(char c in value)if(!(Char.IsLetterOrDigit(c)||c=='-'||c=='_'))return false;return true;}
    void Run(){string failure="Relay closed";SslStream tls=null;Thread writer=null;try{
        // Discovery request sockets are deliberately short-lived, but an authenticated
        // relay winner may be quiet indefinitely. Closing it on the request timeout
        // creates a false disconnect/retry loop. TcpClient.Close still interrupts this
        // unbounded read during profile shutdown; writes retain their bounded timeout.
        tls=discovery.OpenAuthenticatedStream(out tcp,true);
        var request=new Dictionary<string,object>{{"version",1},{"type","relay_attach"},{"sessionToken",discovery.SessionToken??""},{"publicIdentity",localPeer},{"targetAccountId",targetAccount.ToString()},{"targetPublicIdentity",targetPeer}};
        if(!String.IsNullOrEmpty(groupId)){if(rosterGeneration<=0)throw new InvalidOperationException("Group-control roster generation is unavailable.");request["purpose"]="group_key";request["groupId"]=groupId;request["rosterGeneration"]=rosterGeneration.ToString();}
        if(archive!=null){if(!String.IsNullOrEmpty(groupId)||archive.Account!=discovery.AccountId||targetAccount!=archive.Account||
            !((localPeer==archive.Source&&targetPeer==archive.Reader)||(localPeer==archive.Reader&&targetPeer==archive.Source)))throw new InvalidDataException("Archive endpoint binding changed.");foreach(var field in archive.Fields())request[field.Key]=field.Value;}
        DiscoveryClient.WriteFrame(tls,Encoding.UTF8.GetBytes(json.Serialize(request)));
        var response=json.Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(DiscoveryClient.ReadFrame(tls)));object ok,error,sessionValue,incomingValue;
        if(!response.TryGetValue("ok",out ok)||!(bool)ok){response.TryGetValue("error",out error);throw new InvalidOperationException("Relay: "+(error??"request_failed"));}
        if(!response.TryGetValue("sessionId",out sessionValue)||!ValidSession(sessionValue as string)||!response.TryGetValue("incoming",out incomingValue))throw new InvalidDataException("Relay pairing response was invalid.");
        object purposeValue,groupValue,generationValue;
        if(archive!=null)archive.Verify(response);
        if(!String.IsNullOrEmpty(groupId)&&(!response.TryGetValue("purpose",out purposeValue)||!String.Equals(Convert.ToString(purposeValue),"group_key",StringComparison.Ordinal)||!response.TryGetValue("groupId",out groupValue)||!String.Equals(Convert.ToString(groupValue),groupId,StringComparison.Ordinal)||!response.TryGetValue("rosterGeneration",out generationValue)||Convert.ToInt64(generationValue)!=rosterGeneration))throw new InvalidDataException("Group-control relay scope changed during rendezvous.");
        string sessionId=(string)sessionValue;bool incoming=Convert.ToBoolean(incomingValue);if(!core.RelayReady(attempt,incoming))throw new InvalidOperationException("Relay attempt is no longer current.");
        writer=new Thread(()=>WriteLoop(tls,sessionId)){IsBackground=true,Name="PeerChat relay writer"};writer.Start();byte[] session=Encoding.ASCII.GetBytes(sessionId);
        while(!stopping){byte[] frame=DiscoveryClient.ReadFrame(tls,session.Length+MaximumPayload);if(frame.Length<=session.Length)throw new InvalidDataException("Relay frame was empty.");for(int i=0;i<session.Length;i++)if(frame[i]!=session[i])throw new InvalidDataException("Relay session binding changed.");int count=frame.Length-session.Length;var payload=new byte[count];Buffer.BlockCopy(frame,session.Length,payload,0,count);if(!core.RelayReceive(attempt,payload,count))throw new InvalidOperationException("Relay attempt was superseded.");}
    }catch(Exception ex){failure=stopping?"Relay stopped":ex.GetType().Name+": "+ex.Message;}
    finally{stopping=true;try{if(tcp!=null)tcp.Close();}catch{}if(writer!=null&&writer!=Thread.CurrentThread)writer.Join(1500);try{if(tls!=null)tls.Dispose();}catch{}core.RelayClosed(attempt,failure);if(completed!=null)completed(attempt);}}
    void WriteLoop(SslStream tls,string sessionId){byte[] output=new byte[MaximumPayload];byte[] session=Encoding.ASCII.GetBytes(sessionId);try{while(!stopping){int count=core.TakeRelayOutput(attempt,output,1000);if(count<0)return;if(count==0)continue;var frame=new byte[session.Length+count];Buffer.BlockCopy(session,0,frame,0,session.Length);Buffer.BlockCopy(output,0,frame,session.Length,count);DiscoveryClient.WriteFrame(tls,frame);}}catch{try{if(tcp!=null)tcp.Close();}catch{}}}
    public void Dispose(){stopping=true;try{if(tcp!=null)tcp.Close();}catch{}if(worker!=null&&worker!=Thread.CurrentThread)worker.Join(2000);}
}
}
