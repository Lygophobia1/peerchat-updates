using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Net.Security;
using System.Reflection;
using PeerChatDesktop;

// Compile the real physical adapter against test-only owner/server boundaries.
// The socket is real: native retirement must release its server attachment.
namespace PeerChatDesktop {
    public sealed class CoreAdapter {
        public bool Throw;
        public int TakeRelayOutput(ulong attempt,byte[] bytes,int wait){if(Throw)throw new InvalidOperationException("test owner failure");return -1;}
        public bool RelayReady(ulong attempt,bool incoming){return true;}
        public bool RelayReceive(ulong attempt,byte[] bytes,int count){return true;}
        public void RelayClosed(ulong attempt,string reason){}
    }
    public sealed class DiscoveryClient {
        public string SessionToken;public long AccountId;
        public SslStream OpenAuthenticatedStream(out TcpClient client,bool relay){client=null;throw new NotSupportedException();}
        public static void WriteFrame(SslStream stream,byte[] bytes){throw new NotSupportedException();}
        public static byte[] ReadFrame(SslStream stream,int limit=0){throw new NotSupportedException();}
    }
    public sealed class ArchiveRelayScope {
        public long Account;public string Source,Reader;
        public Dictionary<string,object> Fields(){return new Dictionary<string,object>();}
        public void Verify(Dictionary<string,object> response){}
    }
}
class RelayRetirementTest {
    static int Main(){try {
        for(int cycle=0;cycle<100;cycle++){
            var listener=new TcpListener(IPAddress.Loopback,0);listener.Start();
            try {
                using(var client=new TcpClient()){
                    client.Connect(IPAddress.Loopback,((IPEndPoint)listener.LocalEndpoint).Port);
                    using(var server=listener.AcceptTcpClient()){
                        server.ReceiveTimeout=2000;
                        using(var adapter=new RelayTransport(new DiscoveryClient(),new CoreAdapter{Throw=cycle%2!=0},1,"local",2,"remote",null)){
                            typeof(RelayTransport).GetField("tcp",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(adapter,client);
                            typeof(RelayTransport).GetMethod("WriteLoop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(adapter,new object[]{null,new string('s',43)});
                            if(server.GetStream().ReadByte()!=-1)throw new Exception("Retired writer retained server attachment");
                        }
                    }
                }
            }finally{listener.Stop();}
        }
        Console.WriteLine("PASS: 100 real socket retirement/reopen cycles; native EOF and owner exception release attachment.");return 0;
    }catch(Exception e){Console.Error.WriteLine("FAIL: "+e.GetType().Name+": "+e.Message);return 1;}}
}
