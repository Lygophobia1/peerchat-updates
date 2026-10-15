using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace PeerChatDesktop {
public static class AttachmentDownloadTarget {
    public const uint DirectOperation=15,GroupOperation=9;
    static string lastFolder;
    public sealed class Selection {public string Path,Approval;public bool OverwriteApproved;}
    public static string FirstAvailableName(string folder,string originalName,Func<string,bool> occupied=null){
        string name=Path.GetFileName(originalName);if(String.IsNullOrWhiteSpace(name))name="attachment";
        occupied=occupied??(path=>File.Exists(path)||Directory.Exists(path));
        if(!occupied(Path.Combine(folder,name)))return name;
        string stem=Path.GetFileNameWithoutExtension(name),extension=Path.GetExtension(name);
        for(int number=1;number<=9999;number++){string candidate=stem+" ("+number+")"+extension;if(!occupied(Path.Combine(folder,candidate)))return candidate;}
        return name;
    }
    static string InitialFolder(){
        if(!String.IsNullOrEmpty(lastFolder)&&Directory.Exists(lastFolder))return lastFolder;
        string downloads=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),"Downloads");
        return Directory.Exists(downloads)?downloads:Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
    }
    public static Microsoft.Win32.SaveFileDialog Dialog(string originalName){string folder=InitialFolder();return new Microsoft.Win32.SaveFileDialog{InitialDirectory=folder,FileName=FirstAvailableName(folder,originalName),DefaultExt=Path.GetExtension(originalName),AddExtension=true,CheckPathExists=true,ValidateNames=true,Title="Download attachment",OverwritePrompt=false};}
    // Capture the existing file's native identity before asking for consent.
    // A declined prompt keeps Save As open; an absent capture callback is closed.
    public static Selection Choose(Window owner,string originalName,Func<string,string> captureOverwrite=null){
        var dialog=Dialog(originalName);Selection selected=null;
        dialog.FileOk+=(sender,args)=>{
            string path;try{path=ValidatePath(dialog.FileName);}catch(IOException error){MessageBox.Show(owner,error.Message,"Download attachment",MessageBoxButton.OK,MessageBoxImage.Warning);args.Cancel=true;return;}
            if(Directory.Exists(path)){MessageBox.Show(owner,"Choose a filename, not an existing folder.","Download attachment",MessageBoxButton.OK,MessageBoxImage.Warning);args.Cancel=true;return;}
            bool overwrite=File.Exists(path);
            string approval="";
            if(overwrite){
                if(captureOverwrite==null){args.Cancel=true;return;}
                try{approval=captureOverwrite(path);if(String.IsNullOrEmpty(approval)||Convert.FromBase64String(approval).Length==0)throw new InvalidDataException();}
                catch{MessageBox.Show(owner,"The existing file could not be checked safely. Choose another name or try again.","Download attachment",MessageBoxButton.OK,MessageBoxImage.Warning);args.Cancel=true;return;}
                if(MessageBox.Show(owner,"Replace the existing file ‘"+System.IO.Path.GetFileName(path)+"’? The current file will be retained for recovery during replacement.","Confirm overwrite",MessageBoxButton.YesNo,MessageBoxImage.Warning,MessageBoxResult.No)!=MessageBoxResult.Yes){args.Cancel=true;return;}
            }
            selected=new Selection{Path=path,OverwriteApproved=overwrite,Approval=approval};
        };
        if(dialog.ShowDialog(owner)!=true||selected==null)return null;
        lastFolder=Path.GetDirectoryName(selected.Path);return selected;
    }
    static void WriteField(BinaryWriter writer,byte[] bytes){writer.Write((ulong)bytes.Length);writer.Write(bytes);}
    public static string Wire(Selection selection){
        if(selection==null)throw new ArgumentNullException("selection");string path=ValidatePath(selection.Path);
        byte[] approval=String.IsNullOrEmpty(selection.Approval)?new byte[0]:Convert.FromBase64String(selection.Approval);
        if(selection.OverwriteApproved!=(approval.Length>0)||approval.Length>2048)throw new IOException("Download overwrite approval is invalid.");
        using(var stream=new MemoryStream())using(var writer=new BinaryWriter(stream,Encoding.UTF8)){
            writer.Write(new byte[]{80,84,1,30});WriteField(writer,Encoding.UTF8.GetBytes("PeerChat/DownloadSelection/v1"));WriteField(writer,Encoding.UTF8.GetBytes(path));WriteField(writer,approval);
            if(stream.Length>4096)throw new IOException("Download selection is too long.");return Convert.ToBase64String(stream.ToArray());
        }
    }
    static string ValidatePath(string path){
        string full;try{full=Path.GetFullPath(path);}catch(Exception error)when(error is ArgumentException||error is NotSupportedException||error is PathTooLongException){throw new IOException("Choose a valid download filename and folder.");}
        if(!Path.IsPathRooted(path)||!String.Equals(full,path,StringComparison.OrdinalIgnoreCase)||String.IsNullOrEmpty(Path.GetFileName(full))||!Directory.Exists(Path.GetDirectoryName(full)))throw new IOException("Choose a valid download filename and folder.");
        return full;
    }
    public static string Validate(string path){
        string full=ValidatePath(path);
        if(File.Exists(full)||Directory.Exists(full))throw new IOException("A file with this name already exists. Choose a different name; existing files will not be overwritten.");
        return full;
    }
}
// Presentation only. Native owns identity, durable progress and verification.
public sealed partial class CoreAdapter {
    public sealed class GroupAttachmentRecipient {public long account,done;public string device,name,state,error;}
    public sealed class GroupAttachmentData {
        public string id,logicalId,group,name,path,error,senderPeer,state,exportState,exportPath,exportError;
        public long size,transferSize,done,time;
        public bool sending,plaintextVerified,sourceVerified,canRetry,canCancel,canOpen,outputPending,preparationRequired;
        public GroupAttachmentRecipient[] recipients;
    }
    public sealed class GroupAttachmentSnapshot {public bool supported;public string reason,historyCursor,capabilityStage,capabilityCode;public ulong capabilityGeneration;public bool historyMore;public GroupAttachmentData[] items;}
    [DllImport(Library,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)]
    static extern IntPtr ui_group_attachment(IntPtr handle,uint operation,string group,string id,string path);
    public string GroupAttachment(uint operation,string group="",string id="",string path=""){
        if(operation==1||operation==4&&!String.IsNullOrEmpty(path))SelectedTransferFile.ValidateSize(path);
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
    public GroupAttachmentSnapshot GroupAttachmentHistory(string group,string before){
        if(String.IsNullOrEmpty(group)||String.IsNullOrEmpty(before)||!GroupAttachmentHistoryCursor.Valid(before))throw new InvalidDataException("Attachment history cursor unavailable.");
        var page=new JavaScriptSerializer{MaxJsonLength=4*1024*1024}.Deserialize<GroupAttachmentSnapshot>(GroupAttachment(8,group,before));
        if(page==null)throw new InvalidDataException("Attachment history page unavailable.");
        return page;
    }
}
public static class GroupAttachmentHistoryCursor {
    public static bool Valid(string value){ulong parsed;return !String.IsNullOrEmpty(value)&&value[0]!='0'&&value.All(ch=>ch>='0'&&ch<='9')&&UInt64.TryParse(value,out parsed)&&parsed>0&&parsed<=Int64.MaxValue;}
    public static bool Advances(string before,string after){ulong oldValue,newValue;return Valid(before)&&Valid(after)&&UInt64.TryParse(before,out oldValue)&&UInt64.TryParse(after,out newValue)&&newValue<oldValue;}
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
        if(!row.sending&&row.plaintextVerified)return row.exportState=="complete"?"Saved — integrity verified":row.exportState=="failed"?"Verified download — save needs attention":"Verified download — saving";
        if(!row.sending&&row.state=="complete")return "Downloaded — plaintext verification pending";
        return State(row.state,row.sending);
    }
    public static bool Apply(GroupModel group,CoreAdapter.GroupAttachmentData row){
        if(row==null||String.IsNullOrEmpty(row.id)||String.IsNullOrEmpty(LogicalId(row))||row.group!=group.Id||row.size<0||row.transferSize<0||row.done<0)return false;
        var message=group.Messages.FirstOrDefault(item=>(item.GroupAttachment!=null&&LogicalId(item.GroupAttachment)==LogicalId(row))||(item.Data.id==LogicalId(row)&&item.Data.status=="attachment_reference"));
        string revision=new JavaScriptSerializer().Serialize(row);
        if(message!=null){if(message.GroupAttachmentRevision==revision)return false;if(message.Data.status=="attachment_reference"){message.Data.time=row.time;message.Data.logical=row.time;message.Data.status="";message.Data.sender=row.senderPeer??"";message.Outgoing=row.sending;}message.SetGroupAttachment(row,revision);group.Refresh();return true;}
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
        if(!String.IsNullOrEmpty(row.exportError))Children.Add(new TextBlock{Text="Save failed: "+row.exportError,Foreground=Brushes.LightSalmon,TextWrapping=TextWrapping.Wrap});
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
        if(row.canRetry&&row.state!="expired")AddAction(actions,row.preparationRequired?"Prepare again…":row.state=="paused"?"Resume":"Retry","retry");
        if(row.canCancel)AddAction(actions,"Cancel","cancel");
        if(GroupAttachmentProjection.CanRequestOpen(row)&&!row.outputPending)AddAction(actions,"Preview","preview");
        if(!row.sending&&row.plaintextVerified){
            if(row.exportState=="complete"&&!String.IsNullOrWhiteSpace(row.exportPath)&&File.Exists(row.exportPath)){AddAction(actions,"Open File","open-saved");AddAction(actions,"Open Folder","folder-saved");}
            else if(row.exportState!="copying"&&row.exportState!="pending")AddAction(actions,"Retry Save…","retry-save");
        }
    }
}
// A clipboard draft stays in memory until native durably accepts its source.
public sealed class GroupImageDraft {
    public const int MaximumEncodedBytes=16*1024*1024;
    public readonly string Group,Name,Base64;public readonly ulong ProfileGeneration;public readonly BitmapSource Preview;
    GroupImageDraft(string group,ulong profile,string name,string base64,BitmapSource preview){Group=group;ProfileGeneration=profile;Name=name;Base64=base64;Preview=preview;}
    public static GroupImageDraft Create(string group,ulong profile,BitmapSource image){
        string encoded=Convert.ToBase64String(ClipboardAttachments.EncodePng(image));
        var preview=ClipboardAttachments.Preview(image);
        return new GroupImageDraft(group,profile,"Clipboard-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".png",encoded,preview);
    }
    public bool Matches(string group,ulong profile){return Group==group&&ProfileGeneration==profile;}
}
public sealed class GroupFileDraft {
    public sealed class Entry {
        public readonly string Path,Name;public readonly long Size;public readonly DateTime ModifiedUtc;
        internal Entry(string path,FileInfo info){Path=path;Name=info.Name;Size=info.Length;ModifiedUtc=info.LastWriteTimeUtc;}
        public bool Unchanged(){
            try{var info=new FileInfo(Path);return info.Exists&&(info.Attributes&FileAttributes.ReparsePoint)==0&&info.Length==Size&&info.LastWriteTimeUtc==ModifiedUtc&&info.Name==Name;}
            catch{return false;}
        }
    }
    public readonly string Group;public readonly ulong ProfileGeneration;public readonly Entry[] Files;public BitmapSource Preview{get;internal set;}
    GroupFileDraft(string group,ulong profile,Entry[] files){Group=group;ProfileGeneration=profile;Files=files;}
    public static GroupFileDraft Create(string group,ulong profile,string[] paths){
        if(String.IsNullOrEmpty(group)||paths==null||paths.Length==0||paths.Length>4)throw new InvalidDataException("Choose one to four regular files.");
        var entries=new List<Entry>();var seen=new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach(var selected in paths){
            if(String.IsNullOrWhiteSpace(selected))throw new InvalidDataException("A selected file is unavailable.");
            var path=Path.GetFullPath(selected);if(!seen.Add(path))throw new InvalidDataException("A file was selected more than once.");
            var info=new FileInfo(path);
            if(!info.Exists||(info.Attributes&FileAttributes.ReparsePoint)!=0)throw new InvalidDataException("Choose regular files, not links or folders.");
            SelectedTransferFile.ValidateSize(path);entries.Add(new Entry(path,info));
        }
        return new GroupFileDraft(group,profile,entries.ToArray());
    }
    public bool Matches(string group,ulong profile){return Group==group&&ProfileGeneration==profile;}
}
public partial class DesktopApplication {
    GroupAttachmentSnapshotState groupAttachmentState=new GroupAttachmentSnapshotState();
    sealed class GroupAttachmentSnapshotState {public bool Supported;public ulong Profile;public object Owner;public string Reason="Checking group attachment availability…";}
    bool groupAttachmentRefreshing,groupAttachmentOperating,groupImageDraftPreparing;GroupImageDraft groupImageDraft;GroupFileDraft groupFileDraft;int groupFileDraftNext;
    sealed class PendingGroupAttachmentAction {public string Action,Destination,Group;public ulong Profile;public object Owner;public bool Busy;}
    sealed class GroupAttachmentHistoryState {public ulong Profile;public object Owner;public string Cursor;public bool More,Loading;}
    readonly Dictionary<string,GroupAttachmentHistoryState> groupAttachmentHistory=new Dictionary<string,GroupAttachmentHistoryState>();
    GroupAttachmentHistoryState AttachmentHistoryState(string group){
        foreach(var stale in groupAttachmentHistory.Where(pair=>pair.Value.Profile!=profileSessionGeneration||pair.Value.Owner!=discovery).Select(pair=>pair.Key).ToArray())groupAttachmentHistory.Remove(stale);
        GroupAttachmentHistoryState state;
        if(!groupAttachmentHistory.TryGetValue(group,out state)||state.Profile!=profileSessionGeneration||state.Owner!=discovery){
            state=new GroupAttachmentHistoryState{Profile=profileSessionGeneration,Owner=discovery};groupAttachmentHistory[group]=state;
        }
        return state;
    }
    void RefreshGroupAttachmentHistoryButton(){
        var button=Find<Button>("OlderGroupAttachments");
        string selected=model.Groups&&model.SelectedGroup!=null?model.SelectedGroup.Id:null;
        GroupAttachmentHistoryState state=null;
        if(selected!=null)groupAttachmentHistory.TryGetValue(selected,out state);
        bool current=state!=null&&state.Profile==profileSessionGeneration&&state.Owner==discovery;
        button.Visibility=current&&state.More?Visibility.Visible:Visibility.Collapsed;
        button.IsEnabled=current&&state.More&&!state.Loading&&!groupAttachmentOperating;
        button.Content=current&&state.Loading?"Loading older attachments…":"↑    Load older attachments";
    }
    async Task LoadOlderGroupAttachments(){
        if(closing||groupAttachmentRefreshing||groupAttachmentOperating||!model.Groups||model.SelectedGroup==null)return;
        var group=model.SelectedGroup;var state=AttachmentHistoryState(group.Id);
        if(!state.More||state.Loading||!GroupAttachmentHistoryCursor.Valid(state.Cursor))return;
        string before=state.Cursor;ulong generation=profileSessionGeneration;var owner=discovery;
        state.Loading=true;RefreshGroupAttachmentHistoryButton();
        try{
            var page=await Task.Run(()=>core.GroupAttachmentHistory(group.Id,before));
            if(closing||generation!=profileSessionGeneration||owner!=discovery||state!=AttachmentHistoryState(group.Id))return;
            var items=page.items??new CoreAdapter.GroupAttachmentData[0];
            if(items.Length>32||items.Any(row=>row==null||row.group!=group.Id)||page.historyMore&&!GroupAttachmentHistoryCursor.Advances(before,page.historyCursor)||items.Length==0&&page.historyMore)throw new InvalidDataException("Attachment history page is invalid.");
            var viewer=Find<ScrollViewer>("MessageScroll");double offset=viewer.VerticalOffset,extent=viewer.ExtentHeight;
            bool changed=false;foreach(var row in items)changed|=GroupAttachmentProjection.Apply(group,row);
            state.More=page.historyMore;state.Cursor=GroupAttachmentHistoryCursor.Advances(before,page.historyCursor)?page.historyCursor:before;
            if(changed){Refresh();SizeBubbles();window.Dispatcher.BeginInvoke((Action)(()=>{if(!closing&&model.Groups&&model.SelectedGroup==group)viewer.ScrollToVerticalOffset(offset+Math.Max(0,viewer.ExtentHeight-extent));}));}
        }catch(Exception error){if(!closing&&generation==profileSessionGeneration&&owner==discovery)Notify("Older attachments unavailable: "+error.Message);}
        finally{state.Loading=false;if(!closing)RefreshGroupAttachmentHistoryButton();}
    }
    readonly Dictionary<string,PendingGroupAttachmentAction> pendingGroupAttachmentActions=new Dictionary<string,PendingGroupAttachmentAction>();
    bool GroupAttachmentsAvailable(){return groupAttachmentState.Supported&&groupAttachmentState.Profile==profileSessionGeneration&&groupAttachmentState.Owner==discovery;}
    string GroupAttachmentUnavailable(){if(!String.IsNullOrEmpty(groupAttachmentDeviceError))return groupAttachmentDeviceError;return groupAttachmentState.Profile==profileSessionGeneration&&groupAttachmentState.Owner==discovery?groupAttachmentState.Reason:"Group attachments are unavailable for this session.";}
    void BindGroupAttachments(){
        Find<Button>("OlderGroupAttachments").Click+=async(s,e)=>await LoadOlderGroupAttachments();
        window.AddHandler(GroupAttachmentCard.ActionEvent,new RoutedEventHandler(async(s,e)=>{var action=(GroupAttachmentActionEventArgs)e;e.Handled=true;await GroupAttachmentAction(action);}));
        Find<Button>("RemoveGroupImageDraft").Click+=(s,e)=>{if(!groupAttachmentOperating&&!groupImageDraftPreparing&&!directImageDraftPreparing&&!directImageDraftSending){if(model.Groups){groupImageDraft=null;groupFileDraft=null;groupFileDraftNext=0;}else directImageDraft=null;RefreshGroupImageDraft();}};
        window.Closed+=(s,e)=>{groupImageDraft=null;groupFileDraft=null;pendingGroupAttachmentActions.Clear();groupAttachmentHistory.Clear();};
    }
    async Task PrepareGroupClipboardImage(IDataObject data){
            if(model.SelectedGroup==null||model.SelectedGroup.Invited){Notify("Select a group you have joined to paste an image.");return;}
            string group=model.SelectedGroup.Id;ulong profile=profileSessionGeneration;var owner=discovery;
            if(groupAttachmentOperating||groupImageDraftPreparing){Notify("Please wait for the current attachment operation.");return;}
            groupImageDraftPreparing=true;RefreshGroupImageDraft();
            try{
                var image=ClipboardAttachments.ReadImage(data);
                if(!await EnsureGroupAttachmentReady()){Notify(GroupAttachmentUnavailable());return;}
                if(closing||!model.Groups||model.SelectedGroup==null||model.SelectedGroup.Id!=group||model.SelectedGroup.Invited||profile!=profileSessionGeneration||owner!=discovery)return;
                if(groupFileDraft!=null){Notify("A file draft is waiting. Send or remove it before pasting an image.");return;}
                if(groupImageDraft!=null&&!groupImageDraft.Matches(group,profile)){Notify("An image draft is waiting in another group. Send or remove that draft before pasting another image.");return;}
                var draft=await Task.Run(()=>GroupImageDraft.Create(group,profile,image));if(!closing&&profile==profileSessionGeneration&&owner==discovery){groupImageDraft=draft;RefreshGroupImageDraft();}
            }
            catch(Exception error){if(!closing&&profile==profileSessionGeneration&&owner==discovery)Notify("Image draft could not be prepared: "+error.Message);}
            finally{groupImageDraftPreparing=false;if(!closing)RefreshGroupImageDraft();}
    }
    void RefreshGroupImageDraft(){
        if(groupImageDraft!=null&&groupImageDraft.ProfileGeneration!=profileSessionGeneration)groupImageDraft=null;
        if(groupFileDraft!=null&&groupFileDraft.ProfileGeneration!=profileSessionGeneration){groupFileDraft=null;groupFileDraftNext=0;}
        if(directImageDraft!=null&&(directImageDraft.Profile!=profileSessionGeneration||directImageDraft.Owner!=discovery))directImageDraft=null;
        bool imageVisible=groupImageDraft!=null&&model.Groups&&model.SelectedGroup!=null&&groupImageDraft.Matches(model.SelectedGroup.Id,profileSessionGeneration);
        bool fileVisible=groupFileDraft!=null&&model.Groups&&model.SelectedGroup!=null&&groupFileDraft.Matches(model.SelectedGroup.Id,profileSessionGeneration);
        bool directVisible=DirectDraftVisible();bool visible=imageVisible||fileVisible||directVisible;
        Find<Border>("GroupImageDraft").Visibility=visible?Visibility.Visible:Visibility.Collapsed;
        Find<Image>("GroupImageDraftPreview").Source=imageVisible?groupImageDraft.Preview:fileVisible?groupFileDraft.Preview:directVisible?directImageDraft.Preview:null;
        Find<TextBlock>("GroupImageDraftName").Text=imageVisible?groupImageDraft.Name+" · Image draft — Send to share":fileVisible?String.Join("\n",groupFileDraft.Files.Skip(groupFileDraftNext).Select(file=>file.Name+" · "+file.Size.ToString("N0")+" bytes"))+"\nFile draft — Send to share":directVisible?directImageDraft.Name+" · Image draft — Send to share":"";
        Find<Button>("RemoveGroupImageDraft").IsEnabled=!groupAttachmentOperating&&!groupImageDraftPreparing&&!directImageDraftPreparing&&!directImageDraftSending;
    }
    async Task<bool> SendGroupImageDraft(){
        if(model.Groups&&groupImageDraftPreparing)return true;
        var files=groupFileDraft;
        if(files!=null&&model.Groups&&model.SelectedGroup!=null&&files.Matches(model.SelectedGroup.Id,profileSessionGeneration)){
            if(groupAttachmentOperating)return true;
            while(groupFileDraft==files&&groupFileDraftNext<files.Files.Length){
                var entry=files.Files[groupFileDraftNext];
                if(!entry.Unchanged()){Notify("The selected file changed. Remove this draft and select it again.");return true;}
                int index=groupFileDraftNext;
                if(!await GroupAttachmentOperation(1,files.Group,path:entry.Path,accepted:result=>{if(groupFileDraft==files&&groupFileDraftNext==index&&!String.IsNullOrEmpty(result))groupFileDraftNext++;}))return true;
                if(groupFileDraft==files)RefreshGroupImageDraft();
            }
            if(groupFileDraft==files){groupFileDraft=null;groupFileDraftNext=0;RefreshGroupImageDraft();}
            return true;
        }
        var draft=groupImageDraft;if(draft==null||!model.Groups||model.SelectedGroup==null||!draft.Matches(model.SelectedGroup.Id,profileSessionGeneration))return false;
        if(groupAttachmentOperating)return true;
        if(await GroupAttachmentOperation(7,draft.Group,draft.Name,draft.Base64))if(groupImageDraft==draft){groupImageDraft=null;RefreshGroupImageDraft();}
        return true;
    }
    async Task<bool> GroupAttachmentOperation(uint operation,string group,string id="",string path="",Action<string> accepted=null){
        if(groupAttachmentOperating){Notify("Please wait for the current attachment operation.");return false;}
        ulong generation=profileSessionGeneration;var owner=discovery;
        groupAttachmentOperating=true;
        try{if(operation==1||operation==7){if(!await EnsureGroupAttachmentReady()){Notify(GroupAttachmentUnavailable());return false;}if(closing||generation!=profileSessionGeneration||owner!=discovery||!model.Groups||model.SelectedGroup==null||model.SelectedGroup.Id!=group)return false;}string result=await Task.Run(()=>core.GroupAttachment(operation,group,id,path));if(accepted!=null)accepted(result);return !closing&&generation==profileSessionGeneration&&owner==discovery;}
        catch(FileTooLargeException){Notify(FileTooLargeException.Explanation);return false;}
        catch(Exception error){if(!closing&&generation==profileSessionGeneration&&owner==discovery)Notify(error.Message);return false;}
        finally{groupAttachmentOperating=false;if(!closing){RefreshGroupImageDraft();await RefreshGroupAttachments();}}
    }
    async Task ChooseGroupFiles(){
        var group=model.SelectedGroup;if(group==null||group.Invited){Notify("Select a group you have joined.");return;}
        ulong generation=profileSessionGeneration;var owner=discovery;
        if(!await EnsureGroupAttachmentReady()){Notify(GroupAttachmentUnavailable());return;}
        if(closing||generation!=profileSessionGeneration||owner!=discovery||!model.Groups||model.SelectedGroup!=group)return;
        var choose=new Microsoft.Win32.OpenFileDialog{Title="Attach files to "+group.Name,CheckFileExists=true,Multiselect=true};
        if(choose.ShowDialog(window)==true&&generation==profileSessionGeneration&&model.Groups&&model.SelectedGroup==group)await SendGroupFiles(group,choose.FileNames);
    }
    async Task SendGroupFiles(GroupModel group,string[] files){
        if(group==null||group.Invited||files==null||files.Length==0||files.Length>4){Notify("Choose up to four regular files for a group you have joined.");return;}
        ulong generation=profileSessionGeneration;var owner=discovery;
        if(groupAttachmentOperating||groupImageDraftPreparing){Notify("Please wait for the current attachment operation.");return;}
        if(groupFileDraft!=null||groupImageDraft!=null){Notify("An attachment draft is waiting. Send or remove it before selecting another file.");return;}
        if(!await EnsureGroupAttachmentReady()){Notify(GroupAttachmentUnavailable());return;}
        if(closing||generation!=profileSessionGeneration||owner!=discovery||!model.Groups||model.SelectedGroup!=group)return;
        try{
            var draft=GroupFileDraft.Create(group.Id,generation,files);
            if(closing||generation!=profileSessionGeneration||owner!=discovery||!model.Groups||model.SelectedGroup!=group)return;
            groupFileDraft=draft;groupFileDraftNext=0;RefreshGroupImageDraft();
            groupImageDraftPreparing=true;
            try{
                foreach(var entry in draft.Files){
                    var preview=await SafeAttachmentPreview.ReadAsync(entry.Path,entry.Size);
                    if(closing||generation!=profileSessionGeneration||owner!=discovery||groupFileDraft!=draft)return;
                    if(preview.Image!=null){draft.Preview=preview.Image;RefreshGroupImageDraft();break;}
                }
            }finally{groupImageDraftPreparing=false;if(!closing)RefreshGroupImageDraft();}
        }catch(FileTooLargeException){Notify(FileTooLargeException.Explanation);}
        catch(Exception error){Notify("File draft unavailable: "+error.Message);}
    }
    async Task GroupAttachmentAction(GroupAttachmentActionEventArgs action){
        var row=action.Attachment;if(row==null)return;
        if(!model.Groups||model.SelectedGroup==null||model.SelectedGroup.Id!=row.group)return;
        if(action.Action=="cancel"){await GroupAttachmentOperation(3,row.group,row.id);return;}
        if(action.Action=="retry"){
            if(row.preparationRequired){
                var retryGeneration=profileSessionGeneration;var retryOwner=discovery;
                var choose=new Microsoft.Win32.OpenFileDialog{FileName=row.name,Title="Select the original file to prepare again",CheckFileExists=true};
                if(choose.ShowDialog(window)!=true)return;
                if(closing||retryGeneration!=profileSessionGeneration||retryOwner!=discovery||!model.Groups||model.SelectedGroup==null||model.SelectedGroup.Id!=row.group)return;
                try{
                    SelectedTransferFile.ValidateSize(choose.FileName);
                    var info=new FileInfo(choose.FileName);
                    if(!info.Exists||(info.Attributes&FileAttributes.ReparsePoint)!=0||info.Name!=row.name||info.Length!=row.size)throw new InvalidDataException("Choose the same original file name and size to prepare again.");
                }catch(FileTooLargeException){Notify(FileTooLargeException.Explanation);return;}
                catch(Exception error){Notify(error.Message);return;}
                await GroupAttachmentOperation(4,row.group,row.id,choose.FileName);
            }else await GroupAttachmentOperation(4,row.group,row.id);
            return;
        }
        if(action.Action=="accept"||action.Action=="retry-save"){
            var selectionOwner=discovery;ulong selectionProfile=profileSessionGeneration;
            var chosen=AttachmentDownloadTarget.Choose(window,row.name,path=>core.FileTransfer(16,"","",path));if(chosen==null)return;
            if(closing||selectionOwner!=discovery||selectionProfile!=profileSessionGeneration||!model.Groups||model.SelectedGroup==null||model.SelectedGroup.Id!=row.group)return;
            try{
                uint operation=action.Action=="retry-save"?11u:chosen.OverwriteApproved?10u:AttachmentDownloadTarget.GroupOperation;
                string target=operation==AttachmentDownloadTarget.GroupOperation?AttachmentDownloadTarget.Validate(chosen.Path):AttachmentDownloadTarget.Wire(chosen);
                await GroupAttachmentOperation(operation,row.group,row.id,target);
            }catch(IOException error){Notify(error.Message);}
            return;
        }
        if(action.Action=="open-saved"||action.Action=="folder-saved"){
            if(row.exportState!="complete"||String.IsNullOrWhiteSpace(row.exportPath)||!File.Exists(row.exportPath)){Notify("Saved attachment is unavailable. Use Retry Save to restore it.");return;}
            try{if(action.Action=="folder-saved")System.Diagnostics.Process.Start("explorer.exe","/select,\""+row.exportPath+"\"");else System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(row.exportPath){UseShellExecute=true});}
            catch{Notify("Saved attachment is unavailable. Use Retry Save to restore it.");}
            return;
        }
        if(!GroupAttachmentProjection.CanRequestOpen(row)){Notify("The local attachment is not available for verification.");return;}
        if(pendingGroupAttachmentActions.ContainsKey(row.id)){Notify("This attachment is being verified.");return;}
        var generation=profileSessionGeneration;var owner=discovery;
        try{
            string destination="";
            if(closing||generation!=profileSessionGeneration||owner!=discovery||!model.Groups||model.SelectedGroup==null||model.SelectedGroup.Id!=row.group)return;
            var pending=new PendingGroupAttachmentAction{Action=action.Action,Destination=destination,Group=row.group,Profile=generation,Owner=owner,Busy=true};
            pendingGroupAttachmentActions[row.id]=pending;
            string path=await Task.Run(()=>core.GroupAttachment(6,row.group,row.id,destination));
            pending.Busy=false;
            if(closing||generation!=profileSessionGeneration||owner!=discovery)return;
            if(String.IsNullOrEmpty(path)){Notify("Verifying local attachment…");return;}
            pendingGroupAttachmentActions.Remove(row.id);
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
            if(pending.Busy)continue;
            var row=rows.FirstOrDefault(item=>item!=null&&item.id==pair.Key&&item.group==pending.Group);if(row==null)continue;
            try{
                pending.Busy=true;
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
            finally{pending.Busy=false;}
        }
    }
    async Task RefreshGroupAttachments(){
        if(closing||groupAttachmentRefreshing||groupAttachmentOperating||!model.Listening)return;groupAttachmentRefreshing=true;
        var owner=discovery;ulong generation=profileSessionGeneration;
        try{
            string selected=model.Groups&&model.SelectedGroup!=null?model.SelectedGroup.Id:"";
            var snapshot=await Task.Run(()=>core.GroupAttachments(selected));if(closing||generation!=profileSessionGeneration||owner!=discovery)return;
            RecordAttachmentCapability(snapshot);
            if(!String.IsNullOrEmpty(selected)){
                var history=AttachmentHistoryState(selected);
                if(history.Cursor==null){
                    if(snapshot.historyMore&&!GroupAttachmentHistoryCursor.Valid(snapshot.historyCursor))throw new InvalidDataException("Attachment history cursor unavailable.");
                    history.Cursor=snapshot.historyCursor??"";history.More=snapshot.historyMore;
                }
            }
            groupAttachmentState=new GroupAttachmentSnapshotState{Supported=snapshot.supported,Profile=generation,Owner=owner,Reason=String.IsNullOrEmpty(snapshot.reason)?"Group attachments are unavailable on this connection.":snapshot.reason};
            bool changed=false;foreach(var row in snapshot.items??new CoreAdapter.GroupAttachmentData[0]){GroupModel group;if(row!=null&&row.group!=null&&model.GroupChats.TryGetValue(row.group,out group))changed|=GroupAttachmentProjection.Apply(group,row);}
            if(changed){Refresh();SizeBubbles();}
            await ContinueGroupAttachmentActions(snapshot.items??new CoreAdapter.GroupAttachmentData[0]);
        }catch{if(generation==profileSessionGeneration&&owner==discovery)groupAttachmentState=new GroupAttachmentSnapshotState{Profile=generation,Owner=owner,Reason="Group attachment status unavailable. Reconnect and try again."};}
        finally{groupAttachmentRefreshing=false;if(!closing)RefreshGroupAttachmentHistoryButton();}
    }
}
}
