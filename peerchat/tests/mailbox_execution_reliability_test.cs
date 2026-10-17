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
    static Dictionary<string,object> Eligibility(long recipient,string sender){long now=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds();return new Dictionary<string,object>{
        {"state","ready"},{"snapshotVersion",1},{"senderAccountId",1},{"senderDevice",sender},{"recipientAccountId",recipient},
        {"snapshotId",new string('b',32)},{"snapshotSequence",recipient},{"issuedUtcMs",now},{"freshUntilUtcMs",now+120000},
        {"candidates",new object[]{new Dictionary<string,object>{{"device",new string('c',64)},{"online",true},{"binding",Convert.ToBase64String(new byte[]{9,8})}}}}
    };}
    static void Rejected(Action action){try{action();}catch(InvalidOperationException){return;}throw new Exception("Expected snapshot rejection.");}
    static long BigEndian(byte[] bytes,ref int position,int width){long value=0;for(int i=0;i<width;i++)value=(value<<8)|bytes[position++];return value;}
    static string WireText(byte[] bytes,ref int position){int length=checked((int)BigEndian(bytes,ref position,4));string value=Encoding.UTF8.GetString(bytes,position,length);position+=length;return value;}
    public static void Main(){
        var view=new ChatViewModel();var local=new string('a',64);var remote=new string('b',64);var logicalId=new string('c',32);
        view.Apply(new CoreEvent{kind=0,peer=local,text="Fixture"});
        view.Apply(new CoreEvent{kind=5,peer=remote,messages=new[]{new MessageData{id=logicalId,sender=local,text="fixture",delivery=0}}});
        var bubble=view.Conversations.Values.Single().Messages.Single();
        view.Apply(new CoreEvent{kind=7,peer=remote,messages=new[]{new MessageData{id=logicalId,sender=local,delivery=1}}});
        Check(bubble.Data.delivery==1&&!bubble.Stamp.Contains("✓✓")&&bubble.Stamp.Contains("not fully confirmed"));
        view.Apply(new CoreEvent{kind=7,peer=remote,messages=new[]{new MessageData{id=logicalId,sender=local,delivery=2}}});
        Check(bubble.Data.delivery==2&&bubble.Stamp.Contains("✓✓"));
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

        // A refused device must not back off another device of the same message.
        var deviceStores=new List<byte>();
        var twoDevices=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};if(op==0)return new byte[]{2};if(op==1||op==4||op==12)return new byte[]{1};
            if(op==2)return Record(4,w=>{Number(w,1);Text(w,new string('d',32));Text(w,"peer");Number(w,2);});
            if(op==8)return Record(4,w=>{Number(w,2);Bytes(w,new byte[]{3});Bytes(w,new byte[]{4});});
            if(op==3)return input;
            throw new Exception("Unexpected native operation "+op);
        });
        twoDevices.Service(null,"device",true,(peer,inserted)=>{},diagnostics.Add,(operation,body)=>{
            if(operation=="mailbox_store"){
                byte device=Convert.FromBase64String((string)body["envelope"])[0];deviceStores.Add(device);
                return Reply(device==3?"recipient_key_retired":"stored");
            }
            return rpc(operation,body);
        });
        Check(deviceStores.SequenceEqual(new byte[]{3,4})&&twoDevices.NextRetryUtc>DateTime.UtcNow);

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
        string own=new string('a',64);var installed=new List<byte[]>();var requested=new List<long>();
        var eligibility=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};if(op==0)return new byte[]{2};if(op==1)return new byte[]{1};
            if(op==2)return Record(4,w=>Number(w,0));
            if(op==11){installed.Add(input);return new byte[]{1};}
            throw new Exception("Unexpected native operation "+op);
        });
        Func<string,Dictionary<string,object>,Dictionary<string,object>> eligibilityRpc=(operation,body)=>{
            if(operation=="mailbox_register")return Reply("accepted");
            if(operation=="mailbox_fetch")return Page("envelopes",new object[0]);
            if(operation=="mailbox_receipts")return Page("receipts",new object[0]);
            if(operation=="mailbox_eligibility_snapshot"){
                long recipient=long.Parse((string)body["recipientAccountId"]);requested.Add(recipient);return Eligibility(recipient,own);
            }
            throw new Exception("Unexpected RPC "+operation);
        };
        long[] active={2,3,4,5,6};
        eligibility.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,eligibilityRpc,active,7,1);
        Check(installed.Count==4&&eligibility.NeedsContinuation&&eligibility.NextEligibilityUtc<=DateTime.UtcNow.AddSeconds(2));
        eligibility.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,eligibilityRpc,active,7,1);
        Check(installed.Count==5&&requested.Distinct().Count()==5&&eligibility.NextEligibilityUtc>DateTime.UtcNow.AddSeconds(50));
        var refreshClock=DateTime.UtcNow;
        Check(MailboxExecution.EligibilityNextRefreshUtc(refreshClock,refreshClock.AddSeconds(120))==refreshClock.AddSeconds(60));
        Check(MailboxExecution.EligibilityNextRefreshUtc(refreshClock,refreshClock.AddSeconds(35))==refreshClock.AddSeconds(20));
        Check(MailboxExecution.EligibilityNextRefreshUtc(refreshClock,refreshClock.AddSeconds(5))==refreshClock.AddSeconds(1));
        var reusedSnapshot=Eligibility(2,own);
        reusedSnapshot["issuedUtcMs"]=new DateTimeOffset(refreshClock.AddSeconds(-85)).ToUnixTimeMilliseconds();
        reusedSnapshot["freshUntilUtcMs"]=new DateTimeOffset(refreshClock.AddSeconds(35)).ToUnixTimeMilliseconds();
        var reused=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};if(op==0)return new byte[]{2};
            if(op==1||op==11)return new byte[]{1};
            throw new Exception("Unexpected reused-snapshot native operation "+op);
        },()=>refreshClock);
        reused.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,(operation,body)=>
            operation=="mailbox_register"?Reply("accepted"):
            operation=="mailbox_eligibility_snapshot"?reusedSnapshot:
            operation=="mailbox_fetch"?Page("envelopes",new object[0]):
            operation=="mailbox_receipts"?Page("receipts",new object[0]):
            throw new Exception("Unexpected reused-snapshot RPC "+operation),new long[]{2},7,1);
        Check(reused.NextEligibilityUtc<=refreshClock.AddSeconds(21)&&reused.NextEligibilityUtc>=refreshClock.AddSeconds(19));
        eligibility.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,eligibilityRpc,new long[]{2},8,1);
        Check(installed.Count==6&&requested.Last()==2); // Published friend generation invalidates the managed refresh cache.
        var prefix=Encoding.ASCII.GetBytes("PeerChat/mailbox-eligibility-snapshot/v1\0");
        Check(installed[0].Take(prefix.Length).SequenceEqual(prefix));int cursor=prefix.Length;
        Check(BigEndian(installed[0],ref cursor,8)==1&&BigEndian(installed[0],ref cursor,8)==7&&WireText(installed[0],ref cursor)==own);
        Check(BigEndian(installed[0],ref cursor,8)==2&&BigEndian(installed[0],ref cursor,4)==1&&WireText(installed[0],ref cursor)==new string('b',32));
        Check(BigEndian(installed[0],ref cursor,8)==2);BigEndian(installed[0],ref cursor,8);BigEndian(installed[0],ref cursor,8);
        Check(installed[0][cursor++]==1&&WireText(installed[0],ref cursor)==new string('c',64)&&installed[0][cursor++]==1);
        Check(BigEndian(installed[0],ref cursor,4)==2&&installed[0][cursor++]==9&&installed[0][cursor++]==8&&cursor==installed[0].Length);
        var spoof=Eligibility(2,own);spoof["senderAccountId"]=9;Rejected(()=>MailboxExecution.EligibilitySnapshotWire(spoof,1,own,7,2,DateTime.UtcNow));
        var expired=Eligibility(2,own);expired["freshUntilUtcMs"]=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()-1;Rejected(()=>MailboxExecution.EligibilitySnapshotWire(expired,1,own,7,2,DateTime.UtcNow));
        var wrongRecipient=Eligibility(9,own);Rejected(()=>MailboxExecution.EligibilitySnapshotWire(wrongRecipient,1,own,7,2,DateTime.UtcNow));
        bool invalidated=false;
        try{eligibility.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,(operation,body)=>operation=="mailbox_eligibility_snapshot"?Reply("session_invalidated"):operation=="mailbox_fetch"?Page("envelopes",new object[0]):Page("receipts",new object[0]),new long[]{2},9,1);}
        catch(DiscoveryRequestException error){invalidated=error.ErrorCode=="session_invalidated";}
        Check(invalidated&&installed.Count==6);
        // Physical Pi/Windows skew: no install before local issuance, exact
        // response bytes after issuance, and no refetch/sleep on the owner.
        var clock=DateTime.UtcNow;var origin=clock;int skewInstalls=0,skewFetches=0;
        byte[] skewWire=null;var skewReply=Eligibility(2,own);
        skewReply["issuedUtcMs"]=new DateTimeOffset(origin.AddMilliseconds(37500)).ToUnixTimeMilliseconds();
        skewReply["freshUntilUtcMs"]=new DateTimeOffset(origin.AddMilliseconds(157500)).ToUnixTimeMilliseconds();
        var skew=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};if(op==0)return new byte[]{2};if(op==1)return new byte[]{1};
            if(op==2)return Record(4,w=>Number(w,0));
            if(op==11){Check(clock>=origin.AddMilliseconds(37500));skewInstalls++;skewWire=input;return new byte[]{1};}
            throw new Exception("Unexpected native skew operation");
        },()=>clock);
        Func<string,Dictionary<string,object>,Dictionary<string,object>> skewRpc=(operation,body)=>{
            if(operation=="mailbox_register")return Reply("accepted");
            if(operation=="mailbox_fetch")return Page("envelopes",new object[0]);
            if(operation=="mailbox_receipts")return Page("receipts",new object[0]);
            if(operation=="mailbox_eligibility_snapshot"){skewFetches++;return skewReply;}
            throw new Exception("Unexpected skew RPC");
        };
        Action<long[]> stepSkew=targets=>skew.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,skewRpc,targets,7,1);
        stepSkew(new long[]{2});Check(skewInstalls==0&&skewFetches==1&&skew.DiagnosticsSnapshot().Contains("waiting_for_issuance"));
        clock=origin.AddMilliseconds(37499);stepSkew(new long[]{2});Check(skewInstalls==0&&skewFetches==1);
        clock=origin.AddMilliseconds(38500);stepSkew(new long[]{2});Check(skewInstalls==1&&skewFetches==1);
        Check(skewWire.SequenceEqual(MailboxExecution.EligibilitySnapshotWire(skewReply,1,own,7,2,clock)));
        skew.Reset();clock=origin;stepSkew(new long[]{2});
        clock=origin.AddSeconds(40);stepSkew(new long[0]);Check(skewInstalls==1);
        // A new authenticated generation must discard the deferred scope.
        skew.Reset();clock=origin;stepSkew(new long[]{2});clock=origin.AddSeconds(40);
        skew.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,skewRpc,new long[0],8,1);Check(skewInstalls==1);
        skew.Reset();clock=origin;stepSkew(new long[]{2});clock=origin.AddSeconds(158);
        stepSkew(new long[]{2});Check(skewInstalls==1); // Expired evidence is never installed.
        // Native rejection after waiting still fails closed; it is not a
        // successful install merely because an authenticated response arrived.
        int rejectedInstalls=0;clock=origin;
        var nativeReject=new MailboxExecution((op,input,id,allowed)=>{
            if(op==7)return new byte[]{1};if(op==0)return new byte[]{2};if(op==1)return new byte[]{1};
            if(op==11){rejectedInstalls++;throw new InvalidOperationException();}
            throw new Exception("Unexpected rejected-snapshot operation");
        },()=>clock);
        nativeReject.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,skewRpc,new long[]{2},7,1);
        Check(rejectedInstalls==0);clock=origin.AddSeconds(40);
        nativeReject.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,skewRpc,new long[]{2},7,1);
        Check(rejectedInstalls==1&&nativeReject.DiagnosticsSnapshot().Contains("unavailable reason=native_or_protocol_rejected"));
        // A changed scope discards old bytes, fetching a fresh scoped response.
        skew.Reset();clock=origin;stepSkew(new long[]{2});clock=origin.AddSeconds(40);
        skew.Service(null,own,false,(peer,inserted)=>{},diagnostics.Add,skewRpc,new long[]{2},8,1);
        int scopePosition=prefix.Length;BigEndian(skewWire,ref scopePosition,8);
        Check(BigEndian(skewWire,ref scopePosition,8)==8);
        Console.WriteLine("PASS: fair outgoing/incoming/receipt scans; bounded authenticated eligibility refresh; source/recipient/expiry mismatches rejected.");
    }
}
