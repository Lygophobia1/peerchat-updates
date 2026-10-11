using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections.Generic;
using System.Security.Cryptography;
using System.Web.Script.Serialization;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Automation;
using System.Windows.Media;

namespace PeerChatDesktop {
// Cosmetic metadata only. Account + exact identity, never nickname, is the key.
public sealed class HistoryDeviceNames {
    readonly string path; readonly JavaScriptSerializer json=new JavaScriptSerializer();
    public HistoryDeviceNames(string profile){path=Path.Combine(profile,"history-device-names.dat");}
    static bool ValidScope(long account,string peer){return account>0&&peer!=null&&peer.Length==64&&peer.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f');}
    static string Key(long account,string peer){if(!ValidScope(account,peer))throw new InvalidDataException("Invalid device metadata scope");return account+":"+peer;}
    public bool DetailsUnavailable {get;private set;}
    Dictionary<string,string> Load(bool strict=false){try{if(!File.Exists(path)){DetailsUnavailable=false;return new Dictionary<string,string>();}if(new FileInfo(path).Length>262144)throw new InvalidDataException();var rows=json.Deserialize<Dictionary<string,string>>(Encoding.UTF8.GetString(ProtectedData.Unprotect(File.ReadAllBytes(path),Encoding.UTF8.GetBytes("PeerChat/HistoryNames/v1"),DataProtectionScope.CurrentUser)))??new Dictionary<string,string>();DetailsUnavailable=false;return rows;}catch{DetailsUnavailable=true;if(strict)throw new InvalidDataException("History device details unavailable; existing metadata was not changed.");return new Dictionary<string,string>();}}
    // Display metadata is optional. An unset account during Settings startup is
    // not an authorized scope and must never read another account's nicknames.
    public string Get(long account,string peer,string fallback){if(!ValidScope(account,peer))return fallback;string value;return Load().TryGetValue(Key(account,peer),out value)&&Valid(value)?value:fallback;}
    static bool Valid(string value){return !String.IsNullOrWhiteSpace(value)&&value.Length<=64&&!value.Any(Char.IsControl);}
    public void Set(long account,string peer,string value){if(!Valid(value))throw new InvalidDataException("Nickname must contain 1–64 printable characters.");var key=Key(account,peer);var rows=Load(true);if(!rows.ContainsKey(key)&&rows.Count>=256)throw new InvalidDataException("Nickname limit reached");rows[key]=value.Trim();Directory.CreateDirectory(Path.GetDirectoryName(path));var bytes=ProtectedData.Protect(Encoding.UTF8.GetBytes(json.Serialize(rows)),Encoding.UTF8.GetBytes("PeerChat/HistoryNames/v1"),DataProtectionScope.CurrentUser);string temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";File.WriteAllBytes(temp,bytes);if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);}
}
public sealed class HistoryProgressView {
    public string State,Direction,Counters;public bool Active,Indeterminate;public double Percent;
    // No current native reliable total exists. Optional total is for future
    // authoritative snapshots, never estimated from bytes/cursors/time.
    internal static HistoryProgressView Create(ArchiveEnrollmentExecution.SyncProgress p,string source,string[] providers,long? reliableTotal=null){
        var value=new HistoryProgressView{State="Authorization required",Direction="History authorization needs attention.",Counters=""};
        if(p==null)return value;
        string raw=(p.State??"").ToLowerInvariant();
        value.State=raw.Contains("failed")||raw.Contains("rejected")?"Failed":raw.Contains("authorization")?"Authorization required":raw.Contains("off")?"Permission disabled":raw.Contains("paused")?"Paused":p.Complete?"Complete":raw.Contains("unavailable")||raw.Contains("timed out")||raw.Contains("waiting")?"Waiting for device":raw.Contains("connecting")?"Connecting":raw.Contains("synchronizing")?"Downloading history":"Idle";
        value.Direction=value.State=="Downloading history"?"↓ Downloading history from "+source:value.State=="Connecting"?"Connecting to "+source:value.State=="Waiting for device"?"Waiting for "+source:value.State=="Complete"?"Up to date — last sync completed":value.State=="Authorization required"?"History authorization needs attention.":value.State;
        value.Active=p.Running&&(value.State=="Downloading history"||value.State=="Connecting"||value.State=="Waiting for device");
        value.Counters=p.Received+" new messages received this session · "+p.Conversations+" conversations completed";
        value.Counters+="\nLast successful sync: "+(p.LastSuccessful==0?"Not recorded":new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(p.LastSuccessful).ToLocalTime().ToString("g"));
        if(providers!=null&&providers.Length>0){value.Active=true;string serving="↑ Providing history to "+String.Join(", ",providers);value.Direction=p.Running?value.Direction+"\n"+serving:serving;value.State=p.Running?value.State:"Providing history";if(!p.Running)value.Counters="Providing session active; sent-message totals are not exposed.";}
        value.Indeterminate=value.Active&&(!reliableTotal.HasValue||providers!=null&&providers.Length>0);
        if(reliableTotal.HasValue&&reliableTotal.Value>0&&p.Processed<=reliableTotal.Value)value.Percent=100.0*p.Processed/reliableTotal.Value;else if(value.Active)value.Indeterminate=true;
        return value;
    }
}
public partial class DesktopApplication {
    ArchiveEnrollmentExecution.Device[] historyDevices=new ArchiveEnrollmentExecution.Device[0];
    string[] historyProviding=new string[0];bool historySettingsOpen;
    long historyAccount;ulong historyGeneration;
    HistoryDeviceNames historyNames;
    HistoryDeviceNames HistoryNames {get{return historyNames??(historyNames=new HistoryDeviceNames(directory));}}
    string HistoryName(long account,ArchiveEnrollmentExecution.Device device){
        int index=Array.FindIndex(historyDevices,d=>d.PeerId==device.PeerId);
        string name=HistoryNames.Get(account,device.PeerId,device.Local?"This PC":"History device "+(index+1));
        // Duplicate friendly names do not collapse targets or replace identity.
        if(historyDevices.Count(d=>HistoryNames.Get(account,d.PeerId,d.Local?"This PC":"History device "+(Array.IndexOf(historyDevices,d)+1))==name)>1)name+=" · device "+(index+1);
        return name;
    }
    string HistoryPeerName(string peer){if(String.IsNullOrEmpty(peer))return "No source selected";var d=historyDevices.FirstOrDefault(item=>item.PeerId==peer);return d==null?"Device "+peer.Substring(0,Math.Min(12,peer.Length))+"…":HistoryName(historyAccount,d);}
    HistoryProgressView CurrentHistoryView(){return HistoryProgressView.Create(archiveProgress,HistoryPeerName(archiveProgress==null?"":archiveProgress.Source),historyProviding.Select(HistoryPeerName).ToArray());}
    void BindHistoryIndicator(){
        var parent=Find<Button>("Settings").Parent as StackPanel;if(parent==null)return;
        var indicator=new Button{Visibility=Visibility.Collapsed,FontSize=11,HorizontalContentAlignment=HorizontalAlignment.Left,Padding=new Thickness(6),ToolTip="Open History Sync",Foreground=Brush("#83CCFC")};
        AutomationProperties.SetName(indicator,"History sync activity");parent.Children.Insert(0,indicator);indicator.Click+=(s,e)=>SettingsPage("Privacy & Security");
        HistoryPresentationChanged+=(s,e)=>{bool current=historyAccount>0&&discovery!=null&&historyAccount==discovery.AccountId&&historyGeneration==authenticatedFriendSnapshotGeneration;var view=current?CurrentHistoryView():null;indicator.Visibility=view!=null&&view.Active?Visibility.Visible:Visibility.Collapsed;indicator.Content=view==null?"":view.Direction;};
    }
    StackPanel HistorySettingsPanel(Window dialog){
        var panel=new StackPanel();var sync=VoiceProcessingSwitch("History Sync",false);sync.IsThreeState=true;sync.IsChecked=null;sync.IsEnabled=false;
        panel.Children.Add(sync);panel.Children.Add(HistoryText("Automatically keep history up to date across approved devices. Existing custom permissions and automatic-sync preferences are preserved."));
        var localPanel=new StackPanel();var remotePanel=new StackPanel();
        var status=HistoryText("History authorization needs attention. Sign in to load this account's devices.");AutomationProperties.SetName(status,"History sync status");
        var counters=HistoryText("");var bar=new ProgressBar{Height=6,Maximum=100,Visibility=Visibility.Collapsed,Margin=new Thickness(0,6,0,6)};AutomationProperties.SetName(bar,"History sync progress");
        var actions=new WrapPanel();panel.Children.Add(HistoryTitle("SYNC STATUS"));panel.Children.Add(status);panel.Children.Add(bar);panel.Children.Add(counters);panel.Children.Add(actions);
        panel.Children.Add(HistoryTitle("THIS DEVICE"));panel.Children.Add(localPanel);panel.Children.Add(HistoryTitle("OTHER DEVICES"));panel.Children.Add(remotePanel);
        var advancedPanel=new StackPanel();var advanced=new Expander{Header="Advanced history security",Content=advancedPanel,IsExpanded=false,Margin=new Thickness(0,16,0,0),Foreground=Brush("#EDF1F5")};panel.Children.Add(advanced);
        var raw=HistoryText("");advancedPanel.Children.Add(raw);
        bool cachedCurrent=discovery!=null&&archiveLoadedOwner==discovery&&historyAccount==discovery.AccountId&&historyGeneration==authenticatedFriendSnapshotGeneration;
        long account=cachedCurrent?historyAccount:0;ulong generation=cachedCurrent?historyGeneration:0;bool updating=false,operating=false;bool? desiredSync=null;ArchiveEnrollmentExecution.Device[] renderedRows=null;
        var automatic=new Dictionary<string,bool>();Action render=null;Func<uint,ArchiveEnrollmentExecution.Device,long,bool?,Task<bool>> run=null;
        Action progress=()=>{var view=CurrentHistoryView();status.Text="Phase: "+view.State+"\nSource: "+HistoryPeerName(archiveProgress==null?"":archiveProgress.Source)+"\n"+view.Direction;counters.Text=view.Counters;bar.Visibility=view.Active?Visibility.Visible:Visibility.Collapsed;bar.IsIndeterminate=view.Indeterminate;bar.Value=view.Percent;if(archiveProgress!=null){updating=true;sync.IsThreeState=false;sync.IsChecked=archiveProgress.Enabled;sync.IsEnabled=true;updating=false;}raw.Text=(archiveProgress==null?"No validated snapshot.":archiveProgress.Summary)+"\nAccount: "+account+"\nAuthentication snapshot: "+generation+"\n"+archiveAuthorizationDiagnostic;};
        run=async(action,target,capabilities,auto)=>{
            var owner=discovery;long current=owner==null?0:owner.AccountId;ulong revision=authenticatedFriendSnapshotGeneration;string local=model.OwnId;
            if(operating||historyCommandInFlight)return false;
            if(closing||owner==null||current<=0||mailboxContextAccount!=current||revision==0||revision>Int64.MaxValue){status.Text="Sign in and establish authenticated server presence first.";return false;}
            if(action!=0&&(account!=current||generation!=revision)){status.Text="Account context changed. Wait for device verification before making changes.";return false;}
            if(action==5&&capabilities==0){status.Text="At least one history permission is required while this device is authorized. Revoke the device if you want to disable history access completely.";return false;}
            if(action>=4&&action<=7&&!Confirm("Confirm history device access",HistoryName(current,target)+"\nAccount "+current+"\nDevice identity:\n"+target.PeerId+"\n\n"+(action==4?"Trust this device to sign history permissions?":action==5?"Apply Receive / Provide permissions to this exact device?":action==6?"Revoke this device's history access?":"Disable this trusted history authorizer?")+"\nThis does not erase history already received.","Confirm"))return false;
            operating=historyCommandInFlight=true;long operationEpoch=++archiveUiEpoch;panel.IsEnabled=false;
            try{
                var result=await Task.Run(()=>{
                    var execution=new ArchiveEnrollmentExecution((op,input)=>core.Archive(op,input),(op,fields)=>owner.Archive(op,local,fields));execution.Refresh(current,(long)revision,local);
                    if(action>=4&&action<=7)execution.Consent(current,(long)revision,action,target.PeerId,capabilities,true);
                    if(auto.HasValue){if(!auto.Value&&archiveProgress!=null&&archiveProgress.Running&&(target.Local||archiveProgress.Source==target.PeerId))execution.PauseSync(current,(long)revision);execution.AutomaticSync(current,(long)revision,target.PeerId,auto);}
                    bool enabled=execution.HistorySync(current,(long)revision,action==11?desiredSync:null);execution.PublishPending(current,(long)revision,local);
                    try{if(action==12)execution.StartSync(current,(long)revision,target.PeerId,true);if(action==14)execution.PauseSync(current,(long)revision);}
                    finally{var admission=execution.AuthorizationDiagnostics(current,(long)revision);dialog.Dispatcher.BeginInvoke((Action)(()=>{if(discovery==owner&&authenticatedFriendSnapshotGeneration==revision&&operationEpoch==archiveUiEpoch&&dialog.IsVisible)archiveAuthorizationDiagnostic=admission;}));}
                    var rows=execution.SnapshotDevices(current,(long)revision);var preferences=rows.ToDictionary(d=>d.PeerId,d=>execution.AutomaticSync(current,(long)revision,d.PeerId));
                    return new {Rows=rows,Preferences=preferences,Enabled=enabled,Progress=execution.Progress(current,(long)revision),Providing=execution.Providing(current,(long)revision)};
                });
                if(!IsHistoryCallbackCurrent(owner,revision,operationEpoch)||!dialog.IsVisible||!panel.IsVisible)return false;
                account=historyAccount=current;generation=historyGeneration=revision;archiveLoadedOwner=owner;archiveLoadedGeneration=revision;historyDevices=result.Rows;historyProviding=result.Providing;automatic=result.Preferences;archiveProgress=result.Progress;archiveMetadataUtc=DateTime.UtcNow.AddSeconds(10);
                updating=true;sync.IsThreeState=false;sync.IsChecked=result.Enabled;sync.IsEnabled=true;updating=false;render();PublishHistoryPresentation();return true;
            }catch(Exception error){if(discovery==owner&&authenticatedFriendSnapshotGeneration==revision&&operationEpoch==archiveUiEpoch&&panel.IsVisible){sync.IsEnabled=false;status.Text="History operation failed. Retry without changing device permissions.";raw.Text="Operation rejected: "+error.GetType().Name;TraceUi("history settings operation rejected: "+error.GetType().Name);}return false;}
            finally{operating=historyCommandInFlight=false;panel.IsEnabled=true;archiveNextUtc=DateTime.MinValue;}
        };
        Action<ArchiveEnrollmentExecution.Device,bool> manage=(device,setup)=>{
            var body=new StackPanel{Margin=new Thickness(18)};string originalPeer=device.PeerId;long originalAccount=account;ulong originalGeneration=generation;
            var focus=new Window{Owner=dialog,Title=setup?"New history device":HistoryName(account,device),Width=510,Height=590,MinWidth=440,MinHeight=480,Background=Brush("#0C1116"),Foreground=Brush("#EDF1F5"),Resources=window.Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,Content=new ScrollViewer{Content=body,VerticalScrollBarVisibility=ScrollBarVisibility.Auto}};
            body.Children.Add(HistoryTitle(setup?"New history device":HistoryName(account,device)));
            if(setup)body.Children.Add(HistoryText("Device: "+device.PeerId.Substring(0,12)+"… Compare the full identity in Advanced before authorizing."));
            body.Children.Add(HistoryText("Nickname"));var nickname=new TextBox{Text=HistoryNames.Get(account,device.PeerId,device.Local?"This PC":"New history device"),MaxLength=64,Margin=new Thickness(0,4,0,12)};body.Children.Add(nickname);
            var receive=VoiceProcessingSwitch("Receive history",setup||device.GrantedRead);var provide=VoiceProcessingSwitch("Provide history",setup||device.GrantedServe);var autoSwitch=VoiceProcessingSwitch("Sync automatically",!automatic.ContainsKey(device.PeerId)||automatic[device.PeerId]);
            autoSwitch.ToolTip="Automatic initial downloads from this device on this profile. OFF pauses its active download. Existing explicit Pause remains paused until Sync now.";
            body.Children.Add(HistoryText(setup?"Approve this exact device to share history. Review custom permissions in Advanced before authorizing.":"Existing verified permissions are preserved. Custom access controls are in Advanced."));var message=HistoryText(device.AuthorizationState+" · "+CurrentHistoryView().State);body.Children.Add(message);
            var controls=new WrapPanel();body.Children.Add(controls);var detail=new StackPanel();detail.Children.Add(HistoryText("Peer ID: "+device.PeerId+"\nAuthorization: "+device.AuthorizationState+"\nTrusted authorizer: "+device.ApprovedIssuer+"\nGrants remain verified by the native history owner."));
            detail.Children.Insert(0,autoSwitch);detail.Children.Insert(0,provide);detail.Children.Insert(0,receive);
            detail.Children.Add(HistoryButton("Trust history authorizer",async()=>{if(await run(4,device,0,null))message.Text="Authorizer trust updated.";}));detail.Children.Add(HistoryButton("Disable authorizer",async()=>{if(await run(7,device,0,null))message.Text="Authorizer disabled.";}));body.Children.Add(new Expander{Header="Advanced",Content=detail,IsExpanded=false});
            Func<bool> same=()=>account==originalAccount&&generation==originalGeneration&&historyDevices.Any(d=>d.PeerId==originalPeer);
            Action revoke=async()=>{if(!same()){message.Text="Account/device context changed; reopen Manage.";return;}if(await run(6,device,0,null))focus.Close();};
            bool restoring=false;
            RoutedEventHandler lastPermission=(s,e)=>{if(restoring||receive.IsChecked==true||provide.IsChecked==true)return;restoring=true;((CheckBox)s).IsChecked=true;restoring=false;message.Text="At least one history permission is required while this device is authorized. Revoke the device if you want to disable history access completely.";if(!setup&&Confirm("History permission required",message.Text,"Revoke device"))revoke();};
            receive.Click+=lastPermission;provide.Click+=lastPermission;
            controls.Children.Add(HistoryButton("Cancel",()=>focus.Close()));
            controls.Children.Add(HistoryButton(setup?"Authorize device":"Save changes",async()=>{
                if(!same()){message.Text="Account/device context changed; reopen this dialog.";return;}
                long caps=(receive.IsChecked==true?1:0)|(provide.IsChecked==true?2:0);if(caps==0){message.Text="At least one history permission is required.";return;}
                try{HistoryNames.Set(account,device.PeerId,nickname.Text);}catch(Exception error){message.Text=error.Message;return;}
                bool changed=setup||receive.IsChecked!=device.GrantedRead||provide.IsChecked!=device.GrantedServe;
                focus.IsEnabled=false;bool ok=await run(changed?5u:0u,device,caps,autoSwitch.IsChecked==true);focus.IsEnabled=true;if(ok)focus.Close();else message.Text=status.Text;
            }));
            // Aggregate ON switches may describe a different issuer's grant.
            // A separate explicit action can establish the requested local-issuer
            // grant even when the displayed permissions have not changed. Never
            // issue it on open, refresh, ordinary Save or automatic reconciliation.
            bool localAuthorizer=historyDevices.Any(d=>d.Local&&d.ApprovedIssuer);
            detail.Children.Add(HistoryText("ON permissions may have been granted by another authorizer. Saving unchanged switches does not create a grant signed by this device."));
            if(localAuthorizer)detail.Children.Add(HistoryButton("Authorize selected permissions from this device",async()=>{
                if(!same()){message.Text="Account/device context changed; reopen Manage.";return;}
                long caps=(receive.IsChecked==true?1:0)|(provide.IsChecked==true?2:0);
                if(caps==0){message.Text="At least one history permission is required.";return;}
                focus.IsEnabled=false;bool ok=await run(5,device,caps,null);focus.IsEnabled=true;if(ok)focus.Close();else message.Text=status.Text;
            }));
            else body.Children.Add(HistoryText("This device is not an enabled trusted authorizer. No grant will be issued and disabled trust will not be restored automatically."));
            if(!device.Local)controls.Children.Add(HistoryButton("Sync now",async()=>{if(same()&&await run(12,device,0,null))focus.Close();}));
            if(!setup)detail.Children.Add(HistoryButton("Revoke device",revoke));focus.ShowDialog();
        };
        render=()=>{
            renderedRows=historyDevices;localPanel.Children.Clear();remotePanel.Children.Clear();
            // Cached rows belong to the last authenticated snapshot, not the
            // new panel's initially unset account. Wait for run() to bind it.
            bool bound=account>0&&account==historyAccount&&generation==historyGeneration;
            foreach(var d in bound?historyDevices:new ArchiveEnrollmentExecution.Device[0]){var card=new StackPanel{Margin=new Thickness(8)};card.Children.Add(HistoryTitle(HistoryName(account,d)));card.Children.Add(HistoryText((d.Local?"Windows · This device":"Platform unknown")+" · "+(d.Local?"Authenticated server session":archiveProgress!=null&&archiveProgress.Source==d.PeerId&&archiveProgress.DisplayState=="Syncing"?"Available":"Availability unknown")+"\n"+d.AuthorizationState));
                card.Children.Add(HistoryText((d.MayRead||d.MayServe?"Verified history access":d.Authorized?"Registered grant · review trust and custom permissions":"Approval required before history can be exchanged")+(d.Automatic?"":" · Automatic sync paused in custom settings")));
                var row=new WrapPanel();row.Children.Add(HistoryButton(d.Authorized?"Manage":"Authorize device",()=>manage(d,!d.Authorized)));if(!d.Local&&d.MayServe){var now=HistoryButton("Sync now",async()=>await run(12,d,0,null));now.IsEnabled=sync.IsChecked==true&&!(archiveProgress!=null&&archiveProgress.Running);row.Children.Add(now);}card.Children.Add(row);
                (d.Local?localPanel:remotePanel).Children.Add(new Border{Child=card,BorderBrush=Brush("#28343F"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(6),Margin=new Thickness(0,5,0,5)});
            }
            if(localPanel.Children.Count==0)localPanel.Children.Add(HistoryText("Sign in to verify this device."));if(remotePanel.Children.Count==0)remotePanel.Children.Add(HistoryText("No other history devices registered. Open History Sync on the other device to register its public identity; this does not authorize it."));if(account>0)progress();
            if(HistoryNames.DetailsUnavailable){status.Text="History device details unavailable. Retry in Advanced history security; device identities and permissions are unchanged.";raw.Text+="\nOptional protected nickname metadata could not be read.";TraceUi("history optional nickname metadata unavailable");}
        };
        actions.Children.Add(HistoryButton("Add device",()=>{var pending=historyDevices.FirstOrDefault(d=>!d.Local&&!d.Authorized);if(pending==null){status.Text="No new registered device. Open History Sync on the other device; authorization is still required.";return;}manage(pending,true);}));
        var retry=HistoryButton("Retry",async()=>{var source=historyDevices.FirstOrDefault(d=>archiveProgress!=null&&d.PeerId==archiveProgress.Source&&d.MayServe&&!d.Local)??historyDevices.FirstOrDefault(d=>d.MayServe&&!d.Local);await run(source==null?0u:12u,source,0,null);});actions.Children.Add(retry);
        advancedPanel.Children.Add(HistoryButton("Pause",async()=>await run(14,null,0,null)));
        advancedPanel.Children.Add(HistoryButton("Refresh history devices",async()=>await run(0,null,0,null)));
        advancedPanel.Children.Add(HistoryText("Trust must be explicitly established in a device's Manage / Advanced section. Login and nicknames never grant access. Multiple authorities may grant different permissions; Manage changes only grants issued by this device."));
        var files=new StackPanel();var attachments=new ComboBox{MinHeight=32,Margin=new Thickness(0,6,0,6)};AutomationProperties.SetName(attachments,"Historical attachment selector");files.Children.Add(attachments);long cursor=0;var next=new Button{Content="Next attachment page",IsEnabled=false};
        Func<bool,Task> load=async reset=>{try{var page=await Task.Run(()=>core.ArchiveAttachments(reset?0:cursor));if(!dialog.IsVisible)return;attachments.ItemsSource=page.records;attachments.SelectedIndex=page.records.Length>0?0:-1;cursor=page.next;next.IsEnabled=!page.complete;}catch{status.Text="Historical attachment metadata unavailable.";}};
        files.Children.Add(HistoryButton("Refresh attachments",async()=>await load(true)));next.Click+=async(s,e)=>await load(false);files.Children.Add(next);
        files.Children.Add(HistoryButton("Download selected attachment…",async()=>{var file=attachments.SelectedItem as CoreAdapter.ArchiveAttachment;var source=historyDevices.FirstOrDefault(d=>archiveProgress!=null&&d.PeerId==archiveProgress.Source&&d.MayServe&&!d.Local);if(file==null||source==null||account!=(discovery==null?0:discovery.AccountId)||generation!=authenticatedFriendSnapshotGeneration){status.Text="Sync from an authorized source first.";return;}var choose=new Microsoft.Win32.SaveFileDialog{FileName=file.name,Title="Choose download folder",OverwritePrompt=false};if(choose.ShowDialog(dialog)==true)try{await Task.Run(()=>core.FileTransfer(5,source.PeerId,file.id,Path.GetDirectoryName(choose.FileName)));ShowFileTransfers();}catch{status.Text="Attachment request rejected; no insecure fallback.";}}));
        panel.Children.Add(HistoryText("Historical attachments: Download on demand in conversations."));advancedPanel.Children.Add(new Expander{Header="Historical attachment testing",Content=files,IsExpanded=false});
        sync.Click+=async(s,e)=>{if(!updating){desiredSync=sync.IsChecked==true;await run(11,null,0,null);}};
        render();panel.Loaded+=async(s,e)=>{historySettingsOpen=true;await run(0,null,0,null);};
        panel.Unloaded+=(s,e)=>historySettingsOpen=false;
        EventHandler tick=async(s,e)=>{if(!dialog.IsVisible||!panel.IsVisible||operating)return;bool current=discovery!=null&&account>0&&account==discovery.AccountId&&generation==authenticatedFriendSnapshotGeneration;if(!current){historyDevices=new ArchiveEnrollmentExecution.Device[0];automatic.Clear();localPanel.Children.Clear();remotePanel.Children.Clear();sync.IsEnabled=false;sync.IsChecked=null;await run(0,null,0,null);}else {if(renderedRows!=historyDevices){automatic=historyDevices.ToDictionary(d=>d.PeerId,d=>d.Automatic);render();}else progress();retry.Visibility=archiveProgress==null||archiveProgress.DisplayState=="Error"||archiveProgress.DisplayState=="Waiting"||archiveProgress.DisplayState=="Paused"?Visibility.Visible:Visibility.Collapsed;}};
        DateTime nextPresentation=DateTime.MinValue;EventHandler contextTick=(s,e)=>{if(DateTime.UtcNow<nextPresentation)return;nextPresentation=DateTime.UtcNow.AddSeconds(1);tick(s,e);};
        panel.Loaded+=(s,e)=>{HistoryPresentationChanged-=tick;HistoryPresentationChanged+=tick;timer.Tick-=contextTick;timer.Tick+=contextTick;};
        panel.Unloaded+=(s,e)=>{HistoryPresentationChanged-=tick;timer.Tick-=contextTick;};
        dialog.Closed+=(s,e)=>{historySettingsOpen=false;HistoryPresentationChanged-=tick;timer.Tick-=contextTick;};return panel;
    }
    static TextBlock HistoryText(string text){return new TextBlock{Text=text,TextWrapping=TextWrapping.Wrap,Foreground=Brushes.LightGray,Margin=new Thickness(0,4,0,6)};}
    static TextBlock HistoryTitle(string text){return new TextBlock{Text=text,FontSize=18,Foreground=Brushes.White,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,10,0,6)};}
    static Button HistoryButton(string text,Action action){var b=new Button{Content=text,Padding=new Thickness(10,7,10,7),Margin=new Thickness(0,4,6,4)};AutomationProperties.SetName(b,text);b.Click+=(s,e)=>action();return b;}
}
}
