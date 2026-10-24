using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace PeerChatDesktop {
// Dispatcher-owned outgoing presentation. Native snapshots alone own transfer completion.
public sealed class LocalAttachmentSource {
    public string Path {get;private set;} public readonly string Name; public readonly long Size;
    public BitmapSource Preview {get;private set;} public readonly bool IsPhoto;
    byte[] png; readonly DateTime modified;
    public LocalAttachmentSource(string path,long size,string name=null,DateTime? expectedModified=null){Path=path;Name=name??System.IO.Path.GetFileName(path);Size=size;modified=expectedModified??File.GetLastWriteTimeUtc(path);IsPhoto=PhotoName(Name);}
    public LocalAttachmentSource(string name,byte[] bytes,BitmapSource preview){Name=name;Size=bytes.LongLength;png=bytes;Preview=preview;IsPhoto=true;}
    public static bool PhotoName(string name){string ext=System.IO.Path.GetExtension(name??"").ToLowerInvariant();return ext==".png"||ext==".jpg"||ext==".jpeg";}
    public bool Unchanged(){try{return png!=null||File.Exists(Path)&&new FileInfo(Path).Length==Size&&(File.GetAttributes(Path)&(FileAttributes.Directory|FileAttributes.ReparsePoint))==0&&(modified==default(DateTime)||File.GetLastWriteTimeUtc(Path)==modified);}catch{return false;}}
    public async Task PersistAsync(string directory){if(png==null)return;var bytes=png;string folder=System.IO.Path.Combine(directory,Guid.NewGuid().ToString("N"));Directory.CreateDirectory(folder);string path=System.IO.Path.Combine(folder,Name);try{await Task.Run(()=>{using(var file=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None)){file.Write(bytes,0,bytes.Length);file.Flush(true);}});Path=path;png=null;}catch{try{File.Delete(path);Directory.Delete(folder);}catch{}throw;}}
    public async Task LoadPreviewAsync(){if(Preview!=null||!IsPhoto)return;var result=await SafeAttachmentPreview.ReadAsync(Path,Size);Preview=result.Image;}
    public async Task<BitmapSource> OpenImageAsync(){if(!IsPhoto||!Unchanged())throw new IOException("Local image is unavailable or changed.");var bytes=png;string path=Path;return await Task.Run(()=>{using(var stream=bytes!=null?(Stream)new MemoryStream(bytes,false):new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)){if(stream.Length!=Size)throw new IOException("Local image changed.");var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.DelayCreation,BitmapCacheOption.None);var frame=decoder.Frames[0];if(frame.PixelWidth<=0||frame.PixelHeight<=0||(long)frame.PixelWidth*frame.PixelHeight>ClipboardAttachments.MaximumPixels)throw new InvalidDataException("Image exceeds the local viewer bound.");stream.Position=0;var image=new BitmapImage();image.BeginInit();image.CacheOption=BitmapCacheOption.OnLoad;image.StreamSource=stream;image.EndInit();image.Freeze();return (BitmapSource)image;}});}
}
public sealed class PendingOutgoingAttachment {
    public readonly string LocalId="local-attachment:"+Guid.NewGuid().ToString("N");
    public readonly LocalAttachmentSource Source; public string AcceptedId {get;private set;}
    public string Status {get;private set;}="Preparing"; public string Error {get;private set;}="";
    public bool RunCompleted {get;private set;}
    public bool LocalDeleted {get;private set;}
    public void DeletePresentation(){LocalDeleted=true;Notify();}
    public void RestorePresentation(){LocalDeleted=false;Notify();}
    public bool Running {get;private set;} public bool NativeAdmissionInFlight {get;private set;} public bool CancelRequested {get;private set;}
    public bool CanRetry {get{return !LocalDeleted&&!Running&&String.IsNullOrEmpty(AcceptedId)&&current();}}
    readonly Func<bool> current; Func<Task<string>> submit; readonly Func<string,Task> cancel; readonly Action<string> accepted;
    public event Action Changed;
    public PendingOutgoingAttachment(LocalAttachmentSource source,Func<bool> scope,Func<Task<string>> send,Func<string,Task> stop,Action<string> bind){Source=source;current=scope;submit=send;cancel=stop;accepted=bind;}
    public void BeginNativeAdmission(){if(!Running)throw new InvalidOperationException("Attachment admission is not active.");NativeAdmissionInFlight=true;}
    void Notify(){if(Changed!=null)Changed();}
    public async Task RunAsync(){
        if(Running||!String.IsNullOrEmpty(AcceptedId)||LocalDeleted&&RunCompleted)return;
        if(!current()){Status="Not sent";Error="The account or recipient changed. Select this attachment again.";Notify();return;}
        Running=true;RunCompleted=false;CancelRequested=false;Status="Preparing";Error="";Notify();
        try{string id=await submit();if(!current())return;if(String.IsNullOrEmpty(id)){Status=CancelRequested?"Canceled":"Not sent";return;}AcceptedId=id;accepted(id);submit=null;if(CancelRequested){Status="Canceling";Notify();await cancel(id);}else Status="Preparing";}
        catch(Exception error){if(current()){Status=String.IsNullOrEmpty(AcceptedId)?"Failed to send":"Cancel failed";Error=error.Message;}}
        finally{NativeAdmissionInFlight=false;Running=false;RunCompleted=true;if(current())Notify();}
    }
    public async Task CancelAsync(){if(!current())return;CancelRequested=true;if(Running){Status="Canceling";Notify();return;}if(!String.IsNullOrEmpty(AcceptedId)){try{Status="Canceling";Notify();await cancel(AcceptedId);}catch(Exception error){Status="Cancel failed";Error=error.Message;}}else Status="Canceled";Notify();}
}
// Image-only layout shared by direct and group cards. Picture clicks never toggle details.
public static class PhotoAttachmentPresentation {
    public static StackPanel Render(StackPanel panel,MessageModel message,BitmapSource image,string name,long size,string status,Action open,Action<StackPanel> details,Action<WrapPanel> actions,bool detailsExpanded=false,Action toggle=null){
        double maxWidth=Math.Min(360,message==null?360:Math.Max(120,message.BubbleWidth));double width=maxWidth,height=220;
        if(image!=null){double scale=Math.Min(maxWidth/image.PixelWidth,480.0/image.PixelHeight);width=image.PixelWidth*scale;height=image.PixelHeight*scale;}
        var surface=new Grid{Width=width,Height=height,Background=new SolidColorBrush(Color.FromRgb(16,29,42)),Cursor=Cursors.Hand,ToolTip="Open original image"};
        if(image!=null)surface.Children.Add(new Image{Source=image,Stretch=Stretch.Uniform,Width=width,Height=height});
        else surface.Children.Add(new TextBlock{Text="Photo",HorizontalAlignment=HorizontalAlignment.Center,VerticalAlignment=VerticalAlignment.Center,Foreground=Brushes.LightGray});
        surface.MouseLeftButtonUp+=(s,e)=>{e.Handled=true;if(open!=null)open();};panel.Children.Add(surface);
        var footer=new Grid{Background=new SolidColorBrush(Color.FromRgb(27,73,125)),MinWidth=width,Margin=new Thickness(0),Cursor=Cursors.Hand};footer.ColumnDefinitions.Add(new ColumnDefinition());footer.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});
        footer.Children.Add(new TextBlock{Text=ChatAttachmentProjection.CompactSize(size)+" · "+status,FontSize=12,Foreground=Brushes.LightGray,Margin=new Thickness(10,7,6,7),TextWrapping=TextWrapping.Wrap});
        var chevron=new TextBlock{Text=message!=null&&message.IsExpanded?"⌃":"⌄",FontSize=16,Foreground=Brushes.LightGray,Margin=new Thickness(3,3,10,3)};Grid.SetColumn(chevron,1);footer.Children.Add(chevron);
        footer.MouseLeftButtonUp+=(s,e)=>{e.Handled=true;if(message!=null)message.IsExpanded=!message.IsExpanded;else if(toggle!=null)toggle();};panel.Children.Add(footer);
        var expanded=new StackPanel{Margin=new Thickness(10,6,10,8),Visibility=(message!=null?message.IsExpanded:detailsExpanded)?Visibility.Visible:Visibility.Collapsed};expanded.Children.Add(new TextBlock{Text=name,FontWeight=FontWeights.SemiBold,TextWrapping=TextWrapping.Wrap});if(details!=null)details(expanded);panel.Children.Add(expanded);
        var buttons=new WrapPanel{Margin=new Thickness(8,0,8,5)};if(actions!=null)actions(buttons);if(buttons.Children.Count>0){bool failure=status.IndexOf("fail",StringComparison.OrdinalIgnoreCase)>=0||status=="Not sent"||status=="Canceled"||status=="Paused";if(failure)panel.Children.Add(buttons);else expanded.Children.Add(buttons);}return expanded;
    }
    public static async void Open(LocalAttachmentSource source){try{var image=await source.OpenImageAsync();var view=new Image{Source=image,Stretch=Stretch.None};new Window{Title=source.Name+" — original image",Owner=Application.Current==null?null:Application.Current.MainWindow,Width=900,Height=700,Background=Brushes.Black,Content=new ScrollViewer{Content=view,HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,VerticalScrollBarVisibility=ScrollBarVisibility.Auto}}.Show();}catch(Exception error){MessageBox.Show(error.Message,"Image unavailable");}}
    public static Button Button(string text,Action action){var button=new Button{Content=text,Padding=new Thickness(8,4,8,4),Margin=new Thickness(0,3,6,0)};button.Click+=(s,e)=>{e.Handled=true;action();};return button;}
    public static void PendingDetails(StackPanel panel,PendingOutgoingAttachment pending){panel.Children.Add(new TextBlock{Text=pending.Source.Size.ToString("N0")+" bytes · "+pending.Status,Foreground=Brushes.LightGray});if(!String.IsNullOrEmpty(pending.Error))panel.Children.Add(new TextBlock{Text=pending.Error,Foreground=Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});}
    public static void PendingActions(WrapPanel panel,PendingOutgoingAttachment pending){if(pending.CanRetry)panel.Children.Add(Button("Retry",()=>{_ = pending.RunAsync();}));if(pending.Running||String.IsNullOrEmpty(pending.AcceptedId)&&pending.Status!="Canceled")panel.Children.Add(Button("Cancel",()=>{_ = pending.CancelAsync();}));}
}
public enum DmAttachmentDeliveryMode { Cancel, OnlineDevice, EncryptedCache }
// A user-selected delivery scope, never an automatic authorization fallback.
public static class DmAttachmentDelivery {
    public const string OnlineDescription="Send only to this verified device using the existing encrypted peer connection. Other linked devices will NOT receive this attachment, and no offline cache copy is created.";
    public const string CacheDescription="Upload once for every authorized recipient device. All required device capabilities and cache authorization checks must pass; older devices are never silently omitted.";
    public static bool OnlineSelectable(ConversationModel target){return target!=null&&target.AccountId>0&&target.IsFriend&&target.Contact!=null&&target.Contact.verified&&!String.IsNullOrEmpty(target.Contact.peer);}
    public static bool OnlineReady(ConversationModel target){return OnlineSelectable(target)&&target.Online&&target.WinningConnectionId>0;}
    public static bool CompatibilityBlocked(string state){return state=="cache_dm_recipient_upgrade_required"||state=="cache_dm_source_upgrade_required"||state=="cache_dm_feature_gated"||state=="cache_unsupported_operation"||state=="unsupported_request";}
    // Only a transport failure of the read-only preflight may offer a separate
    // user-selected direct scope. A server refusal or unclassified I/O error
    // might be an authorization decision and is never treated as compatibility.
    public static bool PreflightTransportUnavailable(Exception error){
        return error is System.Net.Sockets.SocketException||error is TimeoutException||
            error is IOException&&error.InnerException is System.Net.Sockets.SocketException;
    }
    public static string CompatibilityMessage(string state){return state=="preflight_transport_unavailable"?"The offline attachment preflight could not reach the server. No cache attachment was started. A direct transfer reaches only the selected verified device; other linked devices will not receive it.":state=="cache_dm_recipient_upgrade_required"?"No recipient device currently supports offline attachments. Update and connect at least one recipient device, then retry.":state=="cache_dm_source_upgrade_required"?"This sending device has not registered offline attachment capability. No recipient was omitted.":"This server does not currently support the offline attachment cache. No recipient was omitted.";}
    public static string Target(DmAttachmentDeliveryMode mode,long account){if(account<=0||(mode!=DmAttachmentDeliveryMode.OnlineDevice&&mode!=DmAttachmentDeliveryMode.EncryptedCache))throw new InvalidOperationException("Attachment delivery was not selected.");return (mode==DmAttachmentDeliveryMode.OnlineDevice?"online:":"")+account.ToString(System.Globalization.CultureInfo.InvariantCulture);}
}
// Presentation only: no persisted UI transfer state, sockets, ACK or retry owner.
public static class ChatAttachmentProjection {
    // Presentation marker in the existing authenticated attachment name field;
    // never an authorization or executable-type discriminator.
    public const string LongMessageName="message.peerchat-text";
    public static bool IsLongMessage(CoreAdapter.FileTransferData row){return row!=null&&(row.protectedSource||row.name==LongMessageName);}
    public const int InlineCharacters=4096,SnippetCharacters=1024*1024;
    public const int SnippetUtf8Bytes=512*1024;
    public static bool RequiresSnippet(string text){return text!=null&&text.Length>InlineCharacters;}
    public static string LogicalId(CoreAdapter.FileTransferData row){return String.IsNullOrEmpty(row.logicalId)?row.id:row.logicalId;}
    public static bool ValidDrop(string captured,string current,bool group,string[] files){
        return !group&&!String.IsNullOrEmpty(captured)&&captured==current&&files!=null&&files.Length>0&&files.Length<=4&&files.All(path=>!String.IsNullOrWhiteSpace(path)&&File.Exists(path)&&!Directory.Exists(path));
    }
    public static string Status(CoreAdapter.FileTransferData row){
        if(row.error=="Sender is preparing file")return row.state==2?"Sender is preparing — Download to accept now":"Accepted — waiting for sender to finish preparing";
        if(row.dmCache){if(row.dmState=="paused")return "Paused — encrypted attachment retained";if(row.dmState=="failed")return "Encrypted attachment failed";if(row.dmState=="canceled")return "Canceled";if(row.sending){if(row.dmState=="complete")return "Recipient files verified";if(row.noticesStored&&row.uploadComplete)return "Available to recipients — awaiting file verification";if(row.noticesStored)return "Offer delivered — uploading encrypted file";if(row.uploadComplete)return "Encrypted upload stored — sending recipient notices";}if(row.dmState=="waiting_for_upload")return "Accepted — sender is preparing the cached file";if(row.dmState=="verifying")return "Verifying and retaining decrypted file";if(row.dmState=="available"&&!row.sending)return "Encrypted attachment — Download to accept";}
        string[] states={"Preparing / hashing","Waiting for recipient / route","Incoming attachment — Download to accept",row.sending?"Sending":"Receiving","Complete — SHA-256 verified","Canceled","Failed","Revalidating retained file","Paused — retained for restart","Archive metadata — file not local"};
        string label=row.state>=0&&row.state<states.Length?states[row.state]:"Unavailable";
        if(row.state==4&&!row.sending){
            if(row.saveState=="complete")label="Saved — SHA-256 verified";
            else if(row.saveState=="pending"||row.saveState=="copying")label="Download verified — saving";
            else label="Download verified — save needs attention";
        }
        if(row.state==3&&row.size>0)label+=" · "+Math.Min(100,Math.Max(0,100.0*row.done/row.size)).ToString("0")+"%";
        return label;
    }
    public static string CompactStatus(CoreAdapter.FileTransferData row){
        if(row==null)return "";
        if(row.state==5||row.dmState=="canceled")return "Canceled";
        if(row.state==6||row.dmState=="failed")return "Failed";
        if(!row.sending&&row.saveState=="failed")return "Retry Save";
        if(!row.sending&&(row.saveState=="pending"||row.saveState=="copying"))return "Saving";
        if(row.dmState=="paused"||row.state==8)return "Paused";
        if(row.dmCache&&row.sending&&row.uploadComplete)return "Uploaded to server";
        if(row.dmCache&&row.sending&&row.size>0&&row.done>=row.size)return "Finalizing server upload";
        if(row.state==3){
            string action=row.sending?"Sending":"Receiving";
            return row.size>0?action+" · "+Math.Min(100,Math.Max(0,100.0*row.done/row.size)).ToString("0")+"%":action;
        }
        if(row.dmCache&&row.uploadComplete)return row.noticesStored?"Available":"Uploaded";
        if(row.state==0||row.state==7)return "Preparing";
        if(row.state==1)return "Waiting";
        if(row.state==2&&!row.sending)return "Ready to download";
        if(row.state==4)return row.saveState=="complete"?"Saved":row.sending?"Uploaded":"Downloaded";
        if(row.state==9)return "Archive reference";
        if(row.dmState=="waiting_for_upload")return "Accepted — sender is preparing the cached file";if(row.dmState=="verifying")return "Verifying";
        if(row.dmState=="available")return "Available";
        return "Unavailable";
    }
    public static string CompactSize(long bytes){
        if(bytes<0)return "Size unavailable";
        string[] units={"bytes","KB","MB","GB","TB"};double value=bytes;int unit=0;
        while(value>=1024&&unit<units.Length-1){value/=1024;unit++;}
        return unit==0?bytes.ToString("N0")+" bytes":value.ToString(value>=100?"0":value>=10?"0.#":"0.##",System.Globalization.CultureInfo.InvariantCulture)+" "+units[unit];
    }
    public static string Statistics(CoreAdapter.FileTransferData row){
        var culture=System.Globalization.CultureInfo.InvariantCulture;
        double percent=row.size>0?Math.Min(100,100.0*row.done/row.size):row.state==4?100:0;
        string average=row.statisticsKnown&&row.durationMs>0?(row.statisticsBytes*8000.0/row.durationMs/1000000).ToString("0.00",culture)+" Mbps":"unavailable";
        string result=row.done.ToString("N0")+" / "+row.size.ToString("N0")+" bytes · "+percent.ToString("0.0",culture)+"%";
        if(row.dmCache)return result+"\nEncrypted payload throughput: unavailable · Duration: unavailable";
        if(row.state==4)return result+"\nAverage "+average+" · Duration "+(row.statisticsKnown?(row.durationMs/1000.0).ToString("0.0",culture)+" s":"unavailable");
        result+="\nCurrent "+(row.state==3?row.rateBits/1000000.0:0).ToString("0.00",culture)+" Mbps · Average "+average;
        if(row.state==3&&row.etaReliable&&row.rateBits>0&&row.done<row.size){double seconds=Math.Ceiling((row.size-row.done)*8.0/row.rateBits);if(seconds>0&&seconds<=604800)result+=" · ETA "+seconds.ToString("0",culture)+" s";}
        return result;
    }
    public static bool Apply(ConversationModel conversation,CoreAdapter.FileTransferData row){
        if(conversation.LocalDeletions.Hidden(row.id,row.logicalId))return false;
        var message=conversation.Messages.FirstOrDefault(item=>item.Attachment!=null&&(LogicalId(item.Attachment)==LogicalId(row)||item.Attachment.id==row.id||item.PendingAttachment!=null&&item.PendingAttachment.AcceptedId==row.id));
        if(message!=null){bool wasPending=message.PendingAttachment!=null;if(wasPending){message.PendingAttachment=null;message.Data.id=LogicalId(row);message.Data.time=row.time;message.Data.logical=row.time;}var previous=message.Attachment;if(!wasPending&&previous.id==row.id&&previous.state==row.state&&previous.done==row.done&&previous.path==row.path&&previous.error==row.error&&previous.rateBits==row.rateBits&&previous.durationMs==row.durationMs&&previous.statisticsBytes==row.statisticsBytes&&previous.statisticsKnown==row.statisticsKnown&&previous.etaReliable==row.etaReliable&&previous.saveState==row.saveState&&previous.savePath==row.savePath&&previous.saveError==row.saveError&&previous.dmState==row.dmState&&previous.uploadComplete==row.uploadComplete&&previous.noticesStored==row.noticesStored&&previous.thumbnail==row.thumbnail)return false;message.SetAttachment(row);return true;}
        bool outgoing=row.isArchive?row.originalSending:row.sending;
        message=new MessageModel{Data=new MessageData{id=LogicalId(row),sender=row.senderPeer??"",text="",time=row.time,logical=row.time},Outgoing=outgoing};
        message.SetAttachment(row);
        int index=0;while(index<conversation.Messages.Count&&(conversation.Messages[index].Data.time<=row.time||row.time==0))index++;
        conversation.Messages.Insert(index,message);conversation.Refresh();return true;
    }
}
public sealed class AttachmentPreview {
    public string Text="",Error="";public BitmapSource Image;public bool Truncated;
}
public static class EarlyAttachmentThumbnail {
    public const int MaximumBytes=2048,MaximumPixels=128;
    static readonly Dictionary<string,BitmapSource> cache=new Dictionary<string,BitmapSource>(StringComparer.Ordinal);
    static readonly Queue<string> order=new Queue<string>();
    static bool BoundedJpegDimensions(byte[] bytes,out int width,out int height){
        width=height=0;int at=2;bool dimensions=false;
        while(at+4<=bytes.Length){
            if(bytes[at++]!=0xff)return false;
            while(at<bytes.Length&&bytes[at]==0xff)at++;
            if(at>=bytes.Length)return false;
            int marker=bytes[at++];
            if(marker==0||marker==0xd8||marker==0xd9||marker>=0xd0&&marker<=0xd7)return false;
            if(at+2>bytes.Length)return false;
            int length=bytes[at]<<8|bytes[at+1];
            if(length<2||length>bytes.Length-at)return false;
            if(marker==0xc0){
                if(dimensions||length<8||bytes[at+2]!=8)return false;
                height=bytes[at+3]<<8|bytes[at+4];width=bytes[at+5]<<8|bytes[at+6];
                int components=bytes[at+7];
                if(width<1||height<1||width>MaximumPixels||height>MaximumPixels||
                   (components!=1&&components!=3)||length!=8+3*components)return false;
                dimensions=true;
            }else if(marker>=0xc1&&marker<=0xcf&&marker!=0xc4&&marker!=0xc8&&marker!=0xcc)return false;
            if(marker==0xda)return dimensions;
            at+=length;
        }
        return false;
    }
    public static BitmapSource Decode(string encoded){
        if(String.IsNullOrEmpty(encoded)||encoded.Length>2732)return null;
        lock(cache){BitmapSource found;if(cache.TryGetValue(encoded,out found))return found;}
        try{
            byte[] bytes=Convert.FromBase64String(encoded);
            if(bytes.Length<4||bytes.Length>MaximumBytes||bytes[0]!=0xff||bytes[1]!=0xd8||bytes[2]!=0xff||bytes[bytes.Length-2]!=0xff||bytes[bytes.Length-1]!=0xd9)return null;
            int width,height;if(!BoundedJpegDimensions(bytes,out width,out height))return null;
            using(var stream=new MemoryStream(bytes,false)){
                var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.DelayCreation,BitmapCacheOption.None);
                if(decoder.Frames.Count!=1)return null;
                var frame=decoder.Frames[0];
                if(frame.PixelWidth!=width||frame.PixelHeight!=height)return null;
            }
            BitmapSource image;
            using(var stream=new MemoryStream(bytes,false)){
                var bitmap=new BitmapImage();bitmap.BeginInit();bitmap.CacheOption=BitmapCacheOption.OnLoad;
                bitmap.StreamSource=stream;bitmap.DecodePixelWidth=width;bitmap.DecodePixelHeight=height;
                bitmap.EndInit();bitmap.Freeze();image=bitmap;
            }
            lock(cache){if(!cache.ContainsKey(encoded)){while(order.Count>=64)cache.Remove(order.Dequeue());cache[encoded]=image;order.Enqueue(encoded);}}return image;
        }catch{return null;}
    }
}
public static class SafeAttachmentPreview {
    public const int Bytes=32768,Lines=200,ThumbnailWidth=360,ThumbnailHeight=480;
    static readonly SemaphoreSlim reads=new SemaphoreSlim(2,2);
    public static bool TryNativeRead(){return reads.Wait(0);}
    public static void EndNativeRead(){reads.Release();}
    public static AttachmentPreview FromText(string text,long size){int lines=1,end=text.Length;for(int i=0;i<text.Length;i++)if(text[i]=='\n'&&++lines>Lines){end=i;break;}return new AttachmentPreview{Text=text.Substring(0,end),Truncated=end<text.Length||Encoding.UTF8.GetByteCount(text)<size};}
    static readonly Dictionary<string,AttachmentPreview> cache=new Dictionary<string,AttachmentPreview>(StringComparer.Ordinal);
    static readonly Queue<string> order=new Queue<string>();
    public static async Task<AttachmentPreview> ReadAsync(string path,long expectedSize){
        if(!reads.Wait(0))return new AttachmentPreview{Error="Preview busy — try Preview again"};
        try{
            return await Task.Run(()=>{
                string key;
                try{var info=new FileInfo(path);key=path+"|"+info.Length+"|"+info.LastWriteTimeUtc.Ticks;if(info.Length!=expectedSize||(info.Attributes&FileAttributes.ReparsePoint)!=0)return new AttachmentPreview{Error="Local file missing or changed"};}
                catch{return new AttachmentPreview{Error="Local file unavailable"};}
                lock(cache){AttachmentPreview cached;if(cache.TryGetValue(key,out cached))return cached;}
                var value=Read(path,expectedSize);
                lock(cache){if(!cache.ContainsKey(key)){while(order.Count>=24)cache.Remove(order.Dequeue());order.Enqueue(key);cache[key]=value;}}return value;
            });
        }finally{reads.Release();}
    }
    public static async Task<string> FullTextAsync(string path,long size){
        if(size>1024*1024)throw new InvalidDataException("Full text viewer is limited to 1 MiB; use Retry Save and an external viewer for larger files.");
        if(!reads.Wait(0))throw new IOException("File viewer busy; try again.");
        try{return await Task.Run(()=>{using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)){
            if(stream.Length!=size)throw new InvalidDataException("File changed");var data=new byte[(int)size];int count=0,n;while(count<data.Length&&(n=stream.Read(data,count,data.Length-count))>0)count+=n;if(count!=data.Length)throw new IOException("Incomplete local read");
            int offset=data.Length>=3&&data[0]==239&&data[1]==187&&data[2]==191?3:0;string text=new UTF8Encoding(false,true).GetString(data,offset,data.Length-offset);
            if(text.Any(ch=>ch=='\0'||(Char.IsControl(ch)&&ch!='\r'&&ch!='\n'&&ch!='\t')))throw new InvalidDataException("Not safe text");return text;
        }});}finally{reads.Release();}
    }
    public static AttachmentPreview Read(string path,long expectedSize){
        var result=new AttachmentPreview();
        try{
            using(var stream=new FileStream(path,FileMode.Open,FileAccess.Read,FileShare.Read)){
                if(stream.Length!=expectedSize)return new AttachmentPreview{Error="Local file changed"};
                var prefix=new byte[Math.Min(Bytes,stream.Length)];int count=0,n;while(count<prefix.Length&&(n=stream.Read(prefix,count,prefix.Length-count))>0)count+=n;
                bool png=count>=8&&prefix[0]==137&&prefix[1]==80&&prefix[2]==78&&prefix[3]==71&&prefix[4]==13&&prefix[5]==10&&prefix[6]==26&&prefix[7]==10;
                bool jpeg=count>=3&&prefix[0]==255&&prefix[1]==216&&prefix[2]==255;
                if(png||jpeg){
                    // WIC probes metadata first; decode target and pixel count are bounded.
                    stream.Position=0;var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.DelayCreation,BitmapCacheOption.None);
                    var frame=decoder.Frames[0];int width=frame.PixelWidth,height=frame.PixelHeight;
                    if(width<=0||height<=0||(long)width*height>16L*1024*1024||width>16384||height>16384)throw new InvalidDataException();
                    double scale=Math.Min(1,Math.Min((double)ThumbnailWidth/width,(double)ThumbnailHeight/height));
                    stream.Position=0;var image=new BitmapImage();image.BeginInit();image.CacheOption=BitmapCacheOption.OnLoad;image.StreamSource=stream;image.DecodePixelWidth=Math.Max(1,(int)(width*scale));image.DecodePixelHeight=Math.Max(1,(int)(height*scale));image.EndInit();image.Freeze();result.Image=image;return result;
                }
                // Strict UTF-8 (optional BOM). Extensions never authorize execution or decoding.
                int offset=count>=3&&prefix[0]==239&&prefix[1]==187&&prefix[2]==191?3:0;
                var encoding=new UTF8Encoding(false,true);var decoderText=encoding.GetDecoder();var chars=new char[Bytes];int used,written;bool completed;
                decoderText.Convert(prefix,offset,count-offset,chars,0,chars.Length,stream.Length<=Bytes,out used,out written,out completed);
                string text=new string(chars,0,written);
                if(text.Any(ch=>ch=='\0'||(Char.IsControl(ch)&&ch!='\r'&&ch!='\n'&&ch!='\t')))throw new InvalidDataException();
                int line=1,end=text.Length;for(int i=0;i<text.Length;i++)if(text[i]=='\n'&&++line>Lines){end=i;break;}
                result.Text=text.Substring(0,end);result.Truncated=stream.Length>Bytes||end<text.Length;return result;
            }
        }catch{result.Error="Preview unavailable — use Retry Save or Open Folder";return result;}
    }
}
// Shared read-only text surface for files, protected snippets and ordinary long
// chat messages. Height is bounded independently of exact text/line count.
public sealed class BoundedTextView:StackPanel {
    public readonly TextBox Viewer;
    public readonly Button Expand,Copy,OpenFull;
    public static bool CodeLike(string text){return System.Text.RegularExpressions.Regex.Matches(text??"",@"(?m)^\s*(?:public |private |class |def |function |#include|import |using |.*[;{}]\s*$)").Count>=3;}
    public static TextBox ReadOnly(string text,bool code){return new TextBox{Text=text??"",IsReadOnly=true,AcceptsReturn=true,FontSize=16,FontFamily=new FontFamily(code?"Consolas, Segoe UI Emoji":"Segoe UI, Segoe UI Emoji"),Background=new SolidColorBrush(Color.FromRgb(11,21,30)),Foreground=Brushes.White,TextWrapping=code?TextWrapping.NoWrap:TextWrapping.Wrap,HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Padding=new Thickness(6),BorderThickness=new Thickness(0)};}
    public static void ShowFull(string text,bool code,string title){var owner=Application.Current==null?null:Application.Current.MainWindow;var dialog=new Window{Title=title+" — read-only",Width=760,Height=520,Background=new SolidColorBrush(Color.FromRgb(11,21,30)),Foreground=Brushes.White,Content=ReadOnly(text,code)};if(owner!=null&&owner.IsVisible)dialog.Owner=owner;dialog.Show();}
    public BoundedTextView(string text,bool code,string title,Action copy=null,Action open=null){
        code=code||CodeLike(text);Children.Add(new TextBlock{Text=title,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,0,0,6)});
        Viewer=ReadOnly(text,code);Viewer.MaxHeight=160;Children.Add(Viewer);
        ScrollViewer colorView=null;
        if(!code){colorView=new ScrollViewer{Content=EmojiColor.Preview(text),MaxHeight=160,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled};Children.Add(colorView);Viewer.Visibility=Visibility.Collapsed;
            var selectText=new Button{Content="Select Unicode text / Color view",Padding=new Thickness(8,4,8,4),HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,4,0,0)};selectText.Click+=(s,e)=>{bool showText=Viewer.Visibility!=Visibility.Visible;Viewer.Visibility=showText?Visibility.Visible:Visibility.Collapsed;colorView.Visibility=showText?Visibility.Collapsed:Visibility.Visible;};Children.Add(selectText);}
        var actions=new WrapPanel{Margin=new Thickness(0,4,0,0)};Children.Add(actions);
        Expand=new Button{Content="Expand",Padding=new Thickness(8,4,8,4),Margin=new Thickness(0,0,6,0)};Expand.Click+=(s,e)=>{bool expanded=Viewer.MaxHeight>160;Viewer.MaxHeight=expanded?160:320;if(colorView!=null)colorView.MaxHeight=Viewer.MaxHeight;Expand.Content=expanded?"Expand":"Show less";};actions.Children.Add(Expand);
        Copy=new Button{Content="Copy",Padding=new Thickness(8,4,8,4),Margin=new Thickness(0,0,6,0)};Copy.Click+=(s,e)=>{if(copy!=null)copy();else try{Clipboard.SetText(text);}catch{}};actions.Children.Add(Copy);
        OpenFull=new Button{Content="Open full",Padding=new Thickness(8,4,8,4)};OpenFull.Click+=(s,e)=>{if(open!=null)open();else ShowFull(text,code,title);};actions.Children.Add(OpenFull);
    }
}
public sealed class AttachmentActionEventArgs:RoutedEventArgs {
    public readonly CoreAdapter.FileTransferData Transfer;public readonly string Action;
    public AttachmentActionEventArgs(RoutedEvent routed,CoreAdapter.FileTransferData transfer,string action):base(routed){Transfer=transfer;Action=action;}
}
public sealed class AttachmentCard:StackPanel {
    public static readonly RoutedEvent ActionEvent=EventManager.RegisterRoutedEvent("Action",RoutingStrategy.Bubble,typeof(RoutedEventHandler),typeof(AttachmentCard));
    public static readonly DependencyProperty TransferProperty=DependencyProperty.Register("Transfer",typeof(CoreAdapter.FileTransferData),typeof(AttachmentCard),new PropertyMetadata(null,(sender,args)=>((AttachmentCard)sender).TransferChanged()));
    public static readonly DependencyProperty DetailsExpandedProperty=DependencyProperty.Register("DetailsExpanded",typeof(bool),typeof(AttachmentCard),new PropertyMetadata(false,(sender,args)=>((AttachmentCard)sender).UpdateDetailsVisibility()));
    public CoreAdapter.FileTransferData Transfer{get{return (CoreAdapter.FileTransferData)GetValue(TransferProperty);}set{SetValue(TransferProperty,value);}}
    public bool DetailsExpanded{get{return (bool)GetValue(DetailsExpandedProperty);}set{SetValue(DetailsExpandedProperty,value);}}
    string previewPath="",previewId="";AttachmentPreview preview;bool autoPreviewAttempted;
    StackPanel detailsPanel;
    void UpdateDetailsVisibility(){if(detailsPanel!=null)detailsPanel.Visibility=DetailsExpanded?Visibility.Visible:Visibility.Collapsed;}
    static readonly Queue<WeakReference> residentCards=new Queue<WeakReference>();
    MessageModel messageModel;
    public AttachmentCard(){MaxWidth=360;DataContextChanged+=(s,e)=>{if(messageModel!=null)messageModel.PropertyChanged-=PresentationChanged;messageModel=DataContext as MessageModel;if(messageModel!=null)messageModel.PropertyChanged+=PresentationChanged;Render();};Unloaded+=(s,e)=>{if(messageModel!=null)messageModel.PropertyChanged-=PresentationChanged;};Loaded+=(s,e)=>{if(messageModel!=null){messageModel.PropertyChanged-=PresentationChanged;messageModel.PropertyChanged+=PresentationChanged;}Render();TryAutomaticPreview();};}
    void PresentationChanged(object sender,System.ComponentModel.PropertyChangedEventArgs args){if(args.PropertyName=="IsExpanded"||args.PropertyName=="Attachment"||args.PropertyName=="BubbleWidth"&&messageModel!=null&&messageModel.IsPhotoAttachment)Render();}
    Button ActionButton(string label,Action action){var button=new Button{Content=label,Margin=new Thickness(0,4,6,0),Padding=new Thickness(8,4,8,4)};button.Click+=(s,e)=>action();return button;}
    void Signal(CoreAdapter.FileTransferData row,string action){RaiseEvent(new AttachmentActionEventArgs(ActionEvent,row,action));}
    void TransferChanged(){Render();TryAutomaticPreview();}
    void TryAutomaticPreview(){var row=Transfer;if(IsLoaded&&IsVisible&&row!=null&&row.state==4&&!autoPreviewAttempted){autoPreviewAttempted=true;LoadPreview(row);}}
    void Render(){
        var row=Transfer;Visibility=row==null?Visibility.Collapsed:Visibility.Visible;Children.Clear();if(row==null)return;
        if(previewId!=row.id||previewPath!=row.path){preview=null;previewId=row.id;previewPath=row.path;autoPreviewAttempted=false;}
        var message=DataContext as MessageModel;var pending=message==null?null:message.PendingAttachment;
        if(pending!=null){if(pending.Source.IsPhoto)detailsPanel=PhotoAttachmentPresentation.Render(this,message,pending.Source.Preview,row.name,row.size,pending.Status,()=>PhotoAttachmentPresentation.Open(pending.Source),panel=>PhotoAttachmentPresentation.PendingDetails(panel,pending),panel=>PhotoAttachmentPresentation.PendingActions(panel,pending),DetailsExpanded,()=>DetailsExpanded=!DetailsExpanded);else{Children.Add(new TextBlock{Text=row.name,FontWeight=FontWeights.SemiBold});Children.Add(new TextBlock{Text=ChatAttachmentProjection.CompactSize(row.size)+" · "+pending.Status,Foreground=Brushes.LightGray});var panel=new StackPanel();PhotoAttachmentPresentation.PendingDetails(panel,pending);Children.Add(panel);var buttons=new WrapPanel();PhotoAttachmentPresentation.PendingActions(buttons,pending);Children.Add(buttons);}return;}
        if((message!=null&&message.IsPhotoAttachment)||LocalAttachmentSource.PhotoName(row.name)||!String.IsNullOrEmpty(row.thumbnail)){RenderPhoto(row,message);return;}
        bool longMessage=ChatAttachmentProjection.IsLongMessage(row);
        if(!longMessage)Children.Add(new TextBlock{Text=row.name,FontWeight=FontWeights.SemiBold,TextWrapping=TextWrapping.Wrap});
        else if(preview==null)Children.Add(new TextBlock{Text="Long message",FontWeight=FontWeights.SemiBold});
        Children.Add(new TextBlock{Text=ChatAttachmentProjection.CompactSize(row.size)+" · "+ChatAttachmentProjection.CompactStatus(row),Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap,FontSize=12});
        if(row.state==3&&row.size>0)Children.Add(new ProgressBar{Height=4,Maximum=row.size,Value=Math.Min(Math.Max(0,row.done),row.size),Margin=new Thickness(0,4,0,2)});
        var earlyImage=preview!=null&&preview.Image!=null?null:EarlyAttachmentThumbnail.Decode(row.thumbnail);
        if(earlyImage!=null){var image=new Image{Source=earlyImage,MaxWidth=128,MaxHeight=128,HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,8,0,0),Cursor=Cursors.Hand,ToolTip="Encrypted photo preview — select to download or view the original"};image.MouseLeftButtonUp+=(s,e)=>{e.Handled=true;if(row.state==2&&!row.sending)Signal(row,"accept");else if(row.state==4)LoadPreview(row);};Children.Add(image);}
        if(preview!=null&&preview.Error.Length==0){
            if(preview.Image!=null){var image=new Image{Source=preview.Image,MaxWidth=320,MaxHeight=240,HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,8,0,0),Cursor=Cursors.Hand,ToolTip="Open read-only image viewer"};image.MouseLeftButtonUp+=(s,e)=>{e.Handled=true;ShowPreview(row,preview);};Children.Add(image);}
            else{Children.Add(new BoundedTextView(preview.Text,!longMessage,longMessage?"Long message":"Text preview",null,()=>OpenFull(row)));if(preview.Truncated)Children.Add(new TextBlock{Text="Preview limited to 32 KiB / 200 lines — Open full to read more",Foreground=Brushes.LightGray});}
        }
        var actions=new WrapPanel{Margin=new Thickness(0,2,0,0)};Children.Add(actions);
        if(row.sending&&row.state!=4&&!row.dmCache)actions.Children.Add(ActionButton("Transfer settings…",()=>Signal(row,"settings")));
        if(row.state==2&&!row.sending)actions.Children.Add(ActionButton("Download…",()=>Signal(row,"accept")));
        if(row.state<4||row.state==7||row.state==8)actions.Children.Add(ActionButton("Cancel",()=>Signal(row,"cancel")));
        if((row.protectedSource||!String.IsNullOrEmpty(row.path))&&(preview==null||preview.Error.Length>0))actions.Children.Add(ActionButton(preview==null?"Preview":"Retry preview",()=>{preview=null;LoadPreview(row);}));
        if(row.state==4&&!row.sending){
            if(row.saveState=="complete"&&!String.IsNullOrWhiteSpace(row.savePath)&&File.Exists(row.savePath)){actions.Children.Add(ActionButton("Open File",()=>Signal(row,"open-saved")));actions.Children.Add(ActionButton("Open Folder",()=>Signal(row,"folder-saved")));}
            else if(row.saveState!="copying"&&row.saveState!="pending")actions.Children.Add(ActionButton("Retry Save…",()=>Signal(row,"retry-save")));
        }
        var details=new StackPanel{Visibility=DetailsExpanded?Visibility.Visible:Visibility.Collapsed,Margin=new Thickness(0,5,0,0)};detailsPanel=details;Children.Add(details);
        details.Children.Add(new TextBlock{Text=row.size.ToString("N0")+" bytes · "+ChatAttachmentProjection.Status(row),Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        details.Children.Add(new TextBlock{Text=row.dmCache?"Cached transfer · encrypted attachment":row.isArchive?"Archive transfer · source availability is not proven":row.sending?"Direct transfer · selected device only":"Direct transfer · this device only",Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        details.Children.Add(new TextBlock{Text=ChatAttachmentProjection.Statistics(row),Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap,FontSize=12});
        details.Children.Add(ActionButton("Open Diagnostics",()=>Signal(row,"diagnostics")));
        if(!String.IsNullOrEmpty(row.error))details.Children.Add(new TextBlock{Text=row.dmCache&&row.error=="cache_dm_recipient_upgrade_required"?"Update and connect at least one recipient device before retrying this encrypted attachment.":row.error,Foreground=row.error=="Sender is preparing file"?Brushes.LightGray:Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});
        if(!String.IsNullOrEmpty(row.saveError))details.Children.Add(new TextBlock{Text="Save failed: "+row.saveError,Foreground=Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});
        if(!row.sending&&(row.saveState=="pending"||row.saveState=="copying"))details.Children.Add(new TextBlock{Text=row.saveState=="pending"?"Verifying retained attachment…":"Saving verified attachment…",Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        if(row.state==9)details.Children.Add(new TextBlock{Text="Restore through an authorized source in History / Archive Settings. Source availability is not yet proven.",Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        if(preview!=null&&!String.IsNullOrEmpty(preview.Error))details.Children.Add(new TextBlock{Text=preview.Error,Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
    }
    void RenderPhoto(CoreAdapter.FileTransferData row,MessageModel message){
        var local=message==null?null:message.LocalAttachment;
        var image=local!=null&&local.Preview!=null?local.Preview:preview!=null?preview.Image:EarlyAttachmentThumbnail.Decode(row.thumbnail);
        detailsPanel=PhotoAttachmentPresentation.Render(this,message,image,row.name,row.size,ChatAttachmentProjection.CompactStatus(row),()=>{if(local!=null)PhotoAttachmentPresentation.Open(local);else if(row.state==4&&row.saveState=="complete"&&!String.IsNullOrEmpty(row.savePath))PhotoAttachmentPresentation.Open(new LocalAttachmentSource(row.savePath,row.size,row.name));else if(row.state==4&&!row.protectedSource&&!String.IsNullOrEmpty(row.path))PhotoAttachmentPresentation.Open(new LocalAttachmentSource(row.path,row.size,row.name));else if(preview!=null&&preview.Image!=null)ShowPreview(row,preview);else if(row.state==2&&!row.sending)Signal(row,"accept");else if(row.state==4)LoadPreview(row);},panel=>{
            panel.Children.Add(new TextBlock{Text=row.size.ToString("N0")+" bytes · "+ChatAttachmentProjection.Status(row),Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
            if(row.state==3&&row.size>0)panel.Children.Add(new ProgressBar{Height=4,Maximum=row.size,Value=Math.Min(row.done,row.size),Margin=new Thickness(0,6,0,6)});
            panel.Children.Add(new TextBlock{Text=ChatAttachmentProjection.Statistics(row),Foreground=Brushes.LightGray,FontSize=12,TextWrapping=TextWrapping.Wrap});
            panel.Children.Add(new TextBlock{Text=row.dmCache?"Cached transfer · encrypted attachment":row.sending?"Direct transfer · selected device only":"Direct transfer · this device only",Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
            if(!String.IsNullOrEmpty(row.error))panel.Children.Add(new TextBlock{Text=row.error,Foreground=Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});
            if(!String.IsNullOrEmpty(row.saveError))panel.Children.Add(new TextBlock{Text="Save failed: "+row.saveError,Foreground=Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});
            panel.Children.Add(ActionButton("Open Diagnostics",()=>Signal(row,"diagnostics")));
        },buttons=>{
            if(row.state==2&&!row.sending)buttons.Children.Add(ActionButton("Download…",()=>Signal(row,"accept")));
            if(row.state<4||row.state==7||row.state==8)buttons.Children.Add(ActionButton("Cancel",()=>Signal(row,"cancel")));
            if(!row.sending&&row.state==4&&row.saveState!="complete"&&row.saveState!="pending"&&row.saveState!="copying")buttons.Children.Add(ActionButton("Retry Save…",()=>Signal(row,"retry-save")));
            if(!row.sending&&row.state==4&&row.saveState=="complete"&&!String.IsNullOrWhiteSpace(row.savePath)&&File.Exists(row.savePath)){buttons.Children.Add(ActionButton("Open File",()=>Signal(row,"open-saved")));buttons.Children.Add(ActionButton("Open Folder",()=>Signal(row,"folder-saved")));}
        },DetailsExpanded,()=>DetailsExpanded=!DetailsExpanded);
    }
    bool loading;
    public void ProtectedPreview(string id,string text,string error){loading=false;if(Transfer==null||Transfer.id!=id)return;while(residentCards.Count>=24){var old=residentCards.Dequeue().Target as AttachmentCard;if(old!=null&&old!=this){old.preview=null;old.autoPreviewAttempted=true;old.Render();}}residentCards.Enqueue(new WeakReference(this));preview=String.IsNullOrEmpty(error)?(ChatAttachmentProjection.IsLongMessage(Transfer)?new AttachmentPreview{Text=text}:SafeAttachmentPreview.FromText(text,Transfer.size)):new AttachmentPreview{Error=error};previewPath=Transfer.path;Render();}
    public static void DisplayText(CoreAdapter.FileTransferData row,string text){ShowPreview(row,new AttachmentPreview{Text=text});}
    async void LoadPreview(CoreAdapter.FileTransferData row){if(loading)return;loading=true;if(row.protectedSource){Signal(row,"preview-snippet");return;}try{var value=ChatAttachmentProjection.IsLongMessage(row)?new AttachmentPreview{Text=await SafeAttachmentPreview.FullTextAsync(row.path,row.size)}:await SafeAttachmentPreview.ReadAsync(row.path,row.size);if(Transfer!=null&&Transfer.id==row.id&&Transfer.path==row.path){
        while(residentCards.Count>=24){var old=residentCards.Dequeue().Target as AttachmentCard;if(old!=null&&old!=this){old.preview=null;old.autoPreviewAttempted=true;old.Render();}}
        residentCards.Enqueue(new WeakReference(this));preview=value;previewPath=row.path;Render();
    }}catch{if(Transfer!=null&&Transfer.id==row.id){preview=new AttachmentPreview{Error="Text unavailable or changed"};Render();}}finally{loading=false;}}
    async void OpenFull(CoreAdapter.FileTransferData row){if(row.protectedSource){Signal(row,"full-snippet");return;}try{string text=await SafeAttachmentPreview.FullTextAsync(row.path,row.size);ShowPreview(row,new AttachmentPreview{Text=text});}catch(Exception error){MessageBox.Show(error.Message,"Read-only text viewer");}}
    static void ShowPreview(CoreAdapter.FileTransferData row,AttachmentPreview value){
        // No shell-open for scripts/HTML/executables. Images and text stay read-only.
        if(value.Image==null){BoundedTextView.ShowFull(value.Text,!ChatAttachmentProjection.IsLongMessage(row)||BoundedTextView.CodeLike(value.Text),ChatAttachmentProjection.IsLongMessage(row)?"Long message":row.name);return;}
        object content=new Image{Source=value.Image,Stretch=Stretch.Uniform};
        new Window{Title=row.name+" — read-only bounded preview",Owner=Application.Current==null?null:Application.Current.MainWindow,Width=760,Height=520,Background=new SolidColorBrush(Color.FromRgb(11,21,30)),Foreground=Brushes.White,Content=content}.Show();
    }
}
public partial class DesktopApplication {
    readonly HashSet<PendingOutgoingAttachment> pendingOutgoingAttachments=new HashSet<PendingOutgoingAttachment>();
    bool PendingNativeAttachmentAdmission {get{return pendingOutgoingAttachments.Any(value=>value.NativeAdmissionInFlight);}}
    readonly SemaphoreSlim pendingAttachmentAdmissions=new SemaphoreSlim(1,1);
    Func<bool> DirectAttachmentScope(ConversationModel target){var owner=discovery;ulong profile=profileSessionGeneration;long localAccount=owner==null?0:owner.AccountId,account=target.AccountId;string peer=target.Contact.peer;return ()=>!closing&&owner==discovery&&profile==profileSessionGeneration&&(discovery==null?0:discovery.AccountId)==localAccount&&target.Contact!=null&&target.Contact.peer==peer&&target.AccountId==account&&(account>0?target.IsFriend:target.Contact.verified);}
    Func<bool> GroupAttachmentScope(GroupModel group){var owner=discovery;ulong profile=profileSessionGeneration;long account=owner==null?0:owner.AccountId;return ()=>!closing&&owner==discovery&&profile==profileSessionGeneration&&(discovery==null?0:discovery.AccountId)==account&&!group.Invited&&model.GroupChats.ContainsKey(group.Id)&&model.GroupChats[group.Id]==group;}
    PendingOutgoingAttachment AddPendingAttachment(ConversationModel conversation,GroupModel group,LocalAttachmentSource source,Func<bool> current,Func<PendingOutgoingAttachment,Task<string>> submit,Func<string,Task> cancel){
        var message=new MessageModel{Data=new MessageData{id="local-attachment:"+Guid.NewGuid().ToString("N"),text="",sender="",time=DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()},Outgoing=true,IsGroupMessage=group!=null,LocalAttachment=source};
        PendingOutgoingAttachment pending=null;pending=new PendingOutgoingAttachment(source,current,async()=>{await pendingAttachmentAdmissions.WaitAsync();try{if(!current()||pending.CancelRequested)return "";return await submit(pending);}finally{pendingAttachmentAdmissions.Release();}},cancel,id=>{
            if(!current())return;
            var rows=group==null?conversation.Messages:group.Messages;
            // A polling snapshot can finish before the command continuation. Merge it
            // into the original bubble rather than keeping a second logical message.
            var existing=rows.FirstOrDefault(value=>value!=message&&(group==null?value.Attachment!=null&&value.Attachment.id==id:value.GroupAttachment!=null&&value.GroupAttachment.id==id));
            if(existing!=null){message.PendingAttachment=null;message.Data=existing.Data;if(group==null)message.SetAttachment(existing.Attachment);else message.SetGroupAttachment(existing.GroupAttachment,existing.GroupAttachmentRevision);rows.Remove(existing);}
            else if(group==null)message.SetAttachment(new CoreAdapter.FileTransferData{id=id,logicalId=message.Data.id,name=source.Name,size=source.Size,sending=true,state=0,time=message.Data.time});
            else message.SetGroupAttachment(new CoreAdapter.GroupAttachmentData{id=id,logicalId=message.Data.id,group=group.Id,name=source.Name,size=source.Size,sending=true,state="preparing",time=message.Data.time},null);
            DeleteAcceptedPendingAttachment(conversation,group,pending,message,id);
        });message.PendingAttachment=pending;
        if(group==null){message.SetAttachment(new CoreAdapter.FileTransferData{id=pending.LocalId,logicalId=message.Data.id,name=source.Name,size=source.Size,sending=true,state=0,time=message.Data.time});conversation.Messages.Add(message);conversation.Refresh();}
        else{message.SetGroupAttachment(new CoreAdapter.GroupAttachmentData{id=pending.LocalId,logicalId=message.Data.id,group=group.Id,name=source.Name,size=source.Size,sending=true,state="preparing",time=message.Data.time},null);group.Messages.Add(message);group.Refresh();}
        pendingOutgoingAttachments.Add(pending);
        pending.Changed+=()=>{if(pending.RunCompleted&&!pending.CanRetry)pendingOutgoingAttachments.Remove(pending);if(current()){message.RefreshAttachmentPresentation();if(group==null)conversation.Refresh();else group.Refresh();}};
        Refresh();SizeBubbles();_ = LoadPendingAttachmentPreview(message,current);_ = RunPendingAttachment(pending,current,group!=null);return pending;
    }
    async Task LoadPendingAttachmentPreview(MessageModel message,Func<bool> current){try{await message.LocalAttachment.LoadPreviewAsync();if(current()){message.RefreshAttachmentPresentation();SizeBubbles();}}catch{/* A photo placeholder remains usable; admission reports source failures separately. */}}
    async Task RunPendingAttachment(PendingOutgoingAttachment pending,Func<bool> current,bool group){await Task.Yield();try{await pending.RunAsync();}finally{if(!pending.CanRetry)pendingOutgoingAttachments.Remove(pending);}if(current()){if(group)await RefreshGroupAttachments();else await RefreshChatAttachments();}}
    void BeginDirectAttachment(ConversationModel target,LocalAttachmentSource source,bool directOnly=false,Func<Task<DmAttachmentDeliveryMode>> deliveryChoice=null){
        var current=DirectAttachmentScope(target);string peer=target.Contact.peer;long account=target.AccountId;bool cached=false;
        AddPendingAttachment(target,null,source,current,async pending=>{
            if(String.IsNullOrEmpty(source.Path)){if(localProfile==null||profileManager==null)throw new IOException("Image source storage is unavailable.");await source.PersistAsync(Path.Combine(profileManager.ProfilePath(localProfile.Id),"ClipboardAttachments"));}
            if(!current()||pending.CancelRequested)return "";
            var mode=account>0?await SelectDmAttachmentDeliveryCore(target,directOnly,current,deliveryChoice):DmAttachmentDeliveryMode.OnlineDevice;
            if(mode==DmAttachmentDeliveryMode.Cancel||!current()||pending.CancelRequested)return "";
            if(!current()||pending.CancelRequested)return "";if(!source.Unchanged())throw new IOException("The selected file changed. Select it again.");cached=account>0&&mode==DmAttachmentDeliveryMode.EncryptedCache;
            pending.BeginNativeAdmission();return await Task.Run(()=>core.FileTransfer(account>0?21u:1u,peer,account>0?DmAttachmentDelivery.Target(mode,account):"",source.Path));
        },async id=>{if(current()){await Task.Run(()=>core.FileTransfer(cached?23u:3u,"",id,""));if(current())await RefreshChatAttachments();}});
    }
    void BeginGroupAttachment(GroupModel group,LocalAttachmentSource source){
        var current=GroupAttachmentScope(group);AddPendingAttachment(null,group,source,current,async pending=>{
            if(String.IsNullOrEmpty(source.Path)){if(localProfile==null||profileManager==null)throw new IOException("Image source storage is unavailable.");await source.PersistAsync(Path.Combine(profileManager.ProfilePath(localProfile.Id),"ClipboardAttachments"));}
            if(!current()||pending.CancelRequested)return "";
            if(!await EnsureGroupAttachmentReady())throw new IOException(GroupAttachmentUnavailable());if(!current()||pending.CancelRequested)return "";
            if(!current()||pending.CancelRequested)return "";if(!source.Unchanged())throw new IOException("The selected file changed. Select it again.");
            pending.BeginNativeAdmission();return await Task.Run(()=>core.GroupAttachment(1,group.Id,"",source.Path));
        },async id=>{if(current()){await Task.Run(()=>core.GroupAttachment(3,group.Id,id));if(current())await RefreshGroupAttachments();}});
    }
    string dmHistoryCursor="",dmHistoryScope="";bool dmHistoryMore=true,dmHistoryLoading;ulong dmHistoryProfile,dmHistoryEpoch;
    bool DmHistoryMayHaveOlder {get{return dmHistoryProfile!=profileSessionGeneration||dmHistoryMore;}}
    async Task LoadOlderDmAttachments(bool initial=false){
        if(closing||model.Groups||model.Selected==null||dmHistoryLoading)return;
        if(dmHistoryProfile!=profileSessionGeneration){dmHistoryProfile=profileSessionGeneration;dmHistoryCursor="";dmHistoryScope="";dmHistoryMore=true;++dmHistoryEpoch;}
        if(!dmHistoryMore||initial&&!String.IsNullOrEmpty(dmHistoryCursor))return;
        dmHistoryLoading=true;ulong profile=profileSessionGeneration,epoch=dmHistoryEpoch;string before=dmHistoryCursor;
        try{var page=await Task.Run(()=>core.DmAttachmentHistory(before));if(closing||profile!=profileSessionGeneration||epoch!=dmHistoryEpoch)return;
            if(!String.IsNullOrEmpty(dmHistoryScope)&&dmHistoryScope!=page.scope){dmHistoryCursor="";dmHistoryMore=true;dmHistoryScope=page.scope;return;}
            dmHistoryScope=page.scope??"";dmHistoryCursor=page.cursor??"";dmHistoryMore=page.more;
            foreach(var row in page.items??new CoreAdapter.FileTransferData[0]){var conversation=model.FindByPeer(row.peer)??model.Conversations.Values.FirstOrDefault(value=>row.account>0&&value.AccountId==row.account);if(conversation!=null)ChatAttachmentProjection.Apply(conversation,row);}
            Refresh();SizeBubbles();
        }catch{/* Protected local history can be retried; no network or trust is established. */}
        finally{dmHistoryLoading=false;}
    }
    bool chatAttachmentRefreshing;long chatAttachmentCursor;DateTime chatCatalogAt=DateTime.MinValue;
    string attachmentTelemetry="";long attachmentTelemetryAccount;ulong attachmentTelemetryGeneration;
    string AttachmentDiagnostics(){return discovery!=null&&attachmentTelemetryAccount==discovery.AccountId&&attachmentTelemetryGeneration==authenticatedFriendSnapshotGeneration?attachmentTelemetry:"No current profile/account transfer snapshot.";}
    readonly SemaphoreSlim attachmentCopies=new SemaphoreSlim(2,2);
    readonly Dictionary<string,CoreAdapter.FileTransferData> chatCatalog=new Dictionary<string,CoreAdapter.FileTransferData>();
    string dragConversation;bool dragTargetCaptured;ulong dragProfileGeneration;
    string ChatTarget(){return model.Groups?(model.SelectedGroup==null?null:"group:"+model.SelectedGroup.Id):model.Selected==null?null:"direct:"+model.Selected.ConversationId;}
    bool ValidChatDrop(string captured,string[] files){return dragProfileGeneration==profileSessionGeneration&&(model.Groups?GroupAttachmentProjection.ValidDrop(captured,ChatTarget(),GroupAttachmentsAvailable()&&model.SelectedGroup!=null&&!model.SelectedGroup.Invited,files):ChatAttachmentProjection.ValidDrop(captured,ChatTarget(),false,files));}
    void BindChatAttachments(){
        var refresh=new DispatcherTimer{Interval=TimeSpan.FromSeconds(1)};refresh.Tick+=async(s,e)=>{await RefreshChatAttachments();await RefreshGroupAttachments();};refresh.Start();window.Closed+=(s,e)=>{refresh.Stop();chatCatalog.Clear();};
        Find<Button>("Attachment").ToolTip="Attach file to this conversation";
        window.AddHandler(AttachmentCard.ActionEvent,new RoutedEventHandler(async(s,e)=>{
            var action=(AttachmentActionEventArgs)e;var row=action.Transfer;e.Handled=true;if(action.Action=="diagnostics"){OpenTransferDiagnostics(row);return;}
            if(action.Action=="preview-snippet"||action.Action=="full-snippet"){
                var card=action.OriginalSource as AttachmentCard;bool full=action.Action=="full-snippet";
                if(!SafeAttachmentPreview.TryNativeRead()){if(card!=null&&!full)card.ProtectedPreview(row.id,"","Preview busy — try again");return;}
                try{var text=await Task.Run(()=>core.FileTransfer(full||ChatAttachmentProjection.IsLongMessage(row)?9u:8u,"",row.id,""));if(closing)return;if(full)AttachmentCard.DisplayText(row,text);else if(card!=null)card.ProtectedPreview(row.id,text,"");}
                catch{if(!closing&&card!=null&&!full)card.ProtectedPreview(row.id,"","Protected source unavailable or changed");else if(!closing)Notify("Protected source unavailable or changed; no insecure fallback was used.");}
                finally{SafeAttachmentPreview.EndNativeRead();}return;
            }
            if(action.Action=="settings"){ShowTransferSettings(row.id);return;}
            if(action.Action=="cancel"){await FileOperation(row.dmCache?23u:3u,"",row.id,"");return;}
            if(action.Action=="accept"||action.Action=="retry-save"){
                var selectionOwner=discovery;ulong selectionProfile=profileSessionGeneration;
                var chosen=AttachmentDownloadTarget.Choose(window,row.name,path=>core.FileTransfer(16,"","",path));if(chosen==null)return;
                if(closing||selectionOwner!=discovery||selectionProfile!=profileSessionGeneration||model.Groups)return;
                try{
                    uint operation=row.dmCache?22u:action.Action=="retry-save"?18u:chosen.OverwriteApproved?17u:AttachmentDownloadTarget.DirectOperation;
                    string target=operation==AttachmentDownloadTarget.DirectOperation?AttachmentDownloadTarget.Validate(chosen.Path):AttachmentDownloadTarget.Wire(chosen);
                    await FileOperation(operation,"",row.id,target);
                }catch(IOException error){Notify(error.Message);}
                return;
            }
            if(action.Action=="open-saved"||action.Action=="folder-saved"){
                if(row.saveState!="complete"||String.IsNullOrWhiteSpace(row.savePath)||!File.Exists(row.savePath)){Notify("Saved attachment is unavailable. Use Retry Save to restore it.");return;}
                try{if(action.Action=="folder-saved")System.Diagnostics.Process.Start("explorer.exe","/select,\""+row.savePath+"\"");else System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(row.savePath){UseShellExecute=true});}
                catch{Notify("Saved attachment is unavailable. Use Retry Save to restore it.");}
                return;
            }
        }));
        var outline=Find<ScrollViewer>("MessageScroll");var target=(Grid)outline.Parent;target.AllowDrop=true;
        target.PreviewDragEnter+=(s,e)=>{if(!dragTargetCaptured){dragConversation=ChatTarget();dragProfileGeneration=profileSessionGeneration;dragTargetCaptured=true;}SetDrop(e);};
        target.PreviewDragOver+=(s,e)=>SetDrop(e);
        target.PreviewDragLeave+=(s,e)=>{var point=e.GetPosition(target);if(point.X<0||point.Y<0||point.X>target.ActualWidth||point.Y>target.ActualHeight){dragConversation=null;dragTargetCaptured=false;outline.ToolTip=null;outline.BorderThickness=new Thickness(0);}};
        target.PreviewDrop+=async(s,e)=>{e.Handled=true;outline.ToolTip=null;outline.BorderThickness=new Thickness(0);var files=e.Data.GetData(DataFormats.FileDrop) as string[];string captured=dragConversation;dragConversation=null;dragTargetCaptured=false;if(!ValidChatDrop(captured,files)){Notify("Drop canceled: choose a supported conversation and up to four regular files. The conversation must not change during the drag.");return;}if(model.Groups)await SendGroupFiles(model.SelectedGroup,files);else await SendChatFiles(model.Selected,files);};
    }
    void SetDrop(DragEventArgs e){var files=e.Data.GetDataPresent(DataFormats.FileDrop)?e.Data.GetData(DataFormats.FileDrop) as string[]:null;bool valid=ValidChatDrop(dragConversation,files);e.Effects=valid?DragDropEffects.Copy:DragDropEffects.None;e.Handled=true;var target=Find<ScrollViewer>("MessageScroll");target.BorderBrush=new SolidColorBrush(Color.FromRgb(131,204,252));target.BorderThickness=new Thickness(valid?2:0);target.ToolTip=valid?"Drop files into "+(model.Groups?model.SelectedGroup.Name:model.Selected.Name):model.Groups?GroupAttachmentUnavailable():"Choose a verified direct conversation and up to four files";}
    async void ChooseChatFiles(){
        if(model.Groups){await ChooseGroupFiles();return;}
        var selected=model.Groups?null:model.Selected;
        if(selected==null||selected.Contact==null||(selected.AccountId>0?!selected.IsFriend:!selected.Contact.verified)){Notify("Select a current friend conversation, or verify the direct recipient first.");return;}
        var choose=new Microsoft.Win32.OpenFileDialog{Title="Attach files to "+selected.Name,CheckFileExists=true,Multiselect=true};
        if(choose.ShowDialog(window)==true)await SendChatFiles(selected,choose.FileNames);
    }
    async Task SendChatFiles(ConversationModel target,string[] files){
        if(target==null||target.Contact==null||files==null||files.Length==0||files.Length>4||(target.AccountId>0?!target.IsFriend:!target.Contact.verified)){Notify("Choose a current friend or verified direct recipient and at most four files.");return;}
        bool directOnly=false;
        try{foreach(var path in files){SelectedTransferFile.ValidateSize(path);if(new FileInfo(path).Length>SelectedTransferFile.MaximumCachedBytes)directOnly=true;}}
        catch(Exception error){Notify(error.Message);return;}
        DmAttachmentDeliveryMode? batchChoice=null;Func<Task<DmAttachmentDeliveryMode>> choose=()=>{if(!batchChoice.HasValue)batchChoice=ChooseDmAttachmentMode(target,directOnly);return Task.FromResult(batchChoice.Value);};
        foreach(string path in files)BeginDirectAttachment(target,new LocalAttachmentSource(path,new FileInfo(path).Length),directOnly,choose);
        await Task.CompletedTask;
    }

    DmAttachmentDeliveryMode ChooseDmAttachmentMode(ConversationModel target,bool directOnly){
        var dialog=new Window{Title="Attachment delivery",Owner=window,Width=530,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner,ShowInTaskbar=false,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),Resources=window.Resources};
        var panel=new StackPanel{Margin=new Thickness(24)};dialog.Content=panel;
        panel.Children.Add(new TextBlock{Text="Send attachment to "+target.Name,FontSize=22,TextWrapping=TextWrapping.Wrap});
        var choice=new ComboBox{ItemsSource=directOnly?new[]{"Direct Transfer"}:new[]{"Direct Transfer","Cached Transfer"},SelectedIndex=!directOnly&&new AttachmentSendPreferenceStore(directory).Load()==DmAttachmentDeliveryMode.EncryptedCache?1:0,Margin=new Thickness(0,16,0,12)};panel.Children.Add(choice);
        var description=new TextBlock{TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,16)};panel.Children.Add(description);
        if(directOnly)panel.Children.Add(new TextBlock{Text="Direct-only: this selection includes a file over the 1.5 GiB cache limit. Direct transfers support up to 5 GiB.",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,12)});
        Action explain=()=>description.Text=choice.SelectedIndex==0?DmAttachmentDelivery.OnlineDescription+"\nSelected verified device: "+(target.Contact.verified?target.Contact.peer:"Not verified — direct sending is unavailable")+"\nThis encrypted peer connection can use the relay; it is not LAN-only.":DmAttachmentDelivery.CacheDescription+"\nRecipients can retrieve the encrypted attachment after you disconnect.";
        choice.SelectionChanged+=(s,e)=>explain();explain();
        panel.Children.Add(new TextBlock{Text="This choice applies only to this send. Change the profile default in File Transfer Settings.",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,16)});
        var buttons=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right};panel.Children.Add(buttons);
        var cancel=new Button{Content="Cancel",IsCancel=true,Margin=new Thickness(0,0,12,0)};cancel.Click+=(s,e)=>dialog.DialogResult=false;buttons.Children.Add(cancel);
        var proceed=new Button{Content="Continue",IsDefault=true};proceed.Click+=(s,e)=>dialog.DialogResult=true;buttons.Children.Add(proceed);
        if(dialog.ShowDialog()!=true)return DmAttachmentDeliveryMode.Cancel;
        return choice.SelectedIndex==1?DmAttachmentDeliveryMode.EncryptedCache:DmAttachmentDeliveryMode.OnlineDevice;
    }
    Task<DmAttachmentDeliveryMode> SelectDmAttachmentDelivery(ConversationModel target){return SelectDmAttachmentDeliveryCore(target,false);}
    async Task<DmAttachmentDeliveryMode> SelectDmAttachmentDeliveryCore(ConversationModel target,bool directOnly,Func<bool> capturedScope=null,Func<Task<DmAttachmentDeliveryMode>> deliveryChoice=null){
        if(target==null||target.Contact==null||!target.IsFriend||target.AccountId<=0)return DmAttachmentDeliveryMode.Cancel;
        var owner=discovery;ulong profile=profileSessionGeneration;string peer=target.Contact.peer;long account=target.AccountId;
        var selectedMode=deliveryChoice==null?ChooseDmAttachmentMode(target,directOnly):await deliveryChoice();
        if(selectedMode==DmAttachmentDeliveryMode.Cancel)return selectedMode;
        Func<bool> sameScope=()=>!closing&&(capturedScope!=null?capturedScope():!model.Groups&&model.Selected==target)&&owner==discovery&&profile==profileSessionGeneration&&target.IsFriend&&target.AccountId==account&&target.Contact!=null&&target.Contact.peer==peer;
        if(!sameScope())return DmAttachmentDeliveryMode.Cancel;
        if(selectedMode==DmAttachmentDeliveryMode.OnlineDevice){
            if(DmAttachmentDelivery.OnlineSelectable(target)&&await DemandReadyRoute(target,"selected direct attachment")&&sameScope()&&DmAttachmentDelivery.OnlineReady(target))return DmAttachmentDeliveryMode.OnlineDevice;
            if(!sameScope())return DmAttachmentDeliveryMode.Cancel;
            if(directOnly){Notify("This selection is direct-only. Connect and verify the selected recipient device, then try again.");return DmAttachmentDeliveryMode.Cancel;}
            if(MessageBox.Show(window,"Direct delivery requires the selected verified device to be connected. No attachment was queued.\n\nCheck encrypted Cached delivery for the authorized account audience instead? All device and cache authorization checks still apply.","Direct recipient unavailable",MessageBoxButton.YesNo,MessageBoxImage.Information)!=MessageBoxResult.Yes)return DmAttachmentDeliveryMode.Cancel;
            if(!sameScope())return DmAttachmentDeliveryMode.Cancel;
        }
        string state="";bool preflightRequestStarted=false;
        try{
            if(await EnsureDmAttachmentReady(false)){
                var client=dmAttachmentNetworkClient;string token=dmAttachmentToken,pin=dmAttachmentPin,device=dmAttachmentPeer;long localAccount=dmAttachmentAccount;string request=Guid.NewGuid().ToString("N");
                preflightRequestStarted=true;
                var reply=await Task.Run(()=>client.AttachmentCache(device,new Dictionary<string,object>{{"type","cache_dm_prepare"},{"requestId",request},{"recipientAccountId",account}},localAccount,token,pin));
                object value;state=reply.TryGetValue("state",out value)?value as string:"";
                if(state=="accepted"&&(!reply.TryGetValue("requestId",out value)||!String.Equals(value as string,request,StringComparison.Ordinal)||!reply.TryGetValue("operation",out value)||!String.Equals(value as string,"cache_dm_prepare",StringComparison.Ordinal)))throw new InvalidDataException("Attachment preflight correlation failed.");
            }else state=dmAttachmentExecution==null?"":dmAttachmentExecution.LastState;
        }catch(Exception error){if(!preflightRequestStarted||!DmAttachmentDelivery.PreflightTransportUnavailable(error)){if(!closing)Notify("Offline attachment readiness could not be verified. Nothing was queued; retry after the authenticated connection recovers.");return DmAttachmentDeliveryMode.Cancel;}state="preflight_transport_unavailable";}
        if(!sameScope())return DmAttachmentDeliveryMode.Cancel;
        if(state=="accepted"){
            // New friendships have no device pin until the existing connection
            // owner completes its authenticated initial-device handshake. Demand
            // that handshake here just as an explicit message send does; never
            // manufacture a message or bypass the native initial-pin guard.
            if(!target.Contact.verified){
                DiscoverySearchResult friend;
                if(friendDirectory.TryGetValue(account,out friend)&&friend.Online){
                    ConnectFriendCandidate(friend,target,true,"cached attachment: initial device verification");
                    var until=DateTime.UtcNow.AddSeconds(25);
                    while(sameScope()&&!target.Contact.verified&&DateTime.UtcNow<until)await Task.Delay(100);
                    if(!sameScope())return DmAttachmentDeliveryMode.Cancel;
                }
            }
            if(!await RefreshIdentityForCachedSend(account,sameScope)||!sameScope())return DmAttachmentDeliveryMode.Cancel;
            identityUrgentAccount=account;identityNextUtc=DateTime.MinValue;PumpDeviceIdentity();return DmAttachmentDeliveryMode.EncryptedCache;
        }
        if(state!="preflight_transport_unavailable"&&!DmAttachmentDelivery.CompatibilityBlocked(state)){Notify("Attachment preflight did not authorize delivery ("+(state??"unavailable")+"). No cache checks were bypassed and no direct transfer was started.");return DmAttachmentDeliveryMode.Cancel;}
        // A direct transfer is a distinct user-selected scope, never conversion
        // of a failed upload. Try only the existing route owner before offering it.
        if(DmAttachmentDelivery.OnlineSelectable(target)&&!DmAttachmentDelivery.OnlineReady(target))await DemandReadyRoute(target,"attachment compatibility choice");
        if(!sameScope())return DmAttachmentDeliveryMode.Cancel;
        var dialog=new Window{Title="Attachment delivery",Owner=window,Width=530,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner,ShowInTaskbar=false,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),FontFamily=new FontFamily("Segoe UI"),FontSize=15,Resources=window.Resources};
        var panel=new StackPanel{Margin=new Thickness(26)};dialog.Content=panel;
        panel.Children.Add(new TextBlock{Text=state=="preflight_transport_unavailable"?"Offline attachment unavailable":"Offline attachment compatibility",FontSize=23,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,0,0,16)});
        panel.Children.Add(new TextBlock{Text=target.Name+" · account "+account,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,12)});
        panel.Children.Add(new TextBlock{Text=DmAttachmentDelivery.CompatibilityMessage(state),TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,0,0,16)});
        var mode=DmAttachmentDeliveryMode.Cancel;
        Action<string,string,DmAttachmentDeliveryMode,bool> option=(label,description,value,enabled)=>{var button=new Button{Content=label,IsEnabled=enabled,HorizontalContentAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,0,0,8)};button.Click+=(s,e)=>{mode=value;dialog.DialogResult=true;};panel.Children.Add(button);panel.Children.Add(new TextBlock{Text=description,TextWrapping=TextWrapping.Wrap,Foreground=Brush("#A1AEBB"),Margin=new Thickness(0,0,0,20)});};
        option("Send Directly to Online Device",DmAttachmentDelivery.OnlineDescription+"\nSelected device: "+peer+(DmAttachmentDelivery.OnlineReady(target)?"":"\nNo verified online route. The selected recipient must connect before direct delivery is available."),DmAttachmentDeliveryMode.OnlineDevice,DmAttachmentDelivery.OnlineReady(target));
        var cancel=new Button{Content="Cancel",IsCancel=true,HorizontalAlignment=HorizontalAlignment.Right};cancel.Click+=(s,e)=>dialog.DialogResult=false;panel.Children.Add(cancel);
        dialog.Loaded+=(s,e)=>cancel.Focus();if(dialog.ShowDialog()!=true)return DmAttachmentDeliveryMode.Cancel;
        if(mode==DmAttachmentDeliveryMode.OnlineDevice){if(!DmAttachmentDelivery.OnlineSelectable(target)||!await DemandReadyRoute(target,"explicit online attachment")){Notify("The selected verified device is not connected. No attachment was queued and no cache fallback was used.");return DmAttachmentDeliveryMode.Cancel;}}
        if(!sameScope())return DmAttachmentDeliveryMode.Cancel;
        return mode;
    }
    async Task SendChatSnippet(string text){
        if(text.IndexOf('\0')>=0){Notify("Text contains a NUL character and cannot use the file API. Your draft was kept without truncation.");return;}
        if(model.Groups){Notify("This text exceeds 4,096 characters. Group snippet attachments are not supported yet; your draft was kept.");return;}
        var target=model.Selected;if(target==null||target.Contact==null||!target.Contact.verified){Notify("Verify this recipient before sending a text attachment. Your draft was kept.");return;}
        if(text.Length>ChatAttachmentProjection.SnippetCharacters){Notify("Text attachments are limited to 1,048,576 characters. Your draft was kept, without truncation.");return;}
        int encodedBytes;try{encodedBytes=new UTF8Encoding(false,true).GetByteCount(text);}catch{Notify("Text has malformed Unicode. Your draft was kept without truncation.");return;}
        if(encodedBytes>ChatAttachmentProjection.SnippetUtf8Bytes){Notify("Generated text attachments are limited to 512 KiB of exact UTF-8. Your draft was kept without truncation; existing files may be up to 256 MiB.");return;}
        if(!Confirm("Send text attachment?","This message exceeds 4,096 characters. Send the exact text as message.txt?","Send attachment"))return;
        try{string peer=target.Contact.peer;await Task.Run(()=>core.FileTransfer(7,peer,"",text));if(target.Draft==text)target.Draft="";if(model.Selected==target&&Find<TextBox>("Composer").Text==text)Find<TextBox>("Composer").Clear();await RefreshChatAttachments();}
        catch(Exception error){Notify("Text attachment could not be durably queued. Your draft was kept. "+error.Message);}
    }
    async Task RefreshChatAttachments(){
        if(closing||chatAttachmentRefreshing||fileTransferOperating||PendingNativeAttachmentAdmission||!model.Listening)return;chatAttachmentRefreshing=true;
        try{
            var owner=discovery;ulong profileGeneration=profileSessionGeneration;long account=owner==null?0:owner.AccountId;ulong generation=authenticatedFriendSnapshotGeneration;
            var deletionRevisions=model.Conversations.Values.Distinct().ToDictionary(value=>value,value=>value.LocalDeletions.Revision);
            bool groupMedia=(groupVoice!=null&&groupVoice.Snapshot().Joined)||(groupScreen!=null&&groupScreen.AnyActive);
            var sample=await Task.Run(()=>{core.FileTransfer(14,path:groupMedia?"1":"0");return new{Rows=core.FileTransfers(),Diagnostics=core.FileTransfer(11)};});if(closing||profileGeneration!=profileSessionGeneration||owner!=discovery||account!=(discovery==null?0:discovery.AccountId)||generation!=authenticatedFriendSnapshotGeneration)return;if(PendingNativeAttachmentAdmission)return;var rows=sample.Rows;CacheTransferDiagnostics(rows,sample.Diagnostics);attachmentTelemetry=sample.Diagnostics;attachmentTelemetryAccount=account;attachmentTelemetryGeneration=generation;
            if(DateTime.UtcNow-chatCatalogAt>TimeSpan.FromSeconds(5)){
                var page=await Task.Run(()=>core.ArchiveAttachments(chatAttachmentCursor));if(closing)return;
                foreach(var item in page.records??new CoreAdapter.ArchiveAttachment[0]){
                    var conversation=model.FindByPeer(item.peer)??model.Conversations.Values.FirstOrDefault(value=>value.AccountId>0&&value.AccountId==item.account);
                    long deletionRevision;if(conversation==null||!deletionRevisions.TryGetValue(conversation,out deletionRevision)||deletionRevision!=conversation.LocalDeletions.Revision||conversation.LocalDeletions.Hidden(item.id))continue;
                    chatCatalog[item.id]=new CoreAdapter.FileTransferData{id=item.id,peer=conversation.Contact.peer,name=item.name,size=item.size,done=item.size,state=item.protectedSource||item.saveState=="complete"&&!String.IsNullOrEmpty(item.savePath)?4:String.IsNullOrEmpty(item.path)?(item.archiveAttested?9:10):4,path=item.path,saveState=item.saveState,savePath=item.savePath,saveError=item.saveError,sending=item.sending,senderPeer=item.senderPeer,protectedSource=item.protectedSource,time=item.time};
                }
                chatAttachmentCursor=page.complete?0:page.next;chatCatalogAt=DateTime.UtcNow;
            }
            bool changed=false;var current=new HashSet<string>(rows.Select(ChatAttachmentProjection.LogicalId));
            foreach(var row in rows.Concat(chatCatalog.Values.Where(row=>!current.Contains(row.id)))){
                var conversation=model.FindByPeer(String.IsNullOrEmpty(row.conversationPeer)?row.peer:row.conversationPeer)??model.Conversations.Values.FirstOrDefault(value=>row.account>0&&value.AccountId==row.account);if(conversation==null)continue;
                long deletionRevision;if(!deletionRevisions.TryGetValue(conversation,out deletionRevision)||deletionRevision!=conversation.LocalDeletions.Revision)continue;
                changed|=ChatAttachmentProjection.Apply(conversation,row);
            }
            if(changed){Refresh();SizeBubbles();}
        }catch{/* A missing snapshot never fabricates completion or deletes durable history. */}
        finally{chatAttachmentRefreshing=false;}
    }
}
}
