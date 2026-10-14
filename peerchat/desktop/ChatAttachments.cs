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
        string[] states={"Preparing / hashing","Waiting for recipient / route","Incoming attachment — Download to accept",row.sending?"Sending":"Receiving","Complete — SHA-256 verified","Canceled","Failed","Revalidating retained file","Paused — retained for restart","Archive metadata — file not local"};
        string label=row.state>=0&&row.state<states.Length?states[row.state]:"Unavailable";
        if(row.state==3&&row.size>0)label+=" · "+Math.Min(100,Math.Max(0,100.0*row.done/row.size)).ToString("0")+"%";
        return label;
    }
    public static string Statistics(CoreAdapter.FileTransferData row){
        var culture=System.Globalization.CultureInfo.InvariantCulture;
        double percent=row.size>0?Math.Min(100,100.0*row.done/row.size):row.state==4?100:0;
        string average=row.statisticsKnown&&row.durationMs>0?(row.statisticsBytes*8000.0/row.durationMs/1000000).ToString("0.00",culture)+" Mbps":"unavailable";
        string result=row.done.ToString("N0")+" / "+row.size.ToString("N0")+" bytes · "+percent.ToString("0.0",culture)+"%";
        if(row.state==4)return result+"\nAverage "+average+" · Duration "+(row.statisticsKnown?(row.durationMs/1000.0).ToString("0.0",culture)+" s":"unavailable");
        result+="\nCurrent "+(row.state==3?row.rateBits/1000000.0:0).ToString("0.00",culture)+" Mbps · Average "+average;
        if(row.state==3&&row.etaReliable&&row.rateBits>0&&row.done<row.size){double seconds=Math.Ceiling((row.size-row.done)*8.0/row.rateBits);if(seconds>0&&seconds<=604800)result+=" · ETA "+seconds.ToString("0",culture)+" s";}
        return result;
    }
    public static bool Apply(ConversationModel conversation,CoreAdapter.FileTransferData row){
        var message=conversation.Messages.FirstOrDefault(item=>item.Attachment!=null&&LogicalId(item.Attachment)==LogicalId(row));
        if(message!=null){var previous=message.Attachment;if(previous.id==row.id&&previous.state==row.state&&previous.done==row.done&&previous.path==row.path&&previous.error==row.error&&previous.rateBits==row.rateBits&&previous.durationMs==row.durationMs&&previous.statisticsBytes==row.statisticsBytes&&previous.statisticsKnown==row.statisticsKnown&&previous.etaReliable==row.etaReliable)return false;message.SetAttachment(row);return true;}
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
public static class SafeAttachmentPreview {
    public const int Bytes=32768,Lines=200,ThumbnailWidth=320,ThumbnailHeight=240;
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
        if(size>1024*1024)throw new InvalidDataException("Full text viewer is limited to 1 MiB; use Save As for larger files.");
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
        }catch{result.Error="Preview unavailable — use Save As or containing folder";return result;}
    }
}
// Shared read-only text surface for files, protected snippets and ordinary long
// chat messages. Height is bounded independently of exact text/line count.
public sealed class BoundedTextView:StackPanel {
    public readonly TextBox Viewer;
    public readonly Button Expand,Copy,OpenFull;
    public static bool CodeLike(string text){return System.Text.RegularExpressions.Regex.Matches(text??"",@"(?m)^\s*(?:public |private |class |def |function |#include|import |using |.*[;{}]\s*$)").Count>=3;}
    public static TextBox ReadOnly(string text,bool code){return new TextBox{Text=text??"",IsReadOnly=true,AcceptsReturn=true,FontSize=16,FontFamily=new FontFamily(code?"Consolas":"Segoe UI"),Background=new SolidColorBrush(Color.FromRgb(11,21,30)),Foreground=Brushes.White,TextWrapping=code?TextWrapping.NoWrap:TextWrapping.Wrap,HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Padding=new Thickness(6),BorderThickness=new Thickness(0)};}
    public static void ShowFull(string text,bool code,string title){var owner=Application.Current==null?null:Application.Current.MainWindow;var dialog=new Window{Title=title+" — read-only",Width=760,Height=520,Background=new SolidColorBrush(Color.FromRgb(11,21,30)),Foreground=Brushes.White,Content=ReadOnly(text,code)};if(owner!=null&&owner.IsVisible)dialog.Owner=owner;dialog.Show();}
    public BoundedTextView(string text,bool code,string title,Action copy=null,Action open=null){
        code=code||CodeLike(text);Children.Add(new TextBlock{Text=title,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,0,0,6)});
        Viewer=ReadOnly(text,code);Viewer.MaxHeight=160;Children.Add(Viewer);
        var actions=new WrapPanel{Margin=new Thickness(0,4,0,0)};Children.Add(actions);
        Expand=new Button{Content="Expand",Padding=new Thickness(8,4,8,4),Margin=new Thickness(0,0,6,0)};Expand.Click+=(s,e)=>{bool expanded=Viewer.MaxHeight>160;Viewer.MaxHeight=expanded?160:320;Expand.Content=expanded?"Expand":"Show less";};actions.Children.Add(Expand);
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
    public static readonly DependencyProperty TransferProperty=DependencyProperty.Register("Transfer",typeof(CoreAdapter.FileTransferData),typeof(AttachmentCard),new PropertyMetadata(null,(sender,args)=>((AttachmentCard)sender).Render()));
    public CoreAdapter.FileTransferData Transfer{get{return (CoreAdapter.FileTransferData)GetValue(TransferProperty);}set{SetValue(TransferProperty,value);}}
    string previewPath="",previewId="";AttachmentPreview preview;bool autoPreviewAttempted;
    static readonly Queue<WeakReference> residentCards=new Queue<WeakReference>();
    public AttachmentCard(){MaxWidth=360;Loaded+=(s,e)=>Render();}
    Button ActionButton(string label,Action action){var button=new Button{Content=label,Margin=new Thickness(0,4,6,0),Padding=new Thickness(8,4,8,4)};button.Click+=(s,e)=>action();return button;}
    void Signal(CoreAdapter.FileTransferData row,string action){RaiseEvent(new AttachmentActionEventArgs(ActionEvent,row,action));}
    void Render(){
        var row=Transfer;Visibility=row==null?Visibility.Collapsed:Visibility.Visible;Children.Clear();if(row==null)return;
        if(previewId!=row.id||previewPath!=row.path){preview=null;previewId=row.id;previewPath=row.path;autoPreviewAttempted=false;}
        bool longMessage=ChatAttachmentProjection.IsLongMessage(row);
        if(!longMessage)Children.Add(new TextBlock{Text=row.name,FontWeight=FontWeights.SemiBold,TextWrapping=TextWrapping.Wrap});
        else if(preview==null)Children.Add(new TextBlock{Text="Long message",FontWeight=FontWeights.SemiBold});
        Children.Add(new TextBlock{Text=row.size.ToString("N0")+" bytes · "+ChatAttachmentProjection.Status(row),Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        Children.Add(new TextBlock{Text=ChatAttachmentProjection.Statistics(row),Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap,FontSize=12});
        if(!String.IsNullOrEmpty(row.error))Children.Add(new TextBlock{Text=row.error,Foreground=Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});
        if(row.state==3&&row.size>0)Children.Add(new ProgressBar{Height=4,Maximum=row.size,Value=Math.Min(row.done,row.size),Margin=new Thickness(0,6,0,2)});
        var actions=new WrapPanel();Children.Add(actions);actions.Children.Add(ActionButton("Open Diagnostics",()=>Signal(row,"diagnostics")));
        if(row.state==2&&!row.sending)actions.Children.Add(ActionButton("Download…",()=>Signal(row,"accept")));
        if(row.state<4||row.state==7||row.state==8)actions.Children.Add(ActionButton("Cancel",()=>Signal(row,"cancel")));
        if(row.state==9)Children.Add(new TextBlock{Text="Restore through an authorized source in History / Archive Settings. Source availability is not yet proven.",Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        if(row.state!=4||(!row.protectedSource&&String.IsNullOrEmpty(row.path)))return;
        if(!longMessage)actions.Children.Add(ActionButton("Save As…",()=>Signal(row,"save")));
        if(!longMessage&&!row.protectedSource)actions.Children.Add(ActionButton("Folder",()=>Signal(row,"folder")));
        if(preview==null){actions.Children.Add(ActionButton("Preview",()=>LoadPreview(row)));if(IsLoaded&&IsVisible&&!autoPreviewAttempted){autoPreviewAttempted=true;LoadPreview(row);}return;}
        if(preview.Image!=null){var image=new Image{Source=preview.Image,MaxWidth=320,MaxHeight=240,HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,8,0,0),Cursor=Cursors.Hand,ToolTip="Open read-only image viewer"};image.MouseLeftButtonUp+=(s,e)=>ShowPreview(row,preview);Children.Add(image);}
        else if(preview.Error.Length==0){Children.Add(new BoundedTextView(preview.Text,!longMessage,longMessage?"Long message":"Text preview",null,()=>OpenFull(row)));if(preview.Truncated)Children.Add(new TextBlock{Text="Preview limited to 32 KiB / 200 lines — Open full to read more",Foreground=Brushes.LightGray});}
        else{Children.Add(new TextBlock{Text=preview.Error,Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});actions.Children.Add(ActionButton("Retry preview",()=>{preview=null;LoadPreview(row);}));}
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
    bool chatAttachmentRefreshing;long chatAttachmentCursor;DateTime chatCatalogAt=DateTime.MinValue;
    string attachmentTelemetry="";long attachmentTelemetryAccount;ulong attachmentTelemetryGeneration;
    string AttachmentDiagnostics(){return discovery!=null&&attachmentTelemetryAccount==discovery.AccountId&&attachmentTelemetryGeneration==authenticatedFriendSnapshotGeneration?attachmentTelemetry:"No current profile/account transfer snapshot.";}
    readonly SemaphoreSlim attachmentCopies=new SemaphoreSlim(2,2);
    readonly Dictionary<string,CoreAdapter.FileTransferData> chatCatalog=new Dictionary<string,CoreAdapter.FileTransferData>();
    string dragConversation;bool dragTargetCaptured;
    string ChatTarget(){return model.Groups||model.Selected==null?null:model.Selected.ConversationId;}
    void BindChatAttachments(){
        var refresh=new DispatcherTimer{Interval=TimeSpan.FromSeconds(1)};refresh.Tick+=async(s,e)=>await RefreshChatAttachments();refresh.Start();window.Closed+=(s,e)=>{refresh.Stop();chatCatalog.Clear();};
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
            if(action.Action=="cancel"){await FileOperation(3,"",row.id,"");return;}
            if(action.Action=="accept"){var choose=new Microsoft.Win32.SaveFileDialog{FileName=row.name,Title="Download to folder (unique ID filename)",OverwritePrompt=false};if(choose.ShowDialog(window)==true)await FileOperation(2,"",row.id,Path.GetDirectoryName(choose.FileName));return;}
            if(action.Action=="folder"){try{System.Diagnostics.Process.Start("explorer.exe","/select,\""+row.path+"\"");}catch{}return;}
            if(action.Action=="save"){var choose=new Microsoft.Win32.SaveFileDialog{FileName=row.name,Title="Save an attachment copy",OverwritePrompt=false};if(choose.ShowDialog(window)==true){if(!attachmentCopies.Wait(0)){Notify("Two file copies are already running; try again.");return;}string destination=choose.FileName;try{await Task.Run(()=>{if(row.protectedSource){core.FileTransfer(10,"",row.id,destination);return;}using(var input=new FileStream(row.path,FileMode.Open,FileAccess.Read,FileShare.Read))using(var output=new FileStream(destination,FileMode.CreateNew,FileAccess.Write,FileShare.None)){input.CopyTo(output,32768);output.Flush(true);}});}catch(Exception error){Notify("Copy failed: "+error.Message);}finally{attachmentCopies.Release();}}}
        }));
        var outline=Find<ScrollViewer>("MessageScroll");var target=(Grid)outline.Parent;target.AllowDrop=true;
        target.PreviewDragEnter+=(s,e)=>{if(!dragTargetCaptured){dragConversation=ChatTarget();dragTargetCaptured=true;}SetDrop(e);};
        target.PreviewDragOver+=(s,e)=>SetDrop(e);
        target.PreviewDragLeave+=(s,e)=>{var point=e.GetPosition(target);if(point.X<0||point.Y<0||point.X>target.ActualWidth||point.Y>target.ActualHeight){dragConversation=null;dragTargetCaptured=false;outline.ToolTip=null;outline.BorderThickness=new Thickness(0);}};
        target.PreviewDrop+=async(s,e)=>{e.Handled=true;outline.ToolTip=null;outline.BorderThickness=new Thickness(0);var files=e.Data.GetData(DataFormats.FileDrop) as string[];string captured=dragConversation;dragConversation=null;dragTargetCaptured=false;if(!ChatAttachmentProjection.ValidDrop(captured,ChatTarget(),model.Groups,files)){Notify("Drop canceled: choose a direct conversation and up to four regular files. The conversation must not change during the drag.");return;}await SendChatFiles(model.Selected,files);};
    }
    void SetDrop(DragEventArgs e){var files=e.Data.GetDataPresent(DataFormats.FileDrop)?e.Data.GetData(DataFormats.FileDrop) as string[]:null;bool valid=ChatAttachmentProjection.ValidDrop(dragConversation,ChatTarget(),model.Groups,files);e.Effects=valid?DragDropEffects.Copy:DragDropEffects.None;e.Handled=true;var target=Find<ScrollViewer>("MessageScroll");target.BorderBrush=new SolidColorBrush(Color.FromRgb(131,204,252));target.BorderThickness=new Thickness(valid?2:0);target.ToolTip=valid?"Drop files into "+model.Selected.Name:"Choose a direct conversation; group attachment fanout is not available";}
    async void ChooseChatFiles(){
        var selected=model.Groups?null:model.Selected;
        if(selected==null||selected.Contact==null||!selected.Contact.verified){Notify("Select a verified direct conversation. Group attachments are not available yet.");return;}
        var choose=new Microsoft.Win32.OpenFileDialog{Title="Attach files to "+selected.Name,CheckFileExists=true,Multiselect=true};
        if(choose.ShowDialog(window)==true)await SendChatFiles(selected,choose.FileNames);
    }
    async Task SendChatFiles(ConversationModel target,string[] files){
        if(target==null||target.Contact==null||!target.Contact.verified||files.Length>4){Notify("Use a verified recipient and at most four files per selection.");return;}
        string peer=target.Contact.peer;foreach(string path in files)await FileOperation(1,peer,"",path);
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
        if(closing||chatAttachmentRefreshing||fileTransferOperating||!model.Listening)return;chatAttachmentRefreshing=true;
        try{
            var owner=discovery;ulong profileGeneration=profileSessionGeneration;long account=owner==null?0:owner.AccountId;ulong generation=authenticatedFriendSnapshotGeneration;
            var sample=await Task.Run(()=>new{Rows=core.FileTransfers(),Diagnostics=core.FileTransfer(11)});if(closing||profileGeneration!=profileSessionGeneration||owner!=discovery||account!=(discovery==null?0:discovery.AccountId)||generation!=authenticatedFriendSnapshotGeneration)return;var rows=sample.Rows;CacheTransferDiagnostics(rows,sample.Diagnostics);attachmentTelemetry=sample.Diagnostics;attachmentTelemetryAccount=account;attachmentTelemetryGeneration=generation;
            if(DateTime.UtcNow-chatCatalogAt>TimeSpan.FromSeconds(5)){
                var page=await Task.Run(()=>core.ArchiveAttachments(chatAttachmentCursor));if(closing)return;
                foreach(var item in page.records??new CoreAdapter.ArchiveAttachment[0]){
                    var conversation=model.FindByPeer(item.peer)??model.Conversations.Values.FirstOrDefault(value=>value.AccountId>0&&value.AccountId==item.account);
                    if(conversation==null)continue;
                    chatCatalog[item.id]=new CoreAdapter.FileTransferData{id=item.id,peer=conversation.Contact.peer,name=item.name,size=item.size,done=item.size,state=item.protectedSource?4:String.IsNullOrEmpty(item.path)?(item.archiveAttested?9:10):4,path=item.path,sending=item.sending,senderPeer=item.senderPeer,protectedSource=item.protectedSource,time=item.time};
                }
                chatAttachmentCursor=page.complete?0:page.next;chatCatalogAt=DateTime.UtcNow;
            }
            bool changed=false;var current=new HashSet<string>(rows.Select(ChatAttachmentProjection.LogicalId));
            foreach(var row in rows.Concat(chatCatalog.Values.Where(row=>!current.Contains(row.id)))){
                var conversation=model.FindByPeer(String.IsNullOrEmpty(row.conversationPeer)?row.peer:row.conversationPeer)??model.Conversations.Values.FirstOrDefault(value=>row.account>0&&value.AccountId==row.account);if(conversation==null)continue;
                changed|=ChatAttachmentProjection.Apply(conversation,row);
            }
            if(changed){Refresh();SizeBubbles();}
        }catch{/* A missing snapshot never fabricates completion or deletes durable history. */}
        finally{chatAttachmentRefreshing=false;}
    }
}
}
