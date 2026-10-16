using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using PeerChatDesktop;

internal static class MailboxExecutionReliabilityTest {
    static void Check(bool condition){if(!condition)throw new Exception("Mailbox reliability assertion failed.");}
    static byte[] Record(int type,Action<BinaryWriter> write){using(var output=new MemoryStream()){using(var writer=new BinaryWriter(output,Encoding.UTF8,true)){writer.Write(new byte[]{80,84,1,(byte)type});write(writer);}return output.ToArray();}}
    static void Number(BinaryWriter writer,long value){writer.Write(value);}
    static void Bytes(BinaryWriter writer,byte[] value){Number(writer,value.Length);writer.Write(value);}
    static void Text(BinaryWriter writer,string value){Bytes(writer,Encoding.UTF8.GetBytes(value));}
    static Dictionary<string,object> Reply(string state){return new Dictionary<string,object>{{"state",state}};}
    static Dictionary<string,object> Page(string name,object[] rows){return new Dictionary<string,object>{{"state","ready"},{name,rows},{"nextCursor",rows.Length==0?"":((Dictionary<string,object>)rows.Last())["cursor"]}};}
    public static void Main(){
        var stored=new List<string>();var diagnostics=new List<string>();
        var execution=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};
            if(op==0)return new byte[]{2};
            if(op==1||op==4)return new byte[]{1};
            if(op==2)return Record(4,w=>{Number(w,5);for(int i=1;i<=5;i++){Text(w,"m"+i);Text(w,"peer");Number(w,2);}});
            if(op==8)return Record(4,w=>{Number(w,1);Bytes(w,new byte[]{3});});
            if(op==3)return Encoding.ASCII.GetBytes(id);
            throw new Exception("Unexpected native operation "+op);
        });
        Func<string,Dictionary<string,object>,Dictionary<string,object>> rpc=(operation,body)=>{
            if(operation=="mailbox_register")return Reply("accepted");
            if(operation=="mailbox_fetch")return Page("envelopes",new object[0]);
            if(operation=="mailbox_receipts")return Page("receipts",new object[0]);
            if(operation=="mailbox_keys")return new Dictionary<string,object>{{"state","ready"},{"keys",new object[]{new Dictionary<string,object>{{"binding",Convert.ToBase64String(new byte[]{3})}}}}};
            if(operation=="mailbox_store"){
                string id=Encoding.ASCII.GetString(Convert.FromBase64String((string)body["envelope"]));
                if(id!="m5")return Reply("recipient_key_retired");
                stored.Add(id);return Reply("stored");
            }
            throw new Exception("Unexpected RPC "+operation);
        };
        execution.Service(null,"device",true,(peer,inserted)=>{},diagnostics.Add,rpc);
        Check(stored.Count==0&&execution.NeedsContinuation);
        execution.Service(null,"device",true,(peer,inserted)=>{},diagnostics.Add,rpc);
        Check(stored.SequenceEqual(new[]{"m5"}));
        Check(diagnostics.Count(x=>x.Contains("reason=recipient_key_retired"))==4);
        Check(execution.NextRetryUtc>DateTime.UtcNow);
        string snapshot=execution.DiagnosticsSnapshot();
        Check(snapshot.Contains("Authenticated mailbox_store RPC")&&snapshot.Contains("Native envelope encryption/preparation"));
        Check(snapshot.Contains("Retry pending: 4")&&snapshot.Contains("recipient_key_retired=4")&&!snapshot.Contains("message=m5"));

        var fetched=new List<string>();var cursorRequests=new List<string>();
        var receiver=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};if(op==0)return new byte[]{2};if(op==1)return new byte[]{1};
            if(op==5){string item=Encoding.ASCII.GetString(input);if(item!="e5")throw new InvalidDataException("Poisoned fixture envelope");
                return Record(3,w=>{Number(w,1);Bytes(w,new byte[]{4});Text(w,"peer");Text(w,"m5");});}
            throw new Exception("Unexpected native operation "+op);
        });
        var rows=Enumerable.Range(1,5).Select(i=>(object)new Dictionary<string,object>{{"cursor","opaque"+i},{"envelope",Convert.ToBase64String(Encoding.ASCII.GetBytes("e"+i))}}).ToArray();
        Func<string,Dictionary<string,object>,Dictionary<string,object>> receiveRpc=(operation,body)=>{
            if(operation=="mailbox_register")return Reply("accepted");
            if(operation=="mailbox_receipts")return Page("receipts",new object[0]);
            if(operation=="mailbox_ack"){fetched.Add("ack");return Reply("acknowledged");}
            if(operation=="mailbox_fetch"){
                string cursor=(string)body["cursor"];cursorRequests.Add(cursor);
                return Page("envelopes",cursor==""?rows:cursor=="opaque4"?new[]{rows[4]}:new object[0]);
            }
            throw new Exception("Unexpected RPC "+operation);
        };
        receiver.Service(null,"device",false,(peer,inserted)=>{},diagnostics.Add,receiveRpc);
        Check(receiver.NeedsContinuation&&fetched.Count==0);
        receiver.Service(null,"device",false,(peer,inserted)=>{},diagnostics.Add,receiveRpc);
        Check(cursorRequests.SequenceEqual(new[]{"","opaque4"})&&fetched.Count==1);
        int verified=0;var receiptCursors=new List<string>();
        var reconciler=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};if(op==0)return new byte[]{2};if(op==1||op==6){if(op==6)verified++;return new byte[]{1};}
            throw new Exception("Unexpected native operation "+op);
        });
        var receiptRows=Enumerable.Range(1,65).Select(i=>(object)new Dictionary<string,object>{{"cursor","receipt-cursor-"+i},{"messageId",i==64||i==65?"shared":"m"+i},{"receipt",Convert.ToBase64String(new byte[]{5})}}).ToArray();
        Func<string,Dictionary<string,object>,Dictionary<string,object>> receiptRpc=(operation,body)=>{
            if(operation=="mailbox_register")return Reply("accepted");
            if(operation=="mailbox_fetch")return Page("envelopes",new object[0]);
            if(operation=="mailbox_receipts"){
                string cursor=(string)body["cursor"];receiptCursors.Add(cursor);
                return Page("receipts",cursor==""?receiptRows.Take(64).ToArray():cursor=="receipt-cursor-64"?new[]{receiptRows[64]}:new object[0]);
            }
            throw new Exception("Unexpected RPC "+operation);
        };
        for(int i=0;i<3;i++)reconciler.Service(null,"device",false,(peer,inserted)=>{},diagnostics.Add,receiptRpc);
        Check(verified==65&&receiptCursors.SequenceEqual(new[]{"","receipt-cursor-64","receipt-cursor-65"}));
        Console.WriteLine("PASS: four refused outgoing items do not starve fifth; poisoned incoming rows allow later fetch; all 65 per-device receipts are verified across a boundary sharing one message ID.");
    }
}
