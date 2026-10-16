using System;
using System.IO;
using PeerChat.Server;

// Expected-observation audit, not a security acceptance test. This records the
// current cursor defect until receipt pagination is fixed and the assertion is
// intentionally changed to require all receipts to be reachable.
internal static class MessagingReceiptPaginationAuditTest {
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
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
                    "INSERT INTO devices(account_id,public_identity,created_utc) VALUES(7,'"+sender+"',1),(8,'"+recipientOne+"',1),(8,'"+recipientTwo+"',1);");
                long expires=DateTimeOffset.UtcNow.AddDays(1).ToUnixTimeMilliseconds();
                for(int i=1;i<=63;i++)Insert(db,sender,recipientOne,Id(i),expires);
                string shared=Id(64);
                Insert(db,sender,recipientOne,shared,expires);
                Insert(db,sender,recipientTwo,shared,expires);
                var first=db.MailboxReceipts(7,sender);
                Check(first.Count==64,"Fixture should fill one receipt page.");
                Check((string)first[63]["messageId"]==shared,"Page should end inside the two-device message.");
                var next=db.MailboxReceipts(7,sender,shared);
                Check(next.Count==0,"Expected-observation: message-only cursor skips sibling device receipt.");
                Check(db.MailboxReceipts(7,sender).Count==64,"Rewinding repeats the first page, so the sibling is still unreachable.");
                Console.WriteLine("OBSERVED DEFECT: 65 durable per-device receipts; first page=64; afterMessageId excludes sibling receipt for boundary message; reset repeats first page.");
            }
            return 0;
        }catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
    static void Insert(AccountDatabase db,string sender,string recipient,string message,long expires){
        db.Execute("INSERT INTO encrypted_mailbox(sender_device,message_id,recipient_device,sender_account,recipient_account,key_id,generation,digest,envelope,receipt,created,expires) VALUES('"+
            sender+"','"+message+"','"+recipient+"',7,8,'"+new string('d',32)+"',1,'"+new string('e',64)+"',NULL,X'01',1,"+expires+");");
    }
}
