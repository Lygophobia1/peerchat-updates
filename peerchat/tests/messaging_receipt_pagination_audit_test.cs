using System;
using System.IO;
using PeerChat.Server;

// Production-query regression using isolated synthetic rows. Signed receipt
// authorization is covered by the authenticated mailbox server fixture.
internal static class MessagingReceiptPaginationAuditTest {
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    static void Reject(Action action){bool rejected=false;try{action();}catch(InvalidOperationException){rejected=true;}Check(rejected,"Foreign or malformed cursor was accepted.");}
    static string Id(int value){return value.ToString("x32");}
    public static int Main(string[] args){
        try {
            Check(args.Length==1,"Pass one isolated output directory.");
            string root=Path.GetFullPath(args[0]);Directory.CreateDirectory(root);
            string path=Path.Combine(root,"receipt-pagination-audit.sqlite");
            string sender=new string('a',64),recipientOne=new string('b',64),recipientTwo=new string('c',64);
            using(var db=new AccountDatabase(path)){
                db.InitializeMailbox();
                db.Execute("INSERT INTO accounts(id,username,password_salt,password_hash,created_utc) VALUES(7,'audit-sender',X'00',X'00',1),(8,'audit-recipient',X'00',X'00',1);"+
                    "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+sender+"',1),(8,'"+recipientOne+"',1),(8,'"+recipientTwo+"',1);"+
                    "INSERT INTO friendships(account_low,account_high,created_utc) VALUES(7,8,1);");
                long expires=DateTimeOffset.UtcNow.AddDays(1).ToUnixTimeMilliseconds();
                for(int i=1;i<=63;i++)Insert(db,sender,recipientOne,Id(i),expires);
                string shared=Id(64);
                Insert(db,sender,recipientOne,shared,expires);
                Insert(db,sender,recipientTwo,shared,expires);
                var first=db.MailboxReceipts(7,sender);
                Check(first.Count==64,"Fixture should fill one receipt page.");
                Check((string)first[63]["messageId"]==shared,"Page should end inside the two-device message.");
                string cursor=(string)first[63]["cursor"];
                var next=db.MailboxReceipts(7,sender,"",cursor);
                Check(next.Count==1&&((string)next[0]["messageId"])==shared&&((string)next[0]["recipientDevice"])==recipientTwo,"Composite cursor must reach sibling receipt.");
                Check(db.MailboxReceipts(7,sender,"",(string)next[0]["cursor"]).Count==0,"Receipt sweep must terminate without duplicates.");
                Reject(()=>db.MailboxReceipts(8,recipientOne,"",cursor));
                Reject(()=>db.MailboxReceipts(7,sender,"","7:"+recipientOne+":"+shared+":"+recipientTwo));
                db.Execute("INSERT INTO mailbox_recipient_keys(device,key_id,account_id,generation,state,allowed,proof) VALUES('"+recipientOne+"','"+new string('d',32)+"',8,1,'active',1,X'01');"+
                    "UPDATE encrypted_mailbox SET envelope=X'01',receipt=NULL WHERE recipient_device='"+recipientOne+"';");
                Insert(db,sender,recipientOne,Id(65),expires);
                db.Execute("UPDATE encrypted_mailbox SET envelope=X'01',receipt=NULL WHERE message_id='"+Id(65)+"';");
                var fetchFirst=db.FetchMailbox(8,recipientOne);
                Check(fetchFirst.Count==64,"Invalid envelopes should remain in the first bounded page.");
                var fetchNext=db.FetchMailbox(8,recipientOne,(string)fetchFirst[63]["cursor"]);
                Check(fetchNext.Count==1&&(string)fetchNext[0]["messageId"]==Id(65),"Fetch cursor must reach later envelope despite 64 invalid earlier rows.");
                Check(db.FetchMailbox(8,recipientOne,(string)fetchNext[0]["cursor"]).Count==0,"Fetch sweep must terminate without ACKing invalid records.");
                Check(db.FetchMailbox(8,recipientOne).Count==64,"Invalid ciphertext must remain available for diagnosis or expiry.");
                Reject(()=>db.FetchMailbox(8,recipientTwo,(string)fetchFirst[63]["cursor"]));
                Reject(()=>db.FetchMailbox(7,sender,(string)fetchFirst[63]["cursor"]));
                Console.WriteLine("PASS: 65 per-device receipts across shared-message boundary; 65 retained invalid envelopes traverse bounded fetch pages; account/device cursor scope and termination.");
            }
            return 0;
        }catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
    static void Insert(AccountDatabase db,string sender,string recipient,string message,long expires){
        db.Execute("INSERT INTO encrypted_mailbox(sender_device,message_id,recipient_device,sender_account,recipient_account,key_id,generation,digest,envelope,receipt,created,expires) VALUES('"+
            sender+"','"+message+"','"+recipient+"',7,8,'"+new string('d',32)+"',1,'"+new string('e',64)+"',NULL,X'01',1,"+expires+");");
    }
}
