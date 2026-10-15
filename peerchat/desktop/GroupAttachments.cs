using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace PeerChatDesktop {
// Presentation only. Native owns identity, durable progress and verification.
public sealed partial class CoreAdapter {
    public sealed class GroupAttachmentRecipient {public long account,done;public string device,name,state,error;}
    public sealed class GroupAttachmentData {
        public string id,logicalId,group,name,path,error,senderPeer,state,exportState,exportPath;
        public long size,transferSize,done,time;
        public bool sending,plaintextVerified,sourceVerified,canRetry,canCancel,canOpen,outputPending;
        public GroupAttachmentRecipient[] recipients;
    }
    public sealed class GroupAttachmentSnapshot {public bool supported;public string reason;public GroupAttachmentData[] items;}
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]
    static extern IntPtr ui_group_attachment(IntPtr handle,uint operation,string group,string id,string path);
    public string GroupAttachment(uint operation,string group="",string id="",string path=""){
        if(operation==1)SelectedTransferFile.ValidateSize(path);
        lock(mailboxGate){
            if(handle==IntPtr.Zero)throw new ObjectDisposedException("CoreAdapter");
            IntPtr result;
            try{result=ui_group_attachment(handle,operation,group,id,path);}
            catch(EntryPointNotFoundException){throw new NotSupportedException("This client does not support group attachments.");}
            if(result==IntPtr.Zero)throw new InvalidOperationException("Group attachment operation unavailable. Check the current membership, connection and local file, then try again.");
            return Marshal.PtrToStringUni(result);
        }
    }
    public GroupAttachmentSnapshot GroupAttachments(string group=""){
        try{return new JavaScriptSerializer{MaxJsonLength=4*1024*1024}.Deserialize<GroupAttachmentSnapshot>(GroupAttachment(0,group))??new GroupAttachmentSnapshot{reason="Group attachment snapshot unavailable."};}
        catch(NotSupportedException error){return new GroupAttachmentSnapshot{reason=error.Message};}
    }
}
public static class GroupAttachmentProjection {
    public static string LogicalId(CoreAdapter.GroupAttachmentData row){return String.IsNullOrEmpty(row.logicalId)?row.id:row.logicalId;}
    public static bool CanOpen(CoreAdapter.GroupAttachmentData row){return row!=null&&!String.IsNullOrEmpty(row.path)&&(row.sending?row.sourceVerified:row.plaintextVerified);}
    public static bool CanRequestOpen(CoreAdapter.GroupAttachmentData row){return row!=null&&(row.canOpen||CanOpen(row));}
    public static bool ValidDrop(string captured,string current,bool supported,string[] files){return supported&&ChatAttachmentProjection.ValidDrop(captured,current,false,files);}
    public static string State(string state,bool sending){
        switch(state){
            case "preparing":return "Preparing encrypted attachment";
            case "uploading":return "Uploading encrypted attachment";
            case "available":return sending?"Cached — awaiting recipients":"Ready to download";
            case "downloading":return "Downloading encrypted attachment";
            case "verifying":return "Verifying attachment";
            case "complete":return sending?"Upload complete":"Download complete";
            case "paused":return "Paused — retained for resume";
            case "canceled":return "Canceled";
            case "failed":return "Transfer failed";
            case "expired":return "Expired — cached attachment unavailable";
            case "verified":return "Plaintext verified";
            case "key_unavailable":return "Recipient key unavailable";
            case "pending":return "Waiting for recipient";
            case "revoked":return "Access revoked";
            default:return "Unavailable";
        }
    }
    public static string Status(CoreAdapter.GroupAttachmentData row){
        if(!row.sending&&row.plaintextVerified)return "Complete — plaintext verified";
        if(!row.sending&&row.state=="complete")return "Downloaded — plaintext verification pending";
        return State(row.state,row.sending);
    }
    public static bool Apply(GroupModel group,CoreAdapter.GroupAttachmentData row){
        if(row==null||String.IsNullOrEmpty(row.id)||String.IsNullOrEmpty(LogicalId(row))||row.group!=group.Id||row.size<0||row.transferSize<0||row.done<0)return false;
        var message=group.Messages.FirstOrDefault(item=>item.GroupAttachment!=null&&LogicalId(item.GroupAttachment)==LogicalId(row));
        string revision=new JavaScriptSerializer().Serialize(row);
        if(message!=null){if(message.GroupAttachmentRevision==revision)return false;message.SetGroupAttachment(row,revision);group.Refresh();return true;}
        message=new MessageModel{Data=new MessageData{id=LogicalId(row),sender=row.senderPeer??"",text="",time=row.time,logical=row.time},Outgoing=row.sending};
        message.SetGroupAttachment(row,revision);
        int at=0;while(at<group.Messages.Count&&(group.Messages[at].Data.time<=row.time||row.time==0))at++;
        group.Messages.Insert(at,message);group.Refresh();return true;
    }
}
public sealed class GroupAttachmentActionEventArgs:RoutedEventArgs {
    public readonly CoreAdapter.GroupAttachmentData Attachment;public readonly string Action;
    public GroupAttachmentActionEventArgs(RoutedEvent routed,CoreAdapter.GroupAttachmentData row,string action):base(routed){Attachment=row;Action=action;}
}
public sealed class GroupAttachmentCard:StackPanel {
    public static readonly RoutedEvent ActionEvent=EventManager.RegisterRoutedEvent("Action",RoutingStrategy.Bubble,typeof(RoutedEventHandler),typeof(GroupAttachmentCard));
    public static readonly DependencyProperty AttachmentProperty=DependencyProperty.Register("Attachment",typeof(CoreAdapter.GroupAttachmentData),typeof(GroupAttachmentCard),new PropertyMetadata(null,(sender,args)=>((GroupAttachmentCard)sender).Render()));
    public CoreAdapter.GroupAttachmentData Attachment{get{return (CoreAdapter.GroupAttachmentData)GetValue(AttachmentProperty);}set{SetValue(AttachmentProperty,value);}}
    public GroupAttachmentCard(){MaxWidth=360;Visibility=Visibility.Collapsed;}
    void AddAction(WrapPanel actions,string title,string action){var button=new Button{Content=title,Padding=new Thickness(8,4,8,4),Margin=new Thickness(0,4,6,0)};button.Click+=(s,e)=>RaiseEvent(new GroupAttachmentActionEventArgs(ActionEvent,Attachment,action));actions.Children.Add(button);}
    void Render(){
        Children.Clear();var row=Attachment;Visibility=row==null?Visibility.Collapsed:Visibility.Visible;if(row==null)return;
        Children.Add(new TextBlock{Text=row.name,FontWeight=FontWeights.SemiBold,TextWrapping=TextWrapping.Wrap});
        Children.Add(new TextBlock{Text=row.size.ToString("N0")+" file bytes · "+GroupAttachmentProjection.Status(row),Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        if(row.state=="uploading"||row.state=="downloading"){
            Children.Add(new TextBlock{Text=Math.Max(0,row.done).ToString("N0")+" confirmed encrypted bytes",FontSize=12,Foreground=Brushes.LightGray});
            if(row.transferSize>0)Children.Add(new ProgressBar{Height=4,Maximum=row.transferSize,Value=Math.Min(row.done,row.transferSize),Margin=new Thickness(0,6,0,2)});
        }
        if(!String.IsNullOrEmpty(row.error))Children.Add(new TextBlock{Text=row.error,Foreground=Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});
        if(row.outputPending||row.exportState=="copying")Children.Add(new TextBlock{Text=row.exportState=="copying"?"Saving verified attachment…":"Verifying local attachment…",Foreground=Brushes.LightGray,TextWrapping=TextWrapping.Wrap});
        if(row.recipients!=null&&row.recipients.Length>0){
            var list=new StackPanel();foreach(var recipient in row.recipients){
                string label=String.IsNullOrEmpty(recipient.name)?"Account "+recipient.account:recipient.name;
                label+=" · "+(recipient.device??"")+" · "+GroupAttachmentProjection.State(recipient.state,false);
                if(!String.IsNullOrEmpty(recipient.error))label+=" · "+recipient.error;
                list.Children.Add(new TextBlock{Text=label,FontSize=12,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,2,0,2)});
            }
            Children.Add(new Expander{Header="Recipient status ("+row.recipients.Length+")",Content=new ScrollViewer{Content=list,MaxHeight=160,VerticalScrollBarVisibility=ScrollBarVisibility.Auto},Margin=new Thickness(0,6,0,0)});
        }
        var actions=new WrapPanel();Children.Add(actions);
        if(!row.sending&&row.state=="available"&&!row.plaintextVerified)AddAction(actions,"Download…","accept");
        if(row.canRetry&&row.state!="expired")AddAction(actions,row.state=="paused"?"Resume":"Retry","retry");
        if(row.canCancel)AddAction(actions,"Cancel","cancel");
        if(GroupAttachmentProjection.CanRequestOpen(row)&&!row.outputPending&&row.exportState!="copying"){AddAction(actions,"Preview","preview");AddAction(actions,"Open","open");AddAction(actions,"Save As…","save");}
    }
}
// A clipboard draft stays in memory until native durably accepts its source.
public sealed class GroupImageDraft {
    public const int MaximumEncodedBytes=16*1024*1024;
    public readonly string Group,Name,Base64;public readonly ulong ProfileGeneration;public readonly BitmapSource Preview;
    GroupImageDraft(string group,ulong profile,string name,string base64,BitmapSource preview){Group=group;ProfileGeneration=profile;Name=name;Base64=base64;Preview=preview;}
    public static GroupImageDraft Create(string group,ulong profile,BitmapSource image){
        if(image==null||image.PixelWidth<=0||image.PixelHeight<=0||image.PixelWidth>16384||image.PixelHeight>16384||(long)image.PixelWidth*image.PixelHeight>16L*1024*1024)throw new InvalidOperationException("Clipboard images are limited to 16 million pixels.");
        string encoded;var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(image));
        using(var output=new MemoryStream()){encoder.Save(output);if(output.Length>MaximumEncodedBytes)throw new InvalidOperationException("Clipboard images are limited to 16 MiB. Save the image and attach the file instead.");encoded=Convert.ToBase64String(output.ToArray());}
        double scale=Math.Min(1,Math.Min(320.0/image.PixelWidth,160.0/image.PixelHeight));
        var preview=new TransformedBitmap(image,new ScaleTransform(scale,scale));preview.Freeze();
        return new GroupImageDraft(group,profile,"Clipboard-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".png",encoded,preview);
    }
    public bool Matches(string group,ulong profile){return Group==group&&ProfileGeneration==profile;}
}
public partial class DesktopApplication {
    GroupAttachmentSnapshotState groupAttachmentState=new GroupAttachmentSnapshotState();
    sealed class GroupAttachmentSnapshotState {public bool Supported;public ulong Profile;public object Owner;public string Reason="Checking group attachment availability…";}
    bool groupAttachmentRefreshing,groupAttachmentOperating,groupImageDraftPreparing;GroupImageDraft groupImageDraft;
    sealed class PendingGroupAttachmentAction {public string Action,Destination,Group;public ulong Profile;public object Owner;}
    readonly Dictionary<string,PendingGroupAttachmentAction> pendingGroupAttachmentActions=new Dictionary<string,PendingGroupAttachmentAction>();
    bool GroupAttachmentsAvailable(){return groupAttachmentState.Supported&&groupAttachmentState.Profile==profileSessionGeneration&&groupAttachmentState.Owner==discovery;}
    string GroupAttachmentUnavailable(){return groupAttachmentState.Profile==profileSessionGeneration&&groupAttachmentState.Owner==discovery?groupAttachmentState.Reason:"Group attachments are unavailable for this session.";}
    void BindGroupAttachments(){
        window.AddHandler(GroupAttachmentCard.ActionEvent,new RoutedEventHandler(async(s,e)=>{var action=(GroupAttachmentActionEventArgs)e;e.Handled=true;await GroupAttachmentAction(action);}));
        DataObject.AddPastingHandler(Find<TextBox>("Composer"),async(s,e)=>{
            if(!model.Groups||model.SelectedGroup==null||!e.DataObject.GetDataPresent(DataFormats.Bitmap))return;
            e.CancelCommand();
            if(groupAttachmentOperating||groupImageDraftPreparing){Notify("Please wait for the current attachment operation.");return;}
            if(!GroupAttachmentsAvailable()){Notify(GroupAttachmentUnavailable());return;}
            if(groupImageDraft!=null&&!groupImageDraft.Matches(model.SelectedGroup.Id,profileSessionGeneration)){Notify("An image draft is waiting in another group. Send or remove that draft before pasting another image.");return;}
            string group=model.SelectedGroup.Id;ulong profile=profileSessionGeneration;var owner=discovery;groupImageDraftPreparing=true;RefreshGroupImageDraft();
            try{var image=e.DataObject.GetData(DataFormats.Bitmap) as BitmapSource;if(image==null)throw new InvalidDataException("Clipboard bitmap unavailable.");image=image.Clone();image.Freeze();var draft=await Task.Run(()=>GroupImageDraft.Create(group,profile,image));if(!closing&&profile==profileSessionGeneration&&owner==discovery){groupImageDraft=draft;RefreshGroupImageDraft();}}
            catch(Exception error){if(!closing&&profile==profileSessionGeneration&&owner==discovery)Notify("Image draft could not be prepared: "+error.Message);}
            finally{groupImageDraftPreparing=false;if(!closing)RefreshGroupImageDraft();}
        });
        Find<Button>("RemoveGroupImageDraft").Click+=(s,e)=>{if(!groupAttachmentOperating&&!groupImageDraftPreparing){groupImageDraft=null;RefreshGroupImageDraft();}};
        window.Closed+=(s,e)=>{groupImageDraft=null;pendingGroupAttachmentActions.Clear();};
    }
    void RefreshGroupImageDraft(){
        if(groupImageDraft!=null&&groupImageDraft.ProfileGeneration!=profileSessionGeneration)groupImageDraft=null;
        bool visible=groupImageDraft!=null&&model.Groups&&model.SelectedGroup!=null&&groupImageDraft.Matches(model.SelectedGroup.Id,profileSessionGeneration);
        Find<Border>("GroupImageDraft").Visibility=visible?Visibility.Visible:Visibility.Collapsed;
        Find<Image>("GroupImageDraftPreview").Source=visible?groupImageDraft.Preview:null;
        Find<TextBlock>("GroupImageDraftName").Text=visible?groupImageDraft.Name+" · Image draft — Send to share":"";
        Find<Button>("RemoveGroupImageDraft").IsEnabled=!groupAttachmentOperating&&!groupImageDraftPreparing;
    }
    async Task<bool> SendGroupImageDraft(){
        if(model.Groups&&groupImageDraftPreparing)return true;
        var draft=groupImageDraft;if(draft==null||!model.Groups||model.SelectedGroup==null||!draft.Matches(model.SelectedGroup.Id,profileSessionGeneration))return false;
        if(groupAttachmentOperating)return true;
        if(await GroupAttachmentOperation(7,draft.Group,draft.Name,draft.Base64))if(groupImageDraft==draft){groupImageDraft=null;RefreshGroupImageDraft();}
        return true;
    }
    async Task<bool> GroupAttachmentOperation(uint operation,string group,string id="",string path=""){
        if(groupAttachmentOperating){Notify("Please wait for the current attachment operation.");return false;}
        if(operation==1||operation==7){if(!GroupAttachmentsAvailable()){Notify(GroupAttachmentUnavailable());return false;}}
        groupAttachmentOperating=true;var generation=profileSessionGeneration;var owner=discovery;
        try{await Task.Run(()=>core.GroupAttachment(operation,group,id,path));return !closing&&generation==profileSessionGeneration&&owner==discovery;}
        catch(FileTooLargeException){Notify(FileTooLargeException.Explanation);return false;}
        catch(Exception error){if(!closing&&generation==profileSessionGeneration&&owner==discovery)Notify(error.Message);return false;}
        finally{groupAttachmentOperating=false;if(!closing){RefreshGroupImageDraft();await RefreshGroupAttachments();}}
    }
    async Task ChooseGroupFiles(){
        var group=model.SelectedGroup;if(group==null||group.Invited){Notify("Select a group you have joined.");return;}
        if(!GroupAttachmentsAvailable()){Notify(GroupAttachmentUnavailable());return;}
        ulong generation=profileSessionGeneration;var choose=new Microsoft.Win32.OpenFileDialog{Title="Attach files to "+group.Name,CheckFileExists=true,Multiselect=true};
        if(choose.ShowDialog(window)==true&&generation==profileSessionGeneration&&model.Groups&&model.SelectedGroup==group)await SendGroupFiles(group,choose.FileNames);
    }
    async Task SendGroupFiles(GroupModel group,string[] files){
        if(group==null||group.Invited||files==null||files.Length==0||files.Length>4){Notify("Choose up to four regular files for a group you have joined.");return;}
        ulong generation=profileSessionGeneration;var owner=discovery;
        foreach(string path in files){if(generation!=profileSessionGeneration||owner!=discovery)return;if(!await GroupAttachmentOperation(1,group.Id,path:path))return;}
    }
    async Task GroupAttachmentAction(GroupAttachmentActionEventArgs action){
        var row=action.Attachment;if(row==null)return;
        if(action.Action=="cancel"){await GroupAttachmentOperation(3,row.group,row.id);return;}
        if(action.Action=="retry"){await GroupAttachmentOperation(4,row.group,row.id);return;}
        if(action.Action=="accept"){var choose=new Microsoft.Win32.SaveFileDialog{FileName=Path.GetFileName(row.name),Title="Download attachment to folder",OverwritePrompt=false};if(choose.ShowDialog(window)==true)await GroupAttachmentOperation(2,row.group,row.id,Path.GetDirectoryName(choose.FileName));return;}
        if(!GroupAttachmentProjection.CanRequestOpen(row)){Notify("The local attachment is not available for verification.");return;}
        if(pendingGroupAttachmentActions.ContainsKey(row.id)){Notify("This attachment is being verified.");return;}
        var generation=profileSessionGeneration;var owner=discovery;
        try{
            string destination="";if(action.Action=="save"){var choose=new Microsoft.Win32.SaveFileDialog{FileName=Path.GetFileName(row.name),Title="Save a verified attachment copy",OverwritePrompt=false};if(choose.ShowDialog(window)!=true)return;destination=choose.FileName;}
            if(closing||generation!=profileSessionGeneration||owner!=discovery)return;
            var pending=new PendingGroupAttachmentAction{Action=action.Action,Destination=destination,Group=row.group,Profile=generation,Owner=owner};
            pendingGroupAttachmentActions[row.id]=pending;
            string path=await Task.Run(()=>core.GroupAttachment(action.Action=="save"?5u:6u,row.group,row.id,destination));
            if(closing||generation!=profileSessionGeneration||owner!=discovery)return;
            if(String.IsNullOrEmpty(path)){Notify("Verifying local attachment…");return;}
            pendingGroupAttachmentActions.Remove(row.id);
            if(action.Action=="save"){if(path!="ok")throw new IOException("Verified export did not complete.");Notify("Verified attachment saved.");return;}
            await ShowVerifiedGroupAttachment(row,pending,path);
        }catch(Exception error){pendingGroupAttachmentActions.Remove(row.id);if(!closing&&generation==profileSessionGeneration&&owner==discovery)Notify("Attachment unavailable: "+error.Message);}
    }
    bool PendingGroupActionCurrent(PendingGroupAttachmentAction pending){return !closing&&pending.Profile==profileSessionGeneration&&pending.Owner==discovery&&model.Groups&&model.SelectedGroup!=null&&model.SelectedGroup.Id==pending.Group;}
    async Task ShowVerifiedGroupAttachment(CoreAdapter.GroupAttachmentData row,PendingGroupAttachmentAction pending,string path){
            if(!PendingGroupActionCurrent(pending))return;
            if(String.IsNullOrWhiteSpace(path)||!Path.IsPathRooted(path))throw new IOException("Verified local file unavailable.");
            if(pending.Action=="open"){System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(path){UseShellExecute=true});return;}
            var preview=await SafeAttachmentPreview.ReadAsync(path,row.size);if(!PendingGroupActionCurrent(pending))return;
            if(preview.Image!=null)new Window{Title=row.name+" — read-only preview",Owner=window,Width=760,Height=520,Content=new Image{Source=preview.Image,Stretch=Stretch.Uniform},Background=Brushes.Black}.Show();
            else if(String.IsNullOrEmpty(preview.Error))BoundedTextView.ShowFull(preview.Text,true,row.name+(preview.Truncated?" — limited preview":""));
            else Notify(preview.Error);
    }
    async Task ContinueGroupAttachmentActions(CoreAdapter.GroupAttachmentData[] rows){
        foreach(var pair in pendingGroupAttachmentActions.ToArray()){
            var pending=pair.Value;if(!PendingGroupActionCurrent(pending)){pendingGroupAttachmentActions.Remove(pair.Key);continue;}
            var row=rows.FirstOrDefault(item=>item!=null&&item.id==pair.Key&&item.group==pending.Group);if(row==null)continue;
            try{
                if(pending.Action=="save"){
                    if(row.exportState=="failed")throw new IOException(row.error??"Verified export failed.");
                    string result=await Task.Run(()=>core.GroupAttachment(5,row.group,row.id,pending.Destination));
                    if(!PendingGroupActionCurrent(pending)){pendingGroupAttachmentActions.Remove(pair.Key);continue;}
                    if(String.IsNullOrEmpty(result))continue;
                    if(result!="ok")throw new IOException("Verified export did not complete.");
                    pendingGroupAttachmentActions.Remove(pair.Key);Notify("Verified attachment saved.");
                }else{
                    if(row.outputPending)continue;
                    if(!GroupAttachmentProjection.CanOpen(row)){if(!String.IsNullOrEmpty(row.error))throw new IOException(row.error);continue;}
                    string path=await Task.Run(()=>core.GroupAttachment(6,row.group,row.id));if(String.IsNullOrEmpty(path))continue;
                    pendingGroupAttachmentActions.Remove(pair.Key);await ShowVerifiedGroupAttachment(row,pending,path);
                }
            }catch(Exception error){pendingGroupAttachmentActions.Remove(pair.Key);if(PendingGroupActionCurrent(pending))Notify("Attachment unavailable: "+error.Message);}
        }
    }
    async Task RefreshGroupAttachments(){
        if(closing||groupAttachmentRefreshing||groupAttachmentOperating||!model.Listening)return;groupAttachmentRefreshing=true;
        var owner=discovery;ulong generation=profileSessionGeneration;
        try{
            string selected=model.Groups&&model.SelectedGroup!=null?model.SelectedGroup.Id:"";
            var snapshot=await Task.Run(()=>core.GroupAttachments(selected));if(closing||generation!=profileSessionGeneration||owner!=discovery)return;
            groupAttachmentState=new GroupAttachmentSnapshotState{Supported=snapshot.supported,Profile=generation,Owner=owner,Reason=String.IsNullOrEmpty(snapshot.reason)?"Group attachments are unavailable on this connection.":snapshot.reason};
            bool changed=false;foreach(var row in snapshot.items??new CoreAdapter.GroupAttachmentData[0]){GroupModel group;if(row!=null&&row.group!=null&&model.GroupChats.TryGetValue(row.group,out group))changed|=GroupAttachmentProjection.Apply(group,row);}
            if(changed){Refresh();SizeBubbles();}
            await ContinueGroupAttachmentActions(snapshot.items??new CoreAdapter.GroupAttachmentData[0]);
        }catch{if(generation==profileSessionGeneration&&owner==discovery)groupAttachmentState=new GroupAttachmentSnapshotState{Profile=generation,Owner=owner,Reason="Group attachment status unavailable. Reconnect and try again."};}
        finally{groupAttachmentRefreshing=false;}
    }
}
}
