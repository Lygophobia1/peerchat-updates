using System;
using System.Linq;
using PeerChatDesktop;

static class DmAttachmentHistoryProjectionTest {
    static CoreAdapter.FileTransferData Row(int number,bool current=false){return new CoreAdapter.FileTransferData{id=number.ToString("x32"),logicalId="dm-"+number,peer="peer",dmCache=true,dmState="complete",state=4,time=1000-number,plaintextVerified=true,saveState=current?"complete":"",savePath=current?"saved":""};}
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    [STAThread] static int Main(){try{
        var conversation=new ConversationModel{Contact=new ContactData{peer="peer",name="history"}};
        // More than both the active snapshot and former lifetime index bounds.
        for(int page=0;page<19;++page)for(int offset=0;offset<32;++offset)ChatAttachmentProjection.Apply(conversation,Row(page*32+offset));
        for(int number=0;number<32;++number)ChatAttachmentProjection.Apply(conversation,Row(number,true));
        Check(conversation.Messages.Count==608,"Current snapshot removed loaded older attachments or duplicated them");
        Check(conversation.Messages.Any(value=>value.Attachment.logicalId=="dm-607"),"Oldest paged attachment disappeared");
        Check(conversation.Messages.Single(value=>value.Attachment.logicalId=="dm-0").Attachment.saveState=="complete","Current save state did not replace paged state");
        for(int number=576;number<608;++number)ChatAttachmentProjection.Apply(conversation,Row(number));
        Check(conversation.Messages.Count==608,"Repeated protected history page duplicated bubbles");
        var active=Row(700);active.state=3;active.done=17;active.size=100;var statistics=ChatAttachmentProjection.Statistics(active);Check(statistics.Contains("Encrypted payload throughput: unavailable")&&!statistics.Contains("Current 0")&&!statistics.Contains("ETA"),"DM throughput or ETA fabricated from logical plaintext progress");
        Console.WriteLine("PASS DM protected history projection: 608 rows, 32-row refresh, repeated older page");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
