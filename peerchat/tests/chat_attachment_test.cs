using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using PeerChatDesktop;
class ChatAttachmentTest {
    static void Check(bool result,string reason){if(!result)throw new Exception(reason);}
    static string Put(string root,string name,byte[] bytes){string path=Path.Combine(root,name);File.WriteAllBytes(path,bytes);return path;}
    [STAThread] static int Main(string[] args){try{
        string root=args[0];Directory.CreateDirectory(root);
        var conversation=new ConversationModel{Contact=new ContactData{peer="peer",name="test"}};
        foreach(int state in new[]{0,1,2,3,4,5,6,7,8}){
            var row=new CoreAdapter.FileTransferData{id="logical-file",peer="peer",name="message.txt",size=100,done=state==3?50:100,state=state,sending=true,time=100};
            Check(ChatAttachmentProjection.Apply(conversation,row),"snapshot state not projected");Check(conversation.Messages.Count==1,"duplicate/retry bubble");
            Check(!String.IsNullOrEmpty(ChatAttachmentProjection.Status(row)),"missing status");
        }
        var completed=new CoreAdapter.FileTransferData{id="complete",name="a.txt",size=1,state=4,time=200};
        var chat=new ChatViewModel();var typed=chat.Get("peer" );ChatAttachmentProjection.Apply(typed,new CoreAdapter.FileTransferData{id="same-id",peer="peer",senderPeer="peer",state=2});
        chat.Apply(new CoreEvent{kind=5,peer="peer",messages=new[]{new MessageData{id="same-id",sender="peer",text="separate text identity",time=1}}});Check(typed.Messages.Count==2,"attachment suppressed a text message with the same ID" );
        chat.Apply(new CoreEvent{kind=7,messages=new[]{new MessageData{id="same-id"}}});Check(typed.Messages.Single(value=>value.Attachment==null).Data.delivery==2&&typed.Messages.Single(value=>value.Attachment!=null).Data.delivery==0,"text ACK affected attachment state" );
        var restore=new CoreAdapter.FileTransferData{id="restore-transfer",logicalId="logical-file",isArchive=true,originalSending=true,state=3,name="message.txt",time=100};
        ChatAttachmentProjection.Apply(conversation,restore);Check(conversation.Messages.Count==1&&conversation.Messages[0].Attachment.id=="restore-transfer"&&conversation.Messages[0].Outgoing,"restore created a new logical bubble or rewrote author side");
        var reopened=new ConversationModel{Contact=conversation.Contact};ChatAttachmentProjection.Apply(reopened,completed);Check(reopened.Messages.Count==1&&reopened.Messages[0].Attachment.state==4,"completed reload projection");
        var card=new AttachmentCard{Transfer=completed};card.Measure(new Size(300,500));Check(card.Visibility==Visibility.Visible,"attachment card hidden");
        var protectedCard=new AttachmentCard{Transfer=new CoreAdapter.FileTransferData{id="protected",state=4,protectedSource=true,name="message.txt",size=60000}};
        protectedCard.ProtectedPreview("protected",String.Join("\n",Enumerable.Repeat("read-only <script> text",400)),"");Check(protectedCard.Visibility==Visibility.Visible,"protected preview requires a plaintext path");
        var text="Unicode 日本 🙂\r\n<script>alert('text only')</script>\n";
        Check(SafeAttachmentPreview.FromText(String.Join("\n",Enumerable.Repeat(text,400)),100000).Text.Split('\n').Length<=200,"native snippet preview line limit");
        Check(!ChatAttachmentProjection.RequiresSnippet(new string('x',4096))&&ChatAttachmentProjection.RequiresSnippet(new string('x',4097)),"inline limit boundary");
        Check(ChatAttachmentProjection.RequiresSnippet(String.Concat(Enumerable.Repeat(text,20000))),"large Unicode snippet not recognized");
        foreach(string ext in new[]{"txt","c","java","json","md","html","ps1"}){
            var path=Put(root,"source."+ext,new UTF8Encoding(false,true).GetBytes(text));var preview=SafeAttachmentPreview.Read(path,new FileInfo(path).Length);Check(preview.Text==text&&preview.Error=="","safe Unicode/code preview");Check(SafeAttachmentPreview.FullTextAsync(path,new FileInfo(path).Length).GetAwaiter().GetResult()==text,"full viewer text differs");
        }
        string large=Put(root,"large.log",Encoding.UTF8.GetBytes(String.Join("\n",Enumerable.Repeat(new string('x',100),2000))));
        var bounded=SafeAttachmentPreview.Read(large,new FileInfo(large).Length);Check(bounded.Text.Length<=32768&&bounded.Text.Split('\n').Length<=200&&bounded.Truncated,"preview unbounded");
        foreach(var bytes in new[]{new byte[]{0,1,2},new byte[]{255,255,255},new byte[]{137,80,78,71,13,10,26,10}}){var path=Put(root,"fake.txt",bytes);Check(SafeAttachmentPreview.Read(path,bytes.Length).Error.Length>0,"malformed/binary accepted");}
        foreach(bool jpeg in new[]{false,true}){
            var bitmap=BitmapSource.Create(800,600,96,96,PixelFormats.Bgr32,null,new byte[800*600*4],800*4);BitmapEncoder encoder=jpeg?(BitmapEncoder)new JpegBitmapEncoder():new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));string path=Path.Combine(root,jpeg?"jpeg.wrong":"png.wrong");using(var stream=File.Create(path))encoder.Save(stream);
            var value=SafeAttachmentPreview.Read(path,new FileInfo(path).Length);Check(value.Image!=null&&value.Image.PixelWidth<=320&&value.Image.PixelHeight<=240,"image content detection/bounds");
        }
        Check(ChatAttachmentProjection.ValidDrop("account:1","account:1",false,new[]{large}),"correct drop rejected");
        Check(!ChatAttachmentProjection.ValidDrop(null,null,false,new[]{large}),"no conversation drop accepted");
        Check(!ChatAttachmentProjection.ValidDrop("account:1","account:2",false,new[]{large}),"changed selection misrouted");
        Check(!ChatAttachmentProjection.ValidDrop("account:1","account:1",true,new[]{large}),"group drop accepted");
        Check(!ChatAttachmentProjection.ValidDrop("account:1","account:1",false,new[]{Path.Combine(root,"missing")}),"bad path accepted");
        Check(ChatAttachmentProjection.ValidDrop("a","a",false,new[]{large,large}),"multiple drop rejected");
        Check(!ChatAttachmentProjection.ValidDrop("a","a",false,Enumerable.Repeat(large,5).ToArray()),"unbounded multi drop");
        var huge=BitmapSource.Create(5000,4000,96,96,PixelFormats.Bgr32,null,new byte[5000*4000*4],5000*4);var hugeEncoder=new PngBitmapEncoder();hugeEncoder.Frames.Add(BitmapFrame.Create(huge));string hugePath=Path.Combine(root,"oversized.png");using(var stream=File.Create(hugePath))hugeEncoder.Save(stream);Check(SafeAttachmentPreview.Read(hugePath,new FileInfo(hugePath).Length).Error.Length>0,"oversized image decoded");
        Console.WriteLine("PASS chat attachment projection/dedupe/states/reload/drop target/Unicode/CRLF/code/binary/corrupt/image signatures/thumbnail/text limits");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
