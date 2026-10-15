using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using PeerChatDesktop;

class GroupAttachmentUiTest {
    static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
    static void Rejected(Action action,string why){try{action();}catch(InvalidDataException){return;}throw new Exception(why);}
    static string Text(BinaryReader reader){return new UTF8Encoding(false,true).GetString(reader.ReadBytes(checked((int)reader.ReadUInt64())));}
    static object Value(BinaryReader reader){switch(reader.ReadByte()){case 0:return null;case 1:return false;case 2:return true;case 3:return reader.ReadUInt64();case 4:return Text(reader);case 5:{var values=new List<object>();ulong count=reader.ReadUInt64();while(count-->0)values.Add(Value(reader));return values;}case 6:{var values=new Dictionary<string,object>();ulong count=reader.ReadUInt64();while(count-->0)values.Add(Text(reader),Value(reader));return values;}default:throw new Exception("bad value tag");}}
    static string[] Actions(GroupAttachmentCard card){return card.Children.OfType<WrapPanel>().SelectMany(panel=>panel.Children.OfType<Button>()).Select(button=>(string)button.Content).ToArray();}
    static string[] DirectActions(AttachmentCard card){return card.Children.OfType<WrapPanel>().SelectMany(panel=>panel.Children.OfType<Button>()).Select(button=>(string)button.Content).ToArray();}
    static void Codec(){
        var expected=new Dictionary<string,object>{{"zero",0},{"large",UInt64.MaxValue},{"unicode","日本 👩🏽‍💻"},{"flags",new object[]{null,false,true}},{"nested",new Dictionary<string,object>{{"offset",65536L}}}};
        var bytes=AttachmentNetworkCodec.Response(44,new string('a',32),expected);
        using(var stream=new MemoryStream(bytes))using(var reader=new BinaryReader(stream)){
            Check(reader.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,30}),"ABI header");Check(Text(reader)=="PeerChat/AttachmentResponse/v1"&&reader.ReadUInt64()==44&&Text(reader)==new string('a',32),"generation/domain/correlation");
            var result=(Dictionary<string,object>)Value(reader);Check((ulong)result["large"]==UInt64.MaxValue&&(string)result["unicode"]=="日本 👩🏽‍💻"&&(ulong)((Dictionary<string,object>)result["nested"])["offset"]==65536,"integer/Unicode/object changed");
            Check(stream.Position==stream.Length,"extra bytes");
        }
        Rejected(()=>AttachmentNetworkCodec.Response(1,"id",new Dictionary<string,object>{{"x",-1}}),"negative integer accepted");
        Rejected(()=>AttachmentNetworkCodec.Response(1,"id",new Dictionary<string,object>{{"x",1.5m}}),"fractional integer accepted");
        Rejected(()=>AttachmentNetworkCodec.Response(1,"id",new Dictionary<string,object>{{"x",new string('x',100000)}}),"oversize response accepted");
        var deep=new Dictionary<string,object>();var cursor=deep;for(int i=0;i<14;i++){var next=new Dictionary<string,object>();cursor["next"]=next;cursor=next;}
        Rejected(()=>AttachmentNetworkCodec.Response(1,"id",deep),"deep response accepted");
        Rejected(()=>AttachmentNetworkCodec.Session(1,1,"wrong"),"unbound server accepted");
        Check(AttachmentNetworkCodec.Session(0,2,"").Length>0,"clear session unavailable");
        var executionType=typeof(CoreAdapter).Assembly.GetType("PeerChatDesktop.GroupAttachmentExecution");
        var constructor=executionType.GetConstructor(BindingFlags.Instance|BindingFlags.NonPublic,null,new[]{typeof(Func<uint,byte[],byte[]>),typeof(ulong)},null);
        var stop=executionType.GetMethod("Stop",BindingFlags.Instance|BindingFlags.NonPublic);var service=executionType.GetMethod("Service",BindingFlags.Instance|BindingFlags.NonPublic);
        string pending="{\"requestId\":\""+new string('a',32)+"\",\"type\":\"cache_status\",\"generation\":44}";
        int deliveries=0;uint deliveredOperation=0;byte[] lastInput=null;Func<uint,byte[],byte[]> native=(operation,input)=>{if(operation==1)return Encoding.UTF8.GetBytes(pending);deliveries++;deliveredOperation=operation;lastInput=input;return new byte[0];};
        var execution=constructor.Invoke(new object[]{native,44UL});
        Func<Dictionary<string,object>,Dictionary<string,object>> late=payload=>{Check(!payload.ContainsKey("generation"),"private generation sent to server");stop.Invoke(execution,null);return new Dictionary<string,object>{{"state","accepted"}};};
        Check(!(bool)service.Invoke(execution,new object[]{late})&&deliveries==0,"late profile response reached native");
        execution=constructor.Invoke(new object[]{native,44UL});Func<Dictionary<string,object>,Dictionary<string,object>> fail=payload=>{throw new IOException("private path and token should not escape");};
        Check((bool)service.Invoke(execution,new object[]{fail})&&deliveries==1&&deliveredOperation==3,"network failure not returned to native retry owner");
        Check(Encoding.UTF8.GetString(lastInput).Contains("network_unavailable")&&!Encoding.UTF8.GetString(lastInput).Contains("private path"),"failure leaked exception text");
        execution=constructor.Invoke(new object[]{native,44UL});Func<Dictionary<string,object>,Dictionary<string,object>> closed=payload=>new Dictionary<string,object>{{"state","cache_feature_gated"}};
        Check((bool)service.Invoke(execution,new object[]{closed})&&deliveredOperation==2&&Encoding.UTF8.GetString(lastInput).Contains("cache_feature_gated"),"server rejection concealed as network failure");
        pending=pending.Replace("44","45");execution=constructor.Invoke(new object[]{native,44UL});int calls=0;Func<Dictionary<string,object>,Dictionary<string,object>> wrong=payload=>{calls++;return expected;};Check(!(bool)service.Invoke(execution,new object[]{wrong})&&calls==0,"stale generation made RPC");
        var discovery=new DiscoveryClient("127.0.0.1",1,new string('a',64)){AccountId=7,SessionToken="current-session"};typeof(DiscoveryClient).GetField("supportsRpc",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(discovery,true);
        var request=new Dictionary<string,object>{{"type","cache_status"},{"requestId",new string('a',32)}};
        foreach(int mismatch in new[]{0,1,2}){bool rejected=false;try{discovery.AttachmentCache(new string('b',64),request,mismatch==0?8:7,mismatch==1?"old-session":"current-session",mismatch==2?new string('c',64):new string('a',64));}catch(InvalidOperationException){rejected=true;}Check(rejected,"stale account/token/server pin was sent to RPC");}
    }
    [STAThread] static int Main(string[] args){try{
        Directory.CreateDirectory(args[0]);var app=new Application{ShutdownMode=ShutdownMode.OnExplicitShutdown};Codec();
        var download=AttachmentDownloadTarget.Dialog("cat.png");Check(download.FileName==AttachmentDownloadTarget.FirstAvailableName(download.InitialDirectory,"cat.png")&&download.DefaultExt.TrimStart('.')=="png"&&download.AddExtension&&!download.OverwritePrompt,"download default original filename/extension changed");
        string fixture=Path.GetFullPath(args[0]);
        var occupied=new HashSet<string>(StringComparer.OrdinalIgnoreCase){Path.Combine(fixture,"cat.png"),Path.Combine(fixture,"cat (1).png")};
        Check(AttachmentDownloadTarget.FirstAvailableName(fixture,"cat.png",occupied.Contains)=="cat (2).png","download collision default was not numbered");
        Check(AttachmentDownloadTarget.FirstAvailableName(fixture,"cat.jpeg",occupied.Contains)=="cat.jpeg","different extension was rewritten");
        Check(AttachmentDownloadTarget.FirstAvailableName(fixture,"user-chosen.final.png",occupied.Contains)=="user-chosen.final.png","custom filename was rewritten");
        string chosen=Path.Combine(Path.GetFullPath(args[0]),"cat2.png");Check(AttachmentDownloadTarget.Validate(chosen)==chosen,"download selected basename discarded");
        using(var wire=new MemoryStream(Convert.FromBase64String(AttachmentDownloadTarget.Wire(new AttachmentDownloadTarget.Selection{Path=chosen}))))using(var reader=new BinaryReader(wire)){
            Check(reader.ReadBytes(4).SequenceEqual(new byte[]{80,84,1,30})&&Text(reader)=="PeerChat/DownloadSelection/v1"&&Text(reader)==chosen&&reader.ReadUInt64()==0&&wire.Position==wire.Length,"download selection wire changed");
        }
        File.WriteAllText(chosen,"preserve");bool collision=false;try{AttachmentDownloadTarget.Validate(chosen);}catch(IOException){collision=true;}Check(collision&&File.ReadAllText(chosen)=="preserve","existing download target changed");
        var group=new GroupModel{Data=new GroupData{id="group-one",name="Attachment checks"}};
        foreach(string state in new[]{"preparing","uploading","available","downloading","verifying","paused","failed","expired","complete"}){
            var row=new CoreAdapter.GroupAttachmentData{id="attempt-"+state,logicalId="one-message",group=group.Id,name="日本 👩🏽‍💻.png",size=100,transferSize=200,done=50,state=state,sending=false,time=100};
            Check(GroupAttachmentProjection.Apply(group,row)&&group.Messages.Count==1,"retry duplicate bubble");Check(!GroupAttachmentProjection.Apply(group,row),"unchanged snapshot redraw");Check(group.Messages[0].TextVisibility==Visibility.Collapsed,"attachment showed extra text bubble");
            var card=new GroupAttachmentCard{Attachment=row};Check(!Actions(card).Contains("Preview")&&!Actions(card).Contains("Save As…"),"unverified local content exposed");
            if(state=="uploading"||state=="downloading"){Check(card.Children.OfType<ProgressBar>().Single().Maximum==200,"progress used plaintext size for ciphertext ACKs");Check(card.Children.OfType<TextBlock>().Any(text=>text.Text.Contains("confirmed encrypted bytes")),"ciphertext ACK units missing");}
            if(state=="complete")Check(!GroupAttachmentProjection.Status(row).Contains("Complete — plaintext verified"),"ciphertext completion mislabeled verified");
        }
        var complete=group.Messages[0].GroupAttachment;complete.path=Path.Combine(args[0],"local.png");complete.plaintextVerified=true;
        var request=new CoreAdapter.GroupAttachmentData{canOpen=true,state="complete",size=100};var requestCard=new GroupAttachmentCard{Attachment=request};Check(Actions(requestCard).Contains("Preview")&&!GroupAttachmentProjection.CanOpen(request),"verification request exposed unverified path");request.outputPending=true;requestCard.Attachment=null;requestCard.Attachment=request;Check(!Actions(requestCard).Contains("Preview"),"pending verification repeated action");
        complete.exportState="complete";complete.exportPath=chosen;
        var verified=new GroupAttachmentCard{Attachment=complete};Check(Actions(verified).Contains("Open File")&&Actions(verified).Contains("Open Folder")&&!Actions(verified).Contains("Save As…"),"saved target actions absent");
        complete.exportState="failed";complete.exportPath="";verified.Attachment=null;verified.Attachment=complete;Check(Actions(verified).Contains("Retry Save…")&&!Actions(verified).Contains("Open File"),"failed save exposed an internal path");
        complete.sending=true;Check(!GroupAttachmentProjection.CanOpen(complete),"recipient verification authorized sender source");complete.sourceVerified=true;Check(GroupAttachmentProjection.CanOpen(complete),"verified source unavailable");
        complete.state="expired";complete.canRetry=true;var expired=new GroupAttachmentCard{Attachment=complete};Check(!Actions(expired).Contains("Retry")&&Actions(expired).Contains("Preview"),"expiration renewed cache or hid verified local copy");
        var prepareAgain=new CoreAdapter.GroupAttachmentData{id="source",group=group.Id,name="original.png",size=12,state="failed",sending=true,canRetry=true,preparationRequired=true};
        Check(Actions(new GroupAttachmentCard{Attachment=prepareAgain}).Contains("Prepare again…"),"failed unsealed preparation did not request original file");
        complete.group="another";Check(!GroupAttachmentProjection.Apply(group,complete),"wrong group projection");complete.group=group.Id;
        Check(GroupAttachmentHistoryCursor.Valid("100")&&!GroupAttachmentHistoryCursor.Valid("0")&&!GroupAttachmentHistoryCursor.Valid("01")&&!GroupAttachmentHistoryCursor.Valid("9223372036854775808")&&!GroupAttachmentHistoryCursor.Valid("12x"),"history cursor bounds");
        Check(GroupAttachmentHistoryCursor.Advances("100","64")&&!GroupAttachmentHistoryCursor.Advances("100","100")&&!GroupAttachmentHistoryCursor.Advances("100","101"),"history cursor did not advance monotonically");
        var dm=new ConversationModel{Contact=new ContactData{peer="peer",name="DM"}};ChatAttachmentProjection.Apply(dm,new CoreAdapter.FileTransferData{id="dm",state=4,name="original.txt"});Check(dm.Messages.Single().Attachment.name=="original.txt"&&dm.Messages.Single().GroupAttachment==null,"DM attachment changed");
        var direct=new AttachmentCard{Transfer=new CoreAdapter.FileTransferData{id="dm-saved",state=4,name="cat.png",path=chosen,saveState="complete",savePath=chosen}};
        Check(DirectActions(direct).Contains("Open File")&&DirectActions(direct).Contains("Open Folder")&&!DirectActions(direct).Contains("Save As…"),"DM saved target actions absent");
        direct.Transfer=new CoreAdapter.FileTransferData{id="dm-unsaved",state=4,name="cat.png",path=chosen,saveState="failed"};Check(DirectActions(direct).Contains("Retry Save…")&&!DirectActions(direct).Contains("Open File"),"DM failed save exposed internal file");
        direct.Transfer=new CoreAdapter.FileTransferData{id="dm-legacy",state=4,name="cat.png",path=chosen};Check(DirectActions(direct).Contains("Retry Save…")&&!DirectActions(direct).Contains("Save As…"),"legacy download recovery missing");
        direct.Transfer=new CoreAdapter.FileTransferData{id="dm-pending",state=4,name="cat.png",path=chosen,saveState="pending"};Check(!DirectActions(direct).Contains("Retry Save…"),"active verification offered a retry that cannot run");
        direct.Transfer=new CoreAdapter.FileTransferData{id="dm-history",state=4,name="cat.png",path="",saveState="complete",savePath=chosen};Check(DirectActions(direct).Contains("Open File")&&DirectActions(direct).Contains("Open Folder"),"saved historical file hidden by unavailable internal preview");
        var changedSave=new CoreAdapter.FileTransferData{id="dm",state=4,name="original.txt",saveState="complete",savePath=chosen};Check(ChatAttachmentProjection.Apply(dm,changedSave),"save-only state change did not refresh bubble");
        var image=BitmapSource.Create(800,600,96,96,PixelFormats.Bgr32,null,new byte[800*600*4],800*4);image.Freeze();
        var draft=GroupImageDraft.Create(group.Id,9,image);Check(draft.Matches(group.Id,9)&&!draft.Matches(group.Id,10)&&!draft.Matches("other",9),"draft crossed group/profile");
        Check(draft.Preview.IsFrozen&&draft.Preview.PixelWidth<=320&&draft.Preview.PixelHeight<=160&&draft.Name.EndsWith(".png")&&Convert.FromBase64String(draft.Base64).Take(8).SequenceEqual(new byte[]{137,80,78,71,13,10,26,10}),"clipboard draft format/bounds");
        string file=Path.Combine(args[0],"drop.txt");File.WriteAllText(file,"drop");
        var selected=GroupFileDraft.Create(group.Id,9,new[]{file});Check(selected.Matches(group.Id,9)&&!selected.Matches("another",9)&&selected.Files.Length==1&&selected.Files[0].Name=="drop.txt"&&selected.Files[0].Size==4&&selected.Files[0].Unchanged(),"selected file draft metadata/scope");
        Rejected(()=>GroupFileDraft.Create(group.Id,9,new[]{file,file}),"duplicate file draft accepted");
        File.AppendAllText(file," changed");Check(!selected.Files[0].Unchanged(),"changed file draft could be sent without reselection");
        string pngFile=Path.Combine(args[0],"draft-preview.png");File.WriteAllBytes(pngFile,Convert.FromBase64String(draft.Base64));
        var pngDraft=GroupFileDraft.Create(group.Id,9,new[]{pngFile});Check(pngDraft.Files[0].Unchanged()&&SafeAttachmentPreview.Read(pngFile,pngDraft.Files[0].Size).Image!=null,"selected PNG preview unavailable");
        Check(GroupAttachmentProjection.ValidDrop("group:one","group:one",true,new[]{file}),"compatible group drop rejected");Check(!GroupAttachmentProjection.ValidDrop("group:one","group:two",true,new[]{file})&&!GroupAttachmentProjection.ValidDrop("group:one","group:one",false,new[]{file}),"drop changed target or bypassed capability");
        using(var resource=typeof(CoreAdapter).Assembly.GetManifestResourceStream("Main.xaml")){
            var window=(Window)XamlReader.Load(resource);window.ShowInTaskbar=false;window.ShowActivated=false;window.Left=-20000;
            var composer=(TextBox)window.FindName("Composer");Check(composer.MaxHeight==150&&composer.MinHeight==42,"compact composer changed");composer.Text="Exact Unicode 👩🏽‍💻 🇺🇸 ❤️";((TextBlock)window.FindName("ComposerHint")).Visibility=Visibility.Collapsed;EmojiColor.DraftPreview((WrapPanel)window.FindName("EmojiDraftPreview"),composer.Text);
            var olderAttachments=(Button)window.FindName("OlderGroupAttachments");Check(olderAttachments!=null&&olderAttachments.Visibility==Visibility.Collapsed&&((Button)window.FindName("Older"))!=null,"bounded attachment history control missing or changed message paging");
            ((ItemsControl)window.FindName("Messages")).ItemsSource=group.Messages;((StackPanel)window.FindName("ChatEmpty")).Visibility=Visibility.Collapsed;
            ((Border)window.FindName("GroupImageDraft")).Visibility=Visibility.Visible;((Image)window.FindName("GroupImageDraftPreview")).Source=draft.Preview;((TextBlock)window.FindName("GroupImageDraftName")).Text=draft.Name+" · Image draft — Send to share";
            window.Show();window.UpdateLayout();var content=(FrameworkElement)window.Content;var bitmap=new RenderTargetBitmap((int)content.ActualWidth,(int)content.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(content);var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));using(var output=File.Create(Path.Combine(args[0],"group-image-draft.png")))png.Save(output);window.Close();
            Check(composer.Text=="Exact Unicode 👩🏽‍💻 🇺🇸 ❤️"&&window.FindName("EmojiDraftPreview")!=null&&window.FindName("Emoji")!=null,"draft altered text/emoji controls");
        }
        Console.WriteLine("PASS group attachment UI: stable bubble, verified actions, expiry, scoped drop/draft, bounded PNG/XAML, older-page cursor/control; local ABI bounds and stale response rejection");return 0;
    }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}
