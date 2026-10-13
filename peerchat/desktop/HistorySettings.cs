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
        var value=new HistoryProgressView{State="Waiting for verification",Direction="Sign in to verify history access.",Counters=""};
        if(p==null)return value;
        string raw=(p.State??"").ToLowerInvariant();
        if(p.DisplayState=="Partial"){value.State="Partial";value.Direction=p.State;value.Counters=p.Received+" new messages received · "+p.Deferred+" unresolved/deferred records · "+p.RejectedBatches+" rejected batches";return value;}
        value.State=p.DisplayState=="Error"?"Failed":raw.Contains("off")?"Permission disabled":raw.Contains("paused")?"Paused":p.DisplayState=="Complete"?"Complete":raw.Contains("authorization")?"Waiting for verification":p.DisplayState=="Waiting"?"Waiting for device":p.DisplayState=="Connecting"?"Connecting":p.DisplayState=="Syncing"?"Downloading history":"Idle";
        value.Direction=value.State=="Downloading history"?"↓ Downloading history from "+source:value.State=="Connecting"?"Connecting to "+source:value.State=="Waiting for device"?"Waiting for "+source:value.State=="Complete"?"Up to date — last sync completed":value.State=="Authorization required"?"History authorization needs attention.":value.State;
        value.Active=p.Running&&(value.State=="Downloading history"||value.State=="Connecting"||value.State=="Waiting for device");
        value.Counters=p.Received+" new messages received this session · "+p.Conversations+" conversations completed";
        value.Counters+="\nLast successful sync: "+(p.LastSuccessful==0?"Not recorded":new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(p.LastSuccessful).ToLocalTime().ToString("g"));
        if(providers!=null&&providers.Length>0&&value.State!="Complete"){value.Active=true;string serving="↑ Providing history to "+String.Join(", ",providers);value.Direction=p.Running?value.Direction+"\n"+serving:serving;value.State=p.Running?value.State:"Providing history";if(!p.Running)value.Counters="Providing session active; sent-message totals are not exposed.";}
        value.Indeterminate=value.Active&&(!reliableTotal.HasValue||providers!=null&&providers.Length>0);
        if(value.State=="Connecting"||value.State=="Waiting for device")value.Indeterminate=true;
        else if(value.State=="Complete")value.Percent=100;
        else if(reliableTotal.HasValue&&reliableTotal.Value>0&&p.Processed<=reliableTotal.Value)value.Percent=100.0*p.Processed/reliableTotal.Value;else if(value.Active)value.Indeterminate=true;
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
        return BuildHistorySettingsPanel(dialog,(owner,local)=>new ArchiveEnrollmentExecution((op,input)=>core.Archive(op,input),(op,fields)=>owner.Archive(op,local,fields)));
    }
    // The view consumes the existing application adapter. Its composition stays
    // separate from native grant issuance so the complete UI flow is testable.
    StackPanel BuildHistorySettingsPanel(Window dialog,Func<DiscoveryClient,string,ArchiveEnrollmentExecution> createExecution){
        var panel=new StackPanel{MaxWidth=720,HorizontalAlignment=HorizontalAlignment.Stretch};var sync=VoiceProcessingSwitch("History Sync",false);sync.IsThreeState=true;sync.IsChecked=null;sync.IsEnabled=false;sync.FontSize=16;
        panel.Children.Add(sync);panel.Children.Add(HistoryText("Keep conversations up to date across your approved devices."));AddDiagnosticEntry(panel,DiagnosticSection.HistorySync,()=>{var device=historyDevices.FirstOrDefault(d=>d.Local);return device==null?"":device.PeerId;});
        var localPanel=new StackPanel();var remotePanel=new StackPanel();var expandedDevices=new HashSet<string>();
        var status=HistoryText("Verifying History Sync");status.FontSize=16;status.FontWeight=FontWeights.SemiBold;AutomationProperties.SetName(status,"History sync status");
        var subtitle=HistoryText("Sign in to verify history access.");subtitle.FontSize=12;subtitle.Foreground=Brush("#A1AEBB");
        var counters=HistoryText("");AutomationProperties.SetName(counters,"History sync committed count");AutomationProperties.SetName(subtitle,"History sync subtitle");var conversations=HistoryText("");conversations.FontSize=12;
        var bar=new ProgressBar{Height=8,Maximum=100,Foreground=Brush("#83B9F7"),Visibility=Visibility.Collapsed,Margin=new Thickness(0,4,0,6)};AutomationProperties.SetName(bar,"History sync progress");
        var detailsText=HistoryText("");detailsText.FontSize=12;
        var statusAction=HistoryButton("Sync now",()=>{});var actionRow=new DockPanel{Margin=new Thickness(0,8,0,0)};DockPanel.SetDock(statusAction,Dock.Right);actionRow.Children.Add(statusAction);
        var details=new Expander{Header="View sync details",Foreground=Brush("#A1AEBB"),Content=detailsText,FontSize=12,VerticalAlignment=VerticalAlignment.Center};AutomationProperties.SetName(details,"View sync details");actionRow.Children.Add(details);
        var statusBody=new StackPanel();statusBody.Children.Add(status);statusBody.Children.Add(subtitle);statusBody.Children.Add(counters);statusBody.Children.Add(bar);statusBody.Children.Add(conversations);statusBody.Children.Add(actionRow);
        var statusCard=new Border{Child=statusBody,Padding=new Thickness(18),Background=Brush("#131E28"),BorderBrush=Brush("#2B3946"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(12),Margin=new Thickness(0,12,0,18)};AutomationProperties.SetName(statusCard,"History sync status card");panel.Children.Add(statusCard);
        var actions=new WrapPanel{HorizontalAlignment=HorizontalAlignment.Right};var deviceHeading=new DockPanel();DockPanel.SetDock(actions,Dock.Right);deviceHeading.Children.Add(actions);var title=HistoryTitle("Your devices");title.FontSize=16;deviceHeading.Children.Add(title);panel.Children.Add(deviceHeading);
        var deviceRows=new StackPanel();deviceRows.Children.Add(localPanel);deviceRows.Children.Add(remotePanel);panel.Children.Add(new Border{Child=deviceRows,BorderBrush=Brush("#2B3946"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(12),Margin=new Thickness(0,6,0,14)});
        var advancedPanel=new StackPanel();var advanced=new Expander{Header="Advanced history settings",Content=advancedPanel,IsExpanded=false,Margin=new Thickness(0,2,0,8),Foreground=Brush("#A1AEBB")};panel.Children.Add(advanced);
        panel.Children.Add(HistoryText("History is encrypted and shared only with devices you approve."));
        var raw=HistoryText("");raw.FontSize=12;advancedPanel.Children.Add(raw);
        bool cachedCurrent=discovery!=null&&archiveLoadedOwner==discovery&&historyAccount==discovery.AccountId&&historyGeneration==authenticatedFriendSnapshotGeneration;
        long account=cachedCurrent?historyAccount:0;ulong generation=cachedCurrent?historyGeneration:0;bool updating=false,operating=false;bool? desiredSync=null;ArchiveEnrollmentExecution.Device[] renderedRows=null;
        var automatic=new Dictionary<string,bool>();Action render=null;Func<uint,ArchiveEnrollmentExecution.Device,long,bool?,Task<bool>> run=null;
        Action progress=()=>{
            var p=archiveProgress;var view=CurrentHistoryView();string sourceName=HistoryPeerName(p==null?"":p.Source);var localDevice=historyDevices.FirstOrDefault(d=>d.Local);
            bool approval=account>0&&localDevice!=null&&!localDevice.Authorized&&!localDevice.MayRead&&!localDevice.MayServe;
            bool firstUse=approval&&localDevice.ApprovalKnown&&localDevice.MaySetUp;
            status.Text=approval?"!  Device approval required":view.State=="Complete"?"✓  All conversations up to date":view.State=="Connecting"?"◌  Connecting to "+sourceName:view.State=="Downloading history"?"↻  Syncing conversations":view.State=="Waiting for device"?"◷  Waiting for "+sourceName:view.State=="Failed"?"!  History Sync couldn't finish":view.State=="Permission disabled"?"History Sync is OFF":view.State=="Paused"?"History Sync paused":view.State=="Providing history"?"Providing history":"Verifying History Sync";
            status.Foreground=Brush(view.State=="Complete"?"#8DD4A4":view.State=="Failed"?"#F2AAA5":"#EDF1F5");
            string last=p==null||p.LastSuccessful==0?"Not recorded":new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(p.LastSuccessful).ToLocalTime().ToString("g");
            subtitle.Text=approval?"This device needs approval before exchanging history.":view.State=="Complete"?"Last synced "+last:view.State=="Connecting"?"Establishing a secure history connection":view.State=="Downloading history"?"Receiving history from "+sourceName:view.State=="Waiting for device"?"History will sync when this device is available.":view.State=="Failed"?(p==null?"Unable to read the current operation.":p.State):view.Direction;
            counters.Text=p==null?"":view.State=="Complete"&&p.Received==0?"No new messages — up to date":view.State=="Failed"?"Last verified: "+p.Received+" messages imported":p.Received+" new messages received this session";
            conversations.Text=p==null?"":p.Conversations+" conversations synchronized";
            bar.Visibility=!approval&&(view.Active||view.State=="Complete")?Visibility.Visible:Visibility.Collapsed;bar.IsIndeterminate=view.Indeterminate;bar.Value=view.Indeterminate?0:view.Percent;
            detailsText.Text="Source: "+sourceName+"\nNewly imported messages: "+(p==null?0:p.Received)+"\nConversations completed: "+(p==null?0:p.Conversations)+"\nConnection: "+(view.State=="Downloading history"?"Encrypted history transfer":view.State=="Complete"?"Verified history completed":"Not currently verified live")+"\nLast successful sync: "+last;
            if(p!=null&&p.Activity>=0){subtitle.Text="Current reader activity: "+p.CurrentActivity+"\nLast completed result: "+p.LastResultSummary;detailsText.Text+="\nSource role: "+p.Providers.Length+" active readers · "+p.ProvidedQueued+" records queued (retries included; not confirmed imports)";}
            statusAction.Content=approval?"Review device":view.Active?"Pause":view.State=="Failed"?"Retry":"Sync now";
            if(firstUse){status.Text="Set up History Sync";subtitle.Text="Enroll this first trusted device, then authorize your other devices.";statusAction.Content="Set up History Sync";}
            else if(approval&&localDevice.ApprovalKnown&&!String.IsNullOrEmpty(localDevice.ApprovalIssuer)){subtitle.Text="A trusted history device has authorized this device. Confirm that authorizer to use its verified grants.";statusAction.Content="Approve History Sync";}
            statusAction.IsEnabled=account>0&&(approval||view.Active||historyDevices.Any(d=>!d.Local&&d.MayServe)&&localDevice!=null&&localDevice.MayRead);
            if(p!=null){updating=true;sync.IsThreeState=false;sync.IsChecked=p.Enabled;sync.IsEnabled=true;updating=false;}
            raw.Text=(p==null?"No validated snapshot.":p.Summary)+"\nAccount: "+account+"\nAuthentication snapshot: "+generation+"\n"+CurrentHistoryDiagnostics();
        };
        run=async(action,target,capabilities,auto)=>{
            var owner=discovery;long current=owner==null?0:owner.AccountId;ulong revision=authenticatedFriendSnapshotGeneration;string local=model.OwnId;
            if(operating||historyCommandInFlight){if(action!=0)status.Text="A History Sync operation is already running. Wait for it to finish before authorizing.";return false;}
            if(closing||owner==null||current<=0||mailboxContextAccount!=current||revision==0||revision>Int64.MaxValue){status.Text="Sign in and establish authenticated server presence first.";return false;}
            if(action!=0&&(account!=current||generation!=revision)){status.Text="Account context changed. Wait for device verification before making changes.";return false;}
            if(action==5&&capabilities==0){status.Text="At least one history permission is required while this device is authorized. Revoke the device if you want to disable history access completely.";return false;}
            if(action==24&&!Confirm("Set up History Sync", "Use this authenticated device as your first trusted history device?\nAccount "+current+"\nDevice identity:\n"+target.PeerId+"\n\nReceive history: ON\nProvide history: ON\nThese permissions allow eligible encrypted history exchange with devices you explicitly approve. Existing identities and history remain unchanged.","Set up"))return false;
            if(action==25&&!Confirm("Approve History Sync", "Trust this registered history device's signed authorization?\nAccount "+current+"\nAuthorizer device identity:\n"+target.PeerId+"\n\nOnly its existing verified Receive / Provide grants will be used. No new grants will be issued.","Approve"))return false;
            if(action>=4&&action<=7&&!Confirm("Confirm history device access",HistoryName(current,target)+"\nAccount "+current+"\nDevice identity:\n"+target.PeerId+"\n\n"+(action==4?"Trust this device to sign history permissions?":action==5?"Apply Receive / Provide permissions to this exact device?":action==6?"Revoke this device's history access?":"Disable this trusted history authorizer?")+"\nThis does not erase history already received.","Confirm"))return false;
            operating=historyCommandInFlight=true;long operationEpoch=++archiveUiEpoch;panel.IsEnabled=false;
            ArchiveEnrollmentExecution execution=null;
            try{
                var result=await Task.Run(()=>{
                    execution=createExecution(owner,local);execution.Refresh(current,(long)revision,local);
                    if(action==24)execution.PrepareFirstDevice(current,(long)revision,local,3,true);
                    if(action==25)execution.ConfirmExistingIssuer(current,(long)revision,target.PeerId,true);
                    if(action==5){var currentTarget=execution.SnapshotDevices(current,(long)revision).SingleOrDefault(d=>d.PeerId==target.PeerId);if(currentTarget==null)throw new HistoryApprovalFailure("The selected device is no longer registered to this account.");if(!currentTarget.ApprovalKnown||!currentTarget.MayAuthorize)throw new HistoryApprovalFailure("This device is not an enabled trusted history authorizer. Approve from an existing trusted history device.");}
                    if(action>=4&&action<=7)execution.Consent(current,(long)revision,action,target.PeerId,capabilities,true);
                    if(auto.HasValue){if(!auto.Value&&archiveProgress!=null&&archiveProgress.Running&&(target.Local||archiveProgress.Source==target.PeerId))execution.PauseSync(current,(long)revision);execution.AutomaticSync(current,(long)revision,target.PeerId,auto);}
                    bool enabled=execution.HistorySync(current,(long)revision,action==11?desiredSync:null);execution.PublishPending(current,(long)revision,local);
                    try{if(action==12)execution.StartSync(current,(long)revision,target.PeerId,true);if(action==14)execution.PauseSync(current,(long)revision);}
                    finally{var admission=execution.AuthorizationDiagnostics(current,(long)revision);dialog.Dispatcher.BeginInvoke((Action)(()=>{if(discovery==owner&&authenticatedFriendSnapshotGeneration==revision&&operationEpoch==archiveUiEpoch&&dialog.IsVisible)archiveAuthorizationDiagnostic=admission;}));}
                    var rows=execution.SnapshotDevices(current,(long)revision);if(action==5||action==24){var verified=rows.SingleOrDefault(d=>d.PeerId==target.PeerId);if(verified==null||!verified.ApprovalKnown||!verified.LocalGrantPresent||verified.LocalGrantRead!=(((action==24?3:capabilities)&1)!=0)||verified.LocalGrantServe!=(((action==24?3:capabilities)&2)!=0))throw new HistoryApprovalFailure("Requested history permissions were not verified by the server. Refresh and retry; no approval is claimed.");}var preferences=rows.ToDictionary(d=>d.PeerId,d=>execution.AutomaticSync(current,(long)revision,d.PeerId));
                    return new {Rows=rows,Preferences=preferences,Enabled=enabled,Progress=execution.Progress(current,(long)revision),Providing=execution.Providing(current,(long)revision)};
                });
                if(!IsHistoryCallbackCurrent(owner,revision,operationEpoch)||!dialog.IsVisible||!panel.IsVisible)return false;
                account=historyAccount=current;generation=historyGeneration=revision;archiveLoadedOwner=owner;archiveLoadedGeneration=revision;historyDevices=result.Rows;historyProviding=result.Providing;automatic=result.Preferences;archiveProgress=result.Progress;archiveMetadataUtc=DateTime.UtcNow.AddSeconds(10);
                updating=true;sync.IsThreeState=false;sync.IsChecked=result.Enabled;sync.IsEnabled=true;updating=false;render();PublishHistoryPresentation();return true;
            }catch(Exception error){if(discovery==owner&&authenticatedFriendSnapshotGeneration==revision&&operationEpoch==archiveUiEpoch&&panel.IsVisible){string stage=execution==null?"before_execution":execution.LastFailedStage??"response_validation_or_projection";historyLastOperationAccount=current;historyLastOperationGeneration=revision;historyLastOperationDiagnostic="\n=== History Sync last settings operation ===\naction="+action+" local_device="+local+" target_device="+(target==null?"none":target.PeerId)+" account="+current+" authentication_generation="+revision+" stage="+stage+" error_type="+error.GetType().Name;sync.IsEnabled=false;status.Text=error is HistoryApprovalFailure?error.Message:"History operation failed at "+stage+". No automatic permission retry was performed. Retry after the connection is restored.";raw.Text=CurrentHistoryDiagnostics();TraceUi("history settings operation rejected: action="+action+" target="+(target==null?"none":target.PeerId)+" stage="+stage+" error="+error.GetType().Name);}return false;}
            finally{operating=historyCommandInFlight=false;panel.IsEnabled=true;archiveNextUtc=DateTime.MinValue;}
        };
        Action<ArchiveEnrollmentExecution.Device,bool> manage=(device,setup)=>{
            if(setup&&device.ApprovalKnown&&!device.MayAuthorize){status.Text="Approve from an enabled trusted history device. Disabled authorizers will not be restored automatically.";return;}
            var body=new StackPanel{Margin=new Thickness(18)};string originalPeer=device.PeerId;long originalAccount=account;ulong originalGeneration=generation;
            var focus=new Window{Owner=dialog,Title=setup?"New history device":HistoryName(account,device),Width=510,Height=590,MinWidth=440,MinHeight=480,Background=Brush("#0C1116"),Foreground=Brush("#EDF1F5"),Resources=window.Resources,WindowStartupLocation=WindowStartupLocation.CenterOwner,Content=new ScrollViewer{Content=body,VerticalScrollBarVisibility=ScrollBarVisibility.Auto}};
            body.Children.Add(HistoryTitle(setup?"New history device":HistoryName(account,device)));
            if(setup)body.Children.Add(HistoryText("Device: "+device.PeerId.Substring(0,12)+"… Compare the full identity in Advanced before authorizing."));
            body.Children.Add(HistoryText("Nickname"));var nickname=new TextBox{Text=HistoryNames.Get(account,device.PeerId,device.Local?"This PC":"New history device"),MaxLength=64,Margin=new Thickness(0,4,0,12)};body.Children.Add(nickname);
            bool initialRead=device.LocalGrantPresent?device.LocalGrantRead:device.GrantedRead,initialServe=device.LocalGrantPresent?device.LocalGrantServe:device.GrantedServe;
            bool newNormal=setup&&!initialRead&&!initialServe;
            var receive=VoiceProcessingSwitch("Receive history",newNormal||initialRead);var provide=VoiceProcessingSwitch("Provide history",newNormal||initialServe);var autoSwitch=VoiceProcessingSwitch("Sync automatically",!automatic.ContainsKey(device.PeerId)||automatic[device.PeerId]);
            autoSwitch.ToolTip="Automatic initial downloads from this device on this profile. OFF pauses its active download. Existing explicit Pause remains paused until Sync now.";
            body.Children.Add(HistoryText(setup?"These permissions let the selected device exchange eligible encrypted history with approved devices. Review individual permissions in Advanced before confirming.":"Existing verified permissions are preserved. Custom access controls are in Advanced."));var message=HistoryText(device.AuthorizationState+" · "+CurrentHistoryView().State);body.Children.Add(message);
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
                bool changed=setup||receive.IsChecked!=initialRead||provide.IsChecked!=initialServe;
                focus.IsEnabled=false;bool ok=await run(changed?5u:0u,device,caps,autoSwitch.IsChecked==true);focus.IsEnabled=true;if(ok){try{HistoryNames.Set(account,device.PeerId,nickname.Text);render();focus.Close();}catch{message.Text="Permissions were verified, but the nickname could not be saved. Retry Save changes.";}}else message.Text=status.Text;
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
            foreach(var d in bound?historyDevices:new ArchiveEnrollmentExecution.Device[0]){
                var body=new StackPanel();var row=new DockPanel{LastChildFill=true};var chevron=HistoryText("›");chevron.FontSize=20;DockPanel.SetDock(chevron,Dock.Right);row.Children.Add(chevron);
                string approvalAction=d.ApprovalKnown&&d.MaySetUp?"Set up":d.NeedsApproval&&d.MayAuthorize?"Authorize":d.Local&&!String.IsNullOrEmpty(d.ApprovalIssuer)?"Approve":null; if(approvalAction!=null){var authorize=HistoryButton(approvalAction,async()=>{if(approvalAction=="Set up")await run(24,d,3,null);else if(approvalAction=="Approve"){var issuer=historyDevices.SingleOrDefault(item=>item.PeerId==d.ApprovalIssuer);if(issuer!=null)await run(25,issuer,0,null);}else manage(d,true);});authorize.Padding=new Thickness(12,4,12,4);authorize.Margin=new Thickness(8,0,10,0);authorize.VerticalAlignment=VerticalAlignment.Center;authorize.Background=Brush("#286BC1");authorize.Foreground=Brushes.White;authorize.BorderThickness=new Thickness(0);var border=new FrameworkElementFactory(typeof(Border));border.SetValue(Border.CornerRadiusProperty,new CornerRadius(12));border.SetValue(Border.BackgroundProperty,new TemplateBindingExtension(Control.BackgroundProperty));border.SetValue(Border.PaddingProperty,new TemplateBindingExtension(Control.PaddingProperty));var content=new FrameworkElementFactory(typeof(ContentPresenter));border.AppendChild(content);authorize.Template=new ControlTemplate(typeof(Button)){VisualTree=border};AutomationProperties.SetName(authorize,approvalAction+" history device "+HistoryName(account,d));DockPanel.SetDock(authorize,Dock.Right);row.Children.Add(authorize);}
                var icon=HistoryText(d.Local?"▣":"□");icon.FontSize=24;icon.Width=38;icon.VerticalAlignment=VerticalAlignment.Center;DockPanel.SetDock(icon,Dock.Left);row.Children.Add(icon);
                var caption=new StackPanel();var name=HistoryText(HistoryName(account,d));name.FontWeight=FontWeights.SemiBold;name.FontSize=14;name.Margin=new Thickness(0,0,0,4);caption.Children.Add(name);
                bool approved=d.ApprovalKnown?(d.Local?(d.GrantedRead||d.GrantedServe):d.PairAuthorized):d.MayRead||d.MayServe;string availability=d.Local?"History Sync "+(archiveProgress!=null&&archiveProgress.Enabled?"ON":"OFF"):archiveProgress!=null&&archiveProgress.Source==d.PeerId&&archiveProgress.DisplayState=="Syncing"?"Connected":"Availability unknown";
                if(d.ApprovalKnown&&!approved)availability=!d.MayAuthorize?"Approve from an enabled trusted history device":d.LocalGrantPresent?"Approve this PC first":availability;
                if(d.MaySetUp)availability="Set up this first trusted device";else if(d.Local&&!String.IsNullOrEmpty(d.ApprovalIssuer))availability="Confirm the authorizing device";
                var summary=HistoryText((approved?"Approved":d.Authorized?"Permission verification needed":d.Revoked?"Revoked":"Approval needed")+" · "+availability);summary.FontSize=11;summary.Foreground=Brush("#A1AEBB");summary.Margin=new Thickness(0);caption.Children.Add(summary);row.Children.Add(caption);
                var header=new Button{Content=row,Padding=new Thickness(14,10,14,10),MinHeight=68,Background=Brushes.Transparent,Foreground=Brush("#EDF1F5"),BorderThickness=new Thickness(0),HorizontalContentAlignment=HorizontalAlignment.Stretch,Margin=new Thickness(0)};AutomationProperties.SetName(header,"History device "+HistoryName(account,d));body.Children.Add(header);
                var detail=new StackPanel{Margin=new Thickness(52,0,14,12),Visibility=expandedDevices.Contains(d.PeerId)?Visibility.Visible:Visibility.Collapsed};detail.Children.Add(HistoryText("History access: "+(d.GrantedRead&&d.GrantedServe?"Receive and provide":d.GrantedRead?"Receive":d.GrantedServe?"Provide":"Not authorized")));
                detail.Children.Add(HistoryText("Last synced: "+(archiveProgress!=null&&archiveProgress.Source==d.PeerId&&archiveProgress.LastSuccessful>0?new DateTime(1970,1,1,0,0,0,DateTimeKind.Utc).AddMilliseconds(archiveProgress.LastSuccessful).ToLocalTime().ToString("g"):"Not known for this device")));
                var controls=new WrapPanel();controls.Children.Add(HistoryButton("Manage",()=>manage(d,!d.Authorized)));if(!d.Local&&d.MayServe){var now=HistoryButton("Sync now",async()=>await run(12,d,0,null));now.IsEnabled=sync.IsChecked==true&&historyDevices.Any(local=>local.Local&&local.MayRead)&&!(archiveProgress!=null&&archiveProgress.Running);controls.Children.Add(now);}detail.Children.Add(controls);body.Children.Add(detail);
                header.Click+=(s,e)=>{if(e.Source!=header)return;bool open=detail.Visibility!=Visibility.Visible;detail.Visibility=open?Visibility.Visible:Visibility.Collapsed;chevron.Text=open?"⌄":"›";if(open)expandedDevices.Add(d.PeerId);else expandedDevices.Remove(d.PeerId);};
                (d.Local?localPanel:remotePanel).Children.Add(new Border{Child=body,BorderBrush=Brush("#28343F"),BorderThickness=new Thickness(0,0,0,1)});
            }
            if(localPanel.Children.Count==0)localPanel.Children.Add(HistoryText("Sign in to verify this device."));if(remotePanel.Children.Count==0)remotePanel.Children.Add(new TextBlock{Text="No other registered history devices.",Margin=new Thickness(14),Foreground=Brush("#A1AEBB"),FontSize=12});if(account>0)progress();
            if(HistoryNames.DetailsUnavailable){status.Text="History device details unavailable. Retry in Advanced history security; device identities and permissions are unchanged.";raw.Text+="\nOptional protected nickname metadata could not be read.";TraceUi("history optional nickname metadata unavailable");}
        };
        actions.Children.Add(HistoryButton("Add device",()=>{var pending=historyDevices.FirstOrDefault(d=>!d.Local&&!d.Authorized);if(pending==null){status.Text="No new registered device. Open History Sync on the other device; authorization is still required.";return;}manage(pending,true);}));
        statusAction.Click+=async(s,e)=>{var local=historyDevices.FirstOrDefault(d=>d.Local);if(Object.Equals(statusAction.Content,"Set up History Sync")&&local!=null){await run(24,local,3,null);return;}if(local!=null&&!String.IsNullOrEmpty(local.ApprovalIssuer)){var issuer=historyDevices.SingleOrDefault(d=>d.PeerId==local.ApprovalIssuer);if(issuer!=null){await run(25,issuer,0,null);return;}}if(Object.Equals(statusAction.Content,"Review device")&&local!=null){manage(local,!local.Authorized);return;}if(Object.Equals(statusAction.Content,"Pause")){await run(14,null,0,null);return;}var source=historyDevices.FirstOrDefault(d=>archiveProgress!=null&&d.PeerId==archiveProgress.Source&&d.MayServe&&!d.Local)??historyDevices.FirstOrDefault(d=>d.MayServe&&!d.Local);await run(source==null?0u:12u,source,0,null);};
        var retry=HistoryButton("Retry",async()=>await run(0,null,0,null));advancedPanel.Children.Add(retry);
        advancedPanel.Children.Add(HistoryButton("Copy History Diagnostics",()=>CopyDiagnosticText(CurrentHistoryDiagnostics(),"History diagnostics copied.")));
        advancedPanel.Children.Add(HistoryButton("Pause",async()=>await run(14,null,0,null)));
        advancedPanel.Children.Add(HistoryButton("Refresh history devices",async()=>await run(0,null,0,null)));
        advancedPanel.Children.Add(HistoryText("Trust must be explicitly established in a device's Manage / Advanced section. Login and nicknames never grant access. Multiple authorities may grant different permissions; Manage changes only grants issued by this device."));
        var files=new StackPanel();var attachments=new ComboBox{MinHeight=32,Margin=new Thickness(0,6,0,6)};AutomationProperties.SetName(attachments,"Historical attachment selector");files.Children.Add(attachments);long cursor=0;var next=new Button{Content="Next attachment page",IsEnabled=false};
        Func<bool,Task> load=async reset=>{try{var page=await Task.Run(()=>core.ArchiveAttachments(reset?0:cursor));if(!dialog.IsVisible)return;attachments.ItemsSource=page.records;attachments.SelectedIndex=page.records.Length>0?0:-1;cursor=page.next;next.IsEnabled=!page.complete;}catch{status.Text="Historical attachment metadata unavailable.";}};
        files.Children.Add(HistoryButton("Refresh attachments",async()=>await load(true)));next.Click+=async(s,e)=>await load(false);files.Children.Add(next);
        files.Children.Add(HistoryButton("Download selected attachment…",async()=>{var file=attachments.SelectedItem as CoreAdapter.ArchiveAttachment;var source=historyDevices.FirstOrDefault(d=>archiveProgress!=null&&d.PeerId==archiveProgress.Source&&d.MayServe&&!d.Local);if(file==null||source==null||account!=(discovery==null?0:discovery.AccountId)||generation!=authenticatedFriendSnapshotGeneration){status.Text="Sync from an authorized source first.";return;}var choose=new Microsoft.Win32.SaveFileDialog{FileName=file.name,Title="Choose download folder",OverwritePrompt=false};if(choose.ShowDialog(dialog)==true)try{await Task.Run(()=>core.FileTransfer(5,source.PeerId,file.id,Path.GetDirectoryName(choose.FileName)));ShowFileTransfers();}catch{status.Text="Attachment request rejected; no insecure fallback.";}}));
        advancedPanel.Children.Add(new Expander{Header="Historical attachment testing",Content=files,IsExpanded=false});
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
    sealed class HistoryApprovalFailure:InvalidOperationException {public HistoryApprovalFailure(string reason):base(reason){}}
    static TextBlock HistoryTitle(string text){return new TextBlock{Text=text,FontSize=18,Foreground=Brushes.White,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,10,0,6)};}
    static Button HistoryButton(string text,Action action){var b=new Button{Content=text,Padding=new Thickness(10,7,10,7),Margin=new Thickness(0,4,6,4)};AutomationProperties.SetName(b,text);b.Click+=(s,e)=>action();return b;}
}
}
