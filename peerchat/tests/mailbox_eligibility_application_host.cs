using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using PeerChat.Server;
using PeerChat.ServerShared;
using PeerChatDesktop;

// These are compile-only stubs for MailboxExecution's independent native
// adapter constructor; this fixture uses the production static snapshot codec.
namespace PeerChatDesktop {
    internal sealed class CoreAdapter { internal byte[] Mailbox(uint operation,byte[] input,string id,bool allowed){throw new NotSupportedException();} }
    internal sealed class CoreEvent { internal int kind;internal bool connected;internal string peer;internal int attempt; }
}

internal static class MailboxEligibilityApplicationHost {
    static string stage="initialization";
    static void Check(bool value){if(!value)throw new Exception("Authenticated eligibility application fixture failed at "+stage+".");}
    static byte[] Read(string root,string name){return File.ReadAllBytes(Path.Combine(root,name));}
    static string Device(string root,string name){string value=Encoding.ASCII.GetString(Read(root,name));Check(value.Length==64);return value;}
    static void Write(string root,string name,byte[] bytes){File.WriteAllBytes(Path.Combine(root,name),bytes);}
    static void Write(string root,string name,string value){Write(root,name,Encoding.ASCII.GetBytes(value));}
    static string Hash(string token){using(var sha=SHA256.Create())return BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(token))).Replace("-","").ToLowerInvariant();}
    static Dictionary<string,object>[] Rows(Dictionary<string,object> response,string key){return ((IEnumerable)response[key]).Cast<object>().Cast<Dictionary<string,object>>().ToArray();}
    static void Native(string executable,string action,string root){
        var process=new Process{StartInfo=new ProcessStartInfo(executable,"\""+action+"\" \""+root+"\""){UseShellExecute=false,RedirectStandardOutput=true,RedirectStandardError=true,CreateNoWindow=true}};
        using(process){Check(process.Start());string output=process.StandardOutput.ReadToEnd(),error=process.StandardError.ReadToEnd();bool complete=process.WaitForExit(30000);
            // Native fixture output is deliberately limited to phase names.
            if(output.Length>0)Console.Write(output);if(error.Length>0)Console.Error.Write(error);
            Check(complete&&process.ExitCode==0);
        }
    }
    static Dictionary<string,object> Mailbox(DiscoveryClient client,string device,string operation,Dictionary<string,object> fields){
        var result=client.Mailbox(operation,device,fields);Check(result!=null);return result;
    }
    static void Ready(Dictionary<string,object> result,string expected){if((string)result["state"]!=expected)throw new Exception("Authenticated eligibility application fixture refused at "+stage+" state="+(string)result["state"]);}
    static void Batch(string root,Dictionary<string,object> response){
        Ready(response,"ready");var keys=Rows(response,"keys");Check(keys.Length==2);
        using(var output=new MemoryStream()){
            var domain=Encoding.ASCII.GetBytes("PeerChat/mailbox-recipient-batch/v1\0");output.Write(domain,0,domain.Length);
            output.WriteByte(0);output.WriteByte((byte)keys.Length);
            foreach(var key in keys){var binding=Convert.FromBase64String((string)key["binding"]);Check(binding.Length>0&&binding.Length<=4096);
                for(int shift=24;shift>=0;shift-=8)output.WriteByte((byte)(binding.Length>>shift));output.Write(binding,0,binding.Length);}
            Write(root,"keys-batch.bin",output.ToArray());
        }
    }
    static void WaitReady(int port){var until=DateTime.UtcNow.AddSeconds(10);while(DateTime.UtcNow<until){try{using(var socket=new TcpClient()){socket.Connect("localhost",port);return;}}catch(SocketException){Thread.Sleep(20);}}throw new Exception("Local server did not start.");}
    static void Fetch(DiscoveryClient client,string device,string root,int number){
        var result=Mailbox(client,device,"mailbox_fetch",new Dictionary<string,object>{{"cursor",""}});Ready(result,"ready");
        var rows=Rows(result,"envelopes");Check(rows.Length==1);var wire=Convert.FromBase64String((string)rows[0]["envelope"]);
        Check(wire.SequenceEqual(Read(root,"envelope-"+number+".bin")));Write(root,"fetched-"+number+".bin",wire);
    }
    static void Ack(DiscoveryClient client,string device,string root,int number,byte[] publicKey){
        var receipt=Read(root,"receipt-"+number+".bin");
        Ready(Mailbox(client,device,"mailbox_ack",new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(publicKey)},{"receipt",Convert.ToBase64String(receipt)}}),"acknowledged");
    }
    static void Reconcile(DiscoveryClient sender,string device,string root,int number){
        var result=Mailbox(sender,device,"mailbox_receipts",new Dictionary<string,object>{{"cursor",""}});Ready(result,"ready");
        var recipient=Device(root,"recipient-"+number+"-device.txt");
        var rows=Rows(result,"receipts");var found=rows.Where(row=>(string)row["recipientDevice"]==recipient).ToArray();Check(found.Length==1);
        Write(root,"server-receipt-"+number+".bin",Convert.FromBase64String((string)found[0]["receipt"]));
    }
    public static int Main(string[] args){try{
        Check(args.Length==4);string root=Path.GetFullPath(args[0]),native=Path.GetFullPath(args[1]),certificate=args[2],password=args[3];
        Directory.CreateDirectory(root);Native(native,"setup",root);
        string senderDevice=Device(root,"sender-device.txt"),firstDevice=Device(root,"recipient-1-device.txt"),secondDevice=Device(root,"recipient-2-device.txt");
        string unknownDevice=new string('f',64);Check(unknownDevice!=senderDevice&&unknownDevice!=firstDevice&&unknownDevice!=secondDevice);
        Write(root,"unknown-device.txt",unknownDevice);
        byte[] senderPublic=Read(root,"sender-public.bin"),firstPublic=Read(root,"recipient-1-public.bin"),secondPublic=Read(root,"recipient-2-public.bin");
        string database=Path.Combine(root,"authenticated-server.sqlite");
        var probe=new TcpListener(IPAddress.Loopback,0);probe.Start();int port=((IPEndPoint)probe.LocalEndpoint).Port;probe.Stop();
        var config=new ServerConfiguration{DataDirectory=root,DatabasePath=database,LogDirectory=Path.Combine(root,"server-logs"),Port=port,CertificatePath=certificate,CertificatePassword=password};
        string pin;using(var cert=new X509Certificate2(certificate,password))using(var sha=SHA256.Create())pin=BitConverter.ToString(sha.ComputeHash(cert.RawData)).Replace("-","");
        using(var db=new AccountDatabase(database)){
            db.InitializeMailbox();
            db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'fixture-sender',X'00',X'00',1),(8,'fixture-recipient',X'00',X'00',1);"+
                "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+senderDevice+"',1),(8,'"+firstDevice+"',1),(8,'"+secondDevice+"',1),(8,'"+unknownDevice+"',1);"+
                "INSERT INTO friendships(account_low,account_high,created_utc) VALUES(7,8,1);");
            var sending=new DiscoveryClient("localhost",port,pin);var receiving1=new DiscoveryClient("localhost",port,pin);var receiving2=new DiscoveryClient("localhost",port,pin);
            var unknown=new DiscoveryClient("localhost",port,pin);
            foreach(var pair in new[]{Tuple.Create(sending,7L),Tuple.Create(receiving1,8L),Tuple.Create(receiving2,8L),Tuple.Create(unknown,8L)}){
                pair.Item1.SessionToken=Guid.NewGuid().ToString("N");pair.Item1.AccountId=pair.Item2;
                db.CreateSession(Hash(pair.Item1.SessionToken),pair.Item2,DateTimeOffset.UtcNow.AddHours(1).ToUnixTimeSeconds());
            }
            var server=new DiscoveryServer(config);Exception failure=null;
            var thread=new Thread(()=>{try{server.Run();}catch(Exception error){failure=error;}}){IsBackground=true};thread.Start();
            try{
                stage="server ready";WaitReady(port);
                stage="unknown sibling authenticated presence";unknown.ReportPresence(unknownDevice,"",0,"",0,false);
                stage="recipient one registration";
                Ready(Mailbox(receiving1,firstDevice,"mailbox_register",new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(firstPublic)},{"binding",Convert.ToBase64String(Read(root,"binding-1.bin"))}}),"accepted");
                stage="recipient two registration";
                Ready(Mailbox(receiving2,secondDevice,"mailbox_register",new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(secondPublic)},{"binding",Convert.ToBase64String(Read(root,"binding-2.bin"))}}),"accepted");
                stage="authenticated snapshot";
                var snapshot=Mailbox(sending,senderDevice,"mailbox_eligibility_snapshot",new Dictionary<string,object>{{"recipientAccountId","8"}});
                Ready(snapshot,"ready");stage="snapshot candidate count";Check(Rows(snapshot,"candidates").Length==3);
                stage="managed snapshot wire";
                var wire=MailboxExecution.EligibilitySnapshotWire(snapshot,7,senderDevice,1,8,DateTime.UtcNow);
                Write(root,"snapshot.bin",wire);Write(root,"snapshot-id.txt",(string)snapshot["snapshotId"]);
                stage="authenticated key lookup";
                Batch(root,Mailbox(sending,senderDevice,"mailbox_keys",new Dictionary<string,object>{{"recipientAccountId","8"}}));
                stage="native selection";
                Native(native,"send",root); // Snapshot is installed in WindowsMailbox operation 11 before queue commit.
                stage="ciphertext store";
                for(int number=1;number<=2;++number){
                    Ready(Mailbox(sending,senderDevice,"mailbox_store",new Dictionary<string,object>{{"signingPublicKey",Convert.ToBase64String(senderPublic)},{"envelope",Convert.ToBase64String(Read(root,"envelope-"+number+".bin"))}}),"stored");
                }
                stage="native stored marker";Native(native,"stored",root);
                // Sender process has exited after both stores. Recipient 2 ACKs first.
                stage="recipient two fetch/receive/ack";Fetch(receiving2,secondDevice,root,2);Native(native,"receive2",root);Ack(receiving2,secondDevice,root,2,secondPublic);
                stage="first sender receipt";Reconcile(sending,senderDevice,root,2);Native(native,"reconcile1",root);
                stage="recipient one fetch/receive/ack";Fetch(receiving1,firstDevice,root,1);Native(native,"receive1",root);Ack(receiving1,firstDevice,root,1,firstPublic);
                stage="second sender receipt";Reconcile(sending,senderDevice,root,1);Native(native,"reconcile2",root);
                stage="empty recipient queues";
                Check(Rows(Mailbox(receiving1,firstDevice,"mailbox_fetch",new Dictionary<string,object>{{"cursor",""}}),"envelopes").Length==0);
                Check(Rows(Mailbox(receiving2,secondDevice,"mailbox_fetch",new Dictionary<string,object>{{"cursor",""}}),"envelopes").Length==0);
                Console.WriteLine("PASS authenticated TLS snapshot -> production managed wire -> native op11 freeze -> two independent encrypted recipient receipts across sender/recipient restarts; unknown unpinned sibling skipped; first ACK leaves second pending; no-snapshot send stays unresolved.");
                return 0;
            }finally{
                typeof(DiscoveryServer).GetMethod("RequestStop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(server,null);
                Check(thread.Join(6000));if(failure!=null)throw failure;
            }
        }
    }catch(Exception error){Console.Error.WriteLine(error.GetType().Name+": "+error.Message);return 1;}}
}
