using System;
using System.Collections;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Net.NetworkInformation;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Diagnostics;
using System.ComponentModel;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using Ellipse = System.Windows.Shapes.Ellipse;
using System.Windows.Threading;
using PeerChat.Updater;

namespace PeerChatDesktop {
// Links are built as text/Hyperlink objects. Peer text is never parsed as XAML.
public class ChatText : TextBlock {
    public const int InlinePreviewCharacters=1024;
    public static string DisplayMessage(string message){string value=message??"";if(value.Length<=InlinePreviewCharacters)return value;int length=InlinePreviewCharacters;if(Char.IsHighSurrogate(value[length-1])&&Char.IsLowSurrogate(value[length]))length--;return value.Substring(0,length)+"\n… Large message ("+value.Length+" characters). Use Copy message to copy its full contents.";}
    public static readonly DependencyProperty MessageProperty=DependencyProperty.Register("Message",typeof(string),typeof(ChatText),new PropertyMetadata("",Update));
    public string Message {get{return (string)GetValue(MessageProperty);}set{SetValue(MessageProperty,value);}}
    static void Update(DependencyObject source,DependencyPropertyChangedEventArgs e){
        var view=(ChatText)source;string text=e.NewValue as string??"";string visible=DisplayMessage(text);view.Inlines.Clear();int start=0;
        foreach(Match match in Regex.Matches(visible,@"https?://[^\s<>""\u0000-\u001f]+",RegexOptions.IgnoreCase)){
            view.Inlines.Add(new Run(visible.Substring(start,match.Index-start)));Uri uri;
            if(Uri.TryCreate(match.Value,UriKind.Absolute,out uri)&&(uri.Scheme=="http"||uri.Scheme=="https")){
                var link=new Hyperlink(new Run(match.Value)){NavigateUri=uri,Foreground=new SolidColorBrush(Color.FromRgb(131,204,252))};
                link.RequestNavigate+=(sender,args)=>{try{System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(args.Uri.AbsoluteUri){UseShellExecute=true});}catch{}args.Handled=true;};view.Inlines.Add(link);
            }else view.Inlines.Add(new Run(match.Value));start=match.Index+match.Length;
        }view.Inlines.Add(new Run(visible.Substring(start)));
        var menu=new ContextMenu();var copy=new MenuItem{Header="Copy message"};copy.Click+=(s,a)=>{try{Clipboard.SetText(text);}catch{}};menu.Items.Add(copy);view.ContextMenu=menu;
    }
}
public class DesktopApplication {
    static readonly Stopwatch UiTiming=new Stopwatch();
    static void TraceUi(string message){string line="PeerChat UI timing "+DateTime.UtcNow.ToString("O")+" "+message;System.Diagnostics.Debug.WriteLine(line);try{File.AppendAllText(Path.Combine(Path.GetTempPath(),"PeerChat-ui-timing.log"),line+Environment.NewLine);}catch{}}
    static void TraceStartup(string installRoot,string message){try{Directory.CreateDirectory(installRoot);File.AppendAllText(Path.Combine(installRoot,"startup.log"),DateTime.UtcNow.ToString("O")+" pid="+Process.GetCurrentProcess().Id+" "+message+Environment.NewLine,new UTF8Encoding(false));}catch{}}
    [StructLayout(LayoutKind.Sequential)] struct FlashInfo { public uint size; public IntPtr window; public uint flags; public uint count; public uint timeout; }
    [DllImport("user32.dll")] static extern bool FlashWindowEx(ref FlashInfo info);
    [DllImport("user32.dll")] static extern bool MessageBeep(uint type);
    const uint FlashTaskbar=2;
    [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr window,int attribute,ref int value,int size);
    [StructLayout(LayoutKind.Sequential)] struct NativeRect {public int left,top,right,bottom;}
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateWindowEx(uint exStyle,string className,string title,uint style,int x,int y,int width,int height,IntPtr parent,IntPtr menu,IntPtr instance,IntPtr param);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr window,out NativeRect bounds);
    [DllImport("user32.dll")] static extern bool MoveWindow(IntPtr window,int x,int y,int width,int height,bool repaint);
    readonly Window window;
    readonly CoreAdapter core;
    readonly LocalProfileManager profileManager;
    readonly LocalProfileInfo localProfile;
    readonly ChatViewModel model=new ChatViewModel();
    readonly FriendConversationIndex conversationIndex;
    readonly ConnectionDiagnosticLog connectionLog;
    readonly DiagnosticsRetentionStore diagnosticsRetention;
    readonly DiagnosticsService diagnosticsService;
    IList<FailureSnapshot> startupUncleanOperations=new List<FailureSnapshot>().AsReadOnly();
    string lastScreenDiagnosticText="",activeVoiceDiagnosticId="",activeVoiceDiagnosticStage="";
    string activeScreenDiagnosticId="",activeScreenDiagnosticStage="";
    readonly HashSet<string> surfacedFailureKeys=new HashSet<string>(StringComparer.Ordinal);
    Window failureNotification;
    readonly ProfileAccountStore accountStore;
    readonly VoicePreferenceStore voicePreferenceStore;
    readonly VoicePreferenceData voicePreferences;
    readonly ScreenSharePreferenceStore screenPreferenceStore;
    readonly ScreenSharePreferenceData screenPreferences;
    readonly DesktopUpdateClient updateClient;
    readonly string updateHealthToken;
    readonly DispatcherTimer timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(60)};
    readonly DispatcherTimer voiceTimer=new DispatcherTimer{Interval=TimeSpan.FromSeconds(1)};
    readonly DispatcherTimer networkChangeTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(900)};
    readonly string directory;
    bool changingSelection,busy,closing,closed,trustDialog,friendRefreshInFlight,presenceInFlight,networkRecoveryInFlight,networkRecoveryPending,firewallConfigurationInFlight,accountSetupPrompted,interactiveAccountSetup;
    ushort firewallConfiguredPort;
    Task startupTask=Task.FromResult(0);
    readonly Queue<Tuple<CoreEvent,string>> trustRequests=new Queue<Tuple<CoreEvent,string>>();
    readonly HashSet<string> trustPrompted=new HashSet<string>(StringComparer.Ordinal);
    readonly HashSet<string> trustSuppressed=new HashSet<string>(StringComparer.Ordinal);
    readonly ConnectionAttemptTracker connectionAttempts=new ConnectionAttemptTracker();
    ulong authenticatedFriendSnapshotGeneration;
    readonly HashSet<long> automaticInitialPinAttempts=new HashSet<long>();
    readonly object relayGate=new object();readonly Dictionary<ulong,RelayTransport> relayTransports=new Dictionary<ulong,RelayTransport>();
    string lastPublicCandidate="Unavailable",lastPublicCandidateSource="Unavailable",lastObservedAddressClassification="Unavailable";
    string localTlsCertificateDiagnostics="Unavailable";
    string shareHost=DefaultReachableHost(),networkFingerprint="";ulong networkGeneration=1;bool shareHostManuallyConfigured;
    DiscoveryClient discovery;
    // Ephemeral media credentials are intentionally memory-only.  The WPF views
    // never own them; this application orchestration boundary requests and passes
    // them only for an authenticated profile/session.
    MediaTurnCredentials mediaTurnCredentials;
    ulong profileSessionGeneration=1,discoverySessionGeneration,mediaIceConfigurationGeneration;
    bool mediaTurnRefreshInFlight;
    ProfileAccountLink accountLink;
    readonly ConnectionPreferenceStore connectionPreferenceStore; ConnectionPreferenceData connectionPreferences;
    sealed class ScopedP2pRequest { public string Id,Peer; public long AccountId,ExpiresUtc; public bool Initiated; }
    readonly Dictionary<string,ScopedP2pRequest> scopedP2p=new Dictionary<string,ScopedP2pRequest>();
    readonly Dictionary<long,string> friendRequestStates=new Dictionary<long,string>();
    readonly Dictionary<long,DiscoverySearchResult> friendDirectory=new Dictionary<long,DiscoverySearchResult>();
    readonly Dictionary<long,bool> friendPresence=new Dictionary<long,bool>();
    FriendState friendState=new FriendState();
    DateTime lastFriendRefreshUtc=DateTime.MinValue;
    DateTime lastPresenceUtc=DateTime.MinValue;
    readonly TextBlock profileName=new TextBlock(), profileHandle=new TextBlock();
    Window incomingVoiceWindow;ContextMenu headerFlyout;DateTime voiceConnectedUtc=DateTime.MinValue,voiceLastConnectedUtc=DateTime.MinValue,voiceStartedUtc=DateTime.MinValue,voiceEndedUtc=DateTime.MinValue;string voiceAudioError="",latestVoiceAudioError="None",recentVoiceCallId="",recentVoicePeerId="",recentVoiceState="Idle";bool recentVoiceMuted,recentVoiceDeafened;
    Button screenStartButton,screenEndButton,screenDiagnosticsButton;Border screenStrip;TextBlock screenStatus,screenSummaryText;
    StackPanel screenConnectionPanel;TextBlock screenMethodText,screenQualityText,screenControlText,screenDetailsText;
    Window incomingScreenWindow,screenViewerWindow;IntPtr screenViewerChild=IntPtr.Zero;
    string activeScreenId="",activeScreenPeer="";long activeScreenLifetime,activeScreenGeneration;int activeScreenState;
    bool closingScreenViewer;
    // Older pages are prepended to the bound collection. Keep the same visual message
    // under the reader's eye when WPF remeasures that prepend.
    string historyScrollKey="";
    double historyScrollOffset,historyScrollExtent;
    bool discoveryRestored;
    Task<bool> presenceRefreshTask;
    Task<bool> friendRefreshTask;
    readonly HashSet<string> reconnectRepairs=new HashSet<string>();
    event Action InviteCandidatesChanged;
    static string DefaultReachableHost(){
        NetworkInterface[] networks;try{networks=NetworkInterface.GetAllNetworkInterfaces();}catch{networks=new NetworkInterface[0];}
        // Prefer a current physical Ethernet/Wi-Fi adapter with a default gateway. A
        // broken virtual adapter must not abort inspection of later usable adapters.
        foreach(var network in networks){
            if(network.OperationalStatus!=OperationalStatus.Up)continue;
            if(network.NetworkInterfaceType!=NetworkInterfaceType.Ethernet&&network.NetworkInterfaceType!=NetworkInterfaceType.Wireless80211)continue;
            try{var properties=network.GetIPProperties();bool gateway=properties.GatewayAddresses.Any(g=>g.Address.AddressFamily==AddressFamily.InterNetwork&&!IPAddress.IsLoopback(g.Address));if(!gateway)continue;foreach(var address in properties.UnicastAddresses.Select(item=>item.Address))if(address.AddressFamily==AddressFamily.InterNetwork&&!IPAddress.IsLoopback(address)&&!address.ToString().StartsWith("169.254."))return address.ToString();}catch{/* Continue past an adapter with unavailable properties. */}
        }
        try{foreach(var address in Dns.GetHostEntry(Dns.GetHostName()).AddressList)if(address.AddressFamily==AddressFamily.InterNetwork&&!IPAddress.IsLoopback(address)&&!address.ToString().StartsWith("169.254."))return address.ToString();}catch{}
        return Environment.MachineName;
    }
    public DesktopApplication(string dataDirectory,ushort port,string capturePath=null,bool compact=false,LocalProfileManager manager=null,LocalProfileInfo selectedProfile=null,string healthToken=null){
        directory=dataDirectory;
        conversationIndex=new FriendConversationIndex(directory);connectionLog=new ConnectionDiagnosticLog(directory);accountStore=new ProfileAccountStore(directory);connectionPreferenceStore=new ConnectionPreferenceStore(directory);connectionPreferences=connectionPreferenceStore.Load();voicePreferenceStore=new VoicePreferenceStore(directory);voicePreferences=voicePreferenceStore.Load();screenPreferenceStore=new ScreenSharePreferenceStore(directory);screenPreferences=screenPreferenceStore.Load();updateClient=new DesktopUpdateClient();updateHealthToken=healthToken;accountLink=accountStore.Load();interactiveAccountSetup=capturePath==null;
        DiagnosticsRetentionStore retention=null;IList<FailureSnapshot> unclean=new List<FailureSnapshot>().AsReadOnly();try{retention=new DiagnosticsRetentionStore(directory);unclean=retention.RecoverUncleanOperations(PeerChat.VersionInfo.Display);}catch(Exception error){TraceUi("diagnostic retention initialization unavailable: "+error.GetType().Name);}diagnosticsRetention=retention;startupUncleanOperations=unclean;
        foreach(var binding in conversationIndex.Records){bool reused;model.BindFriend(binding.AccountId,binding.Username,binding.CurrentPeerId,binding.KnownPeerIds,out reused).IsFriend=binding.ActiveFriend;}
        profileManager=manager;localProfile=selectedProfile;
        diagnosticsService=new DiagnosticsService(new DiagnosticsSources{Connection=()=>ConnectionDiagnosticsText(),ConnectionEvents=()=>connectionLog.Read(),Security=()=>localTlsCertificateDiagnostics,VoiceData=CaptureVoiceDiagnosticsData,ScreenShare=()=>String.IsNullOrEmpty(lastScreenDiagnosticText)?core.ScreenShareDiagnostics():lastScreenDiagnosticText,Local=()=>"profile_id="+(localProfile==null?"local":localProfile.Id)+"\nlocal_peer_id="+(model.OwnId??"unavailable"),Status=()=>model.Status,Build=()=>PeerChat.VersionInfo.Display,Profile=()=>localProfile==null?"Local":localProfile.Name});
        using(var stream=Assembly.GetExecutingAssembly().GetManifestResourceStream("Main.xaml"))window=(Window)XamlReader.Load(stream);string iconPath=System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"peerchat-client.ico");if(File.Exists(iconPath))window.Icon=new BitmapImage(new Uri(iconPath));
        if(compact){window.Width=1120;window.Height=740;}
        // Profile selection and any legacy migration finish before the native owner exists.
        core=new CoreAdapter();
        window.SourceInitialized+=(s,e)=>{int enabled=1;var handle=new System.Windows.Interop.WindowInteropHelper(window).Handle;try{if(DwmSetWindowAttribute(handle,20,ref enabled,4)!=0)DwmSetWindowAttribute(handle,19,ref enabled,4);}catch(EntryPointNotFoundException){}};
        BindActions();LoadVoiceDevices();core.SetVoiceVolumes(voicePreferences.InputVolume,voicePreferences.OutputVolume);core.SetVoiceProcessing(voicePreferences.AudioProcessingEnabled,voicePreferences.NoiseSuppressionEnabled,voicePreferences.AutomaticGainEnabled);timer.Tick+=(s,e)=>Poll();voiceTimer.Tick+=(s,e)=>RefreshVoiceUi();networkFingerprint=CurrentNetworkFingerprint();networkChangeTimer.Tick+=(s,e)=>{networkChangeTimer.Stop();HandleNetworkChange();};NetworkChange.NetworkAddressChanged+=NetworkAddressChanged;
        window.Loaded+=async(s,e)=>await Start(port,"");
        window.Loaded+=(s,e)=>{if(startupUncleanOperations.Count>0)window.Dispatcher.BeginInvoke((Action)(()=>ShowFailureNotification(startupUncleanOperations[0])));};
        window.Closing+=async(s,e)=>{if(closed)return;e.Cancel=true;if(closing)return;closing=true;NetworkChange.NetworkAddressChanged-=NetworkAddressChanged;networkChangeTimer.Stop();timer.Stop();voiceTimer.Stop();if(incomingVoiceWindow!=null)incomingVoiceWindow.Close();window.IsEnabled=false;try{await startupTask;}catch{}await Task.Run(()=>{if(discovery!=null)discovery.StopEventChannel();StopRelays();core.Dispose();});try{if(diagnosticsRetention!=null)diagnosticsRetention.CompleteAllOperations();}catch(Exception error){TraceUi("clean-exit diagnostic marker completion failed: "+error.GetType().Name);}closed=true;window.Close();};
        window.SizeChanged+=(s,e)=>SizeBubbles();
        if(capturePath!=null)window.Loaded+=async(s,e)=>{await Task.Delay(3500);window.UpdateLayout();var content=(FrameworkElement)window.Content;var bitmap=new System.Windows.Media.Imaging.RenderTargetBitmap((int)content.ActualWidth,(int)content.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(content);var encoder=new System.Windows.Media.Imaging.PngBitmapEncoder();encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));using(var output=File.Create(capturePath))encoder.Save(output);window.Close();};
        Refresh();
    }
    T Find<T>(string name) where T:FrameworkElement {return (T)window.FindName(name);}
    void Click(string name,Action action){Find<Button>(name).Click+=(s,e)=>{var started=Stopwatch.StartNew();TraceUi(name+" action started");try{action();}catch(Exception error){Notify(error.Message);}finally{TraceUi(name+" action returned after "+started.ElapsedMilliseconds+" ms");}};}
    void BindActions(){
        Find<Button>("ViewConnectionLog").Content="Connection & Diagnostics";
        Find<Button>("Details").Content="Shareable Endpoint";
        // Windows' bundled symbol font gives consistent icons without image assets.
        foreach(string name in new[]{"AddFriend","QuickAdd"})ButtonIcon(name,"\uE8FA","Add Friend");
        ButtonIcon("FindFriend","\uE721","Find Friends");ButtonIcon("FriendRequests","\uE8A7","Friend Requests");
        ButtonIcon("AccountRegister","\uE77B","Create Account");ButtonIcon("AccountLogin","\uE72E","Sign In");ButtonIcon("AccountSignOut","\uE7E8","Sign Out");
        foreach(string name in new[]{"CopyId","QuickCopy"})ButtonIcon(name,"\uE8C8","Copy My ID");
        ButtonIcon("CopyCompact","\uE8C8",null);var profileCard=new StackPanel{Orientation=Orientation.Horizontal};profileCard.Children.Add(new Border{Width=34,Height=34,CornerRadius=new CornerRadius(17),Background=Brush("#365D89"),Child=new TextBlock{Text="P",FontSize=18,HorizontalAlignment=HorizontalAlignment.Center,VerticalAlignment=VerticalAlignment.Center}});var profileText=new StackPanel{Margin=new Thickness(10,0,0,0),VerticalAlignment=VerticalAlignment.Center};profileName.Text=localProfile==null?"Your profile":localProfile.Name;profileName.FontWeight=FontWeights.SemiBold;profileHandle.Text="Local profile and account";profileHandle.Foreground=Brush("#A1AEBB");profileHandle.FontSize=11;profileText.Children.Add(profileName);profileText.Children.Add(profileHandle);profileCard.Children.Add(profileText);Find<Button>("Profile").Content=profileCard;
        foreach(string name in new[]{"Manual","QuickManual"})ButtonIcon(name,"\uE774","Manual Connect");
		ButtonIcon("ViewConnectionLog","\uE713",null);ButtonIcon("Settings","\uE713","Settings");Find<Button>("Settings").ToolTip="Settings";AutomationProperties.SetName(Find<Button>("Settings"),"Settings");ButtonIcon("Reconnect","\uE72C","Try Reconnect");
		ButtonIcon("Details","\uE701","Shareable Endpoint");ButtonIcon("PeerInfo","\uE946",null);ButtonIcon("VoiceCall","\uE717",null);ButtonIcon("Send","\uE724",null);ButtonIcon("Attachment","\uE723",null);
        foreach(string name in new[]{"AddFriend","QuickAdd"})Click(name,AddFriend);
        Click("FindFriend",FindFriend);Click("FriendRequests",FriendRequestsDialog);Click("AccountRegister",()=>Account(false));Click("AccountLogin",()=>Account(true));Click("AccountSignOut",SignOut);
        foreach(string name in new[]{"CopyId","QuickCopy","CopyCompact"})Click(name,CopyId);
        Click("Profile",profileManager==null?(Action)Profile:SwitchLocalProfile);Click("Settings",SettingsPage);
        foreach(string name in new[]{"Manual","QuickManual"})Click(name,Manual);
        Click("DM",()=>Switch(false));Click("Groups",()=>Switch(true));Click("GroupNew",CreateGroup);
        Click("EmptyAction",()=>{if(model.Groups)CreateGroup();else AddFriend();});
        Click("Send",Send);Click("VoiceCall",StartVoiceCall);Click("VoiceHangUp",EndVoiceCall);Click("VoiceMute",ToggleVoiceMute);Click("VoiceDeafen",ToggleVoiceDeafen);Click("VoiceDetails",ShowVoiceDiagnostics);Click("VoiceRefreshDevices",RefreshVoiceDevices);Click("CopyVoiceDiagnostics",CopyVoiceDiagnostics);Click("Reconnect",Reconnect);Click("CopyConnectionDetails",CopyConnectionDetails);Click("ViewConnectionLog",ViewConnectionLog);Click("Older",Older);
        BuildScreenShareControls();
        Find<ScrollViewer>("MessageScroll").ScrollChanged+=(s,e)=>{if(e.VerticalChange<0&&e.VerticalOffset<=48)Older();};
        Click("PeerInfo",ShowConversationFlyout);Click("GroupMembers",()=>ShowGroupFlyout("GroupMembers","members"));Click("GroupInvite",()=>ShowGroupFlyout("GroupInvite","invite"));Click("GroupOverflow",()=>ShowGroupFlyout("GroupOverflow","settings"));
        Click("Details",EditShareableEndpoint);
        Click("ToggleRightPane",ToggleRightPane);
        ArrangeHeaderActions();
        Find<TextBox>("Composer").TextChanged+=(s,e)=>{Find<TextBlock>("ComposerHint").Visibility=String.IsNullOrEmpty(Find<TextBox>("Composer").Text)?Visibility.Visible:Visibility.Collapsed;if(model.Groups&&model.SelectedGroup!=null)model.SelectedGroup.Draft=Find<TextBox>("Composer").Text;else if(!model.Groups&&model.Selected!=null)model.Selected.Draft=Find<TextBox>("Composer").Text;};
        Find<TextBox>("Composer").PreviewKeyDown+=(s,e)=>{if(e.Key==Key.Enter&&(Keyboard.Modifiers&ModifierKeys.Shift)==0){e.Handled=true;Send();}};
        Find<TextBox>("Search").TextChanged+=(s,e)=>RefreshList();
        Find<ListBox>("ContactList").SelectionChanged+=(s,e)=>{if(changingSelection)return;if(model.Groups){var selectedGroup=Find<ListBox>("ContactList").SelectedItem as GroupModel;if(selectedGroup!=null){model.SelectedGroup=selectedGroup;selectedGroup.Unread=0;selectedGroup.Refresh();UpdateUnreadIndicator();Find<TextBox>("Composer").Text=selectedGroup.Draft;RequestGroupHistory(selectedGroup);ShowConversation();Refresh();}}else{var selected=Find<ListBox>("ContactList").SelectedItem as ConversationModel;if(selected!=null){model.Selected=selected;selected.Unread=0;selected.Refresh();UpdateUnreadIndicator();Find<TextBox>("Composer").Text=selected.Draft;RequestConversationHistory(selected);ShowConversation();Refresh();}}};
        Find<Expander>("VoiceAdvancedExpander").Expanded+=(s,e)=>RefreshVoiceDiagnosticsText();
        var removeMenu=new ContextMenu();var clearHistoryItem=new MenuItem{Header="Delete Conversation History"};var removeItem=new MenuItem{Header="Remove Contact"};removeMenu.Items.Add(clearHistoryItem);removeMenu.Items.Add(new Separator());removeMenu.Items.Add(removeItem);clearHistoryItem.Click+=(s,e)=>DeleteSelectedConversationHistory();removeItem.Click+=(s,e)=>RemoveSelectedContact();
        var list=Find<ListBox>("ContactList");list.ContextMenu=removeMenu;list.PreviewMouseRightButtonDown+=(s,e)=>{var element=e.OriginalSource as DependencyObject;var item=ItemsControl.ContainerFromElement(list,element) as ListBoxItem;if(item!=null){item.IsSelected=true;item.Focus();}};
    }
    void ToggleRightPane(){
        var column=(ColumnDefinition)window.FindName("RightPaneColumn");var pane=Find<Border>("RightDetailsPane");var button=Find<Button>("ToggleRightPane");bool opening=column.Width.Value<=0;
        if(opening){pane.Visibility=Visibility.Visible;column.Width=new GridLength(292);button.Content="‹";button.ToolTip="Collapse Security and Connection pane";AutomationProperties.SetName(button,"Collapse Security and Connection pane");}
        else{pane.Visibility=Visibility.Collapsed;column.Width=new GridLength(0);button.Content="›";button.ToolTip="Show Security and Connection pane";AutomationProperties.SetName(button,"Show Security and Connection pane");}
    }
    void ArrangeHeaderActions(){
        var actions=Find<Button>("VoiceCall").Parent as StackPanel;if(actions==null)return;
        var call=Find<Button>("VoiceCall");var diagnostics=Find<Button>("ViewConnectionLog");var toggle=Find<Button>("ToggleRightPane");
        actions.Children.Remove(call);call.Width=36;call.Height=36;call.Padding=new Thickness(0);call.Margin=new Thickness(6,0,0,0);call.ToolTip="Call";AutomationProperties.SetName(call,"Call");
        if(screenStartButton!=null){actions.Children.Remove(screenStartButton);actions.Children.Add(screenStartButton);}actions.Children.Add(call);
        var diagnosticsParent=diagnostics.Parent as Panel;if(diagnosticsParent!=null)diagnosticsParent.Children.Remove(diagnostics);diagnostics.Width=36;diagnostics.Height=36;diagnostics.Padding=new Thickness(0);diagnostics.Margin=new Thickness(6,0,0,0);diagnostics.ToolTip="Connection & Diagnostics";AutomationProperties.SetName(diagnostics,"Connection & Diagnostics");actions.Children.Add(diagnostics);
        var toggleBorder=toggle.Parent as Border;if(toggleBorder!=null){toggleBorder.Child=null;var layout=Find<Grid>("MainLayout");layout.Children.Remove(toggleBorder);}
        toggle.Width=36;toggle.Height=36;toggle.Padding=new Thickness(0);toggle.Margin=new Thickness(6,0,0,0);toggle.FontSize=19;toggle.ToolTip="Collapse Security and Connection pane";AutomationProperties.SetName(toggle,"Collapse Security and Connection pane");actions.Children.Add(toggle);
        var layoutGrid=Find<Grid>("MainLayout");if(layoutGrid.ColumnDefinitions.Count>2)layoutGrid.ColumnDefinitions[2].Width=new GridLength(0);
    }
    // Centralized lightweight geometry set. The same line weight and blue/white palette is used
    // for every action; the OS font glyphs are intentionally not used as app branding.
    static string IconGeometry(string glyph,string text){if(text!=null&&text.IndexOf("Find",StringComparison.OrdinalIgnoreCase)>=0)return "M9,2 A7,7 0 1 0 9,16 A7,7 0 1 0 9,2 M14,14 L20,20 M9,5 A4,4 0 1 0 9,13";if(text!=null&&text.IndexOf("Request",StringComparison.OrdinalIgnoreCase)>=0)return "M8,8 A4,4 0 1 0 8,0 A4,4 0 1 0 8,8 M1,20 C1,14 15,14 15,20 M16,12 L16,20 M12,16 L20,16";if(text!=null&&text.IndexOf("Copy",StringComparison.OrdinalIgnoreCase)>=0||glyph=="\uE8C8")return "M7,6 H18 V20 H7 Z M3,2 H14 V6 M3,2 V16 H7";if(text!=null&&(text.IndexOf("Security",StringComparison.OrdinalIgnoreCase)>=0||text.IndexOf("Connection",StringComparison.OrdinalIgnoreCase)>=0))return "M11,2 L20,5 V11 C20,17 16,21 11,23 C6,21 2,17 2,11 V5 Z M7,11 A4,4 0 0 1 15,11 V16 H7 Z";if((text!=null&&text.IndexOf("Settings",StringComparison.OrdinalIgnoreCase)>=0)||glyph=="\uE713")return "M11,2 L13,5 A8,8 0 0 1 16,7 L20,6 L22,10 L18,13 A8,8 0 0 1 18,17 L21,20 L18,23 L14,20 A8,8 0 0 1 9,20 L5,23 L2,20 L5,16 A8,8 0 0 1 5,11 L1,9 L3,5 L8,7 A8,8 0 0 1 11,2 M11,9 A3,3 0 1 0 11,15 A3,3 0 1 0 11,9";if(glyph=="\uE717"||text!=null&&text.Equals("Call",StringComparison.OrdinalIgnoreCase))return "M5,3 C4,3 3,4 3,6 C3,14 10,21 18,21 C20,21 21,20 21,18 L16,15 L13,18 C10,17 7,14 6,11 L9,8 Z";if(text!=null&&text.IndexOf("Manual",StringComparison.OrdinalIgnoreCase)>=0)return "M4,4 H20 M12,4 V20 M4,20 H20 M7,8 H17 M7,13 H17";if(text!=null&&(text.IndexOf("Profile",StringComparison.OrdinalIgnoreCase)>=0||text.IndexOf("Account",StringComparison.OrdinalIgnoreCase)>=0||text.IndexOf("Sign In",StringComparison.OrdinalIgnoreCase)>=0))return "M11,10 A4,4 0 1 0 11,2 A4,4 0 1 0 11,10 M3,22 C3,15 19,15 19,22";if(text!=null&&text.IndexOf("Sign Out",StringComparison.OrdinalIgnoreCase)>=0)return "M4,12 H18 M13,7 L18,12 L13,17 M4,5 V19";if(text!=null&&text.IndexOf("Details",StringComparison.OrdinalIgnoreCase)>=0||glyph=="\uE701")return "M11,2 A9,9 0 1 0 11,20 A9,9 0 1 0 11,2 M11,10 V16 M11,6 V7";if(text!=null&&text.IndexOf("Reconnect",StringComparison.OrdinalIgnoreCase)>=0)return "M19,8 A9,9 0 1 0 19,16 M19,8 V3 M19,8 H14";if(text!=null&&text.IndexOf("Send",StringComparison.OrdinalIgnoreCase)>=0||glyph=="\uE724")return "M2,3 L21,11 L2,20 L6,12 Z M6,12 H21";if(text!=null&&text.IndexOf("Attachment",StringComparison.OrdinalIgnoreCase)>=0||glyph=="\uE723")return "M8,17 L16,9 A4,4 0 0 0 10,3 L4,9 A6,6 0 0 0 13,18 L19,12";return "M3,3 H19 V19 H3 Z";}
    void ButtonIcon(string name,string glyph,string text){var row=new StackPanel{Orientation=Orientation.Horizontal,VerticalAlignment=VerticalAlignment.Center};row.Children.Add(new System.Windows.Shapes.Path{Data=Geometry.Parse(IconGeometry(glyph,text)),Width=21,Height=21,Stretch=Stretch.Uniform,Stroke=Brush("#78BFFF"),StrokeThickness=1.8,Fill=Brush("Transparent"),VerticalAlignment=VerticalAlignment.Center});if(text!=null)row.Children.Add(new TextBlock{Text=text,Margin=new Thickness(14,0,0,0),VerticalAlignment=VerticalAlignment.Center});Find<Button>(name).Content=row;}
    // The compact sidebar keeps request counts on the Friend Requests action instead of adding
    // raw Friends/Incoming/Pending text above the conversation list.
    void UpdateFriendRequestsButton(){var button=Find<Button>("FriendRequests");var row=new Grid();row.ColumnDefinitions.Add(new ColumnDefinition());row.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});var label=new StackPanel{Orientation=Orientation.Horizontal,VerticalAlignment=VerticalAlignment.Center};label.Children.Add(new System.Windows.Shapes.Path{Data=Geometry.Parse(IconGeometry("\uE8A7","Friend Requests")),Width=21,Height=21,Stretch=Stretch.Uniform,Stroke=Brush("#78BFFF"),StrokeThickness=1.8,Fill=Brush("Transparent"),VerticalAlignment=VerticalAlignment.Center});label.Children.Add(new TextBlock{Text="Friend Requests",Margin=new Thickness(14,0,0,0),VerticalAlignment=VerticalAlignment.Center});row.Children.Add(label);if(friendState.Incoming.Count>0){var badge=new Border{Background=Brush("#275AA7"),CornerRadius=new CornerRadius(12),Padding=new Thickness(8,3,8,3),Child=new TextBlock{Text=friendState.Incoming.Count.ToString(),FontWeight=FontWeights.SemiBold,FontSize=12}};Grid.SetColumn(badge,1);row.Children.Add(badge);}button.ToolTip=friendState.Friends.Count+" friend"+(friendState.Friends.Count==1?"":"s")+", "+friendState.Incoming.Count+" incoming request"+(friendState.Incoming.Count==1?"":"s");button.Content=row;}
    async Task Start(ushort port,string name){
        if(busy)return;busy=true;timer.Stop();model.Listening=false;model.Status="Opening your encrypted profile…";Refresh();
        try{
            // A zero override means "reuse the port stored in the encrypted profile." The native
            // Ready event is therefore the authoritative place to learn the real listener port.
            firewallConfiguredPort=0;
            startupTask=Task.Run(()=>core.Start(directory,port,name));
            await startupTask;
            core.SetNetworkContext(networkGeneration,networkFingerprint);
            // A replacement is healthy only after its local native profile/core initialization succeeds.
            if(!String.IsNullOrWhiteSpace(updateHealthToken)){
                try{UpdateHealthReporter.MarkHealthyFromCommandLine(new[]{"--peerchat-update-health-token",updateHealthToken});TraceStartup(profileManager==null?AppDomain.CurrentDomain.BaseDirectory:profileManager.InstallRoot,"update-health-confirmed");}
                catch(Exception healthError){TraceStartup(profileManager==null?AppDomain.CurrentDomain.BaseDirectory:profileManager.InstallRoot,"update-health-confirm-failed type="+healthError.GetType().Name);}
            }
        }catch(Exception error){model.Status=error.Message;}
        busy=false;if(!closing)timer.Start();Refresh();
    }
    void UpdateUnreadIndicator(){
        int unread=model.Conversations.Values.Sum(item=>item.Unread)+model.GroupChats.Values.Sum(item=>item.Unread);
        window.Title=unread==0?"PeerChat":"PeerChat • "+unread+" unread";
    }
    void NotifyIncomingMessage(){
        // Use both the Windows message-beep API and the managed system sound. Some
        // Windows profiles route one path to a muted event while the other remains audible.
        try{MessageBeep(0xFFFFFFFF); }catch{}
        try{System.Media.SystemSounds.Asterisk.Play();}catch{}
        UpdateUnreadIndicator();
        if(!window.IsActive){var info=new FlashInfo{size=(uint)Marshal.SizeOf(typeof(FlashInfo)),window=new System.Windows.Interop.WindowInteropHelper(window).Handle,flags=FlashTaskbar,count=3,timeout=0};try{FlashWindowEx(ref info);}catch{}}
    }
    string DiscoverySessionPath { get { return Path.Combine(directory,"discovery-session.dat"); } }
    string DiscoveryServicePath { get { return Path.Combine(directory,"discovery-service.dat"); } }
    string BootstrapServicePath { get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"service-bootstrap.json"); } }
    // Service endpoint and the development certificate pin are DPAPI-protected per Windows user.
    // They are configuration/trust data only; no password or account session is stored here.
    string[] LoadDiscoveryService(){try{if(File.Exists(DiscoveryServicePath)){var values=Encoding.UTF8.GetString(ProtectedData.Unprotect(File.ReadAllBytes(DiscoveryServicePath),null,DataProtectionScope.CurrentUser)).Split(new[]{"\n"},StringSplitOptions.None);int port;if(values.Length==3&&Int32.TryParse(values[1],out port)&&port>0&&port<65536)return new[]{values[0],port.ToString(),values[2]};}}catch{}try{if(File.Exists(BootstrapServicePath)){var values=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(BootstrapServicePath));int port=values.ContainsKey("port")?Convert.ToInt32(values["port"]):7443;string host=values.ContainsKey("host")?values["host"] as string:"";string pin=values.ContainsKey("sha256")?values["sha256"] as string:"";if(!String.IsNullOrWhiteSpace(host)&&port>0&&port<65536)return new[]{host,port.ToString(),pin??""};}}catch{}return new[]{"173.225.14.37","7443",""};}
    void SaveDiscoveryService(string host,int port,string pin){Directory.CreateDirectory(directory);File.WriteAllBytes(DiscoveryServicePath,ProtectedData.Protect(Encoding.UTF8.GetBytes(String.Join("\n",new[]{host,port.ToString(),pin??""})),null,DataProtectionScope.CurrentUser));}
    bool ConfirmDiscoveryCertificate(string host,int port,string fingerprint,bool replacement){return window.Dispatcher.Invoke(()=>Confirm(replacement?"Server identity changed":"Trust development server",replacement?"The configured PeerChat server at "+host+":"+port+" presented a different TLS identity. This could mean the server was rebuilt, or it could indicate an interception attempt.\n\nNew SHA-256 identity:\n"+fingerprint+"\n\nTrust this replacement?":"You are connecting to the self-hosted development PeerChat server at "+host+":"+port+". Verify this identity with the server owner before continuing.\n\nSHA-256 identity:\n"+fingerprint+"\n\nTrust this server?","Trust"));}
    DiscoveryClient NewDiscoveryClient(){var service=LoadDiscoveryService();int port=Int32.Parse(service[1]);return new DiscoveryClient(service[0],port,service[2],ConfirmDiscoveryCertificate);}
    // Account ID and renewable session material are DPAPI-protected inside exactly one profile.
    // The password is never retained. Legacy session files are imported only after server validation.
    void SaveDiscoverySession(){if(discovery==null||String.IsNullOrEmpty(discovery.SessionToken))return;SaveDiscoveryService(discovery.Host,discovery.Port,discovery.CertificateSha256);accountLink=accountStore.SaveSession(discovery.AccountId,discovery.Username,discovery.Host,discovery.Port,discovery.CertificateSha256,discovery.SessionToken);try{if(File.Exists(DiscoverySessionPath))File.Delete(DiscoverySessionPath);}catch{}}
    async Task<bool> EnsureMediaIceConfigurationAsync(){
        var activeDiscovery=discovery;
        long now=DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        if(activeDiscovery==null||String.IsNullOrEmpty(model.OwnId))return false;
        if(mediaTurnCredentials!=null&&mediaTurnCredentials.ExpiresUtc>now+60)return true;
        if(mediaTurnRefreshInFlight)return false;
        mediaTurnRefreshInFlight=true;
        try{
            var issued=await Task.Run(()=>activeDiscovery.GetMediaTurnCredentials(model.OwnId));
            // Account/profile or discovery session changes invalidate an in-flight result.
            if(closing||activeDiscovery!=discovery||issued==null||issued.ExpiresUtc<=DateTimeOffset.UtcNow.ToUnixTimeSeconds()+30)return false;
            ulong next=++mediaIceConfigurationGeneration;
            if(!core.SetMediaIceConfiguration(issued,profileSessionGeneration,discoverySessionGeneration,next))return false;
            mediaTurnCredentials=issued;
            return true;
        }catch(Exception error){
            // The exception can include remote protocol text; retain only a bounded
            // local status and never log credential material.
            model.Status="Media relay credentials are unavailable. Messaging remains connected.";
            TraceUi("TURN credential request failed: "+error.GetType().Name);
            Refresh();
            return false;
        }finally{mediaTurnRefreshInFlight=false;}
    }
    static bool SessionRejected(Exception error){string value=(error.Message??"").ToLowerInvariant();return value.Contains("session_invalid")||value.Contains("different server account")||value.Contains("already linked");}
    async void RestoreDiscoverySession(){
        if(discoveryRestored||discovery!=null||String.IsNullOrEmpty(model.OwnId))return;discoveryRestored=true;accountLink=accountStore.Load();DiscoveryClient saved=null;long expectedAccount=accountLink==null?0:accountLink.AccountId;
        try{
            if(accountLink!=null&&accountLink.HasSession)saved=new DiscoveryClient(accountLink.Host,accountLink.Port,accountLink.CertificateSha256,ConfirmDiscoveryCertificate){Username=accountLink.Username,SessionToken=accountLink.SessionToken,AccountId=accountLink.AccountId};
            else if(accountLink==null&&File.Exists(DiscoverySessionPath)){var lines=Encoding.UTF8.GetString(ProtectedData.Unprotect(File.ReadAllBytes(DiscoverySessionPath),null,DataProtectionScope.CurrentUser)).Split(new[]{"\n"},StringSplitOptions.None);int serverPort;long legacyAccount=0;if((lines.Length!=5&&lines.Length!=6)||!Int32.TryParse(lines[1],out serverPort))throw new InvalidDataException("Legacy account session is invalid.");if(lines.Length==6)Int64.TryParse(lines[5],out legacyAccount);expectedAccount=legacyAccount;saved=new DiscoveryClient(lines[0],serverPort,lines[2],ConfirmDiscoveryCertificate){Username=lines[3],SessionToken=lines[4],AccountId=legacyAccount};}
            if(saved==null){model.Status=accountLink==null?(accountStore.Exists?"Protected account link unavailable · Offline / Server Relay":"Local profile ready · Sign in, create an account, or continue offline"):"@"+accountLink.Username+" signed out · Local history remains available";Refresh();PromptProfileAccountSetup();return;}
            await Task.Run(()=>saved.RefreshSessionInfo());if(expectedAccount>0&&saved.AccountId!=expectedAccount)throw new InvalidOperationException("This profile session belongs to a different server account.");var presence=await Task.Run(()=>saved.ReportPresence(model.OwnId,"",0,"",0,false));discovery=saved;discoverySessionGeneration++;mediaTurnCredentials=null;ApplyPresenceDiagnostics(presence);core.SetGroupAccount(saved.AccountId,saved.Username);core.LoadGroups();saved.StartEventChannel(model.OwnId,HandleDiscoveryEvent);SaveDiscoverySession();model.Status="Signed in as "+saved.Username+" · Discoverable";Refresh();RefreshFriendState();
        }catch(Exception error){if(SessionRejected(error)){accountLink=accountStore.ClearSession();try{if(File.Exists(DiscoverySessionPath))File.Delete(DiscoverySessionPath);}catch{}}string linked=accountLink==null?"Unlinked profile":"@"+accountLink.Username;model.Status=linked+" · Offline / Server Relay · "+(SessionRejected(error)?"sign-in required":"server unavailable");Refresh();}
    }
    void Poll(){
        if(busy||closing)return;
        try{
            // Push notifications are the normal update path. Keep only a slow reconciliation
            // snapshot: three minutes while the event channel is healthy, and one minute while
            // it is reconnecting. Login and event-channel reconnects request an immediate
            // authoritative snapshot, so this timer is only a repair mechanism.
            // Events own the normal presence path. A snapshot is only missed-event repair,
            // never a short-lived online probe.
            TimeSpan snapshotCadence=discovery!=null&&discovery.EventChannelConnected?TimeSpan.FromMinutes(15):TimeSpan.FromMinutes(3);
            if(discovery!=null&&!friendRefreshInFlight&&DateTime.UtcNow-lastFriendRefreshUtc>=snapshotCadence){TraceUi("friend snapshot refresh dispatched");RefreshFriendState();}
            // Presence is a small authenticated heartbeat. It updates only this device's
            // presence/candidates and never fetches or rebuilds the friend snapshot.
            if(discovery!=null&&!mediaTurnRefreshInFlight&&(model.VoiceStateCode!=0||activeScreenState!=0)&&
                (mediaTurnCredentials==null||mediaTurnCredentials.ExpiresUtc<=DateTimeOffset.UtcNow.ToUnixTimeSeconds()+60))_ = EnsureMediaIceConfigurationAsync();
            var pollTimer=Stopwatch.StartNew();var events=core.Poll();var pollMs=pollTimer.ElapsedMilliseconds;if(pollMs>20)TraceUi("native core poll took "+pollMs+" ms and returned "+events.Length+" event(s)");RefreshVoiceTelemetry();if(events.Length==0)return;
            bool scroll=false,incomingNotification=false;
            foreach(var e in events){
                // Live incoming messages notify even when their conversation is selected.
                // History replay (kind 6) never notifies, and outgoing echoes are ignored.
                bool incoming=e.kind==5&&e.messages!=null&&e.messages.Any(message=>message.sender!=model.OwnId&&!model.Get(e.peer).Messages.Any(existing=>existing.Data.id==message.id));
                bool incomingGroup=e.kind==10&&e.groupEvents!=null&&e.groupEvents.Any(message=>message.sender!=model.OwnId);
                bool initialDmHistory=e.kind==6&&model.Selected!=null&&model.FindByPeer(e.peer)==model.Selected&&model.Selected.Messages.Count==0;
                bool initialGroupHistory=e.kind==11&&model.SelectedGroup!=null&&e.peer==model.SelectedGroup.Id&&model.SelectedGroup.Messages.Count==0;
                model.Apply(e);
                if(e.kind==9){if(InviteCandidatesChanged!=null)InviteCandidatesChanged();if(model.SelectedGroup!=null)RequestGroupHistory(model.SelectedGroup);}
                if(e.kind==13)HandleVoiceEvent(e);
                if(e.kind==19)HandleScreenShareEvent(e);
                if(incoming||incomingGroup)incomingNotification=true;
                if(e.kind==0){localTlsCertificateDiagnostics=String.IsNullOrWhiteSpace(e.detail)?"Unavailable":e.detail;if(profileManager!=null)TraceStartup(profileManager.InstallRoot,"native-ready profile="+localProfile.Id+" relay-only="+(e.port==0)+"; "+localTlsCertificateDiagnostics);RestoreDiscoverySession();}
                if(e.kind==14){var attempting=model.FindByPeer(e.expected)??model.FindByPeer(e.peer);if(attempting!=null&&!attempting.Online&&attempting.SessionRevision==0){attempting.ConnectionState="Connecting";attempting.Refresh();}connectionAttempts.Update(e,attempting,"Connecting","Not yet presented",null);RecordConnection("connect",attempting,"attempt started",e);UpdateDiagnosticOperation("Connection",e.attempt.ToString(),"connecting · "+(e.source??"route"),e.sessionRevision,diagnosticsService.Capture());}
                if(e.kind==15){var scheduled=model.FindByPeer(e.expected)??model.FindByPeer(e.peer);if(scheduled!=null&&!scheduled.Online&&scheduled.SessionRevision==0){scheduled.ConnectionState="Retrying with backoff";scheduled.Refresh();}connectionAttempts.Update(e,scheduled,"Waiting to retry",null,null);RecordConnection("retry",scheduled,"scheduled in "+e.retryMs+" ms",e);UpdateDiagnosticOperation("Connection",e.attempt.ToString(),"retry scheduled",e.sessionRevision,diagnosticsService.Capture());}
                if(e.kind==17){var sessionConversation=model.FindByPeer(e.expected)??model.FindByPeer(e.peer);connectionAttempts.Update(e,sessionConversation,e.text,e.connected?"Verified":null,null);}
                if(e.kind==18)StartRelay(e);
                if(e.kind==2){
                    var identityConversation=model.FindByPeer(e.expected)??model.FindByPeer(e.peer);
                    bool previouslyPinned=identityConversation!=null&&!String.IsNullOrEmpty(identityConversation.PreviousVerifiedPeerId);
                    bool presentedMismatch=!String.IsNullOrEmpty(e.expected)&&!String.IsNullOrEmpty(e.peer)&&!String.Equals(e.expected,e.peer,StringComparison.Ordinal);
                    bool initialPinRejected=(e.text??"").StartsWith("Initial automatic device pinning was rejected",StringComparison.Ordinal);
                    // This is deliberately only a request. The core independently validates the
                    // authenticated friendship snapshot, session attempt, key hash and pin state.
                    if(!previouslyPinned&&!presentedMismatch&&!initialPinRejected&&e.attempt>0&&automaticInitialPinAttempts.Add(e.attempt)&&
                       core.PinInitialAuthenticatedFriendIdentity(e.peer,(ulong)e.attempt,authenticatedFriendSnapshotGeneration)){
                        connectionAttempts.Update(e,identityConversation,"Authenticating initial friend device","Initial pin requested",null);
                        RecordConnection("initial-identity-pin",identityConversation,"native core initial authenticated-friend pin requested",e);
                        TraceUi("Initial authenticated friend identity pin requested for current attempt="+e.attempt);
                        continue;
                    }
                    if(previouslyPinned)e.text="Peer identity changed for "+identityConversation.Name+". Previously verified "+identityConversation.PreviousVerifiedPeerId+"; current device "+e.peer+". Explicit verification is required.";
                    string failure=previouslyPinned?"Peer identity changed":"Identity verification required";
                    connectionAttempts.Update(e,identityConversation,"TLS identity mismatch","Verification required",failure);RecordConnection("trust",identityConversation,failure,e);
                    string key=(e.expected??"")+"|"+(e.peer??"")+"|"+e.attempt;
                    if(!trustPrompted.Contains(key)&&!trustSuppressed.Contains(key)){trustPrompted.Add(key);trustRequests.Enqueue(Tuple.Create(e,key));}
                    TraceUi("Identity diagnostics: expected Peer ID="+(String.IsNullOrEmpty(e.expected)?"unspecified":e.expected)+"; presented Peer ID="+(e.peer??"unknown")+"; attempt="+e.attempt);
                }
                if(e.kind==8&&(e.text??"").StartsWith("Voice audio processing degraded:",StringComparison.Ordinal)){var reason=(e.text??"").Substring("Voice audio processing degraded:".Length).Trim();RecordConnection("voice-apm",model.FindByPeer(e.peer),reason,e);RetainFailure("Voice processing degraded — raw microphone audio continuing","Voice",model.VoiceCallId,"audio_processing",reason,"microphone capture",model.VoiceMediaGeneration,null);}
                else if(e.kind==8&&(e.text??"").StartsWith("Voice audio device error:",StringComparison.Ordinal)){voiceAudioError=(e.text??"").Substring("Voice audio device error:".Length).Trim();latestVoiceAudioError=voiceAudioError;var voiceConversation=model.FindByPeer(e.peer);RecordConnection("voice-audio",voiceConversation,voiceAudioError,e);RetainFailure("Voice stopped — Audio Device Error","Voice",model.VoiceCallId,"audio_device",voiceAudioError,"voice media",model.VoiceMediaGeneration,null);model.Status="Voice audio unavailable · "+voiceAudioError;RefreshVoiceUi();}
                 else if(e.kind==8){string failure=ConnectionFailureSummary(e.text);var failed=model.FindByPeer(e.expected)??model.FindByPeer(e.peer)??(String.IsNullOrEmpty(e.expected)&&String.IsNullOrEmpty(e.peer)?model.Selected:null);connectionAttempts.Update(e,failed,e.text??"Failed",null,failure);RecordConnection("failure",failed,failure,e);RetainFailure("Connection failed — "+failure,"Connection",e.attempt.ToString(),"reliable_transport",failure,e.text??"attempt failed",e.sessionRevision,null);}
                if(e.kind==3&&!model.Groups){var connected=model.FindByPeer(e.expected)??model.FindByPeer(e.peer)??model.Get(e.peer);connectionAttempts.Update(e,connected,"Ready","Verified","None");RecordConnection("connected",connected,"TCP and TLS authenticated; identity verified",e);CompleteDiagnosticOperation("Connection",e.attempt.ToString());model.Selected=connected;RequestConversationHistory(model.Selected);ShowConversation();}
                if(e.kind==4){var disconnected=model.FindByPeer(e.expected)??model.FindByPeer(e.peer);connectionAttempts.Update(e,disconnected,e.text??"Disconnected",null,null);RecordConnection("disconnected",disconnected,e.text??"Disconnected",e);CompleteDiagnosticOperation("Connection",e.attempt.ToString());}
                if(e.kind==16){var lifecycle=model.FindByPeer(e.expected)??model.FindByPeer(e.peer);connectionAttempts.Update(e,lifecycle,null,null,null);RecordConnection("connection-lifecycle",lifecycle,e.text??"connection lifecycle transition",e);if((e.text??"")=="Temporary reliable direct listener opened"&&e.port>0&&!String.IsNullOrEmpty(e.peer)){var request=scopedP2p.Values.FirstOrDefault(value=>value.Peer==e.peer);if(request!=null)SubmitScopedP2pCandidate(request,e.port);}}
                if((e.kind==5&&model.Selected!=null&&e.peer==model.Selected.Contact.peer)||(e.kind==10&&model.SelectedGroup!=null&&e.peer==model.SelectedGroup.Id)||initialDmHistory||initialGroupHistory)scroll=true;
                string historyKey=e.kind==6&&model.Selected!=null&&model.FindByPeer(e.peer)==model.Selected?model.Selected.ConversationId:e.kind==11?"group:"+e.peer:"";
                if(!String.IsNullOrEmpty(historyKey)&&historyKey==historyScrollKey){string restoreKey=historyScrollKey;double offset=historyScrollOffset,extent=historyScrollExtent;historyScrollKey="";window.Dispatcher.BeginInvoke((Action)delegate {if(CurrentHistoryKey()==restoreKey){var viewer=Find<ScrollViewer>("MessageScroll");viewer.ScrollToVerticalOffset(offset+Math.Max(0,viewer.ExtentHeight-extent));}});}
            }
            if(incomingNotification)NotifyIncomingMessage();
            if(model.Selected==null&&model.Conversations.Count>0){model.Selected=model.Conversations.Values.First();RequestConversationHistory(model.Selected);ShowConversation();}
            RefreshList();Refresh();SizeBubbles();if(scroll)Find<ScrollViewer>("MessageScroll").ScrollToEnd();
            if(!trustDialog&&trustRequests.Count>0){trustDialog=true;var pending=trustRequests.Dequeue();var request=pending.Item1;string trustKey=pending.Item2;TraceUi("trust dialog construction started for "+request.peer);var dialogTimer=Stopwatch.StartNew();bool identityChanged=(request.text??"").IndexOf("identity changed",StringComparison.OrdinalIgnoreCase)>=0;var title=identityChanged?"Verify new device identity":"Verify your friend";var action=identityChanged?"Trust new identity":"Trust peer";bool accepted=Confirm(title,request.text+"\n\nPeer fingerprint:\n"+request.peer+"\n\nCompare this fingerprint over your trusted channel. Trust this identity?",action);TraceUi("trust dialog closed after "+dialogTimer.ElapsedMilliseconds+" ms");string trustResult=accepted?"Explicitly trusted":"Rejected for this connection attempt";var trustConversation=model.FindByPeer(request.expected)??model.FindByPeer(request.peer);connectionAttempts.Update(request,trustConversation,null,trustResult,accepted?"None":null);RecordConnection("trust",trustConversation,trustResult,request);if(!accepted&&!String.IsNullOrEmpty(trustKey))trustSuppressed.Add(trustKey);core.Approve(request.peer,accepted);trustDialog=false;}
        }catch(Exception error){model.Status="UI adapter: "+error.Message;Refresh();}
    }
    void StartRelay(CoreEvent request){
        ulong attempt=request.attempt>0?(ulong)request.attempt:0;var conversation=model.FindByPeer(request.expected)??model.FindByPeer(request.peer);var service=discovery;
        if(attempt==0||request.networkGeneration!=(long)networkGeneration||conversation==null||conversation.AccountId<=0||service==null||String.IsNullOrEmpty(service.SessionToken)||String.IsNullOrEmpty(model.OwnId)){core.RelayClosed(attempt,"Relay request is stale or the authenticated account is unavailable.");return;}
        try{RelayTransport relay=null;relay=new RelayTransport(service,core,attempt,model.OwnId,conversation.AccountId,request.expected,value=>{lock(relayGate)relayTransports.Remove(value);});lock(relayGate){if(relayTransports.ContainsKey(attempt)){relay.Dispose();return;}relayTransports.Add(attempt,relay);}relay.Start();}
        catch(Exception error){core.RelayClosed(attempt,"Relay startup failed: "+error.GetType().Name);}
    }
    void StopRelays(){RelayTransport[] values;lock(relayGate){values=relayTransports.Values.ToArray();relayTransports.Clear();}foreach(var relay in values)try{relay.Dispose();}catch{}}
    string CurrentHistoryKey(){return model.Groups&&model.SelectedGroup!=null?"group:"+model.SelectedGroup.Id:!model.Groups&&model.Selected!=null?model.Selected.ConversationId:"";}
    void Switch(bool groups){model.Groups=groups;if(groups){core.LoadGroups();RequestGroupHistory(model.SelectedGroup);}RefreshList();ShowConversation();Refresh();}
    void CreateGroup(){
        if(discovery==null||discovery.AccountId<=0){Notify("Sign in once to bind your account identity before creating a group. Existing groups continue to work directly when the server is unavailable.");return;}
        var fields=Dialog("Create group",new[]{"Group name"},new[]{"My group"},"Create","The Group ID and signed state are created on this client. PeerChat’s server does not own the group.");if(fields==null)return;string name=fields[0].Trim();if(name.Length==0||name.Length>80){Notify("Use a group name from 1 to 80 characters.");return;}string id=core.CreateGroup(name);if(String.IsNullOrEmpty(id)){Notify("The group could not be created.");return;}model.Groups=true;model.Status="Creating signed local group…";core.LoadGroups();Refresh();
    }
    TextBlock FlyoutText(string text,double size=13,bool muted=false){return new TextBlock{Text=text,FontSize=size,Foreground=Brush(muted?"#A1AEBB":"#EDF1F5"),TextWrapping=TextWrapping.Wrap};}
    Button FlyoutAction(string text,Action action,bool destructive=false){var button=new Button{Content=text,Style=(Style)window.FindResource(typeof(Button)),Foreground=Brush("#EDF1F5")};button.Foreground=Brush("#EDF1F5");button.Background=Brush(destructive?"#632630":"#19222B");button.Padding=new Thickness(10,7,10,7);button.Margin=new Thickness(0,0,0,6);button.HorizontalContentAlignment=HorizontalAlignment.Left;button.Click+=(s,e)=>action();return button;}
    void CloseHeaderFlyout(){if(headerFlyout!=null){headerFlyout.IsOpen=false;headerFlyout=null;}}
    void OpenHeaderFlyout(string anchorName,StackPanel panel){
        CloseHeaderFlyout();panel.Width=340;panel.Margin=new Thickness(3);
        var scroll=new ScrollViewer{Content=panel,MaxHeight=430,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled};
        var host=new MenuItem{Header=scroll,StaysOpenOnClick=true,Style=(Style)window.FindResource(typeof(MenuItem)),Padding=new Thickness(10),Background=Brush("#111920"),Foreground=Brush("#EDF1F5")};
        var menu=new ContextMenu{PlacementTarget=Find<Button>(anchorName),Placement=System.Windows.Controls.Primitives.PlacementMode.Bottom,HorizontalOffset=-8,Background=Brush("#111920"),Foreground=Brush("#EDF1F5"),BorderBrush=Brush("#344351"),BorderThickness=new Thickness(1),Padding=new Thickness(4),Style=(Style)window.FindResource(typeof(ContextMenu))};
        menu.Items.Add(host);menu.Closed+=(s,e)=>{if(Object.ReferenceEquals(headerFlyout,menu))headerFlyout=null;};headerFlyout=menu;menu.IsOpen=true;
    }
    void ShowConversationFlyout(){
        var selected=model.Selected;if(selected==null)return;var panel=new StackPanel();panel.Children.Add(FlyoutText(selected.Name,18));panel.Children.Add(new TextBlock{Text=selected.AccountId>0?"Account "+selected.AccountId:"Direct P2P contact",Foreground=Brush("#A1AEBB"),FontSize=12,Margin=new Thickness(0,3,0,12)});
        string trust=selected.Contact!=null&&selected.Contact.verified?"Verified device":"Verification required";panel.Children.Add(FlyoutText(selected.ConnectionState+" · "+trust));panel.Children.Add(new TextBlock{Text="Peer ID\n"+(selected.Contact==null||String.IsNullOrEmpty(selected.Contact.peer)?"Not available":selected.Contact.peer),Foreground=Brush("#A1AEBB"),FontSize=11,Margin=new Thickness(0,8,0,12),TextWrapping=TextWrapping.Wrap});
        panel.Children.Add(FlyoutAction("Try Reconnect",()=>{CloseHeaderFlyout();Reconnect();}));
        if(selected.IsFriend&&selected.AccountId>0&&selected.Online){
            if(selected.P2pAvailability==2)panel.Children.Add(FlyoutAction("Use P2P",()=>{CloseHeaderFlyout();PromoteScopedP2p(selected);}));
            else if(selected.P2pAvailability==3)panel.Children.Add(FlyoutAction("Return to Server Relay",()=>{CloseHeaderFlyout();ReturnToRelay(selected);}));
            else panel.Children.Add(FlyoutAction("Try P2P Connection",()=>{CloseHeaderFlyout();TryScopedP2p(selected);}));
        }
        panel.Children.Add(FlyoutAction("Copy Connection Details",()=>CopyConnectionDetails()));OpenHeaderFlyout("PeerInfo",panel);
    }
    void ShowGroupFlyout(string anchorName,string focus){
        var group=model.SelectedGroup;if(group==null){Notify("Select a group first.");return;}var panel=new StackPanel();panel.Children.Add(FlyoutText(group.Name,18));panel.Children.Add(new TextBlock{Text=group.MemberCount+" members · client-owned",Foreground=Brush("#A1AEBB"),FontSize=12,Margin=new Thickness(0,3,0,12)});
        var members=group.Data.members??new GroupMemberData[0];var invites=group.Data.invites??new GroupMemberData[0];var mine=members.FirstOrDefault(member=>member.peer==model.OwnId);var invitation=invites.FirstOrDefault(member=>member.peer==model.OwnId);
        if(focus=="invite"){
            panel.Children.Add(FlyoutText("Invite a friend",15));panel.Children.Add(new TextBlock{Text="Offline friends remain listed. Sending still requires a verified device.",Foreground=Brush("#A1AEBB"),FontSize=12,Margin=new Thickness(0,5,0,10)});
            var candidates=model.GroupInviteCandidates(group.Data).ToArray();if(mine==null||mine.role<2)panel.Children.Add(FlyoutText("Only an owner or admin can invite members.",13,true));else if(candidates.Length==0)panel.Children.Add(FlyoutText("No uninvited friends are available.",13,true));else foreach(var friend in candidates){var candidate=friend;panel.Children.Add(FlyoutAction(candidate.Name,()=>{if(candidate.Contact==null||!candidate.Contact.verified){Notify("Verify "+candidate.Name+"'s device before sending this invitation.");return;}if(core.InviteToGroup(group.Id,candidate.AccountId,candidate.Contact.peer,candidate.Name)){model.Status="Invitation queued for "+candidate.Name+".";CloseHeaderFlyout();Refresh();}else Notify("The invitation could not be queued.");}));}
            OpenHeaderFlyout(anchorName,panel);return;
        }
        if(focus=="members"){
            panel.Children.Add(FlyoutText("Members ("+members.Length+")",15));foreach(var member in members.OrderByDescending(item=>item.role).ThenBy(item=>item.name)){var memberCopy=member;string role=member.role==3?"Owner":member.role==2?"Admin":"Member";panel.Children.Add(new TextBlock{Text=member.name+" · "+role,Foreground=Brush("#EDF1F5"),Margin=new Thickness(0,8,0,4)});bool canManage=mine!=null&&member.account!=mine.account&&((mine.role==3&&member.role<3)||(mine.role==2&&member.role==1));if(canManage){var actions=new StackPanel{Orientation=Orientation.Horizontal,Margin=new Thickness(0,0,0,4)};if(mine.role==3){var roleButton=FlyoutAction(member.role==2?"Demote":"Promote",()=>{core.SetGroupAdmin(group.Id,memberCopy.account,memberCopy.role!=2);CloseHeaderFlyout();});roleButton.Margin=new Thickness(0,0,6,0);actions.Children.Add(roleButton);}actions.Children.Add(FlyoutAction("Remove",()=>{if(Confirm("Remove member","Remove "+memberCopy.name+" from this group?","Remove")){core.RemoveGroupMember(group.Id,memberCopy.account);CloseHeaderFlyout();}},true));panel.Children.Add(actions);}}
            if(invites.Length>0){panel.Children.Add(new Separator{Margin=new Thickness(0,10,0,10)});panel.Children.Add(FlyoutText("Pending Invites ("+invites.Length+")",15));foreach(var pending in invites.OrderBy(item=>item.name))panel.Children.Add(new TextBlock{Text=pending.name+" · Pending Invite",Foreground=Brush("#A1AEBB"),Margin=new Thickness(0,7,0,0)});}
            OpenHeaderFlyout(anchorName,panel);return;
        }
        panel.Children.Add(FlyoutText("Group Settings",15));panel.Children.Add(new TextBlock{Text="Group ID\n"+group.Id,Foreground=Brush("#A1AEBB"),FontSize=11,Margin=new Thickness(0,7,0,12),TextWrapping=TextWrapping.Wrap});
        if(invitation!=null){panel.Children.Add(FlyoutAction("Accept invitation",()=>{core.RespondToGroupInvite(group.Id,true);CloseHeaderFlyout();}));panel.Children.Add(FlyoutAction("Decline invitation",()=>{core.RespondToGroupInvite(group.Id,false);CloseHeaderFlyout();},true));}
        if(mine!=null&&mine.role>=2)panel.Children.Add(FlyoutAction("Rename Group",()=>{CloseHeaderFlyout();var values=Dialog("Rename group",new[]{"Group name"},new[]{group.Name},"Rename","The rename is signed locally and replicated to members.");if(values!=null)core.RenameGroup(group.Id,values[0].Trim());}));
        if(mine!=null&&mine.role!=3)panel.Children.Add(FlyoutAction("Leave Group",()=>{CloseHeaderFlyout();if(Confirm("Leave group","Leave "+group.Name+"? Your signed leave event will be replicated to members.","Leave"))core.LeaveGroup(group.Id);},true));else if(mine!=null)panel.Children.Add(FlyoutText("The owner cannot leave while responsible for the group.",12,true));
        OpenHeaderFlyout(anchorName,panel);
    }

    private void ShowGroupInfoDialog()
	{
		GroupModel group = model.SelectedGroup;
		if (group == null)
		{
			Notify("Select a group first.");
			return;
		}
		Window dialog = new Window
		{
			Title = "Group details",
			Owner = window,
			Width = 540.0,
			Height = 650.0,
			WindowStartupLocation = WindowStartupLocation.CenterOwner,
			Background = Brush("#0C1116"),
			Foreground = Brush("#EDF1F5"),
			FontFamily = new FontFamily("Segoe UI"),
			Resources = window.Resources
		};
		ScrollViewer scrollViewer = new ScrollViewer();
		scrollViewer.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
		ScrollViewer scrollViewer2 = scrollViewer;
		StackPanel stackPanel = new StackPanel();
		stackPanel.Margin = new Thickness(24.0);
		StackPanel stackPanel2 = (StackPanel)(scrollViewer2.Content = stackPanel);
		dialog.Content = scrollViewer2;
		stackPanel2.Children.Add(new TextBlock
		{
			Text = group.Name,
			FontSize = 25.0,
			FontWeight = FontWeights.SemiBold
		});
		stackPanel2.Children.Add(new TextBlock
		{
			Text = "Client-owned group · " + group.Id,
			TextWrapping = TextWrapping.Wrap,
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 5.0, 0.0, 18.0)
		});
		GroupMemberData groupMemberData = (group.Data.members ?? new GroupMemberData[0]).FirstOrDefault((GroupMemberData groupMemberData3) => groupMemberData3.peer == model.OwnId);
		GroupMemberData groupMemberData2 = (group.Data.invites ?? new GroupMemberData[0]).FirstOrDefault((GroupMemberData groupMemberData3) => groupMemberData3.peer == model.OwnId);
		if (groupMemberData2 != null)
		{
			StackPanel stackPanel4 = new StackPanel();
			stackPanel4.Orientation = Orientation.Horizontal;
			stackPanel4.Margin = new Thickness(0.0, 0.0, 0.0, 18.0);
			StackPanel stackPanel5 = stackPanel4;
			Button button = new Button();
			button.Content = "Accept invitation";
			button.Padding = new Thickness(14.0, 8.0, 14.0, 8.0);
			button.Margin = new Thickness(0.0, 0.0, 8.0, 0.0);
			Button button2 = button;
			Button button3 = new Button();
			button3.Content = "Decline";
			button3.Padding = new Thickness(14.0, 8.0, 14.0, 8.0);
			Button button4 = button3;
			button2.Click += delegate
			{
				core.RespondToGroupInvite(group.Id, true);
				dialog.Close();
			};
			button4.Click += delegate
			{
				core.RespondToGroupInvite(group.Id, false);
				dialog.Close();
			};
			stackPanel5.Children.Add(button2);
			stackPanel5.Children.Add(button4);
			stackPanel2.Children.Add(stackPanel5);
		}
		if (groupMemberData != null && groupMemberData.role >= 2)
		{
			StackPanel stackPanel6 = new StackPanel();
			stackPanel6.Orientation = Orientation.Horizontal;
			stackPanel6.Margin = new Thickness(0.0, 0.0, 0.0, 18.0);
			StackPanel stackPanel7 = stackPanel6;
			Button button5 = new Button();
			button5.Content = "Rename";
			button5.Padding = new Thickness(14.0, 8.0, 14.0, 8.0);
			button5.Margin = new Thickness(0.0, 0.0, 8.0, 0.0);
			Button button6 = button5;
			button6.Click += delegate
			{
				string[] array2 = Dialog("Rename group", new string[1] { "Group name" }, new string[1] { group.Name }, "Rename", "The rename is signed locally and replicated to members.");
				if (array2 != null && core.RenameGroup(group.Id, array2[0].Trim()))
				{
					dialog.Close();
				}
			};
			Button button7 = new Button();
			button7.Content = "Invite friend";
			button7.Padding = new Thickness(14.0, 8.0, 14.0, 8.0);
			Button button8 = button7;
			button8.Click += delegate
			{
				InviteFriendToGroup(group, dialog);
			};
			stackPanel7.Children.Add(button6);
			stackPanel7.Children.Add(button8);
			stackPanel2.Children.Add(stackPanel7);
		}
		stackPanel2.Children.Add(new TextBlock
		{
			Text = "Members (" + (group.Data.members ?? new GroupMemberData[0]).Length + ")",
			FontSize = 16.0,
			FontWeight = FontWeights.SemiBold,
			Margin = new Thickness(0.0, 0.0, 0.0, 8.0)
		});
		foreach (GroupMemberData member in from item in @group.Data.members ?? new GroupMemberData[0]
			orderby item.role descending, item.name
			select item)
		{
			Grid grid = new Grid();
			grid.Margin = new Thickness(0.0, 0.0, 0.0, 8.0);
			Grid grid2 = grid;
			grid2.ColumnDefinitions.Add(new ColumnDefinition());
			grid2.ColumnDefinitions.Add(new ColumnDefinition
			{
				Width = GridLength.Auto
			});
			string text = ((member.role == 3) ? "Owner" : ((member.role == 2) ? "Admin" : "Member"));
			grid2.Children.Add(new TextBlock
			{
				Text = member.name + " · " + text,
				VerticalAlignment = VerticalAlignment.Center
			});
			if (groupMemberData != null && member.account != groupMemberData.account && ((groupMemberData.role == 3 && member.role < 3) || (groupMemberData.role == 2 && member.role == 1)))
			{
				StackPanel stackPanel8 = new StackPanel();
				stackPanel8.Orientation = Orientation.Horizontal;
				StackPanel stackPanel9 = stackPanel8;
				if (groupMemberData.role == 3)
				{
					Button button9 = new Button();
					button9.Content = ((member.role == 2) ? "Demote" : "Promote");
					button9.Padding = new Thickness(9.0, 5.0, 9.0, 5.0);
					button9.Margin = new Thickness(0.0, 0.0, 6.0, 0.0);
					Button button10 = button9;
					button10.Click += delegate
					{
						core.SetGroupAdmin(group.Id, member.account, member.role != 2);
						dialog.Close();
					};
					stackPanel9.Children.Add(button10);
				}
				Button button11 = new Button();
				button11.Content = "Remove";
				button11.Padding = new Thickness(9.0, 5.0, 9.0, 5.0);
				Button button12 = button11;
				button12.Click += delegate
				{
					if (Confirm("Remove member", "Remove " + member.name + " from this group?", "Remove"))
					{
						core.RemoveGroupMember(group.Id, member.account);
						dialog.Close();
					}
				};
				stackPanel9.Children.Add(button12);
				Grid.SetColumn(stackPanel9, 1);
				grid2.Children.Add(stackPanel9);
			}
			stackPanel2.Children.Add(grid2);
		}
		GroupMemberData[] array = group.Data.invites ?? new GroupMemberData[0];
		if (array.Length > 0)
		{
			stackPanel2.Children.Add(new TextBlock
			{
				Text = "Pending Invites (" + array.Length + ")",
				FontSize = 16.0,
				FontWeight = FontWeights.SemiBold,
				Margin = new Thickness(0.0, 14.0, 0.0, 8.0)
			});
			foreach (GroupMemberData item in array.OrderBy((GroupMemberData item) => item.name))
			{
				stackPanel2.Children.Add(new TextBlock
				{
					Text = item.name + " · Pending Invite",
					Foreground = Brush("#A1AEBB"),
					Margin = new Thickness(0.0, 0.0, 0.0, 8.0)
				});
			}
		}
		if (groupMemberData != null && groupMemberData.role != 3)
		{
			Button button13 = new Button();
			button13.Content = "Leave group";
			button13.HorizontalAlignment = HorizontalAlignment.Left;
			button13.Padding = new Thickness(14.0, 8.0, 14.0, 8.0);
			button13.Margin = new Thickness(0.0, 16.0, 0.0, 0.0);
			Button button14 = button13;
			button14.Click += delegate
			{
				if (Confirm("Leave group", "Leave " + group.Name + "? Your signed leave event will be replicated to members.", "Leave"))
				{
					core.LeaveGroup(group.Id);
					dialog.Close();
				}
			};
			stackPanel2.Children.Add(button14);
		}
		else if (groupMemberData != null && groupMemberData.role == 3)
		{
			stackPanel2.Children.Add(new TextBlock
			{
				Text = "The owner cannot leave while responsible for the group.",
				Foreground = Brush("#A1AEBB"),
				Margin = new Thickness(0.0, 16.0, 0.0, 0.0)
			});
		}
		dialog.ShowDialog();
	}

	private void InviteFriendToGroup(GroupModel group, Window parent)
	{
		Window picker = new Window
		{
			Title = "Invite friend",
			Owner = parent,
			Width = 460.0,
			Height = 520.0,
			WindowStartupLocation = WindowStartupLocation.CenterOwner,
			Background = Brush("#101820"),
			Foreground = Brush("#EDF1F5"),
			FontFamily = new FontFamily("Segoe UI"),
			Resources = window.Resources
		};
		StackPanel stack = new StackPanel
		{
			Margin = new Thickness(22.0)
		};
		picker.Content = new ScrollViewer
		{
			Content = stack,
			VerticalScrollBarVisibility = ScrollBarVisibility.Auto
		};
		Action rebuild = delegate
		{
			stack.Children.Clear();
			stack.Children.Add(new TextBlock
			{
				Text = "Invite a friend",
				FontSize = 23.0,
				FontWeight = FontWeights.SemiBold
			});
			stack.Children.Add(new TextBlock
			{
				Text = "Offline friends remain available. Invitations to verified devices are queued for direct delivery.",
				TextWrapping = TextWrapping.Wrap,
				Margin = new Thickness(0.0, 6.0, 0.0, 16.0)
			});
			ConversationModel[] array = model.GroupInviteCandidates(group.Data).ToArray();
			if (array.Length == 0)
			{
				stack.Children.Add(new TextBlock
				{
					Text = "There are no uninvited friends available."
				});
			}
			ConversationModel[] array2 = array;
			foreach (ConversationModel friend in array2)
			{
				Button button = new Button
				{
					Content = friend.Name,
					Padding = new Thickness(12.0, 9.0, 12.0, 9.0),
					Margin = new Thickness(0.0, 0.0, 0.0, 7.0)
				};
				RoutedEventHandler value = delegate
				{
					if (model.GroupInviteCandidates(group.Data).Any((ConversationModel item) => item.AccountId == friend.AccountId))
					{
						if (friend.Contact == null || !friend.Contact.verified)
						{
							Notify("Verify " + friend.Name + "'s device in your direct conversation before sending this invitation.");
						}
						else if (core.InviteToGroup(group.Id, friend.AccountId, friend.Contact.peer, friend.Name))
						{
							picker.Close();
							parent.Close();
						}
						else
						{
							Notify("The invitation could not be queued.");
						}
					}
				};
				button.Click += value;
				stack.Children.Add(button);
			}
		};
		InviteCandidatesChanged += rebuild;
		picker.Closed += delegate
		{
			InviteCandidatesChanged -= rebuild;
		};
		rebuild();
		picker.ShowDialog();
	}

	private void DeleteSelectedConversationHistory()
	{
		if (model.Groups || model.Selected == null || model.Selected.Contact == null)
		{
			return;
		}
		ConversationModel selected = model.Selected;
		string phrase = "DELETE " + selected.Name;
		string[] values = Dialog("Delete conversation history", new string[1] { "Type exactly: " + phrase }, new string[1] { "" }, "Delete History", "This permanently removes only this profile's encrypted direct-message history with " + selected.Name + ". It does not remove the contact, server account, friendship, device identity, or trust. New messages will begin a fresh local history.", true);
		if (values == null)
		{
			return;
		}
		if (!String.Equals(values[0].Trim(), phrase, StringComparison.Ordinal))
		{
			Notify("Conversation history was not deleted because the confirmation text did not match.");
			return;
		}
		if (!Confirm("Final history deletion", "Permanently delete local conversation history with " + selected.Name + "? This cannot be undone.", "Delete History"))
		{
			return;
		}
		if (!core.EraseHistory(selected.Contact.peer))
		{
			Notify("Could not queue local conversation-history deletion.");
			return;
		}
		model.Status = "Deleting local conversation history…";
		Refresh();
	}

	private void RemoveSelectedContact()
	{
		if (model.Groups || model.Selected == null)
		{
			return;
		}
		ConversationModel selected = model.Selected;
		if (Confirm("Remove contact", "Remove " + selected.Name + " and its saved endpoint? Local chat history will be kept.", "Remove contact"))
		{
			if (!core.Remove(selected.Contact.peer))
			{
				Notify("Could not remove this contact.");
				return;
			}
			model.RemoveConversation(selected);
			model.Selected = null;
			ShowConversation();
			RefreshList();
			Refresh();
		}
	}

	private void RefreshList()
	{
		string search = Find<TextBox>("Search").Text;
		Find<TextBlock>("SearchHint").Visibility = ((search.Length != 0) ? Visibility.Collapsed : Visibility.Visible);
		changingSelection = true;
		List<GroupModel> list = (from @group in model.GroupChats.Values
			where @group.Name.IndexOf(search, StringComparison.CurrentCultureIgnoreCase) >= 0
			orderby @group.Name
			select @group).ToList();
		List<ConversationModel> list2 = (from c in model.Conversations.Values
			where c.Name.IndexOf(search, StringComparison.CurrentCultureIgnoreCase) >= 0
			orderby c.Name
			select c).ToList();
		ListBox listBox = Find<ListBox>("ContactList");
		object itemsSource;
		if (!model.Groups)
		{
			IEnumerable enumerable = list2;
			itemsSource = enumerable;
		}
		else
		{
			itemsSource = list;
		}
		listBox.ItemsSource = (IEnumerable)itemsSource;
		Find<ListBox>("ContactList").SelectedItem = (model.Groups ? ((ObservableModel)model.SelectedGroup) : ((ObservableModel)model.Selected));
		changingSelection = false;
		int num = (model.Groups ? list.Count : list2.Count);
		Find<TextBlock>("ListEmpty").Visibility = ((num != 0) ? Visibility.Collapsed : Visibility.Visible);
		Find<TextBlock>("ListEmpty").Text = ((!model.Groups) ? ((search.Length > 0) ? "No matching conversations." : "Find friends to start building your friend list.") : ((search.Length > 0) ? "No matching groups." : "Create a group or accept an invitation."));
	}

	private void ShowConversation()
	{
		Find<ItemsControl>("Messages").ItemsSource = ((!model.Groups) ? ((model.Selected == null) ? null : model.Selected.Messages) : ((model.SelectedGroup == null) ? null : model.SelectedGroup.Messages));
		Find<TextBox>("Composer").Text = ((!model.Groups) ? ((model.Selected == null) ? "" : model.Selected.Draft) : ((model.SelectedGroup == null) ? "" : model.SelectedGroup.Draft));
		window.Dispatcher.BeginInvoke((Action)delegate { Find<ScrollViewer>("MessageScroll").ScrollToEnd(); });
		SizeBubbles();
	}

	private void SizeBubbles()
	{
		double bubbleWidth = Math.Max(170.0, (Find<ScrollViewer>("MessageScroll").ActualWidth - 65.0) * 0.82);
		foreach (ConversationModel value in model.Conversations.Values)
		{
			foreach (MessageModel message in value.Messages)
			{
				message.BubbleWidth = bubbleWidth;
			}
		}
		foreach (GroupModel value2 in model.GroupChats.Values)
		{
			foreach (MessageModel message2 in value2.Messages)
			{
				message2.BubbleWidth = bubbleWidth;
			}
		}
	}

	private void Refresh()
	{
		bool flag = model.Selected != null && !model.Groups;
		bool flag2 = model.Groups && model.SelectedGroup != null;
		Find<TextBlock>("LocalProfile").Text = ((localProfile == null) ? model.Name : localProfile.Name) + ((accountLink == null) ? " · Unlinked" : (" · @" + accountLink.Username));
		profileName.Text = ((localProfile == null) ? model.Name : localProfile.Name);
		profileHandle.Text = ((discovery != null) ? ("@" + discovery.Username + " · Online") : ((accountLink != null) ? ("@" + accountLink.Username + " · Offline / Server Relay") : (accountStore.Exists ? "Account link unavailable · Offline / Server Relay" : "Unlinked · Offline")));
		Find<TextBlock>("OwnId").Text = ((model.OwnId.Length == 0) ? "Waiting for identity" : model.OwnId);
		Find<TextBlock>("OwnId").ToolTip = model.OwnId;
		bool reliableListenerActive = model.Listening && model.Port > 0;
		Find<TextBlock>("Listener").Text = (reliableListenerActive ? ("●  Reliable P2P listener: " + shareHost + ":" + model.Port) : "○  Reliable P2P listener: Not active");
		Find<TextBlock>("Listener").Foreground = Brush(reliableListenerActive ? "#36D873" : "#A1AEBB");
		int num = model.Conversations.Values.Count((ConversationModel c) => c.Online);
		Find<TextBlock>("PeerCount").Text = num + " peers connected";
		Find<TextBlock>("CompactConnectionStatus").Text = (flag ? ConnectionStatusText(model.Selected) : (model.Listening ? "Server Relay ready" : "Offline"));
		Find<TextBlock>("CompactConnectionStatus").Foreground = Brush((flag && model.Selected.ConnectionState == "Ready") ? "#36D873" : "#EDF1F5");
		Find<TextBlock>("LastConnectionFailure").Text = "Last failure: " + (flag ? connectionAttempts.For(model.Selected).Failure : "None");
		Find<TextBlock>("Status").Text = model.Status;
		Find<TextBlock>("Security").Text = ((num > 0) ? ("Authenticated TLS connections: " + num) : "TLS: no active peer connection");
		var routeConversation=flag?model.Selected:null;
		Find<TextBlock>("ReliableConnectionMethod").Text="Connection: "+ReliableConnectionMethod(routeConversation);
		Find<TextBlock>("ReliableSecurityMode").Text="Security: End-to-end peer TLS";
		Find<TextBlock>("ReliableRemoteRoute").Text="Remote route: "+(routeConversation!=null&&routeConversation.Online&&!String.IsNullOrEmpty(routeConversation.ReliableEndpoint)?routeConversation.ReliableEndpoint:"Unavailable");
		Find<TextBlock>("MediaConnectionMethod").Text="Connection: "+VoiceConnectionMethod();
		Find<TextBlock>("MediaSecurityMode").Text="Security: DTLS-SRTP";
		Find<TextBlock>("MediaConnectionState").Text="State: "+VoiceConnectionState();
		Find<TextBlock>("StorageStatus").Text = (model.Listening ? "Local history: encrypted on this PC" : "Local profile: not ready");
		Find<TextBlock>("VersionText").Text = "App version: " + PeerChat.VersionInfo.Display;
		Find<TextBlock>("EndpointHint").Text = reliableListenerActive ? "Temporary P2P endpoint: " + shareHost + ":" + model.Port : "Reliable direct endpoint: not active";
		Find<TextBlock>("LanEndpoint").Text = reliableListenerActive ? "LAN: " + shareHost + ":" + model.Port : "LAN: not published";
		Find<TextBlock>("PublicEndpoint").Text = reliableListenerActive ? "Public: scoped P2P exchange only" : "Public: not published";
		Find<TextBlock>("FriendAreaStatus").Text = "Friends · " + model.Conversations.Values.Count((ConversationModel item) => item.IsFriend) + "   Incoming · " + friendState.Incoming.Count + "   Pending · " + friendState.Outgoing.Count;
		UpdateFriendRequestsButton();
		bool flag3 = discovery != null && !string.IsNullOrEmpty(discovery.SessionToken);
		Find<Button>("AccountRegister").Visibility = ((flag3 || accountLink != null || accountStore.Exists) ? Visibility.Collapsed : Visibility.Visible);
		Find<Button>("AccountLogin").Visibility = ((flag3 || (accountLink == null && accountStore.Exists)) ? Visibility.Collapsed : Visibility.Visible);
		Find<Button>("AccountLogin").ToolTip = ((accountLink == null) ? "Link this profile to an existing account" : ("Sign this profile back into @" + accountLink.Username));
		Find<Button>("AccountSignOut").Visibility = ((!flag3) ? Visibility.Collapsed : Visibility.Visible);
		Find<TextBlock>("HeaderName").Text = (flag2 ? model.SelectedGroup.Name : (model.Groups ? "Groups" : (flag ? model.Selected.Name : "Your conversations")));
		Find<TextBlock>("HeaderInitial").Text = (flag2 ? model.SelectedGroup.Initial : (model.Groups ? "G" : (flag ? model.Selected.Initial : "p")));
		Find<TextBlock>("HeaderStatus").Text = ((!flag2) ? (model.Groups ? "Client-owned groups · direct P2P" : (flag ? ConnectionStatusText(model.Selected) : "Choose a friend to start chatting")) : (model.SelectedGroup.Invited ? "Group invitation · review before joining" : (model.SelectedGroup.MemberCount + " members · client-owned encrypted history")));
		Find<TextBlock>("HeaderStatus").Foreground = Brush((flag && model.Selected.ConnectionState == "Ready") ? "#36D873" : "#A1AEBB");
		Find<StackPanel>("ChatEmpty").Visibility = ((!flag2) ? ((flag && model.Selected.Messages.Count != 0) ? Visibility.Collapsed : Visibility.Visible) : ((model.SelectedGroup.Messages.Count != 0) ? Visibility.Collapsed : Visibility.Visible));
		Find<TextBlock>("EmptyTitle").Text = ((!flag2) ? (model.Groups ? "A place for your people" : (flag ? "The start of your conversation" : "Good conversations start here.")) : (model.SelectedGroup.Invited ? "You’re invited" : "Start the group conversation"));
		Find<TextBlock>("EmptyDescription").Text = ((!flag2) ? (model.Groups ? "Create a client-owned group. The server is not the source of truth." : (flag ? "Messages and history belong to this PC. Verify your friend to start chatting." : "Find friends to start a conversation. Messaging uses PeerChat Server Relay by default; optional direct P2P requires mutual consent.")) : (model.SelectedGroup.Invited ? "Open group details to accept or decline this signed invitation." : "Messages are signed, stored locally, and replicated directly to members."));
		Find<Button>("EmptyAction").Content = (model.Groups ? "＋    Create Group" : "＋    Add Friend");
		Find<Button>("EmptyAction").Visibility = ((flag || flag2) ? Visibility.Collapsed : Visibility.Visible);
		Find<Button>("GroupNew").Visibility = ((!model.Groups) ? Visibility.Collapsed : Visibility.Visible);
		string[] array = new string[3] { "GroupMembers", "GroupInvite", "GroupOverflow" };
		foreach (string name in array)
		{
			Find<Button>(name).Visibility = ((!flag2) ? Visibility.Collapsed : Visibility.Visible);
		}
		Find<Button>("GroupInvite").IsEnabled = flag2 && !model.SelectedGroup.Invited;
		bool canLoadOlder = (!model.Groups && model.Selected != null && model.Selected.Messages.Count > 0 && !model.Selected.HistoryExhausted) || (model.Groups && model.SelectedGroup != null && model.SelectedGroup.Messages.Count > 0 && !model.SelectedGroup.HistoryExhausted);
		Find<Button>("Older").Visibility = canLoadOlder ? Visibility.Visible : Visibility.Collapsed;
		bool flag4 = flag && model.Listening && model.Selected.Contact.verified && !busy;
		bool flag5 = flag2 && !model.SelectedGroup.Invited && model.Listening && !busy;
		Find<Button>("Send").IsEnabled = flag4 || flag5;
		TextBox textBox = Find<TextBox>("Composer");
		textBox.IsEnabled = flag4 || flag5;
		textBox.IsReadOnly = false;
		textBox.Focusable = true;
		textBox.IsHitTestVisible = true;
		Find<Button>("Reconnect").IsEnabled = flag && model.Listening && !busy;
		bool flag6 = model.VoiceStateCode != 0;
		Find<Button>("VoiceCall").Visibility = ((!flag || flag6) ? Visibility.Collapsed : Visibility.Visible);
		Find<Button>("VoiceCall").IsEnabled = flag && model.Selected.Online && model.Selected.Contact.verified && !flag6 && !busy;
		if(screenStartButton!=null){screenStartButton.Visibility=flag&&activeScreenState==0?Visibility.Visible:Visibility.Collapsed;screenStartButton.IsEnabled=flag&&model.Selected.Online&&model.Selected.Contact!=null&&model.Selected.Contact.verified&&!busy;}
		Find<Button>("PeerInfo").Visibility = ((!flag && !flag2) ? Visibility.Collapsed : Visibility.Visible);
		string[] array2 = new string[10] { "CopyId", "QuickCopy", "CopyCompact", "AddFriend", "QuickAdd", "Manual", "QuickManual", "Details", "CopyConnectionDetails", "ViewConnectionLog" };
		foreach (string name2 in array2)
		{
			Find<Button>(name2).IsEnabled = model.Listening && !busy;
		}
		string[] array3 = new string[2] { "DM", "Groups" };
		foreach (string text in array3)
		{
			bool flag7 = text == "Groups" == model.Groups;
			Button button = Find<Button>(text);
			button.Background = Brush("Transparent");
			button.Foreground = Brush(flag7 ? "#EDF1F5" : "#A1AEBB");
			button.BorderBrush = Brush(flag7 ? "#4297E8" : "Transparent");
			button.BorderThickness = new Thickness(0.0, 0.0, 0.0, 2.0);
		}
		RefreshVoiceUi();
	}

	private static string ConnectionStatusText(ConversationModel conversation)
	{
		string text = conversation.ConnectionState ?? "Offline";
		if (!(text == "Ready"))
		{
			if (!(text == "Offline"))
			{
				return "●  " + text;
			}
			return "○  Offline · Direct message";
		}
		return "●  Online · Authenticated TLS";
	}

	private static Brush Brush(string hex)
	{
		return (Brush)new BrushConverter().ConvertFromString(hex);
	}

	private async void CopyId()
	{
		if (!shareHostManuallyConfigured)
		{
			RefreshAutomaticShareHost();
		}
		string value = core.Share(shareHost, model.Port, model.OwnId);
		if (string.IsNullOrEmpty(value))
		{
			Notify("The local identity is not ready.");
			return;
		}
		for (int attempt = 0; attempt < 6; attempt++)
		{
			try
			{
				Clipboard.SetText(value);
				model.Status = "PeerChat ID copied · " + shareHost + ":" + model.Port;
				Refresh();
				return;
			}
			catch (COMException)
			{
			}
			await Task.Delay(100);
		}
		model.Status = "Clipboard is busy. Please try Copy My ID again.";
		Refresh();
	}

	private void PromptProfileAccountSetup()
	{
		if (accountSetupPrompted || !interactiveAccountSetup || profileManager == null || accountLink != null || accountStore.Exists)
		{
			return;
		}
		accountSetupPrompted = true;
		window.Dispatcher.BeginInvoke((Action)delegate
		{
			Window dialog = new Window
			{
				Title = "Connect this profile",
				Owner = window,
				Width = 490.0,
				SizeToContent = SizeToContent.Height,
				ResizeMode = ResizeMode.NoResize,
				WindowStartupLocation = WindowStartupLocation.CenterOwner,
				Background = Brush("#101820"),
				Foreground = Brush("#EDF1F5"),
				FontFamily = new FontFamily("Segoe UI"),
				Resources = window.Resources
			};
			StackPanel stackPanel = new StackPanel
			{
				Margin = new Thickness(26.0)
			};
			dialog.Content = stackPanel;
			stackPanel.Children.Add(new TextBlock
			{
				Text = "Connect " + ((localProfile == null) ? "this profile" : localProfile.Name),
				FontSize = 24.0,
				FontWeight = FontWeights.SemiBold
			});
			stackPanel.Children.Add(new TextBlock
			{
				Text = "Sign in or create the account that belongs to this local profile. You can also continue offline; local history and Direct P2P do not require the server.",
				TextWrapping = TextWrapping.Wrap,
				Foreground = Brush("#A1AEBB"),
				Margin = new Thickness(0.0, 8.0, 0.0, 20.0)
			});
			Button button = new Button
			{
				Content = "Sign In",
				Margin = new Thickness(0.0, 0.0, 0.0, 8.0),
				Background = Brush("#275AA7")
			};
			Button button2 = new Button
			{
				Content = "Create Account",
				Margin = new Thickness(0.0, 0.0, 0.0, 8.0)
			};
			Button button3 = new Button
			{
				Content = "Continue Offline"
			};
			button.Click += delegate
			{
				dialog.Close();
				window.Dispatcher.BeginInvoke((Action)delegate
				{
					Account(true);
				});
			};
			button2.Click += delegate
			{
				dialog.Close();
				window.Dispatcher.BeginInvoke((Action)delegate
				{
					Account(false);
				});
			};
			button3.Click += delegate
			{
				dialog.Close();
			};
			stackPanel.Children.Add(button);
			stackPanel.Children.Add(button2);
			stackPanel.Children.Add(button3);
			dialog.ShowDialog();
		});
	}

	private string[] AccountDialog(string title, string accept, string presetUsername = "", bool lockUsername = false)
	{
		Window dialog = new Window
		{
			Title = title,
			Owner = window,
			Width = 470.0,
			SizeToContent = SizeToContent.Height,
			ResizeMode = ResizeMode.NoResize,
			WindowStartupLocation = WindowStartupLocation.CenterOwner,
			Background = Brush("#101820"),
			Foreground = Brush("#EDF1F5"),
			FontFamily = new FontFamily("Segoe UI"),
			FontSize = 15.0,
			Resources = window.Resources
		};
		StackPanel stackPanel = new StackPanel();
		stackPanel.Margin = new Thickness(26.0);
		StackPanel stackPanel2 = stackPanel;
		dialog.Content = stackPanel2;
		stackPanel2.Children.Add(new TextBlock
		{
			Text = title,
			FontSize = 24.0,
			FontWeight = FontWeights.SemiBold,
			Margin = new Thickness(0.0, 0.0, 0.0, 8.0)
		});
		stackPanel2.Children.Add(new TextBlock
		{
			Text = "Your account uses the configured PeerChat service. The server receives a password verifier, never this password.",
			TextWrapping = TextWrapping.Wrap,
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 0.0, 0.0, 18.0)
		});
		string[] array = new string[2] { "Username / friend code", "Password" };
		TextBox username = new TextBox
		{
			Text = (presetUsername ?? ""),
			IsReadOnly = lockUsername
		};
		PasswordBox passwordBox = new PasswordBox();
		Control[] array2 = new Control[2] { username, passwordBox };
		for (int i = 0; i < array.Length; i++)
		{
			stackPanel2.Children.Add(new TextBlock
			{
				Text = array[i],
				Margin = new Thickness(0.0, 0.0, 0.0, 6.0)
			});
			array2[i].Margin = new Thickness(0.0, 0.0, 0.0, 13.0);
			stackPanel2.Children.Add(array2[i]);
		}
		StackPanel stackPanel3 = new StackPanel();
		stackPanel3.Orientation = Orientation.Horizontal;
		stackPanel3.HorizontalAlignment = HorizontalAlignment.Right;
		StackPanel stackPanel4 = stackPanel3;
		Button button = new Button();
		button.Content = "Cancel";
		button.IsCancel = true;
		button.Margin = new Thickness(0.0, 0.0, 8.0, 0.0);
		Button button2 = button;
		Button button3 = new Button();
		button3.Content = accept;
		button3.Background = Brush("#275AA7");
		Button button4 = button3;
		button2.Click += delegate
		{
			dialog.DialogResult = false;
		};
		button4.Click += delegate
		{
			dialog.DialogResult = true;
		};
		stackPanel4.Children.Add(button2);
		stackPanel4.Children.Add(button4);
		stackPanel2.Children.Add(stackPanel4);
		dialog.Loaded += delegate
		{
			username.Focus();
		};
		if (dialog.ShowDialog() != true)
		{
			return null;
		}
		return new string[2] { username.Text, passwordBox.Password };
	}

	private async void Account(bool login)
	{
		if (string.IsNullOrEmpty(model.OwnId))
		{
			Notify("Wait for the encrypted profile to finish opening.");
			return;
		}
		ProfileAccountLink binding = accountStore.Load();
		if (binding == null && accountStore.Exists)
		{
			Notify("This profile's protected account link could not be read. It will stay offline and cannot be linked to another account silently.");
			return;
		}
		if (!login && binding != null)
		{
			Notify("This local profile is already linked to @" + binding.Username + ". Create or switch to another local profile for a different account.");
			return;
		}
		string[] values = AccountDialog(login ? "Sign in to PeerChat" : "Create PeerChat account", login ? "Sign in" : "Create account", (binding == null) ? "" : binding.Username, binding != null);
		if (values == null)
		{
			return;
		}
		DiscoveryClient candidate = null;
		try
		{
			busy = true;
			model.Status = (login ? "Signing this profile in…" : "Creating and linking this profile…");
			Refresh();
			candidate = NewDiscoveryClient();
			await Task.Run(delegate
			{
				if (login)
				{
					candidate.Login(values[0].Trim(), values[1]);
				}
				else
				{
					candidate.Register(values[0].Trim(), values[1], model.OwnId);
				}
			});
			if (binding != null && binding.AccountId != candidate.AccountId)
			{
				try
				{
					await Task.Run(delegate
					{
						candidate.Logout();
					});
				}
				catch
				{
				}
				throw new InvalidOperationException("This local profile is linked to @" + binding.Username + ". Switch profiles to use another account.");
			}
			accountLink = accountStore.SaveSession(candidate.AccountId, candidate.Username, candidate.Host, candidate.Port, candidate.CertificateSha256, candidate.SessionToken);
			SaveDiscoveryService(candidate.Host, candidate.Port, candidate.CertificateSha256);
			RefreshAutomaticShareHost();
			PresenceResult presence = await Task.Run(() => candidate.ReportPresence(model.OwnId, "", 0, "", 0, false));
			discovery = candidate;
			discoverySessionGeneration++;
			mediaTurnCredentials = null;
			ApplyPresenceDiagnostics(presence);
			core.SetGroupAccount(discovery.AccountId, discovery.Username);
			core.LoadGroups();
            discovery.StartEventChannel(model.OwnId,HandleDiscoveryEvent);
			SaveDiscoverySession();
			model.Status = "Signed in as " + discovery.Username + " · Discoverable";
		}
		catch (Exception ex)
		{
			string message=AccountFailureMessage(ex,login);
			model.Status = message;
			Notify(message);
		}
		finally
		{
			busy = false;
			Refresh();
		}
		RefreshFriendState();
	}

	static string AccountFailureMessage(Exception error,bool login)
	{
		var rejected=error as DiscoveryRequestException;
		if(rejected!=null)
		{
			switch(rejected.ErrorCode)
			{
				case "login_failed": return "Couldn't sign in. Check the username and password, then try again.";
				case "registration_failed": return "Couldn't create the account. That username may already be in use; try another.";
				case "invalid_registration": return "Choose a valid username and enter a password to create the account.";
				case "device_already_registered": return "This device identity is already linked to an account. Sign in with that account or switch local profiles.";
				case "session_invalid": return "The sign-in session is no longer valid. Please sign in again.";
				case "session_superseded": return "This profile was signed in again elsewhere. Sign in again to continue.";
			}
		}
		if((error.Message??"").Contains("This local profile is linked to"))return "This local profile is already linked to another account. Switch local profiles to use a different account.";
		return login?"Couldn't sign in to PeerChat. Check your connection and try again.":"Couldn't create the PeerChat account. Check your connection and try again.";
	}

	private async void SignOut()
	{
		DiscoveryClient signedOut = discovery;
		var knownFriends=friendDirectory.Values.ToArray();
			if (signedOut != null)
			{
				signedOut.StopEventChannel();
			}
			ClearScopedP2p(true);
			discovery = null;
			discoverySessionGeneration++;
			mediaTurnCredentials = null;
		authenticatedFriendSnapshotGeneration++;
		core.SetAuthenticatedFriends(0, authenticatedFriendSnapshotGeneration, null);
		automaticInitialPinAttempts.Clear();
		StopRelays();
		foreach(var friend in knownFriends){var conversation=model.FindByPeer(friend.PublicIdentity);if(conversation!=null)ConnectFriendCandidate(friend,conversation,false,"account signed out");}
		discoveryRestored = true;
		accountLink = accountStore.ClearSession();
		friendState = new FriendState();
		friendRequestStates.Clear();
		friendDirectory.Clear();
		friendPresence.Clear();
		try
		{
			if (File.Exists(DiscoverySessionPath))
			{
				File.Delete(DiscoverySessionPath);
			}
		}
		catch
		{
		}
		model.Status = ((accountLink == null) ? "Profile" : ("@" + accountLink.Username)) + " signed out · local identity, trust, groups, and history remain available";
		Refresh();
		if (signedOut == null)
		{
			return;
		}
		try
		{
			await Task.Run(delegate
			{
				signedOut.Logout();
			});
		}
		catch
		{
		}
	}

	private void HandleDiscoveryEvent(DiscoveryEvent notification)
	{
		if (window == null)
		{
			return;
		}
		window.Dispatcher.BeginInvoke((Action)delegate
		{
			if (discovery != null)
			{
				if(notification!=null&&notification.Event=="p2p_request"){HandleIncomingP2p(notification);return;}
				if(notification!=null&&(notification.Event=="p2p_accepted"||notification.Event=="p2p_candidates_ready"||notification.Event=="p2p_declined"||notification.Event=="p2p_cancelled")){HandleP2pEvent(notification);return;}
                if (notification != null && notification.Event == "channel_connected")
                {
                    model.Status = "Discovery reconnected · reconciling friend state…";
                    Refresh();
                    // One bounded reconciliation after a real channel reconnect. The channel
                    // itself owns the relay-only presence lease; it must not restart direct
                    // routing or perform a one-shot presence probe.
                    RefreshFriendState();
				}
				else
				{
					model.Status = "Friend state changed · refreshing…";
					Refresh();
					RefreshFriendState();
				}
			}
		});
	}

	private void ApplyPresenceDiagnostics(PresenceResult result)
	{
		if (result != null)
		{
			lastPublicCandidate = (string.IsNullOrEmpty(result.PublicEndpointHost) ? "Unavailable" : (result.PublicEndpointHost + ":" + result.PublicEndpointPort));
			lastPublicCandidateSource = result.PublicCandidateSource ?? "Unavailable";
			lastObservedAddressClassification = result.ObservedAddressClassification ?? "Unavailable";
			TraceUi("Account/device diagnostics: account=" + result.AccountId + " (" + result.Username + "); local Peer ID=" + model.OwnId + "; server-registered Peer ID=" + result.RegisteredPublicIdentity + "; LAN candidate=" + result.EndpointHost + ":" + result.EndpointPort + "; public candidate=" + lastPublicCandidate + "; source=" + lastPublicCandidateSource + "; observed classification=" + lastObservedAddressClassification);
		}
	}

	private void RefreshAutomaticShareHost()
	{
		if (!shareHostManuallyConfigured)
		{
			string value = DefaultReachableHost();
			if (!string.IsNullOrEmpty(value))
			{
				shareHost = value;
			}
		}
	}

	private Task<bool> PublishCurrentPresenceAsync()
	{
		if (presenceRefreshTask != null && !presenceRefreshTask.IsCompleted)
		{
			return presenceRefreshTask;
		}
        DiscoveryClient discoveryClient = discovery;
        if (discoveryClient == null || string.IsNullOrEmpty(discoveryClient.SessionToken))
        {
            return Task.FromResult(false);
        }
        // A healthy authenticated event subscription renews relay-only presence on the server.
        // Do not create a parallel short-lived discovery connection merely to poll liveness.
        if (discoveryClient.EventChannelConnected)
        {
            lastPresenceUtc = DateTime.UtcNow;
            return Task.FromResult(true);
        }
		RefreshAutomaticShareHost();
		presenceInFlight = true;
		lastPresenceUtc = DateTime.UtcNow;
		return presenceRefreshTask = PublishCurrentPresenceCoreAsync(discoveryClient, shareHost, model.Port);
	}

	private async Task<bool> PublishCurrentPresenceCoreAsync(DiscoveryClient source, string host, ushort port)
	{
		try
		{
			int num = default(int);
			int num2 = num;
			int num3 = 0;
			try
			{
				PresenceResult result = await Task.Run(() => source.ReportPresence(model.OwnId, "", 0, "", 0, false));
				if (discovery != source || closing || closed)
				{
					return false;
				}
				ApplyPresenceDiagnostics(result);
				SaveDiscoverySession();
				return true;
			}
			catch
			{
				return false;
			}
		}
		finally
		{
			presenceInFlight = false;
		}
	}

	private async void RefreshPresence()
	{
		await PublishCurrentPresenceAsync();
	}

	private void NetworkAddressChanged(object sender, EventArgs args)
	{
		if (closing || closed)
		{
			return;
		}
		try
		{
			window.Dispatcher.BeginInvoke((Action)delegate
			{
				if (!closing && !closed)
				{
					networkChangeTimer.Stop();
					networkChangeTimer.Start();
				}
			});
		}
		catch (InvalidOperationException)
		{
		}
	}

	private void HandleNetworkChange()
	{
		string text = CurrentNetworkFingerprint();
		if (!(text == networkFingerprint))
		{
			string text2 = networkFingerprint;
			networkFingerprint = text;
			networkGeneration++;
			core.SetNetworkContext(networkGeneration, networkFingerprint);
			TraceUi("Runtime network change detected: generation=" + networkGeneration + "; previous=" + (string.IsNullOrEmpty(text2) ? "none" : text2) + "; current=" + (string.IsNullOrEmpty(text) ? "none" : text));
			BeginNetworkRecovery("runtime network change", true);
		}
	}

	private async void BeginNetworkRecovery(string reason, bool invalidateRoutes)
	{
		if (networkRecoveryInFlight)
		{
			networkRecoveryPending = true;
			return;
		}
		networkRecoveryInFlight = true;
		try
		{
			do
			{
				networkRecoveryPending = false;
				await RecoverNetworkAsync(reason, invalidateRoutes);
				invalidateRoutes = false;
			}
			while (networkRecoveryPending && !closing && !closed);
		}
		finally
		{
			networkRecoveryInFlight = false;
		}
	}

	private async Task<bool> ForceFriendRefreshAsync()
	{
		Task<bool> pending = friendRefreshTask;
		if (pending != null && !pending.IsCompleted)
		{
			await pending;
		}
		if (discovery == null || closing || closed)
		{
			return false;
		}
		friendRefreshTask = RefreshFriendsCoreAsync();
		return await friendRefreshTask;
	}

	private async Task RecoverNetworkAsync(string reason, bool invalidateRoutes)
	{
		List<ConversationModel> affected = new List<ConversationModel>();
		if (invalidateRoutes)
		{
			foreach (ConversationModel item in model.Conversations.Values.Where((ConversationModel conversationModel) => conversationModel.AccountId > 0 && conversationModel.Contact != null && !string.IsNullOrEmpty(conversationModel.Contact.peer)).ToList())
			{
				ContactData contact = item.Contact;
				LocalSubnetDecision localSubnetDecision;
				if (!string.IsNullOrEmpty(contact.host))
				{
					localSubnetDecision = CurrentLocalSubnetDecision(contact.host);
				}
				else
				{
					LocalSubnetDecision localSubnetDecision2 = new LocalSubnetDecision();
					localSubnetDecision2.Diagnostics = "saved LAN candidate unavailable";
					localSubnetDecision = localSubnetDecision2;
				}
				LocalSubnetDecision localSubnetDecision3 = localSubnetDecision;
				bool flag = IsPrivateCandidate(contact.host) && !localSubnetDecision3.SameSubnet;
				core.InvalidateRoute(contact.peer, flag ? contact.host : "", (ushort)(flag ? contact.port : 0), true, networkGeneration);
				if (flag)
				{
					contact.host = "";
					contact.port = 0;
				}
				// The native session owner decides whether the active binding is
				// actually retired. This recovery request must not pre-emptively
				// downgrade a newer or retained authenticated winner in WPF.
				ConnectionAttemptDetails connectionAttemptDetails = connectionAttempts.Update(new CoreEvent
				{
					expected = contact.peer,
					candidate = (flag ? "Discarded stale private endpoint" : "Previous route invalidated"),
					source = "Runtime network change",
					route = reason
				}, item, "Offline", null, "Network changed");
				connectionAttemptDetails.LocalSubnetDiagnostics = localSubnetDecision3.Diagnostics;
				connectionAttemptDetails.RefreshResult = (flag ? "stale private endpoint invalidated; refreshing authoritative candidates" : "local route invalidated; refreshing authoritative candidates");
				RecordConnection("network-change", item, connectionAttemptDetails.RefreshResult);
				affected.Add(item);
			}
		}
		RefreshAutomaticShareHost();
		Refresh();
		if (string.IsNullOrEmpty(CurrentNetworkFingerprint()))
		{
			model.Status = "Network changed · waiting for an active IPv4 connection";
			Refresh();
			return;
		}
		DiscoveryClient source = discovery;
		if (source == null || string.IsNullOrEmpty(source.SessionToken))
		{
			model.Status = "Network changed · Offline / Server Relay · stale private routes discarded";
			Refresh();
			return;
		}
		model.Status = "Network changed · publishing " + shareHost + ":" + model.Port + "…";
		Refresh();
		if (!(await PublishCurrentPresenceAsync()) || discovery != source)
		{
			model.Status = "Network changed · discovery unavailable; stale private routes were not reused";
			Refresh();
			return;
		}
		if (!(await ForceFriendRefreshAsync()) || discovery != source)
		{
			model.Status = "New presence published · friend candidates unavailable";
			Refresh();
			return;
		}
		foreach (ConversationModel item2 in affected)
		{
			DiscoverySearchResult value;
			if (friendDirectory.TryGetValue(item2.AccountId, out value) && value.Online)
			{
				ConnectFriendCandidate(value, item2, true, "network change");
			}
			else
			{
				connectionAttempts.Update(null, item2).RefreshResult = "authoritative refresh complete; friend has no current online candidate";
			}
		}
		model.Status = "Network change reconciled · current candidates refreshed";
		Refresh();
	}

	private async void RefreshFriendState()
	{
		await RefreshFriendsAsync();
	}

	private Task<bool> RefreshFriendsAsync()
	{
		if (friendRefreshTask != null && !friendRefreshTask.IsCompleted)
		{
			return friendRefreshTask;
		}
		return friendRefreshTask = RefreshFriendsCoreAsync();
	}

	private async Task<bool> RefreshFriendsCoreAsync()
	{
		DiscoveryClient source = discovery;
		if (source == null || string.IsNullOrEmpty(source.SessionToken))
		{
			return false;
		}
		friendRefreshInFlight = true;
		try
		{
			int num = default(int);
			int num2 = num;
			int num3 = 0;
			try
			{
				FriendState state = await Task.Run(() => source.GetFriendState());
				if (discovery != source || closing || closed)
				{
					return false;
				}
				ApplyFriendSnapshot(state);
				return true;
			}
			catch (Exception ex)
			{
				model.Status = "Friend status unavailable: " + ex.Message;
				Refresh();
				return false;
			}
		}
		finally
		{
			friendRefreshInFlight = false;
		}
	}

	private void ApplyFriendSnapshot(FriendState state)
	{
		friendState = state;
		lastFriendRefreshUtc = DateTime.UtcNow;
		friendRequestStates.Clear();
		friendDirectory.Clear();
		foreach (DiscoverySearchResult item2 in state.Incoming)
		{
			friendRequestStates[item2.AccountId] = "Incoming Request";
		}
		foreach (DiscoverySearchResult item3 in state.Outgoing)
		{
			friendRequestStates[item3.AccountId] = "Request Sent";
		}
		conversationIndex.Reconcile(state.Friends);
		authenticatedFriendSnapshotGeneration++;
		core.SetAuthenticatedFriends(discovery == null ? 0 : discovery.AccountId, authenticatedFriendSnapshotGeneration, conversationIndex.Records.Where(binding => binding.ActiveFriend));
		HashSet<long> hashSet = new HashSet<long>();
		foreach (DiscoverySearchResult item in state.Friends)
		{
			friendRequestStates[item.AccountId] = "Friends";
			friendDirectory[item.AccountId] = item;
			hashSet.Add(item.AccountId);
			IEnumerable<FriendConversationBinding> records = conversationIndex.Records;
			Func<FriendConversationBinding, bool> predicate = (FriendConversationBinding friendConversationBinding2) => friendConversationBinding2.AccountId == item.AccountId;
			FriendConversationBinding friendConversationBinding = records.First(predicate);
			bool reused;
			ConversationModel conversationModel = model.BindFriend(item.AccountId, item.Username, item.PublicIdentity, friendConversationBinding.KnownPeerIds, out reused);
			ContactData contact = conversationModel.Contact;
            // Ordinary friend snapshots are intentionally endpoint-free in Server Relay mode.
            // Direct route hints are scoped to an explicit P2P authorization and never become
            // saved presence/contact data here.
            bool flag = contact == null || contact.peer != item.PublicIdentity || !String.IsNullOrEmpty(contact.host) || contact.port != 0;
            bool flag2 = flag || !String.IsNullOrEmpty(contact.publicHost) || contact.publicPort != 0;
			bool value;
			bool flag3 = !friendPresence.TryGetValue(item.AccountId, out value) || value != item.Online;
			friendPresence[item.AccountId] = item.Online;
			if (flag2 && !conversationModel.Online && contact != null && !string.IsNullOrEmpty(contact.peer))
			{
				core.InvalidateRoute(contact.peer, flag ? contact.host : "", (ushort)(flag ? contact.port : 0), false, networkGeneration);
			}
			bool verified = contact != null && contact.peer == item.PublicIdentity && contact.verified;
			conversationModel.Contact = new ContactData
			{
				peer = item.PublicIdentity,
				name = item.Username,
                host = "",
                port = 0,
                publicHost = "",
                publicPort = 0,
				verified = verified
			};
			conversationModel.Refresh();
            TraceUi("Friend conversation " + (reused ? "reused" : "created") + ": account=" + item.AccountId + "; username=" + item.Username + "; device Peer ID=" + item.PublicIdentity + "; local conversation=" + conversationModel.ConversationId + "; ordinary route=server relay");
			ConnectFriendCandidate(item,conversationModel,false,(flag2||flag3)?"candidate update":"authoritative refresh");
		}
		model.ReconcileFriends(hashSet);
		RefreshList();
		Refresh();
		if (this.InviteCandidatesChanged != null)
		{
			this.InviteCandidatesChanged();
		}
	}

	private async void SendFriendRequest(DiscoverySearchResult result)
	{
		if (discovery == null)
		{
			return;
		}
		try
		{
			string state = await Task.Run(() => discovery.SendFriendRequest(result.AccountId));
			model.Status = ((state == "accepted_crossed") ? ("You are now friends with " + result.Username + ".") : ("Friend request sent to " + result.Username + "."));
			RefreshFriendState();
		}
		catch (Exception ex)
		{
			model.Status = ex.Message;
			Refresh();
		}
	}

	private async void RespondToFriendRequest(DiscoverySearchResult result, bool accept)
	{
		if (discovery == null)
		{
			return;
		}
		try
		{
			await Task.Run(() => discovery.RespondToFriendRequest(result.AccountId, accept));
			model.Status = (accept ? ("You are now friends with " + result.Username + ".") : "Friend request rejected.");
			RefreshFriendState();
		}
		catch (Exception ex)
		{
			model.Status = ex.Message;
			Refresh();
		}
	}

	private async void CancelFriendRequest(DiscoverySearchResult result)
	{
		if (discovery == null)
		{
			return;
		}
		try
		{
			await Task.Run(() => discovery.CancelFriendRequest(result.AccountId));
			model.Status = "Friend request to " + result.Username + " cancelled.";
			RefreshFriendState();
		}
		catch (Exception ex)
		{
			model.Status = ex.Message;
			Refresh();
		}
	}

	private async void RemoveFriend(DiscoverySearchResult result)
	{
		if (discovery == null)
		{
			return;
		}
		try
		{
			await Task.Run(() => discovery.RemoveFriend(result.AccountId));
			model.Status = result.Username + " was removed from your friends. Local history was kept.";
			RefreshFriendState();
		}
		catch (Exception ex)
		{
			model.Status = ex.Message;
			Refresh();
		}
	}

	private async void SetBlocked(DiscoverySearchResult result, bool blocked)
	{
		if (discovery == null)
		{
			return;
		}
		try
		{
			await Task.Run(() => discovery.SetBlocked(result.AccountId, blocked));
			model.Status = (blocked ? (result.Username + " is blocked.") : (result.Username + " is unblocked."));
			RefreshFriendState();
		}
		catch (Exception ex)
		{
			model.Status = ex.Message;
			Refresh();
		}
	}

	private Border SettingsCard(string title, string description, FrameworkElement body)
	{
		StackPanel stackPanel = new StackPanel();
		stackPanel.Children.Add(new TextBlock
		{
			Text = title,
			FontSize = 18.0,
			FontWeight = FontWeights.SemiBold
		});
		stackPanel.Children.Add(new TextBlock
		{
			Text = description,
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 4.0, 0.0, 14.0),
			TextWrapping = TextWrapping.Wrap
		});
		stackPanel.Children.Add(body);
		Border border = new Border();
		border.Background = Brush("#111920");
		border.BorderBrush = Brush("#25313C");
		border.BorderThickness = new Thickness(1.0);
		border.CornerRadius = new CornerRadius(12.0);
		border.Padding = new Thickness(18.0);
		border.Margin = new Thickness(0.0, 0.0, 0.0, 14.0);
		border.Child = stackPanel;
		return border;
	}

	private Border FriendRequestCard(DiscoverySearchResult person, string primary, Action primaryAction, string secondary = null, Action secondaryAction = null, string tertiary = null, Action tertiaryAction = null)
	{
		Grid grid = new Grid();
		grid.ColumnDefinitions.Add(new ColumnDefinition());
		grid.ColumnDefinitions.Add(new ColumnDefinition
		{
			Width = GridLength.Auto
		});
		grid.Children.Add(new TextBlock
		{
			Text = person.Username + "  ·  " + (person.Online ? "● Online" : "○ Offline"),
			VerticalAlignment = VerticalAlignment.Center,
			Foreground = Brush(person.Online ? "#36D873" : "#AAB7C4")
		});
		StackPanel stackPanel = new StackPanel();
		stackPanel.Orientation = Orientation.Horizontal;
		StackPanel stackPanel2 = stackPanel;
		Button button = new Button();
		button.Content = primary;
		button.Padding = new Thickness(12.0, 7.0, 12.0, 7.0);
		button.Background = Brush("#244C79");
		Button button2 = button;
		button2.Click += delegate
		{
			primaryAction();
		};
		stackPanel2.Children.Add(button2);
		if (!string.IsNullOrEmpty(secondary))
		{
			Button button3 = new Button();
			button3.Content = secondary;
			button3.Padding = new Thickness(12.0, 7.0, 12.0, 7.0);
			button3.Margin = new Thickness(7.0, 0.0, 0.0, 0.0);
			button3.Background = Brush("#202B35");
			Button button4 = button3;
			button4.Click += delegate
			{
				secondaryAction();
			};
			stackPanel2.Children.Add(button4);
		}
		if (!string.IsNullOrEmpty(tertiary))
		{
			Button button5 = new Button();
			button5.Content = tertiary;
			button5.Padding = new Thickness(12.0, 7.0, 12.0, 7.0);
			button5.Margin = new Thickness(7.0, 0.0, 0.0, 0.0);
			button5.Background = Brush("#202B35");
			Button button6 = button5;
			button6.Click += delegate
			{
				tertiaryAction();
			};
			stackPanel2.Children.Add(button6);
		}
		Grid.SetColumn(stackPanel2, 1);
		grid.Children.Add(stackPanel2);
		Border border = new Border();
		border.Background = Brush("#18222C");
		border.BorderBrush = Brush("#2E3C49");
		border.BorderThickness = new Thickness(1.0);
		border.CornerRadius = new CornerRadius(9.0);
		border.Padding = new Thickness(10.0);
		border.Margin = new Thickness(0.0, 5.0, 0.0, 0.0);
		border.Child = grid;
		return border;
	}

	private async void FriendRequestsDialog()
	{
		if (discovery == null)
		{
			return;
		}
		Stopwatch opened = Stopwatch.StartNew();
		TraceUi("Friend Requests triggered; dispatching snapshot");
		try
		{
			if (!(await RefreshFriendsAsync()))
			{
				return;
			}
			TraceUi("Friend Requests snapshot returned after " + opened.ElapsedMilliseconds + " ms");
		}
		catch (Exception ex)
		{
			Notify("Friend requests are unavailable: " + ex.Message);
			return;
		}
		Window dialog = new Window
		{
			Title = "Friend Requests",
			Owner = window,
			Width = 590.0,
			Height = 650.0,
			WindowStartupLocation = WindowStartupLocation.CenterOwner,
			Background = Brush("#0C1116"),
			Foreground = Brush("#EDF1F5"),
			FontFamily = new FontFamily("Segoe UI"),
			Resources = window.Resources
		};
		Border frame = new Border
		{
			Background = Brush("#101820"),
			BorderBrush = Brush("#293844"),
			BorderThickness = new Thickness(1.0),
			CornerRadius = new CornerRadius(14.0),
			Margin = new Thickness(12.0),
			Padding = new Thickness(24.0)
		};
		dialog.Content = frame;
		ScrollViewer scroll = (ScrollViewer)(frame.Child = new ScrollViewer
		{
			VerticalScrollBarVisibility = ScrollBarVisibility.Auto
		});
		StackPanel stack = (StackPanel)(scroll.Content = new StackPanel());
		stack.Children.Add(new TextBlock
		{
			Text = "Friend Requests",
			FontSize = 25.0,
			FontWeight = FontWeights.SemiBold
		});
		stack.Children.Add(new TextBlock
		{
			Text = "Server-backed requests, friends, and blocks.",
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 5.0, 0.0, 18.0)
		});
		StackPanel incoming = new StackPanel();
		if (friendState.Incoming.Count == 0)
		{
			incoming.Children.Add(new TextBlock
			{
				Text = "No incoming requests yet.",
				Foreground = Brush("#A1AEBB")
			});
		}
		foreach (DiscoverySearchResult person in friendState.Incoming)
		{
			incoming.Children.Add(FriendRequestCard(person, "Accept", delegate
			{
				RespondToFriendRequest(person, true);
				dialog.Close();
			}, "Reject", delegate
			{
				RespondToFriendRequest(person, false);
				dialog.Close();
			}));
		}
		stack.Children.Add(SettingsCard("Incoming Requests", "Accept creates a durable account friendship.", incoming));
		StackPanel outgoing = new StackPanel();
		if (friendState.Outgoing.Count == 0)
		{
			outgoing.Children.Add(new TextBlock
			{
				Text = "No pending requests.",
				Foreground = Brush("#A1AEBB")
			});
		}
		foreach (DiscoverySearchResult person2 in friendState.Outgoing)
		{
			outgoing.Children.Add(FriendRequestCard(person2, "Cancel", delegate
			{
				CancelFriendRequest(person2);
				dialog.Close();
			}));
		}
		stack.Children.Add(SettingsCard("Pending Outgoing", "Requests remain for offline recipients.", outgoing));
		StackPanel friends = new StackPanel();
		if (friendState.Friends.Count == 0)
		{
			friends.Children.Add(new TextBlock
			{
				Text = "No friends yet.",
				Foreground = Brush("#A1AEBB")
			});
		}
		foreach (DiscoverySearchResult person3 in friendState.Friends)
		{
			friends.Children.Add(FriendRequestCard(person3, "Message", delegate
			{
				OpenFriendConversation(person3);
				dialog.Close();
			}, "Remove", delegate
			{
				RemoveFriend(person3);
				dialog.Close();
			}, "Block", delegate
			{
				if (Confirm("Block user", "Block " + person3.Username + "? This also removes the friendship and pending requests.", "Block"))
				{
					SetBlocked(person3, true);
				}
				dialog.Close();
			}));
		}
		stack.Children.Add(SettingsCard("Friends", "Open a direct conversation or manage the friendship.", friends));
		StackPanel blocked = new StackPanel();
		if (friendState.Blocked.Count == 0)
		{
			blocked.Children.Add(new TextBlock
			{
				Text = "No blocked accounts.",
				Foreground = Brush("#A1AEBB")
			});
		}
		foreach (DiscoverySearchResult person4 in friendState.Blocked)
		{
			blocked.Children.Add(FriendRequestCard(person4, "Unblock", delegate
			{
				SetBlocked(person4, false);
				dialog.Close();
			}));
		}
		stack.Children.Add(SettingsCard("Blocked", "Blocked accounts cannot search for or request you.", blocked));
		Button close = new Button
		{
			Content = "Close",
			HorizontalAlignment = HorizontalAlignment.Right,
			Padding = new Thickness(18.0, 9.0, 18.0, 9.0)
		};
		close.Click += delegate
		{
			dialog.Close();
		};
		stack.Children.Add(close);
		TraceUi("Friend Requests dialog constructed after " + opened.ElapsedMilliseconds + " ms; showing");
		dialog.ShowDialog();
	}

	private string ClientBackupDirectory()
	{
		return System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "PeerChat-backup-" + DateTime.Now.ToString("yyyyMMdd-HHmmss"));
	}

	private void BackupClientState()
	{
		BeginProfileMaintenance(ProfileMaintenanceAction.Backup, null, "Back up selected local profile?", "PeerChat will safely close this profile, make a verified backup without runtime lock/WAL files, and reopen it.");
	}

	private static void CopyDirectory(string source, string target)
	{
		Directory.CreateDirectory(target);
		string[] files = Directory.GetFiles(source);
		foreach (string text in files)
		{
			File.Copy(text, System.IO.Path.Combine(target, System.IO.Path.GetFileName(text)), true);
		}
		string[] directories = Directory.GetDirectories(source);
		foreach (string text2 in directories)
		{
			CopyDirectory(text2, System.IO.Path.Combine(target, System.IO.Path.GetFileName(text2)));
		}
	}

	private void RestartWithProfile(string profile)
	{
		string fileName = Process.GetCurrentProcess().MainModule.FileName;
		ProcessStartInfo processStartInfo = new ProcessStartInfo(fileName, "--data-dir \"" + profile.Replace("\"", "\\\"") + "\" --port 0");
		processStartInfo.UseShellExecute = false;
		Process.Start(processStartInfo);
		Environment.Exit(0);
	}

		private void StopProfileServices()
		{
			// The maintenance/bootstrap process performs filesystem work only after this
			// profile owner, its relays, timers and native SQLite owner have stopped.
			timer.Stop();
			voiceTimer.Stop();
			networkChangeTimer.Stop();
			ClearScopedP2p(true);
			StopRelays();
			if (discovery != null)
		{
			discovery.StopEventChannel();
		}
		discovery = null;
		core.Dispose();
	}

	private void RestartManaged(string extraArguments="")
	{
		string fullPath = System.IO.Path.GetFullPath(Process.GetCurrentProcess().MainModule.FileName);
		if (!File.Exists(fullPath))
		{
			throw new FileNotFoundException("PeerChat executable was not found for restart.", fullPath);
		}
		string text = "--install-root \"" + profileManager.InstallRoot.Replace("\"", "\\\"") + "\""+(String.IsNullOrWhiteSpace(extraArguments)?"":" "+extraArguments);
		TraceStartup(profileManager.InstallRoot, "restart-launch executable=" + fullPath + " arguments=" + text);
		ProcessStartInfo processStartInfo = new ProcessStartInfo(fullPath, text);
		processStartInfo.UseShellExecute = false;
		processStartInfo.WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory;
		Process process = Process.Start(processStartInfo);
		if (process == null)
		{
			throw new InvalidOperationException("Windows did not start the replacement PeerChat process.");
		}
		TraceStartup(profileManager.InstallRoot, "restart-launched replacement-pid=" + process.Id);
		Environment.Exit(0);
	}

	private async void SwitchLocalProfile()
	{
		if (profileManager == null)
		{
			Notify("Profile switching is available from the normal installed client launch.");
			return;
		}
		TraceStartup(profileManager.InstallRoot, "switch-requested from-profile=" + localProfile.Id);
		if (!Confirm("Switch local profile?", "PeerChat will close all direct sessions and discovery activity, flush encrypted storage, and restart at the local profile chooser.", "Switch Profile"))
		{
			return;
		}
		TraceStartup(profileManager.InstallRoot, "switch-confirmed from-profile=" + localProfile.Id);
		window.IsEnabled = false;
		try
		{
			await Task.Run(delegate
			{
				StopProfileServices();
			});
			TraceStartup(profileManager.InstallRoot, "profile-services-stopped from-profile=" + localProfile.Id);
			RestartManaged();
		}
		catch (Exception ex)
		{
			window.IsEnabled = true;
			Notify("Profile switch failed: " + ex.Message);
		}
	}

	private void ResetFreshClient()
	{
		BeginProfileMaintenance(ProfileMaintenanceAction.Reset, null, "Reset selected local profile?", "Rebuilds this computer's local PeerChat profile using the latest defaults and storage format.\n\nThis erases local history, settings, trust decisions, cached routes, P2P state, and local session credentials. Your PeerChat account association and cryptographic device identity are retained.\n\nA timestamped local recovery copy is kept. Other profiles and server account records are not changed.");
	}

	private void RestoreClientState()
	{
		string[] array = (from x in Directory.GetDirectories(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "PeerChat-backup-*")
			where Directory.Exists(System.IO.Path.Combine(x, (profileManager == null) ? "PeerChat" : "PeerChatProfile"))
			orderby x descending
			select x).ToArray();
		if (array.Length == 0)
		{
			Notify("No compatible selected-profile backups were found on the Desktop.");
			return;
		}
		string[] array2 = array.Select(System.IO.Path.GetFileName).ToArray();
		string[] array3 = Dialog("Restore Client State", new string[1] { "Backup folder name" }, new string[1] { array2[0] }, "Restore", "Only the currently selected local profile will be replaced. Available backups:\n" + string.Join("\n", array2));
		if (array3 == null)
		{
			return;
		}
		string path = System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), array3[0].Trim());
		string text = System.IO.Path.Combine(path, (profileManager == null) ? "PeerChat" : "PeerChatProfile");
		if (!Directory.Exists(text)||profileManager!=null&&!File.Exists(System.IO.Path.Combine(path,"profile-metadata.json")))
		{
			Notify("That backup folder is missing the selected-profile data or metadata.");
		}
		else
		{
			if (!Confirm("Replace selected local profile?", "The current selected profile will be backed up and replaced. Other profiles are untouched.", "Restore Selected Profile"))
			{
				return;
			}
			BeginProfileMaintenance(ProfileMaintenanceAction.Restore, path, "Replace selected local profile?", "The current selected profile will be preserved as a timestamped recovery copy, then the verified backup will be restored. Other profiles are untouched.");
		}
	}

	private async void BeginProfileMaintenance(ProfileMaintenanceAction action,string restoreRoot,string title,string detail)
	{
		if(profileManager==null||localProfile==null){Notify("Local profile maintenance is available from the normal installed client launch.");return;}
		if(!Confirm(title,detail,title))return;
		try{
			string recovery=ClientBackupDirectory();
			var request=profileManager.CreateMaintenanceRequest(action,localProfile.Id,recovery,restoreRoot);
			window.IsEnabled=false;
			await Task.Run(delegate{StopProfileServices();});
			RestartManaged("--profile-maintenance "+request.RequestId);
		}catch(Exception error){window.IsEnabled=true;Notify("Profile maintenance could not start: "+error.Message);}
	}

	private void SettingsPage()
	{
		string[] service = LoadDiscoveryService();
		Window dialog = new Window
		{
			Title = "Settings",
			Owner = window,
			Width = 1060.0,
			Height = 720.0,
			MinWidth = 900.0,
			MinHeight = 620.0,
			WindowStartupLocation = WindowStartupLocation.CenterOwner,
			Background = Brush("#0C1116"),
			Foreground = Brush("#EDF1F5"),
			FontFamily = new FontFamily("Segoe UI"),
			Resources = window.Resources
		};
		Grid grid = new Grid();
		grid.Margin = new Thickness(18.0);
		Grid grid2 = grid;
		grid2.ColumnDefinitions.Add(new ColumnDefinition
		{
			Width = new GridLength(220.0)
		});
		grid2.ColumnDefinitions.Add(new ColumnDefinition());
		dialog.Content = grid2;
		StackPanel stackPanel = new StackPanel();
		stackPanel.Margin = new Thickness(8.0, 8.0, 20.0, 8.0);
		StackPanel stackPanel2 = stackPanel;
		stackPanel2.Children.Add(new TextBlock
		{
			Text = "PeerChat",
			FontSize = 27.0,
			FontWeight = FontWeights.SemiBold,
			Margin = new Thickness(0.0, 0.0, 0.0, 28.0)
		});
		Action<string> showSettingsPage = null;
		Dictionary<string, Button> settingsNavigation = new Dictionary<string, Button>(StringComparer.Ordinal);
		string[] array = new string[8] { "General", "Privacy & Security", "Voice", "Video", "Screen Sharing", "Network", "Updates", "Advanced" };
		foreach (string text in array)
		{
			Button navigationButton = new Button
			{
				Content = text,
				FontSize = 15.0,
				Foreground = Brush((text == "Advanced") ? "#EDF1F5" : "#A1AEBB"),
				Padding = new Thickness(10.0, 7.0, 10.0, 7.0),
				Margin = new Thickness(0.0, 0.0, 0.0, 4.0),
				HorizontalContentAlignment = HorizontalAlignment.Left,
				ToolTip = "Open " + text + " settings"
			};
			AutomationProperties.SetName(navigationButton, "Settings navigation " + text);
			string page = text;
			navigationButton.Click += delegate
			{
				if (showSettingsPage != null)
				{
					showSettingsPage(page);
				}
			};
			settingsNavigation.Add(text, navigationButton);
			stackPanel2.Children.Add(navigationButton);
		}
		stackPanel2.Children.Add(new TextBlock
		{
			Text = "About\nVersion " + PeerChat.VersionInfo.Display,
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 30.0, 0.0, 0.0)
		});
		grid2.Children.Add(stackPanel2);
		ScrollViewer scrollViewer = new ScrollViewer();
		scrollViewer.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
		ScrollViewer scrollViewer2 = scrollViewer;
		Grid.SetColumn(scrollViewer2, 1);
		grid2.Children.Add(scrollViewer2);
		StackPanel stackPanel3 = (StackPanel)(scrollViewer2.Content = new StackPanel());
		StackPanel advancedPanel = stackPanel3;
		advancedPanel.Children.Add(new TextBlock
		{
			Text = "Advanced",
			FontSize = 29.0,
			FontWeight = FontWeights.SemiBold
		});
		advancedPanel.Children.Add(new TextBlock
		{
			Text = "Power-user, direct connection, identity, and development tools.",
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 4.0, 0.0, 18.0)
		});
		StackPanel stackPanel5 = new StackPanel();
		Button button = new Button();
		button.Content = "◉   Direct P2P Connection\n      Connect directly by hostname/IP and port without discovery";
		button.Padding = new Thickness(14.0);
		button.Margin = new Thickness(0.0, 0.0, 0.0, 8.0);
		Button button2 = button;
		button2.Click += delegate
		{
			try
			{
				Manual(dialog);
			}
			catch (Exception ex)
			{
				Notify(ex.Message, dialog);
			}
		};
		Button button3 = new Button();
		button3.Content = "▣   Copy My Peer ID\n      Copy the persistent cryptographic identity bundle";
		button3.Padding = new Thickness(14.0);
		button3.Margin = new Thickness(0.0, 0.0, 0.0, 8.0);
		Button button4 = button3;
		button4.Click += delegate
		{
			CopyId();
		};
		Button button5 = new Button();
		button5.Content = "♙   View Peer Identity\n      Inspect the permanent device identity and fingerprint";
		button5.Padding = new Thickness(14.0);
		button5.Margin = new Thickness(0.0, 0.0, 0.0, 8.0);
		Button button6 = button5;
		button6.Click += delegate
		{
			Notify("Peer ID is the permanent cryptographic identity. IP addresses and ports are current connection endpoints.");
		};
		stackPanel5.Children.Add(button2);
		stackPanel5.Children.Add(button4);
		stackPanel5.Children.Add(button6);
		advancedPanel.Children.Add(SettingsCard("Advanced Tools", "Direct P2P remains available if the optional PeerChat service is unavailable.", stackPanel5));
		StackPanel stackPanel6 = new StackPanel();
		TextBox host = new TextBox
		{
			Text = service[0],
			Margin = new Thickness(0.0, 0.0, 0.0, 8.0)
		};
		TextBox port = new TextBox
		{
			Text = service[1],
			Margin = new Thickness(0.0, 0.0, 0.0, 8.0)
		};
		stackPanel6.Children.Add(new TextBlock
		{
			Text = "PeerChat Service Endpoint",
			Foreground = Brush("#AAB7C4")
		});
		stackPanel6.Children.Add(host);
		stackPanel6.Children.Add(new TextBlock
		{
			Text = "TLS port",
			Foreground = Brush("#AAB7C4")
		});
		stackPanel6.Children.Add(port);
		Button button7 = new Button();
		button7.Content = "Save Endpoint";
		button7.Padding = new Thickness(14.0);
		button7.HorizontalAlignment = HorizontalAlignment.Left;
		Button button8 = button7;
		button8.Click += delegate
		{
			int result;
			if (!int.TryParse(port.Text, out result) || result < 1 || result > 65535)
			{
				Notify("Enter a valid TLS port.");
			}
			else
			{
				SaveDiscoveryService(host.Text.Trim(), result, (host.Text.Trim() == service[0] && result.ToString() == service[1]) ? service[2] : "");
				dialog.Close();
				model.Status = "PeerChat service endpoint saved.";
				Refresh();
			}
		};
		stackPanel6.Children.Add(button8);
		advancedPanel.Children.Add(SettingsCard("PeerChat Service", "Provides authenticated discovery and PeerChat Server Relay for reliable messaging and control. Direct P2P is optional and requires mutual consent.", stackPanel6));
		StackPanel stackPanel7 = new StackPanel();
		bool settingsListenerActive = model.Listening && model.Port > 0;
		stackPanel7.Children.Add(new TextBlock
		{
			Text = settingsListenerActive ? ("LAN candidate: " + shareHost + ":" + model.Port) : "LAN candidate: Not published",
			Foreground = Brush("#D7E2EC")
		});
		stackPanel7.Children.Add(new TextBlock
		{
			Text = settingsListenerActive ? "Public candidate: Scoped P2P exchange only" : "Public candidate: Not published",
			Foreground = Brush("#AAB7C4"),
			Margin = new Thickness(0.0, 7.0, 0.0, 0.0)
		});
		stackPanel7.Children.Add(new TextBlock
		{
			Text = settingsListenerActive ? ("● Reliable P2P listener active on " + model.Port) : "○ Reliable P2P listener: Not active",
			Foreground = Brush(settingsListenerActive ? "#36D873" : "#AAB7C4"),
			Margin = new Thickness(0.0, 7.0, 0.0, 0.0)
		});
		advancedPanel.Children.Add(SettingsCard("Connection Details", "Direct endpoints are shown only while a temporary P2P listener is active. TURN is used for media relay, not messaging.", stackPanel7));
		StackPanel stackPanel8 = new StackPanel();
		stackPanel8.Children.Add(new TextBlock
		{
			Text = "Profile: " + directory,
			Foreground = Brush("#AAB7C4"),
			TextWrapping = TextWrapping.Wrap
		});
		stackPanel8.Children.Add(new TextBlock
		{
			Text = "Peer ID: " + (string.IsNullOrEmpty(model.OwnId) ? "Not ready" : model.OwnId),
			Foreground = Brush("#AAB7C4"),
			TextTrimming = TextTrimming.CharacterEllipsis,
			Margin = new Thickness(0.0, 6.0, 0.0, 0.0)
		});
		stackPanel8.Children.Add(new TextBlock
		{
			Text = "Service source: " + (File.Exists(DiscoveryServicePath) ? "Saved override" : "Packaged bootstrap") + "\nEndpoint: " + service[0] + ":" + service[1],
			Foreground = Brush("#AAB7C4"),
			Margin = new Thickness(0.0, 6.0, 0.0, 12.0)
		});
		Button button9 = new Button();
		button9.Content = "Switch Local Profile";
		button9.Padding = new Thickness(12.0);
		button9.Margin = new Thickness(0.0, 0.0, 0.0, 7.0);
		button9.IsEnabled = profileManager != null;
		Button button10 = button9;
		button10.Click += delegate
		{
			SwitchLocalProfile();
		};
		Button button11 = new Button();
		button11.Content = "Backup Selected Profile";
		button11.Padding = new Thickness(12.0);
		button11.Margin = new Thickness(0.0, 0.0, 0.0, 7.0);
		Button button12 = button11;
		button12.Click += delegate
		{
			BackupClientState();
		};
		Button button13 = new Button();
		button13.Content = "Reset Selected Profile";
		button13.Padding = new Thickness(12.0);
		button13.Margin = new Thickness(0.0, 0.0, 0.0, 7.0);
		Button button14 = button13;
		button14.Click += delegate
		{
			ResetFreshClient();
		};
		Button button15 = new Button();
		button15.Content = "Restore Selected Profile";
		button15.Padding = new Thickness(12.0);
		Button button16 = button15;
		button16.Click += delegate
		{
			RestoreClientState();
		};
		stackPanel8.Children.Add(button10);
		stackPanel8.Children.Add(button12);
		stackPanel8.Children.Add(button14);
		stackPanel8.Children.Add(button16);
		advancedPanel.Children.Add(SettingsCard("Local Profile Tools", "Switching restarts PeerChat after closing direct sessions and storage. Backup, reset, and restore affect only the selected profile.", stackPanel8));
		StackPanel updatesPanel = new StackPanel();
		updatesPanel.Children.Add(new TextBlock
		{
			Text = "Updates",
			FontSize = 29.0,
			FontWeight = FontWeights.SemiBold
		});
		updatesPanel.Children.Add(new TextBlock
		{
			Text = "Choose a channel and check for a verified PeerChat update.",
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 4.0, 0.0, 18.0)
		});
		StackPanel updatePanel = new StackPanel();
		TextBlock updateStatus = new TextBlock { Text = "Channel: " + updateClient.ChannelLabel + "\nChecks are manual. A downloaded update is verified, staged, applied by the separate updater after PeerChat exits, and rolled back if its first native startup does not confirm healthy.", TextWrapping = TextWrapping.Wrap, Foreground = Brush("#AAB7C4"), Margin = new Thickness(0.0, 0.0, 0.0, 10.0) };
		ComboBox updateChannel = new ComboBox { Width = 190.0, SelectedIndex = String.Equals(updateClient.Preferences.Channel, "development", StringComparison.OrdinalIgnoreCase) ? 1 : 0, Margin = new Thickness(0.0, 0.0, 0.0, 8.0) };
		updateChannel.Items.Add("Release"); updateChannel.Items.Add("Development");
		updateChannel.SelectionChanged += delegate { try { updateClient.SelectChannel(updateChannel.SelectedIndex == 1 ? "development" : "release"); updateStatus.Text = "Channel: " + updateClient.ChannelLabel + "\nChecks are manual. A downloaded update is verified, staged, applied by the separate updater after PeerChat exits, and rolled back if its first native startup does not confirm healthy."; } catch (Exception error) { Notify(error.Message); } };
		Button checkUpdates = new Button { Content = "Check for Updates", Padding = new Thickness(12.0), HorizontalAlignment = HorizontalAlignment.Left };
		checkUpdates.Click += async delegate
		{
			checkUpdates.IsEnabled = false; updateStatus.Text = "Checking the " + updateClient.ChannelLabel + " update feed…";
			try
			{
				var result = await updateClient.CheckForUpdate(System.Threading.CancellationToken.None);
				if (!result.UpdateAvailable) { updateStatus.Text = "Channel: " + updateClient.ChannelLabel + "\n" + result.Reason; return; }
				updateStatus.Text = "Version " + result.Manifest.Version + " is ready to download and verify.";
				if (!Confirm("Install update", "Download and verify PeerChat " + result.Manifest.Version + " from the " + updateClient.ChannelLabel + " channel? PeerChat will close only after the separate updater is launched. The previous files are retained until the replacement confirms a healthy first startup.", "Download and restart")) return;
				updateStatus.Text = "Downloading and verifying PeerChat " + result.Manifest.Version + "…";
				await updateClient.DownloadPrepareAndLaunch(result, Process.GetCurrentProcess().Id, System.Threading.CancellationToken.None);
				updateStatus.Text = "Updater started. Closing PeerChat safely…"; window.Close();
			}
			catch (Exception error) { updateStatus.Text = "Update not installed: " + error.Message; }
			finally { if (!closing) checkUpdates.IsEnabled = true; }
		};
		updatePanel.Children.Add(updateStatus); updatePanel.Children.Add(updateChannel); updatePanel.Children.Add(checkUpdates);
		updatesPanel.Children.Add(SettingsCard("Updates", "Release is the normal verified feed. Development is an explicit installation-wide channel for pre-release testing; it does not alter local profiles or account data.", updatePanel));
		Button button17 = new Button();
		button17.Content = "Close";
		button17.Padding = new Thickness(18.0, 9.0, 18.0, 9.0);
		button17.HorizontalAlignment = HorizontalAlignment.Right;
		Button button18 = button17;
		button18.Click += delegate
		{
			dialog.Close();
		};
		advancedPanel.Children.Add(button18);
		Dictionary<string, StackPanel> settingsPages = new Dictionary<string, StackPanel>(StringComparer.Ordinal)
		{
			{ "Advanced", advancedPanel },
			{ "Updates", updatesPanel }
		};
		Func<string, string, StackPanel> createInfoPage = delegate(string title, string description)
		{
			StackPanel panel = new StackPanel();
			panel.Children.Add(new TextBlock
			{
				Text = title,
				FontSize = 29.0,
				FontWeight = FontWeights.SemiBold
			});
			panel.Children.Add(new TextBlock
			{
				Text = description,
				TextWrapping = TextWrapping.Wrap,
				Foreground = Brush("#A1AEBB"),
				Margin = new Thickness(0.0, 4.0, 0.0, 0.0)
			});
			return panel;
		};
		settingsPages.Add("General", createInfoPage("General", "General client preferences will appear here as they are introduced. Local profile and account controls remain available from the profile bubble."));
		settingsPages.Add("Privacy & Security", createInfoPage("Privacy & Security", "Local history is encrypted on this PC. Peer and account trust status remains available in the main Security & Status surface."));
		StackPanel voiceSettings = createInfoPage("Voice", "Profile-specific audio devices and working software levels. Call controls remain in the conversation header.");
		StackPanel voiceDevicesPanel = new StackPanel();
		voiceDevicesPanel.Children.Add(new TextBlock { Text = "Input device", Foreground = Brush("#AAB7C4") });
		ComboBox settingsInput = new ComboBox { Margin = new Thickness(0.0, 5.0, 0.0, 12.0), IsEnabled = model.VoiceStateCode == 0 };
		AutomationProperties.SetName(settingsInput, "Voice settings input device");
		voiceDevicesPanel.Children.Add(settingsInput);
		voiceDevicesPanel.Children.Add(new TextBlock { Text = "Output device", Foreground = Brush("#AAB7C4") });
		ComboBox settingsOutput = new ComboBox { Margin = new Thickness(0.0, 5.0, 0.0, 12.0), IsEnabled = model.VoiceStateCode == 0 };
		AutomationProperties.SetName(settingsOutput, "Voice settings output device");
		voiceDevicesPanel.Children.Add(settingsOutput);
		Button refreshVoiceSettings = new Button { Content = "Refresh Devices", Padding = new Thickness(12.0, 7.0, 12.0, 7.0), HorizontalAlignment = HorizontalAlignment.Left, IsEnabled = model.VoiceStateCode == 0 };
		voiceDevicesPanel.Children.Add(refreshVoiceSettings);
		Action populateVoiceSettings = delegate {
			VoiceDevices devices = core.VoiceDevices(); VoicePreferenceStore.ResolveDevices(voicePreferences, devices);
			settingsInput.ItemsSource = devices.inputs; settingsOutput.ItemsSource = devices.outputs;
			settingsInput.SelectedItem = (devices.inputs ?? new VoiceDevice[0]).FirstOrDefault(item => item.id == voicePreferences.InputDeviceId && item.name == voicePreferences.InputDeviceName);
			settingsOutput.SelectedItem = (devices.outputs ?? new VoiceDevice[0]).FirstOrDefault(item => item.id == voicePreferences.OutputDeviceId && item.name == voicePreferences.OutputDeviceName);
			if (settingsInput.SelectedItem == null && settingsInput.Items.Count > 0) settingsInput.SelectedIndex = 0;
			if (settingsOutput.SelectedItem == null && settingsOutput.Items.Count > 0) settingsOutput.SelectedIndex = 0;
		};
		Action saveVoiceSettingsDevices = delegate { var input = settingsInput.SelectedItem as VoiceDevice; var output = settingsOutput.SelectedItem as VoiceDevice;if (input == null || output == null || model.VoiceStateCode != 0) return;voicePreferences.InputDeviceId = input.id;voicePreferences.InputDeviceName = input.name;voicePreferences.OutputDeviceId = output.id;voicePreferences.OutputDeviceName = output.name;voicePreferenceStore.Save(voicePreferences);core.SelectVoiceDevices(input.id, output.id);LoadVoiceDevices(false); };
		settingsInput.SelectionChanged += delegate { saveVoiceSettingsDevices(); }; settingsOutput.SelectionChanged += delegate { saveVoiceSettingsDevices(); };
		refreshVoiceSettings.Click += delegate { populateVoiceSettings(); };
		populateVoiceSettings(); voiceSettings.Children.Add(SettingsCard("Devices", "A missing saved device safely falls back to the Windows system default on the next load.", voiceDevicesPanel));
		StackPanel levelsPanel = new StackPanel();
		TextBlock inputLevelLabel = new TextBlock { Text = "Input volume · " + voicePreferences.InputVolume + "%", Foreground = Brush("#AAB7C4") };
		Slider inputLevel = new Slider { Minimum = 0, Maximum = 200, Value = voicePreferences.InputVolume, TickFrequency = 10, IsSnapToTickEnabled = true, Margin = new Thickness(0.0, 5.0, 0.0, 14.0) };
		AutomationProperties.SetName(inputLevel, "Voice input volume");
		TextBlock outputLevelLabel = new TextBlock { Text = "Output volume · " + voicePreferences.OutputVolume + "%", Foreground = Brush("#AAB7C4") };
		Slider outputLevel = new Slider { Minimum = 0, Maximum = 200, Value = voicePreferences.OutputVolume, TickFrequency = 10, IsSnapToTickEnabled = true, Margin = new Thickness(0.0, 5.0, 0.0, 0.0) };
		AutomationProperties.SetName(outputLevel, "Voice output volume");
		Action saveLevels = delegate { voicePreferences.InputVolume = (int)inputLevel.Value;voicePreferences.OutputVolume = (int)outputLevel.Value;inputLevelLabel.Text = "Input volume · " + voicePreferences.InputVolume + "%";outputLevelLabel.Text = "Output volume · " + voicePreferences.OutputVolume + "%";voicePreferenceStore.Save(voicePreferences);core.SetVoiceVolumes(voicePreferences.InputVolume, voicePreferences.OutputVolume); };
		inputLevel.ValueChanged += delegate { saveLevels(); }; outputLevel.ValueChanged += delegate { saveLevels(); };
		levelsPanel.Children.Add(inputLevelLabel);levelsPanel.Children.Add(inputLevel);levelsPanel.Children.Add(outputLevelLabel);levelsPanel.Children.Add(outputLevel);
		voiceSettings.Children.Add(SettingsCard("Levels", "These controls apply real software gain in the native capture and playback path. Mute and Deafen remain immediate call controls.", levelsPanel));
		StackPanel processingPanel = new StackPanel();
		CheckBox processingEnabled = new CheckBox { Content = "Enable Audio Processing", IsChecked = voicePreferences.AudioProcessingEnabled, Margin = new Thickness(0,0,0,8) };
		CheckBox noiseSuppression = new CheckBox { Content = "Noise Suppression", IsChecked = voicePreferences.NoiseSuppressionEnabled, Margin = new Thickness(0,0,0,8) };
		CheckBox automaticGain = new CheckBox { Content = "Automatic Gain Control", IsChecked = voicePreferences.AutomaticGainEnabled, Margin = new Thickness(0,0,0,8) };
		CheckBox echoCancellation = new CheckBox { Content = "Echo Cancellation (not available yet)", IsChecked = false, IsEnabled = false };
		AutomationProperties.SetName(processingEnabled,"Enable Audio Processing");AutomationProperties.SetName(noiseSuppression,"Noise Suppression");AutomationProperties.SetName(automaticGain,"Automatic Gain Control");AutomationProperties.SetName(echoCancellation,"Echo Cancellation unavailable");
		Action saveProcessing = delegate { voicePreferences.AudioProcessingEnabled = processingEnabled.IsChecked == true;voicePreferences.NoiseSuppressionEnabled = noiseSuppression.IsChecked == true;voicePreferences.AutomaticGainEnabled = automaticGain.IsChecked == true;voicePreferenceStore.Save(voicePreferences);core.SetVoiceProcessing(voicePreferences.AudioProcessingEnabled,voicePreferences.NoiseSuppressionEnabled,voicePreferences.AutomaticGainEnabled);noiseSuppression.IsEnabled=processingEnabled.IsChecked==true;automaticGain.IsEnabled=processingEnabled.IsChecked==true; };
		processingEnabled.Checked += delegate { saveProcessing(); };processingEnabled.Unchecked += delegate { saveProcessing(); };noiseSuppression.Checked += delegate { saveProcessing(); };noiseSuppression.Unchecked += delegate { saveProcessing(); };automaticGain.Checked += delegate { saveProcessing(); };automaticGain.Unchecked += delegate { saveProcessing(); };
		noiseSuppression.IsEnabled=voicePreferences.AudioProcessingEnabled;automaticGain.IsEnabled=voicePreferences.AudioProcessingEnabled;
		processingPanel.Children.Add(processingEnabled);processingPanel.Children.Add(noiseSuppression);processingPanel.Children.Add(automaticGain);processingPanel.Children.Add(echoCancellation);
		voiceSettings.Children.Add(SettingsCard("Audio processing", "Changes apply to the next call or media generation. Echo cancellation needs a speaker reference and is not available yet.", processingPanel));
		settingsPages.Add("Voice", voiceSettings);
		settingsPages.Add("Video", createInfoPage("Video", "Video is not part of the current client baseline. Future preferences will appear here when that feature is introduced."));
		StackPanel screenSettings=createInfoPage("Screen Sharing", "Profile-specific preferences for secure one-to-one screen sharing. Source selection remains per share; no source/window handle is saved.");
		var screenControls=new StackPanel();
		screenControls.Children.Add(new TextBlock{Text="Frame rate",Foreground=Brush("#AAB7C4")});
		var screenFps=new ComboBox{ItemsSource=new int[]{15,30,60},SelectedItem=screenPreferences.FramesPerSecond,Margin=new Thickness(0,5,0,12)};
		AutomationProperties.SetName(screenFps,"Screen share frame rate");
		screenControls.Children.Add(screenFps);
		screenControls.Children.Add(new TextBlock{Text="Video bitrate",Foreground=Brush("#AAB7C4")});
		var screenRates=new[]{new KeyValuePair<int,string>(2000000,"2 Mbps"),new KeyValuePair<int,string>(4000000,"4 Mbps"),new KeyValuePair<int,string>(8000000,"8 Mbps"),new KeyValuePair<int,string>(16000000,"16 Mbps"),new KeyValuePair<int,string>(25000000,"25 Mbps")};
		var screenBitrate=new ComboBox{ItemsSource=screenRates,DisplayMemberPath="Value",SelectedValuePath="Key",SelectedValue=screenPreferences.BitrateBitsPerSecond,Margin=new Thickness(0,5,0,12)};
		AutomationProperties.SetName(screenBitrate,"Screen share bitrate");
		screenControls.Children.Add(screenBitrate);
		var screenCursor=new CheckBox{Content="Include cursor",IsChecked=screenPreferences.CaptureCursor,Margin=new Thickness(0,2,0,4)};
		AutomationProperties.SetName(screenCursor,"Screen share cursor");
		screenControls.Children.Add(screenCursor);
		Action saveScreenSettings=delegate{if(screenFps.SelectedItem==null||screenBitrate.SelectedValue==null)return;screenPreferences.FramesPerSecond=(int)screenFps.SelectedItem;screenPreferences.BitrateBitsPerSecond=(int)screenBitrate.SelectedValue;screenPreferences.CaptureCursor=screenCursor.IsChecked==true;screenPreferenceStore.Save(screenPreferences);};
		screenFps.SelectionChanged+=delegate{saveScreenSettings();};screenBitrate.SelectionChanged+=delegate{saveScreenSettings();};screenCursor.Checked+=delegate{saveScreenSettings();};screenCursor.Unchecked+=delegate{saveScreenSettings();};
		screenSettings.Children.Add(SettingsCard("Capture quality", "H.264 hardware encoding; effective resolution follows the selected source and available device capability. Settings take effect on the next share.",screenControls));
		settingsPages.Add("Screen Sharing",screenSettings);
		var networkSettings=new StackPanel();networkSettings.Children.Add(new TextBlock{Text="Reliable connection",FontSize=24,FontWeight=FontWeights.SemiBold});networkSettings.Children.Add(new TextBlock{Text="Server Relay — default\nPeerChat uses the server relay for reliable messaging and control. Voice and screen sharing may still use their existing ICE media paths.",Foreground=Brush("#A1AEBB"),TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,8,0,18)});var allowP2p=new CheckBox{Content="Allow direct P2P connections",IsChecked=connectionPreferences.AllowDirectP2p,Foreground=Brush("#EDF1F5")};allowP2p.Checked+=(s,e)=>{connectionPreferences.AllowDirectP2p=true;connectionPreferenceStore.Save(connectionPreferences);};allowP2p.Unchecked+=(s,e)=>{connectionPreferences.AllowDirectP2p=false;connectionPreferenceStore.Save(connectionPreferences);ClearScopedP2p(true);};networkSettings.Children.Add(allowP2p);networkSettings.Children.Add(new TextBlock{Text="Direct P2P can reduce server usage and latency, but it may reveal your IP address to the other person. It is off by default and requires mutual consent for each friend.",Foreground=Brush("#A1AEBB"),TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,8,0,0)});settingsPages.Add("Network",networkSettings);
		showSettingsPage = delegate(string page)
		{
			StackPanel panel;
			if (!settingsPages.TryGetValue(page, out panel))
			{
				return;
			}
			scrollViewer2.Content = panel;
			foreach (KeyValuePair<string, Button> item in settingsNavigation)
			{
				item.Value.Foreground = Brush((item.Key == page) ? "#EDF1F5" : "#A1AEBB");
			}
		};
		showSettingsPage("Advanced");
		dialog.ShowDialog();
	}

	private void DiscoverySettings()
	{
		string[] array = LoadDiscoveryService();
		string[] array2 = Dialog("PeerChat Server", new string[2] { "Server hostname or address", "TLS port" }, new string[2]
		{
			array[0],
			array[1]
		}, "Save server", "Advanced development setting. Changing the endpoint clears its trusted development server identity and asks for confirmation on the next connection.");
		if (array2 != null)
		{
			int result;
			if (string.IsNullOrWhiteSpace(array2[0]) || !int.TryParse(array2[1], out result) || result < 1 || result > 65535)
			{
				Notify("Enter a valid server address and TLS port.");
				return;
			}
			string text = array2[0].Trim();
			string pin = ((string.Equals(text, array[0], StringComparison.OrdinalIgnoreCase) && result.ToString() == array[1]) ? array[2] : "");
			SaveDiscoveryService(text, result, pin);
			model.Status = "PeerChat server settings saved.";
			Refresh();
		}
	}

	private static List<LocalSubnetInterface> CurrentLocalSubnetInterfaces()
	{
		List<LocalSubnetInterface> list = new List<LocalSubnetInterface>();
		NetworkInterface[] allNetworkInterfaces;
		try
		{
			allNetworkInterfaces = NetworkInterface.GetAllNetworkInterfaces();
		}
		catch (Exception ex)
		{
			list.Add(new LocalSubnetInterface
			{
				Name = "Windows interface enumeration",
				IgnoredReason = ex.GetType().Name
			});
			return list;
		}
		NetworkInterface[] array = allNetworkInterfaces;
		foreach (NetworkInterface networkInterface in array)
		{
			string name = string.Concat(networkInterface.Name ?? "unnamed", " [", networkInterface.NetworkInterfaceType, "]");
			if (networkInterface.OperationalStatus != OperationalStatus.Up)
			{
				list.Add(new LocalSubnetInterface
				{
					Name = name,
					IgnoredReason = "interface is " + networkInterface.OperationalStatus
				});
				continue;
			}
			if (networkInterface.NetworkInterfaceType == NetworkInterfaceType.Loopback || networkInterface.NetworkInterfaceType == NetworkInterfaceType.Tunnel)
			{
				list.Add(new LocalSubnetInterface
				{
					Name = name,
					IgnoredReason = "loopback/tunnel adapter"
				});
				continue;
			}
			IPInterfaceProperties iPProperties;
			try
			{
				iPProperties = networkInterface.GetIPProperties();
			}
			catch (Exception ex2)
			{
				list.Add(new LocalSubnetInterface
				{
					Name = name,
					IgnoredReason = "properties unavailable: " + ex2.GetType().Name
				});
				continue;
			}
			bool flag = false;
			foreach (UnicastIPAddressInformation unicastAddress in iPProperties.UnicastAddresses)
			{
				IPAddress address;
				try
				{
					address = unicastAddress.Address;
				}
				catch (Exception ex3)
				{
					list.Add(new LocalSubnetInterface
					{
						Name = name,
						IgnoredReason = "address unavailable: " + ex3.GetType().Name
					});
					continue;
				}
				if (address == null || address.AddressFamily != AddressFamily.InterNetwork)
				{
					continue;
				}
				flag = true;
				if (IPAddress.IsLoopback(address) || address.Equals(IPAddress.Any) || address.ToString().StartsWith("169.254."))
				{
					list.Add(new LocalSubnetInterface
					{
						Name = name,
						Address = address.ToString(),
						IgnoredReason = "loopback, unspecified, or link-local address"
					});
					continue;
				}
				try
				{
					IPAddress iPv4Mask = unicastAddress.IPv4Mask;
					list.Add(new LocalSubnetInterface
					{
						Name = name,
						Address = address.ToString(),
						Mask = ((iPv4Mask == null) ? "" : iPv4Mask.ToString()),
						IgnoredReason = ((iPv4Mask == null) ? "IPv4 subnet mask unavailable" : "")
					});
				}
				catch (Exception ex4)
				{
					list.Add(new LocalSubnetInterface
					{
						Name = name,
						Address = address.ToString(),
						IgnoredReason = "IPv4 subnet mask unavailable: " + ex4.GetType().Name
					});
				}
			}
			if (!flag)
			{
				list.Add(new LocalSubnetInterface
				{
					Name = name,
					IgnoredReason = "no IPv4 unicast address"
				});
			}
		}
		return list;
	}

	private static LocalSubnetDecision CurrentLocalSubnetDecision(string candidate)
	{
		return LocalSubnetClassifier.Evaluate(candidate, CurrentLocalSubnetInterfaces());
	}

	private static string CurrentNetworkFingerprint()
	{
		return string.Join("|", (from item in CurrentLocalSubnetInterfaces()
			where string.IsNullOrEmpty(item.IgnoredReason) && !string.IsNullOrEmpty(item.Address) && !string.IsNullOrEmpty(item.Mask)
			select item.Address + "/" + item.Mask).OrderBy((string value) => value, StringComparer.Ordinal));
	}

	private static bool IsPrivateCandidate(string candidate)
	{
		IPAddress address;
		if (!IPAddress.TryParse(candidate, out address) || address.AddressFamily != AddressFamily.InterNetwork)
		{
			return false;
		}
		byte[] addressBytes = address.GetAddressBytes();
		if (addressBytes[0] != 10 && (addressBytes[0] != 172 || addressBytes[1] < 16 || addressBytes[1] > 31) && (addressBytes[0] != 192 || addressBytes[1] != 168))
		{
			if (addressBytes[0] == 169)
			{
				return addressBytes[1] == 254;
			}
			return false;
		}
		return true;
	}

	private static bool IsRouteFailure(string message)
	{
		string text = (message ?? "").ToLowerInvariant();
		if (!text.Contains("tcp ") && !text.Contains("connection refused") && !text.Contains("unreachable") && !text.Contains("dns"))
		{
			if (text.Contains("address"))
			{
				return text.Contains("invalid");
			}
			return false;
		}
		return true;
	}

	private static string ConnectionFailureSummary(string message)
	{
		string text = (message ?? "").ToLowerInvariant();
		if (text.Contains("peer certificate"))
		{
			// This is bounded public certificate metadata from the native verifier.
			// Preserve it in Copy Connection Details for clock/validity diagnosis.
			return text;
		}
		if (text.Contains("refused"))
		{
			return "Connection refused";
		}
		if (text.Contains("timeout") || text.Contains("timed out"))
		{
			return "TCP timeout";
		}
		if (text.Contains("tls") || text.Contains("secure session") || text.Contains("handshake"))
		{
			return "TLS handshake failed";
		}
		if (text.Contains("identity changed"))
		{
			return "Peer identity changed";
		}
		if (text.Contains("identity") || text.Contains("trust") || text.Contains("verification"))
		{
			return "Identity verification required";
		}
		if (text.Contains("candidate") || text.Contains("unreachable") || text.Contains("dns") || text.Contains("route"))
		{
			return "No reachable candidates";
		}
		return "Connection failed";
	}

	private void RecordConnection(string stage, ConversationModel conversation, string result, CoreEvent connectionEvent = null)
	{
		ConnectionAttemptDetails connectionAttemptDetails = ((connectionEvent == null) ? connectionAttempts.For(conversation) : connectionAttempts.Update(connectionEvent, conversation));
		connectionLog.Record(stage, conversation==null?0:conversation.AccountId, conversation==null?"":conversation.ConversationId, connectionAttemptDetails.ExpectedPeer, connectionAttemptDetails.PresentedPeer, connectionAttemptDetails.Candidate, connectionAttemptDetails.Source, connectionAttemptDetails.Route, result);
	}

	private void RequestConversationHistory(ConversationModel conversation)
	{
		if (conversation == null || conversation.RequestedHistory || conversation.Contact == null || String.IsNullOrEmpty(conversation.Contact.peer))
		{
			return;
		}
		string peer = conversation.Contact.peer;
		conversation.RequestedPeerHistory.Add(peer);
		conversation.LoadingHistoryPeer = peer;
		conversation.HistoryLoading = true;
		core.History(peer, long.MaxValue);
		conversation.RequestedHistory = true;
	}

	private void RequestGroupHistory(GroupModel group)
	{
		if (group == null || group.RequestedHistory)
		{
			return;
		}
		group.RequestedHistory = true;
		group.HistoryLoading = true;
		core.GroupHistory(group.Id, long.MaxValue);
	}

	private string ConnectionDiagnosticsText()
	{
		string[] array = LoadDiscoveryService();
		ConversationModel selected = model.Selected;
		ConnectionAttemptDetails connectionAttemptDetails = connectionAttempts.For(selected);
			string listener=model.Port==0?"None (relay-only)":"0.0.0.0:"+model.Port;
			string localCandidate=model.Port==0?"None (not published)":shareHost+":"+model.Port;
			string publicCandidate=(selected==null||selected.Contact==null||String.IsNullOrWhiteSpace(selected.Contact.publicHost)||selected.Contact.publicPort==0)?"None":selected.Contact.publicHost+":"+selected.Contact.publicPort;
			return "PeerChat connection details\nTimestamp: " + DateTime.UtcNow.ToString("O") + "\nProfile: " + ((localProfile == null) ? "Local" : localProfile.Name) + "\nAccount ID: " + ((selected == null || selected.AccountId <= 0) ? "Unavailable" : selected.AccountId.ToString()) + "\nConversation ID: " + ((selected == null) ? "None" : selected.ConversationId) + "\nDiscovery endpoint: " + array[0] + ":" + array[1] + "\nLocal P2P listener: " + listener + "\nLAN candidate: " + localCandidate + "\nLocal TLS certificate: " + localTlsCertificateDiagnostics + "\nFriend presence: " + connectionAttemptDetails.Presence + "\nCandidate seen UTC (server): " + connectionAttemptDetails.CandidateSeenUtc + "\nRefresh result: " + connectionAttemptDetails.RefreshResult + "\nPublic candidate: " + publicCandidate + "\nLive winning session: " + (connectionAttemptDetails.HasLiveSession ? "Connected" : "None") + "\nWinning connection ID: " + (connectionAttemptDetails.HasLiveSession ? connectionAttemptDetails.LiveConnectionId.ToString() : "None") + "\nWinning direction: " + connectionAttemptDetails.LiveDirection + "\nWinning endpoint: " + connectionAttemptDetails.LiveCandidate + "\nWinning source: " + connectionAttemptDetails.LiveSource + "\nWinning route: " + connectionAttemptDetails.LiveRoute + "\nCandidate source: " + connectionAttemptDetails.Source + "\nSelected candidate: " + connectionAttemptDetails.Candidate + "\nSelection reason: " + connectionAttemptDetails.Route + "\nExpected Peer ID: " + (string.IsNullOrEmpty(connectionAttemptDetails.ExpectedPeer) ? "Unspecified" : connectionAttemptDetails.ExpectedPeer) + "\nPresented/current Peer ID: " + (string.IsNullOrEmpty(connectionAttemptDetails.PresentedPeer) ? "Not presented" : connectionAttemptDetails.PresentedPeer) + "\nTCP/TLS state: " + connectionAttemptDetails.TransportState + "\nTrust state: " + connectionAttemptDetails.TrustState + "\nLatest failed attempt ID: " + ((connectionAttemptDetails.LatestFailedAttemptId > 0) ? connectionAttemptDetails.LatestFailedAttemptId.ToString() : "None") + "\nLatest failed endpoint: " + connectionAttemptDetails.LatestFailedCandidate + "\nLatest failed source: " + connectionAttemptDetails.LatestFailedSource + "\nLatest failed route: " + connectionAttemptDetails.LatestFailedRoute + "\nLatest failed reason: " + connectionAttemptDetails.LatestFailure + "\n\nLocal subnet classification:\n" + connectionAttemptDetails.LocalSubnetDiagnostics;
	}

	private void ConnectFriendCandidate(DiscoverySearchResult friend, ConversationModel conversation, bool force, string trigger)
	{
		var details=connectionAttempts.Update(new CoreEvent{expected=friend.PublicIdentity},conversation);details.Presence=friend.Online?"Online":"Offline";details.CandidateSeenUtc=friend.CandidateSeenUtc;
		var subnet=new LocalSubnetDecision{SameSubnet=false,Diagnostics="Reliable direct candidates are disabled for ordinary relay presence"};details.LocalSubnetDiagnostics=subnet.Diagnostics;
		bool relayAvailable=discovery!=null&&!String.IsNullOrEmpty(discovery.SessionToken);
		lastPublicCandidate="Unavailable";lastPublicCandidateSource="Relay-only presence";lastObservedAddressClassification="Not evaluated";
		TraceUi("Server-first route update: account="+friend.AccountId+"; generation="+networkGeneration+"; relay="+relayAvailable+"; direct candidates suppressed; trigger="+trigger);
		// The manager receives only its relay route on ordinary presence/manual reconnect.
		// P2P candidates enter solely through BeginScopedP2p after mutual consent.
		core.UpdateCandidates(friend.PublicIdentity,"",0,"",0,relayAvailable,networkGeneration,force);
			if(conversation.SessionRevision==0){conversation.ConnectionState="Finding server relay";conversation.Refresh();}model.Status="Connecting to "+friend.Username+" · secure server relay"+(connectionPreferences.AllowDirectP2p?", manual P2P available":"")+"…";Refresh();
	}

    void ClearScopedP2p(bool notifyServer){
        foreach(var request in scopedP2p.Values.ToArray()){
            core.CancelScopedP2p(request.Peer);
            if(notifyServer&&discovery!=null)try{discovery.CancelP2p(request.Id,model.OwnId);}catch{}
        }
        scopedP2p.Clear();
    }
    void SubmitScopedP2pCandidate(ScopedP2pRequest request,ushort port){
        if(request==null||discovery==null||port==0)return;
        Task.Run(()=>{
            try{
                var response=discovery.SubmitP2pCandidate(request.Id,model.OwnId,DefaultReachableHost(),port);
                string state=response.ContainsKey("state")?response["state"] as string:"";
                if(state!="candidates_ready")return;
                var ready=new DiscoveryEvent{Event="p2p_candidates_ready",RequestId=request.Id,PeerHost=response.ContainsKey("peerHost")?response["peerHost"] as string:"",PeerPublicIdentity=response.ContainsKey("peerPublicIdentity")?response["peerPublicIdentity"] as string:"",PeerPort=response.ContainsKey("peerPort")?Convert.ToInt32(response["peerPort"]):0,ExpiresUtc=response.ContainsKey("expiresUtc")?Convert.ToInt64(response["expiresUtc"]):0};
                window.Dispatcher.BeginInvoke((Action)(()=>HandleP2pEvent(ready)));
            }catch(Exception error){window.Dispatcher.BeginInvoke((Action)(()=>{var conversation=model.FindByPeer(request.Peer);if(conversation!=null){conversation.ConnectionState="P2P unavailable · relay remains connected";conversation.Refresh();}RecordConnection("p2p",conversation,"candidate submission failed: "+error.GetType().Name);}));}
        });
    }
    void TryScopedP2p(ConversationModel conversation){
        if(conversation==null||discovery==null||!conversation.IsFriend||conversation.AccountId<=0||conversation.Contact==null||String.IsNullOrEmpty(conversation.Contact.peer)){Notify("P2P is available only for an online established friend with a healthy relay connection.");return;}
        if(!connectionPreferences.AllowDirectP2p){if(!Confirm("Allow direct P2P?","Direct P2P can reduce latency, but it may reveal your IP address to this friend. You can turn it off later in Settings → Network.","Allow P2P"))return;connectionPreferences.AllowDirectP2p=true;connectionPreferenceStore.Save(connectionPreferences);}
        FirewallResult firewall=FirewallRules.EnsurePrivateProgramRule(profileManager==null?AppDomain.CurrentDomain.BaseDirectory:profileManager.InstallRoot);
        if(!firewall.Ok){Notify(firewall.PermissionDenied?"P2P was not started because Windows Firewall permission was not approved. Relay messaging remains connected.":"P2P was not started. "+firewall.Detail);return;}
        try{var response=discovery.RequestP2p(conversation.AccountId,model.OwnId,conversation.Contact.peer);string id=response.ContainsKey("requestId")?response["requestId"] as string:"";long expiry=response.ContainsKey("expiresUtc")?Convert.ToInt64(response["expiresUtc"]):0;if(id.Length!=43)throw new InvalidOperationException("The server did not authorize this P2P request.");scopedP2p[id]=new ScopedP2pRequest{Id=id,Peer=conversation.Contact.peer,AccountId=conversation.AccountId,ExpiresUtc=expiry,Initiated=true};conversation.ConnectionState="Waiting for P2P consent · relay remains connected";conversation.Refresh();RecordConnection("p2p",conversation,"consent requested");}catch(Exception error){Notify("P2P request was not started. Relay messaging remains connected. "+error.Message);}
    }
    void HandleIncomingP2p(DiscoveryEvent e){
        var conversation=model.FindByPeer(e.PublicIdentity);if(conversation==null||!conversation.IsFriend||String.IsNullOrEmpty(e.RequestId)||e.RequestId.Length!=43)return;
        bool allow=Confirm("Try direct P2P connection?",conversation.Name+" wants to try a direct P2P connection. If you allow this, your IP address may be visible to them. Relay messaging stays connected while it is tested.","Allow");
        try{discovery.ConsentP2p(e.RequestId,model.OwnId,allow);if(!allow){conversation.ConnectionState="P2P declined · relay remains connected";conversation.Refresh();return;}scopedP2p[e.RequestId]=new ScopedP2pRequest{Id=e.RequestId,Peer=e.PublicIdentity,AccountId=conversation.AccountId,ExpiresUtc=e.ExpiresUtc};core.PrepareScopedP2pListener(e.PublicIdentity);conversation.ConnectionState="Preparing direct P2P · relay remains connected";conversation.Refresh();}catch(Exception error){Notify("Could not respond to the P2P request. "+error.Message);}
    }
    void HandleP2pEvent(DiscoveryEvent e){
        if(e.Event=="p2p_accepted"){ScopedP2pRequest state;if(scopedP2p.TryGetValue(e.RequestId,out state)){state.ExpiresUtc=e.ExpiresUtc;core.PrepareScopedP2pListener(state.Peer);}return;}
        ScopedP2pRequest request;if(!scopedP2p.TryGetValue(e.RequestId,out request))return;var conversation=model.FindByPeer(request.Peer);
        if(e.Event=="p2p_candidates_ready"){if(e.PeerPublicIdentity!=request.Peer||String.IsNullOrEmpty(e.PeerHost)||e.PeerPort<=0||e.ExpiresUtc<=DateTimeOffset.UtcNow.ToUnixTimeSeconds()){core.CancelScopedP2p(request.Peer);if(conversation!=null){conversation.ConnectionState="P2P unavailable · relay remains connected";conversation.Refresh();}return;}if(!core.BeginScopedP2p(request.Peer,e.RequestId,e.PeerHost,(ushort)e.PeerPort,(ulong)e.ExpiresUtc,networkGeneration)){if(conversation!=null){conversation.ConnectionState="P2P unavailable · relay remains connected";conversation.Refresh();}}else if(conversation!=null){conversation.ConnectionState="Testing P2P · relay remains connected";conversation.Refresh();}return;}
        core.CancelScopedP2p(request.Peer);scopedP2p.Remove(e.RequestId);if(conversation!=null){conversation.ConnectionState=e.Event=="p2p_declined"?"P2P declined · relay remains connected":"P2P cancelled · relay remains connected";conversation.Refresh();}
    }
    void PromoteScopedP2p(ConversationModel conversation){
        var request=scopedP2p.Values.FirstOrDefault(value=>value.Peer==conversation.Contact.peer);
        if(request==null||!core.PromoteScopedP2p(request.Peer,request.Id)){Notify("The verified P2P route is no longer available. Relay messaging remains connected.");return;}
        conversation.ConnectionState="Switching to verified P2P · relay remains connected";conversation.Refresh();
    }
    void ReturnToRelay(ConversationModel conversation){
        ClearScopedP2p(true);conversation.ConnectionState="Server Relay";conversation.Refresh();
    }

	private static string ReliableConnectionMethod(ConversationModel conversation)
	{
		if(conversation==null||!conversation.Online)return "Connecting / Unavailable";
		return conversation.ReliableRoute==1?"Direct LAN":conversation.ReliableRoute==2?"Direct Internet":conversation.ReliableRoute==3?"PeerChat Server Relay":"Direct connection";
	}

	private string VoiceConnectionMethod()
	{
		if(model.VoiceMediaState==3||model.VoiceMediaState==4)return "Recovering";
		if(model.VoiceMediaState!=2)return "Unavailable";
		return model.VoiceMediaRoute==1?"Direct LAN ICE":model.VoiceMediaRoute==2?"Direct ICE / NAT traversal":model.VoiceMediaRoute==3?"TURN Relay":"Direct ICE";
	}

	private string VoiceConnectionState()
	{
		return model.VoiceMediaState==2?"Connected":model.VoiceMediaState==3||model.VoiceMediaState==4?"Recovering · generation "+model.VoiceMediaGeneration:model.VoiceMediaState==1?"Connecting · generation "+model.VoiceMediaGeneration:"Unavailable";
	}

	private void OpenFriendConversation(DiscoverySearchResult requested, bool force = false, string trigger = "selection")
	{
		DiscoverySearchResult friend;
		lock (friendDirectory)
		{
			if (!friendDirectory.TryGetValue(requested.AccountId, out friend))
			{
				friend = requested;
			}
		}
		FriendConversationBinding friendConversationBinding = conversationIndex.Records.FirstOrDefault((FriendConversationBinding value) => value.AccountId == friend.AccountId);
		IEnumerable<string> knownPeers = ((friendConversationBinding == null) ? ((IEnumerable<string>)new string[1] { friend.PublicIdentity }) : friendConversationBinding.KnownPeerIds);
		bool reused;
		ConversationModel conversationModel = model.BindFriend(friend.AccountId, friend.Username, friend.PublicIdentity, knownPeers, out reused);
		bool verified = conversationModel.Contact != null && conversationModel.Contact.peer == friend.PublicIdentity && conversationModel.Contact.verified;
		// Normal discovery presence deliberately carries no reusable reliable P2P route.
		// Scoped consent supplies temporary candidates directly to the native probe path.
		string host = "";
		string publicHost = "";
		int endpointPort = 0;
		int publicEndpointPort = 0;
		conversationModel.Contact = new ContactData
		{
			peer = friend.PublicIdentity,
			name = friend.Username,
			host = host,
			port = (ushort)Math.Max(0, Math.Min(65535, endpointPort)),
			publicHost = publicHost,
			publicPort = (ushort)Math.Max(0, Math.Min(65535, publicEndpointPort)),
			verified = verified
		};
		if (conversationModel.SessionRevision == 0)
		{
			conversationModel.ConnectionState = (friend.Online ? "Finding peer" : "Offline");
		}
		conversationModel.Refresh();
		model.Groups = false;
		model.Selected = conversationModel;
		conversationModel.Unread = 0;
		RequestConversationHistory(conversationModel);
		TraceUi("Friend conversation " + (reused ? "reused" : "created") + ": account=" + friend.AccountId + "; username=" + friend.Username + "; device Peer ID=" + friend.PublicIdentity + "; local conversation=" + conversationModel.ConversationId);
		ShowConversation();
		RefreshList();
		Refresh();
		ConnectFriendCandidate(friend, conversationModel, force, trigger);
	}

	private Border DiscoveryCard(DiscoverySearchResult result)
	{
		Border border = new Border();
		border.Background = Brush("#18222C");
		border.BorderBrush = Brush("#2E3C49");
		border.BorderThickness = new Thickness(1.0);
		border.CornerRadius = new CornerRadius(12.0);
		border.Padding = new Thickness(14.0);
		border.Margin = new Thickness(0.0, 0.0, 0.0, 9.0);
		Border border2 = border;
		Grid grid = new Grid();
		grid.ColumnDefinitions.Add(new ColumnDefinition
		{
			Width = new GridLength(48.0)
		});
		grid.ColumnDefinitions.Add(new ColumnDefinition());
		grid.ColumnDefinitions.Add(new ColumnDefinition
		{
			Width = GridLength.Auto
		});
		border2.Child = grid;
		Grid grid2 = new Grid();
		grid2.Width = 40.0;
		grid2.Height = 40.0;
		grid2.VerticalAlignment = VerticalAlignment.Top;
		Grid grid3 = grid2;
		grid3.Children.Add(new Ellipse
		{
			Fill = Brush("#365D89")
		});
		grid3.Children.Add(new TextBlock
		{
			Text = (string.IsNullOrEmpty(result.Username) ? "?" : result.Username.Substring(0, 1).ToUpperInvariant()),
			HorizontalAlignment = HorizontalAlignment.Center,
			VerticalAlignment = VerticalAlignment.Center,
			FontSize = 18.0,
			FontWeight = FontWeights.SemiBold
		});
		Ellipse ellipse = new Ellipse();
		ellipse.Width = 9.0;
		ellipse.Height = 9.0;
		ellipse.Fill = Brush(result.Online ? "#36D873" : "#71808E");
		ellipse.Stroke = Brush("#18222C");
		ellipse.StrokeThickness = 2.0;
		ellipse.HorizontalAlignment = HorizontalAlignment.Right;
		ellipse.VerticalAlignment = VerticalAlignment.Bottom;
		Ellipse element = ellipse;
		grid3.Children.Add(element);
		grid.Children.Add(grid3);
		StackPanel stackPanel = new StackPanel();
		stackPanel.VerticalAlignment = VerticalAlignment.Center;
		StackPanel stackPanel2 = stackPanel;
		Grid.SetColumn(stackPanel2, 1);
		stackPanel2.Children.Add(new TextBlock
		{
			Text = result.Username,
			FontSize = 16.0,
			FontWeight = FontWeights.SemiBold,
			TextTrimming = TextTrimming.CharacterEllipsis
		});
		StackPanel stackPanel3 = new StackPanel();
		stackPanel3.Orientation = Orientation.Horizontal;
		stackPanel3.Margin = new Thickness(0.0, 3.0, 0.0, 0.0);
		StackPanel stackPanel4 = stackPanel3;
		stackPanel4.Children.Add(new TextBlock
		{
			Text = "@" + result.Username,
			Foreground = Brush("#AAB7C4"),
			FontSize = 12.0
		});
		stackPanel4.Children.Add(new TextBlock
		{
			Text = (result.Online ? "  ● Online" : "  ○ Offline"),
			Foreground = Brush(result.Online ? "#36D873" : "#9AA7B4"),
			FontSize = 12.0
		});
		stackPanel2.Children.Add(stackPanel4);
		grid.Children.Add(stackPanel2);
		string requestState;
		friendRequestStates.TryGetValue(result.AccountId, out requestState);
		string text = (string.IsNullOrEmpty(requestState) ? "Add Friend" : requestState);
		bool flag = text == "Add Friend" || text == "Incoming Request" || text == "Friends";
		if (text == "Friends")
		{
			text = "Message";
		}
		Button button = new Button();
		button.Content = text;
		button.Background = Brush(flag ? "#244C79" : "#202B35");
		button.BorderBrush = Brush(flag ? "#3A6E9F" : "#3A4650");
		button.Foreground = Brush(flag ? "#EDF1F5" : "#AAB7C4");
		button.Padding = new Thickness(12.0, 8.0, 12.0, 8.0);
		button.HorizontalContentAlignment = HorizontalAlignment.Center;
		button.VerticalAlignment = VerticalAlignment.Center;
		button.Margin = new Thickness(10.0, 0.0, 0.0, 0.0);
		button.Tag = result;
		button.IsEnabled = flag;
		Button button2 = button;
		button2.Click += delegate(object s, RoutedEventArgs e)
		{
			DiscoverySearchResult discoverySearchResult = (DiscoverySearchResult)((Button)s).Tag;
			if (requestState == "Incoming Request")
			{
				if (Confirm("Friend request", discoverySearchResult.Username + " wants to be friends. Accept this request?", "Accept"))
				{
					RespondToFriendRequest(discoverySearchResult, true);
				}
				else
				{
					RespondToFriendRequest(discoverySearchResult, false);
				}
			}
			else if (requestState == "Friends")
			{
				OpenFriendConversation(discoverySearchResult);
			}
			else
			{
				SendFriendRequest(discoverySearchResult);
			}
		};
		Grid.SetColumn(button2, 2);
		grid.Children.Add(button2);
		return border2;
	}

	private void ShowDiscoveryProfile(DiscoverySearchResult result)
	{
		Notify(result.Username + "\n@" + result.Username + " · " + (result.Online ? "Online" : "Offline") + "\n\nThis is public discovery information. Use Add Friend to send a server-backed request.");
	}

	private void FindFriend()
	{
		if (discovery == null || string.IsNullOrEmpty(discovery.SessionToken))
		{
			Notify("Create an account or sign in before searching.");
			return;
		}
		Window dialog = new Window
		{
			Title = "Find Friends",
			Owner = window,
			Width = 610.0,
			Height = 620.0,
			MinWidth = 520.0,
			MinHeight = 460.0,
			WindowStartupLocation = WindowStartupLocation.CenterOwner,
			Background = Brush("#0C1116"),
			Foreground = Brush("#EDF1F5"),
			FontFamily = new FontFamily("Segoe UI"),
			FontSize = 15.0,
			Resources = window.Resources
		};
		Border border = new Border();
		border.Background = Brush("#101820");
		border.BorderBrush = Brush("#293844");
		border.BorderThickness = new Thickness(1.0);
		border.CornerRadius = new CornerRadius(14.0);
		border.Margin = new Thickness(12.0);
		border.Padding = new Thickness(24.0);
		Border border2 = border;
		dialog.Content = border2;
		Grid grid = new Grid();
		grid.RowDefinitions.Add(new RowDefinition
		{
			Height = GridLength.Auto
		});
		grid.RowDefinitions.Add(new RowDefinition
		{
			Height = GridLength.Auto
		});
		grid.RowDefinitions.Add(new RowDefinition
		{
			Height = GridLength.Auto
		});
		grid.RowDefinitions.Add(new RowDefinition
		{
			Height = new GridLength(1.0, GridUnitType.Star)
		});
		border2.Child = grid;
		StackPanel stackPanel = new StackPanel();
		stackPanel.Children.Add(new TextBlock
		{
			Text = "Find Friends",
			FontSize = 25.0,
			FontWeight = FontWeights.SemiBold
		});
		stackPanel.Children.Add(new TextBlock
		{
			Text = "Find people by their public username or friend code.",
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(0.0, 5.0, 0.0, 18.0)
		});
		grid.Children.Add(stackPanel);
		Border border3 = new Border();
		border3.Background = Brush("#17212A");
		border3.BorderBrush = Brush("#354553");
		border3.BorderThickness = new Thickness(1.0);
		border3.CornerRadius = new CornerRadius(11.0);
		border3.Padding = new Thickness(13.0, 2.0, 13.0, 2.0);
		Border border4 = border3;
		Grid grid2 = new Grid();
		grid2.ColumnDefinitions.Add(new ColumnDefinition
		{
			Width = GridLength.Auto
		});
		grid2.ColumnDefinitions.Add(new ColumnDefinition());
		grid2.Children.Add(new TextBlock
		{
			Text = "⌕",
			FontFamily = new FontFamily("Segoe MDL2 Assets"),
			FontSize = 17.0,
			Foreground = Brush("#AAB7C4"),
			VerticalAlignment = VerticalAlignment.Center,
			Margin = new Thickness(0.0, 0.0, 10.0, 0.0)
		});
		TextBox query = new TextBox
		{
			Background = Brush("#17212A"),
			BorderThickness = new Thickness(0.0),
			Padding = new Thickness(0.0, 11.0, 0.0, 11.0),
			ToolTip = "Search by username or friend code"
		};
		Grid.SetColumn(query, 1);
		grid2.Children.Add(query);
		border4.Child = grid2;
		Grid.SetRow(border4, 1);
		grid.Children.Add(border4);
		Func<string> summary = () => "Pending outgoing: " + friendState.Outgoing.Count + "   Incoming requests: " + friendState.Incoming.Count + "   Friends: " + friendState.Friends.Count;
		TextBlock state = new TextBlock
		{
			Text = "Start typing to find people on PeerChat\n\n" + summary(),
			Foreground = Brush("#A1AEBB"),
			Margin = new Thickness(2.0, 13.0, 2.0, 10.0),
			FontSize = 13.0
		};
		Grid.SetRow(state, 2);
		grid.Children.Add(state);
		ObservableCollection<DiscoverySearchResult> collection = new ObservableCollection<DiscoverySearchResult>();
		ScrollViewer resultsScroll = new ScrollViewer
		{
			Content = new StackPanel(),
			VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
			HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled
		};
		Grid.SetRow(resultsScroll, 3);
		grid.Children.Add(resultsScroll);
		int generation = 0;
		DispatcherTimer debounce = new DispatcherTimer
		{
			Interval = TimeSpan.FromMilliseconds(300.0)
		};
		Action clear = delegate
		{
			collection.Clear();
			state.Text = "Start typing to find people on PeerChat\n\n" + summary();
		};
		Action rebuildCards = delegate
		{
			StackPanel stackPanel2 = new StackPanel();
			foreach (DiscoverySearchResult item in collection)
			{
				stackPanel2.Children.Add(DiscoveryCard(item));
			}
			resultsScroll.Content = stackPanel2;
			state.Text = ((collection.Count == 0) ? "No users found" : (collection.Count + " public profile" + ((collection.Count == 1) ? "" : "s") + " found"));
		};
		debounce.Tick += async delegate
		{
			debounce.Stop();
			int request = ++generation;
			string prefix = query.Text.Trim();
			if (prefix.Length == 0)
			{
				clear();
				resultsScroll.Content = new StackPanel();
				return;
			}
			state.Text = "Searching…";
			try
			{
				List<DiscoverySearchResult> found = await Task.Run(() => discovery.Suggest(prefix));
				dialog.Dispatcher.BeginInvoke((Action)delegate
				{
					if (request == generation && query.Text.Trim() == prefix)
					{
						collection.Clear();
						foreach (DiscoverySearchResult item2 in found)
						{
							collection.Add(item2);
						}
						rebuildCards();
					}
				});
			}
			catch (Exception)
			{
				dialog.Dispatcher.BeginInvoke((Action)delegate
				{
					if (request == generation)
					{
						collection.Clear();
						resultsScroll.Content = new StackPanel();
						state.Text = "Unable to search — server unavailable";
					}
				});
			}
		};
		query.TextChanged += delegate
		{
			generation++;
			debounce.Stop();
			if (query.Text.Trim().Length == 0)
			{
				clear();
				resultsScroll.Content = new StackPanel();
			}
			else
			{
				debounce.Start();
			}
		};
		dialog.Loaded += delegate
		{
			query.Focus();
		};
		TraceUi("Find Friends dialog constructed; showing");
		dialog.ShowDialog();
	}

	private void AddFriend()
	{
		string[] array = Dialog("Add Friend", new string[1] { "Paste a PeerChat ID" }, new string[1] { "" }, "Save and connect", "Your friend can send you their ID using Copy My ID. Offline friends stay in your contacts.");
		if (array != null)
		{
			string text = core.Add(array[0].Trim(), model.OwnId);
			if (text.Length > 0)
			{
				Notify(text);
				return;
			}
			model.Status = "Contact requested. Connecting…";
			Refresh();
		}
	}

	private void Manual()
	{
		Manual(null);
	}

	private void Manual(Window owner)
	{
		string[] array = Dialog("Manual Connect", new string[2] { "IP address or hostname", "Listening port" }, new string[2] { "", "7777" }, "Connect", "Use this for LAN, VPN, or forwarded-port troubleshooting.", false, owner);
		if (array != null)
		{
			ushort result;
			if (!ushort.TryParse(array[1], out result) || result == 0 || string.IsNullOrWhiteSpace(array[0]))
			{
				Notify("Enter an address and a port from 1 to 65535.", owner);
				return;
			}
			string host = array[0].Trim();
			core.Connect(host, result, "", "Manual entry", "manual direct", true);
			model.Status = "Connecting to " + array[0] + "…";
			Refresh();
		}
	}

	private async void Profile()
	{
		string[] values = Dialog("My Profile", new string[2] { "Display name", "Listening port" }, new string[2]
		{
			model.Name,
			model.Port.ToString()
		}, "Save profile", "Saving restarts your listener. Your persistent identity and encrypted history stay the same.");
		if (values == null)
		{
			return;
		}
		ushort port;
		if (string.IsNullOrWhiteSpace(values[0]) || values[0].Length > 64 || !ushort.TryParse(values[1], out port) || port == 0)
		{
			Notify("Use a name up to 64 characters and a port from 1 to 65535.");
			return;
		}
		foreach (ConversationModel value in model.Conversations.Values)
		{
			value.Online = false;
			value.Refresh();
		}
		await Start(port, values[0].Trim());
	}

	private async void Reconnect()
	{
		ConversationModel selected = model.Selected;
		if (selected == null || !reconnectRepairs.Add(selected.ConversationId))
		{
			return;
		}
		DiscoveryClient source = discovery;
		try
		{
			string refreshResult = "relay reconnect requested";
			if (source != null && selected.AccountId > 0)
			{
				// Reconnect follows the manager-owned Server Relay policy. It never republishes
				// presence, consults saved endpoints, creates a listener, or starts P2P consent.
				bool refreshed = await RefreshFriendsAsync();
				if (discovery != source || closing || closed)
				{
					return;
				}
				refreshResult = (refreshed ? "authoritative friend snapshot refreshed; relay reconnect requested" : "relay discovery refresh unavailable");
				if (refreshed && !selected.IsFriend)
				{
					RecordConnection("repair", selected, "Account is no longer an established friend");
					Notify("This account is no longer in your friends. Local history is retained.");
					return;
				}
			}
			connectionAttempts.Update(null, selected).RefreshResult = refreshResult;
			DiscoverySearchResult current;
			if (selected.AccountId > 0 && friendDirectory.TryGetValue(selected.AccountId, out current))
			{
				RecordConnection("repair", selected, refreshResult + "; account=" + selected.AccountId + "; presence=" + current.Online + "; candidateSeenUtc=" + current.CandidateSeenUtc + "; source=" + current.PublicCandidateSource);
				OpenFriendConversation(current, true, "manual retry");
				return;
			}
			RecordConnection("repair", selected, refreshResult+"; no direct route was attempted");
			Notify("Server Relay is unavailable. No direct route was attempted; use Try P2P Connection only if you want to request a scoped direct probe.");
		}
		finally
		{
			reconnectRepairs.Remove(selected.ConversationId);
		}
	}



    async void StartVoiceCall(){if(model.Groups||model.Selected==null||model.Selected.Contact==null||!model.Selected.Contact.verified){Notify("Choose a verified direct-message friend before calling.");return;}if(!model.Selected.Online){Notify("This friend is currently unavailable for call control. Messaging history remains available.");return;}if(model.VoiceStateCode!=0){Notify("Only one voice call can be active in this profile.");return;}string peer=model.Selected.Contact.peer;if(!await EnsureMediaIceConfigurationAsync()){Notify("Voice media relay is unavailable. Messaging remains connected.");return;}if(model.Selected==null||model.Selected.Contact==null||model.Selected.Contact.peer!=peer||model.VoiceStateCode!=0)return;string call=core.StartVoiceCall(peer);if(String.IsNullOrEmpty(call))Notify("Call control is unavailable for this peer. Messaging may still remain connected.");}
    void BuildScreenShareControls(){
        var actions=Find<Button>("VoiceCall").Parent as StackPanel;
        screenStartButton=new Button{Content="Share Screen",Padding=new Thickness(11,8,11,8),Margin=new Thickness(6,0,0,0),ToolTip="Share a monitor or window with this verified friend"};
        screenStartButton.Click+=(sender,args)=>StartScreenShare();
        if(actions!=null){int callIndex=actions.Children.IndexOf(Find<Button>("VoiceCall"));actions.Children.Insert(callIndex<0?actions.Children.Count:callIndex,screenStartButton);}
        screenStatus=new TextBlock{Text="Starting screen share",VerticalAlignment=VerticalAlignment.Center,FontWeight=FontWeights.SemiBold};
        screenEndButton=new Button{Content="Stop Sharing",Padding=new Thickness(10,6,10,6),Margin=new Thickness(12,0,0,0),Background=Brush("#7A2631")};
        screenEndButton.Click+=(sender,args)=>{if(!String.IsNullOrEmpty(activeScreenId))core.EndScreenShare(activeScreenId);};
        screenDiagnosticsButton=new Button{Content="Copy Diagnostics",Padding=new Thickness(10,6,10,6),Margin=new Thickness(12,0,0,0)};
        screenDiagnosticsButton.Click+=(sender,args)=>CopyScreenDiagnostics();
        screenSummaryText=new TextBlock{Foreground=Brush("#A1AEBB"),FontSize=11,MaxWidth=440,TextTrimming=TextTrimming.CharacterEllipsis};
        var statusColumn=new StackPanel{VerticalAlignment=VerticalAlignment.Center};statusColumn.Children.Add(screenStatus);statusColumn.Children.Add(screenSummaryText);
        var row=new Grid{HorizontalAlignment=HorizontalAlignment.Stretch};row.ColumnDefinitions.Add(new ColumnDefinition());row.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});Grid.SetColumn(statusColumn,0);row.Children.Add(statusColumn);var rowActions=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right,VerticalAlignment=VerticalAlignment.Center};rowActions.Children.Add(screenDiagnosticsButton);rowActions.Children.Add(screenEndButton);Grid.SetColumn(rowActions,1);row.Children.Add(rowActions);
        screenStrip=new Border{BorderBrush=Brush("#2B3D4C"),BorderThickness=new Thickness(0,1,0,0),Margin=new Thickness(0,10,0,0),Padding=new Thickness(0,9,0,0),Child=row,Visibility=Visibility.Collapsed};
        var header=Find<Border>("VoiceCard").Parent as StackPanel;if(header!=null)header.Children.Add(screenStrip);
        screenConnectionPanel=new StackPanel{Visibility=Visibility.Collapsed};
        screenConnectionPanel.Children.Add(new Border{Height=1,Background=Brush("#28343F"),Margin=new Thickness(0,14,0,12)});
        screenConnectionPanel.Children.Add(new TextBlock{Text="Screen Media",FontSize=12,FontWeight=FontWeights.SemiBold});
        screenMethodText=new TextBlock{Foreground=Brush("#A1AEBB"),FontSize=12,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,4,0,0)};
        screenQualityText=new TextBlock{Foreground=Brush("#A1AEBB"),FontSize=12,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,3,0,0)};
        screenControlText=new TextBlock{Foreground=Brush("#A1AEBB"),FontSize=12,TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,3,0,0)};
        screenConnectionPanel.Children.Add(screenMethodText);screenConnectionPanel.Children.Add(new TextBlock{Text="Security: DTLS-SRTP",Foreground=Brush("#A1AEBB"),FontSize=12});screenConnectionPanel.Children.Add(screenQualityText);screenConnectionPanel.Children.Add(screenControlText);
        screenDetailsText=new TextBlock{Foreground=Brush("#A1AEBB"),FontSize=11,TextWrapping=TextWrapping.Wrap,FontFamily=new FontFamily("Consolas")};
        var detailsScroll=new ScrollViewer{Content=screenDetailsText,MaxHeight=260,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Margin=new Thickness(0,6,0,0)};
        screenConnectionPanel.Children.Add(new Expander{Header="Advanced screen statistics",Foreground=Brush("#EDF1F5"),Background=Brush("#101820"),Content=detailsScroll,Margin=new Thickness(0,8,0,0)});
        var context=Find<StackPanel>("ConnectionPanelContent");context.Children.Insert(context.Children.IndexOf(Find<TextBlock>("MediaConnectionState"))+1,screenConnectionPanel);
    }
    async void StartScreenShare(){
        if(model.Groups||model.Selected==null||model.Selected.Contact==null||!model.Selected.Contact.verified||!model.Selected.Online){Notify("Choose a connected, verified direct-message friend before sharing.");return;}
        if(activeScreenState!=0){Notify("Only one screen share can be active in this profile.");return;}
        string peer=model.Selected.Contact.peer;
        ScreenSource[] sources;
        try{
            bool available=await Task.Run(()=>core.ScreenH264Available());
            if(!available){Notify("This PC does not have the required Windows capture and H.264 path.");return;}
            sources=await Task.Run(()=>core.ScreenSources());
        }catch(Exception error){Notify("Screen sharing is unavailable: "+error.Message);return;}
        if(sources.Length==0){Notify("No available monitor or window was found.");return;}
        var dialog=new Window{Title="Share your screen",Owner=window,Width=480,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),FontFamily=new FontFamily("Segoe UI"),Resources=window.Resources,ShowInTaskbar=false};
        var panel=new StackPanel{Margin=new Thickness(24)};dialog.Content=panel;
        panel.Children.Add(new TextBlock{Text="Choose what to share",FontSize=22,FontWeight=FontWeights.SemiBold});
        var sourceList=new ComboBox{ItemsSource=sources,SelectedIndex=0,Margin=new Thickness(0,16,0,10)};panel.Children.Add(sourceList);
        var cursor=new CheckBox{Content="Show cursor",IsChecked=screenPreferences.CaptureCursor,Margin=new Thickness(0,0,0,12)};panel.Children.Add(cursor);
        panel.Children.Add(new TextBlock{Text="Video travels over a separate encrypted media relay connection. Your friend must accept before viewing.",Foreground=Brush("#A1AEBB"),FontSize=12,Margin=new Thickness(0,0,0,18)});
        var buttons=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right};
        var cancel=new Button{Content="Cancel",Margin=new Thickness(0,0,8,0)};cancel.Click+=(sender,args)=>{dialog.DialogResult=false;dialog.Close();};
        var share=new Button{Content="Share Screen",Background=Brush("#275AA7")};share.Click+=(sender,args)=>{dialog.DialogResult=true;dialog.Close();};
        buttons.Children.Add(cancel);buttons.Children.Add(share);panel.Children.Add(buttons);
        if(dialog.ShowDialog()!=true)return;
        var chosen=sourceList.SelectedItem as ScreenSource;if(chosen==null)return;
        if(activeScreenState!=0||model.Groups||model.Selected==null||model.Selected.Contact==null||model.Selected.Contact.peer!=peer||!model.Selected.Online){Notify("That conversation is no longer ready for sharing.");return;}
        if(!await EnsureMediaIceConfigurationAsync()){Notify("Screen-share media relay is unavailable. Messaging remains connected.");return;}
        if(activeScreenState!=0||model.Selected==null||model.Selected.Contact==null||model.Selected.Contact.peer!=peer)return;
        string id=core.StartScreenShare(peer,chosen.kind,chosen.handle,(uint)screenPreferences.FramesPerSecond,(uint)screenPreferences.BitrateBitsPerSecond,cursor.IsChecked==true);
        if(String.IsNullOrEmpty(id))Notify("Could not queue screen sharing for this peer.");
    }
    void ShowIncomingScreenShare(){
        if(incomingScreenWindow!=null||activeScreenState!=2)return;
        string name=model.FindByPeer(activeScreenPeer)==null?"Verified friend":model.FindByPeer(activeScreenPeer).Name;
        var dialog=new Window{Title="Incoming screen share",Owner=window,Width=430,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),FontFamily=new FontFamily("Segoe UI"),Resources=window.Resources,ShowInTaskbar=false};
        var panel=new StackPanel{Margin=new Thickness(24)};dialog.Content=panel;
        panel.Children.Add(new TextBlock{Text="Incoming screen share",FontSize=22,FontWeight=FontWeights.SemiBold});
        panel.Children.Add(new TextBlock{Text=name+" · verified PeerChat identity",Foreground=Brush("#A1AEBB"),Margin=new Thickness(0,8,0,18)});
        var buttons=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right};
        var decline=new Button{Content="Decline",Margin=new Thickness(0,0,8,0)};decline.Click+=(sender,args)=>{core.DeclineScreenShare(activeScreenId);dialog.Close();};
        var accept=new Button{Content="View Screen",Background=Brush("#275AA7")};accept.Click+=(sender,args)=>{dialog.Close();OpenScreenViewer();};
        buttons.Children.Add(decline);buttons.Children.Add(accept);panel.Children.Add(buttons);
        dialog.Closed+=(sender,args)=>{if(incomingScreenWindow==dialog)incomingScreenWindow=null;};
        incomingScreenWindow=dialog;dialog.Show();dialog.Activate();
    }
    void OpenScreenViewer(){
        if(screenViewerWindow!=null||activeScreenState!=2)return;
        var viewer=new Window{Title="PeerChat Screen Share",Owner=window,Width=1060,Height=700,MinWidth=480,MinHeight=320,WindowStartupLocation=WindowStartupLocation.CenterOwner,Background=Brush("#050709"),Foreground=Brush("#EDF1F5"),ShowInTaskbar=true};
        viewer.Content=new Grid{Background=Brush("#050709")};
        viewer.SourceInitialized+=(sender,args)=>{
            var handle=new WindowInteropHelper(viewer).Handle;NativeRect bounds;
            if(GetClientRect(handle,out bounds))screenViewerChild=CreateWindowEx(0,"STATIC","",0x50000000,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,handle,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero);
        };
        viewer.SizeChanged+=(sender,args)=>{NativeRect bounds;var handle=new WindowInteropHelper(viewer).Handle;if(screenViewerChild!=IntPtr.Zero&&GetClientRect(handle,out bounds))MoveWindow(screenViewerChild,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,true);};
        viewer.Closing+=(sender,args)=>{if(!closing&&!closingScreenViewer&&activeScreenState!=0&&!String.IsNullOrEmpty(activeScreenId)&&core.EndScreenShare(activeScreenId))args.Cancel=true;};
        viewer.Closed+=(sender,args)=>{if(screenViewerWindow==viewer)screenViewerWindow=null;screenViewerChild=IntPtr.Zero;};
        screenViewerWindow=viewer;viewer.Show();
        AnswerIncomingScreenShare(viewer);
    }
    async void AnswerIncomingScreenShare(Window viewer){
        string id=activeScreenId;
        if(screenViewerChild==IntPtr.Zero||!await EnsureMediaIceConfigurationAsync()||id!=activeScreenId||activeScreenState!=2||!core.AnswerScreenShare(id,(ulong)screenViewerChild.ToInt64())){Notify("Could not open the secure screen viewer.");if(!String.IsNullOrEmpty(id))core.DeclineScreenShare(id);closingScreenViewer=true;viewer.Close();closingScreenViewer=false;}
    }
    void UpdateScreenViewerTitle(CoreEvent e){
        if(screenViewerWindow==null)return;
        if(e.screenMediaFailed){
            screenViewerWindow.Title=e.screenFailureDomain=="local_encoder"?"Screen Share — Encoder Error":
                e.screenFailureDomain=="local_capture"?"Screen Share — Capture Error":
                e.screenFailureDomain=="remote_decoder"?"Screen Share — Decoder Error":
                e.screenFailureDomain=="presentation"?"Screen Share — Presentation Error":"Screen Share — Error";
            return;
        }
        if(e.shareState==0){screenViewerWindow.Title="PeerChat Screen Share";return;}
        if(e.shareState!=4){screenViewerWindow.Title="Screen Share — Connecting…";return;}
        string route=e.screenRoute==3?"TURN":e.screenRoute==1||e.screenRoute==2?"Direct":"Connecting…";
        if(e.screenWidth<=0||e.screenHeight<=0||e.screenFps<=0){screenViewerWindow.Title="Screen Share — "+route+" | H.264";return;}
        string title="Screen Share — "+route+" | H.264 | "+e.screenWidth+"×"+e.screenHeight+" | "+e.screenFps+" FPS";
        if(e.screenBitrateMbps>0.05)title+=" | "+e.screenBitrateMbps.ToString("0.0",System.Globalization.CultureInfo.InvariantCulture)+" Mbps";
        screenViewerWindow.Title=title;
    }
    void HandleScreenShareEvent(CoreEvent e){
        if(e.shareLifetime<activeScreenLifetime||(e.shareLifetime==activeScreenLifetime&&e.shareGeneration<activeScreenGeneration))return;
        activeScreenLifetime=e.shareLifetime;activeScreenGeneration=e.shareGeneration;activeScreenState=e.shareState;
        activeScreenId=e.detail??"";activeScreenPeer=e.peer??"";
        if(!String.IsNullOrWhiteSpace(e.screenDetails))lastScreenDiagnosticText=e.screenDetails;
        string diagnosticStage="share_state="+e.shareState+"; "+ExtractDiagnosticValue(e.screenDetails,"screen_start_stage_name",e.shareState==5?"recovering":e.shareState==4?"connected":"starting");
        if(e.shareState!=0&&diagnosticsRetention!=null&&!String.IsNullOrEmpty(e.detail)){
            activeScreenDiagnosticId=e.detail;activeScreenDiagnosticStage=diagnosticStage;
            try{diagnosticsRetention.UpdateOperation(new ActiveDiagnosticOperation{OperationType="ScreenShare",OperationId=e.detail,Stage=diagnosticStage,Generation=e.shareGeneration,Module="PeerChat.ScreenMedia.dll",Diagnostics=diagnosticsService.Capture()});}catch(Exception error){TraceUi("screen diagnostic checkpoint unavailable: "+error.GetType().Name);}
        }
        if(screenStrip!=null){screenStrip.Visibility=activeScreenState==0?Visibility.Collapsed:Visibility.Visible;screenStatus.Text=e.text??"Screen sharing";screenSummaryText.Text=e.screenSummary??"";}
        if(screenConnectionPanel!=null){
            screenConnectionPanel.Visibility=activeScreenState==0?Visibility.Collapsed:Visibility.Visible;
            screenMethodText.Text="Connection: "+(activeScreenState==5?"Recovering":e.screenRoute==3?"TURN media relay":e.screenRoute==2?"Direct NAT-traversed ICE":e.screenRoute==1?"Direct host ICE":"Connecting / Unavailable");
            screenQualityText.Text="Video: H.264 · "+(e.screenWidth>0&&e.screenHeight>0?e.screenWidth+" × "+e.screenHeight:"waiting for frames")+" · generation "+e.shareGeneration;
            screenControlText.Text="Verified control path: "+(e.callControlAvailable?"Available":"Unavailable");
            string screenPeerLabel=DiagnosticPeerDisplayName(activeScreenPeer);screenDetailsText.Text=(String.IsNullOrEmpty(screenPeerLabel)?"":"logical_peer="+screenPeerLabel+"\n")+(e.screenDetails??"Waiting for media statistics.");
        }
        if(activeScreenState==2)ShowIncomingScreenShare();
        UpdateScreenViewerTitle(e);
        if(activeScreenState==0){
            if(e.screenMediaFailed&&!String.IsNullOrEmpty(e.screenFailureDomain)&&e.screenFailureDomain!="none")RetainFailure("Screen share stopped — "+ScreenFailureLabel(e.screenFailureDomain),"ScreenShare",e.detail,e.screenFailureDomain,e.screenFailureReason,ExtractDiagnosticValue(e.screenDetails,"screen_start_stage_name",diagnosticStage),e.shareGeneration,e.screenDetails);
            if(diagnosticsRetention!=null&&!String.IsNullOrEmpty(e.detail))try{diagnosticsRetention.CompleteOperation("ScreenShare",e.detail);}catch{}activeScreenDiagnosticId="";activeScreenDiagnosticStage="";
            if(incomingScreenWindow!=null){incomingScreenWindow.Close();incomingScreenWindow=null;}
            if(screenViewerWindow!=null){closingScreenViewer=true;screenViewerWindow.Close();closingScreenViewer=false;}
            activeScreenId="";activeScreenPeer="";
        }else if(e.screenMediaFailed&&!String.IsNullOrEmpty(e.screenFailureDomain)&&e.screenFailureDomain!="none"){
            RetainFailure("Screen share recovering — "+ScreenFailureLabel(e.screenFailureDomain),"ScreenShare",e.detail,e.screenFailureDomain,e.screenFailureReason,diagnosticStage,e.shareGeneration,e.screenDetails);
        }
        Refresh();
    }
    static string ExtractDiagnosticValue(string text,string key,string fallback){if(String.IsNullOrEmpty(text))return fallback;foreach(string line in text.Replace("\r","").Split('\n')){int split=line.IndexOf('=');if(split>0&&line.Substring(0,split).Trim()==key)return line.Substring(split+1).Trim();}return fallback;}
    static string ScreenFailureLabel(string domain){switch(domain){case "local_encoder":return "Encoder Error";case "local_capture":return "Capture Error";case "ice_turn_transport":return "Connection Lost";case "dtls_security":return "Security Error";case "rtp":return "Media Transport Error";case "remote_decoder":return "Decoder Error";case "presentation":return "Presentation Error";default:return "Error";}}
    void UpdateDiagnosticOperation(string type,string id,string stage,long generation,DiagnosticsSnapshot snapshot){if(diagnosticsRetention==null||String.IsNullOrEmpty(id))return;try{diagnosticsRetention.UpdateOperation(new ActiveDiagnosticOperation{OperationType=type,OperationId=id,Stage=stage,Generation=generation,Module=type=="ScreenShare"?"PeerChat.ScreenMedia.dll":"PeerChat.Core.dll",Diagnostics=snapshot});}catch(Exception error){TraceUi("diagnostic operation marker update failed: "+error.GetType().Name);}}
    void CompleteDiagnosticOperation(string type,string id){if(diagnosticsRetention==null||String.IsNullOrEmpty(id))return;try{diagnosticsRetention.CompleteOperation(type,id);}catch(Exception error){TraceUi("diagnostic operation marker completion failed: "+error.GetType().Name);}}
    void RetainFailure(string title,string operation,string operationId,string domain,string reason,string stage,long generation,string subsystemDetails){
        string dedupe=(operation??"")+"|"+(operationId??"")+"|"+(domain??"")+"|"+generation;
        if(!surfacedFailureKeys.Add(dedupe))return;if(surfacedFailureKeys.Count>128)surfacedFailureKeys.Clear();
        if(!String.IsNullOrWhiteSpace(subsystemDetails)){if(operation=="ScreenShare")lastScreenDiagnosticText=subsystemDetails;}
        try{var snapshot=diagnosticsService.Capture();var failure=diagnosticsService.FreezeFailure(title,operation,operationId,domain,reason,stage,generation,snapshot);failure.Module=operation=="ScreenShare"?"PeerChat.ScreenMedia.dll":"PeerChat.Core.dll";if(diagnosticsRetention!=null)diagnosticsRetention.AddFailure(failure);ShowFailureNotification(failure);}catch(Exception error){TraceUi("failure diagnostic retention failed: "+error.GetType().Name);}
    }
    void ShowFailureNotification(FailureSnapshot failure){
        if(failure==null||window==null||!window.IsLoaded)return;if(failureNotification!=null){try{failureNotification.Close();}catch{}failureNotification=null;}
        var dialog=new Window{Title="PeerChat diagnostics",Owner=window,Width=470,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),FontFamily=new FontFamily("Segoe UI"),Resources=window.Resources,ShowInTaskbar=false,ShowActivated=false,Topmost=true};var panel=new StackPanel{Margin=new Thickness(22)};dialog.Content=panel;
        panel.Children.Add(new TextBlock{Text=DiagnosticsPrivacyFilter.Redact(failure.Title),FontSize=20,FontWeight=FontWeights.SemiBold});panel.Children.Add(new TextBlock{Text=DiagnosticsPrivacyFilter.Redact(failure.Reason),Foreground=Brush("#A1AEBB"),TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,8,0,18)});
        var actions=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right};var view=new Button{Content="View Diagnostics",Background=Brush("#275AA7"),Margin=new Thickness(0,0,8,0)};var copy=new Button{Content="Copy Diagnostics",Margin=new Thickness(0,0,8,0)};var dismiss=new Button{Content="Dismiss"};view.Click+=(s,e)=>{dialog.Close();ShowDiagnosticsWindow(failure);};copy.Click+=(s,e)=>CopyDiagnosticText(DiagnosticsService.FormatFailureFull(failure,DiagnosticAccountDisplayName,DiagnosticPeerDisplayName),"Failure diagnostics copied.");dismiss.Click+=(s,e)=>dialog.Close();actions.Children.Add(view);actions.Children.Add(copy);actions.Children.Add(dismiss);panel.Children.Add(actions);dialog.Closed+=(s,e)=>{if(failureNotification==dialog)failureNotification=null;};failureNotification=dialog;dialog.Show();
    }
    sealed class DiagnosticsViewSelection { public DiagnosticsConnectionScope Scope; public string Component; public FailureSnapshot Failure; public bool Full; public bool Local; }
    string DiagnosticAccountDisplayName(string accountId){long id;if(!Int64.TryParse(accountId,out id)||id<=0)return "";DiscoverySearchResult known;if(friendDirectory.TryGetValue(id,out known)&&!String.IsNullOrWhiteSpace(known.Username))return known.Username;var binding=conversationIndex.Records.FirstOrDefault(value=>value.AccountId==id);return binding==null?"":binding.Username;}
    string DiagnosticPeerDisplayName(string peerId){var conversation=model.FindByPeer(peerId);if(conversation==null||conversation.AccountId<=0)return "";string name=conversation.AccountName;if(String.IsNullOrWhiteSpace(name))name=DiagnosticAccountDisplayName(conversation.AccountId.ToString());return String.IsNullOrWhiteSpace(name)?"":name+":"+conversation.AccountId;}
    void ShowDiagnosticsWindow(FailureSnapshot focus){
        var current=diagnosticsService.Capture();var failures=diagnosticsRetention==null?new List<FailureSnapshot>():diagnosticsRetention.Failures().ToList();
        var dialog=new Window{Title="Connection & Diagnostics",Owner=window,Width=1040,Height=700,MinWidth=780,MinHeight=520,WindowStartupLocation=WindowStartupLocation.CenterOwner,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),FontFamily=new FontFamily("Segoe UI"),Resources=window.Resources};
        var root=new DockPanel{Margin=new Thickness(18)};dialog.Content=root;var footer=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right,Margin=new Thickness(0,12,0,0)};DockPanel.SetDock(footer,Dock.Bottom);root.Children.Add(footer);
        var copySummary=new Button{Content="Copy Summary",Margin=new Thickness(0,0,8,0)};var copyFull=new Button{Content="Copy Full Diagnostics",Margin=new Thickness(0,0,8,0)};var save=new Button{Content="Save Diagnostics",Margin=new Thickness(0,0,8,0)};var edit=new Button{Content="Shareable Endpoint",Margin=new Thickness(0,0,8,0)};var close=new Button{Content="Close",IsCancel=true};footer.Children.Add(copySummary);footer.Children.Add(copyFull);footer.Children.Add(save);footer.Children.Add(edit);footer.Children.Add(close);close.Click+=(s,e)=>dialog.Close();edit.Click+=(s,e)=>EditShareableEndpoint();
        var columns=new Grid();columns.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(260)});columns.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(12)});columns.ColumnDefinitions.Add(new ColumnDefinition());root.Children.Add(columns);
        var entries=new ListBox{Background=Brush("#111920"),Foreground=Brush("#EDF1F5"),BorderBrush=Brush("#344351"),Padding=new Thickness(5)};Grid.SetColumn(entries,0);columns.Children.Add(entries);var splitter=new Border{Width=1,Background=Brush("#28343F")};Grid.SetColumn(splitter,1);columns.Children.Add(splitter);
        var detail=new TextBox{IsReadOnly=true,AcceptsReturn=true,TextWrapping=TextWrapping.NoWrap,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,FontFamily=new FontFamily("Consolas"),FontSize=12,Background=Brush("#0D141A"),Foreground=Brush("#EDF1F5"),BorderBrush=Brush("#344351"),Padding=new Thickness(12)};Grid.SetColumn(detail,2);columns.Children.Add(detail);
        Action<string> heading=text=>entries.Items.Add(new ListBoxItem{Content=text,IsEnabled=false,FontWeight=FontWeights.SemiBold,Foreground=Brush("#8FA8C4"),Padding=new Thickness(8,11,8,4)});
        heading("Current");var scopes=DiagnosticsService.ConnectionScopes(current,DiagnosticAccountDisplayName);ListBoxItem first=null;foreach(var scope in scopes){var parent=new ListBoxItem{Content=DiagnosticsPrivacyFilter.Redact(scope.Label),IsEnabled=false,FontWeight=FontWeights.SemiBold,Padding=new Thickness(8,8,8,3)};entries.Items.Add(parent);foreach(string component in new[]{"Overview","Reliable Connection","Voice","Screen Share"}){var item=new ListBoxItem{Content="    "+component,Tag=new DiagnosticsViewSelection{Scope=scope,Component=component}};entries.Items.Add(item);if(first==null)first=item;}}
        if(scopes.Count==0){var item=new ListBoxItem{Content="No logical peer sessions are available",IsEnabled=false,Padding=new Thickness(8)};entries.Items.Add(item);}
        heading("Recent Failures");foreach(var failure in failures){DateTime timestamp;var failureScopes=DiagnosticsService.ConnectionScopes(failure.Diagnostics,DiagnosticAccountDisplayName);string logicalLabel=failureScopes.Count==0?"":failureScopes[0].Label;string label=(DateTime.TryParse(failure.TimestampUtc,out timestamp)?timestamp.ToLocalTime().ToString("g")+"  ·  ":"")+failure.Title+(String.IsNullOrEmpty(logicalLabel)?"":"  ·  "+logicalLabel);entries.Items.Add(new ListBoxItem{Content=DiagnosticsPrivacyFilter.Redact(label),Tag=new DiagnosticsViewSelection{Failure=failure},Padding=new Thickness(8,4,8,4)});}
        heading("Local / Application");var localItem=new ListBoxItem{Content="My Connection",Tag=new DiagnosticsViewSelection{Local=true},Padding=new Thickness(8,4,8,4)};entries.Items.Add(localItem);var fullItem=new ListBoxItem{Content="Full Diagnostics / All Events",Tag=new DiagnosticsViewSelection{Full=true},Padding=new Thickness(8,4,8,4)};entries.Items.Add(fullItem);
        DiagnosticsViewSelection selected=null;Func<DiagnosticsSnapshot> projected=()=>{if(selected==null||selected.Full)return current;if(selected.Local)return new DiagnosticsSnapshot{SnapshotId=current.SnapshotId,TimestampUtc=current.TimestampUtc,Build=current.Build,Profile=current.Profile,Status=current.Status,Sections=(current.Sections??new List<DiagnosticsSection>()).Where(section=>section.Name=="Local"||section.Name=="Security").Select(section=>new DiagnosticsSection{Name=section.Name,Details=section.Details}).ToList()};return DiagnosticsService.ProjectConnection(current,selected.Scope,selected.Component);};Func<string> fullText=()=>selected!=null&&selected.Failure!=null?DiagnosticsService.FormatFailureFull(selected.Failure,DiagnosticAccountDisplayName,DiagnosticPeerDisplayName):DiagnosticsService.FormatFull(projected(),DiagnosticAccountDisplayName,DiagnosticPeerDisplayName);Func<string> summaryText=()=>selected!=null&&selected.Failure!=null?DiagnosticsService.FormatFailureSummary(selected.Failure,DiagnosticAccountDisplayName,DiagnosticPeerDisplayName):DiagnosticsService.FormatSummary(projected(),DiagnosticAccountDisplayName,DiagnosticPeerDisplayName);Action update=()=>{selected=(entries.SelectedItem as ListBoxItem)?.Tag as DiagnosticsViewSelection;if(selected==null){detail.Text="Select a logical peer/session, retained failure, local information, or Full Diagnostics.";copySummary.IsEnabled=false;copyFull.IsEnabled=false;save.IsEnabled=false;return;}detail.Text=fullText();copySummary.IsEnabled=true;copyFull.IsEnabled=true;save.IsEnabled=true;};
        entries.SelectionChanged+=(s,e)=>update();entries.SelectedItem=focus==null?(object)(first??fullItem):entries.Items.Cast<ListBoxItem>().FirstOrDefault(item=>{var value=item.Tag as DiagnosticsViewSelection;return value!=null&&value.Failure!=null&&value.Failure.Id==focus.Id;})??fullItem;
        copySummary.Click+=(s,e)=>CopyDiagnosticText(summaryText(),"Diagnostic summary copied.");copyFull.Click+=(s,e)=>CopyDiagnosticText(fullText(),"Full diagnostics copied.");
        save.Click+=(s,e)=>{var picker=new Microsoft.Win32.SaveFileDialog{Title="Save PeerChat diagnostics",FileName="PeerChat-diagnostics-"+DateTime.UtcNow.ToString("yyyyMMdd-HHmmss")+".txt",Filter="Text files (*.txt)|*.txt|All files (*.*)|*.*"};if(picker.ShowDialog(dialog)==true)try{File.WriteAllText(picker.FileName,DiagnosticsPrivacyFilter.Redact(fullText()),new UTF8Encoding(false));}catch(Exception error){Notify("Could not save diagnostics: "+error.Message,dialog);}};
        dialog.Show();
    }
    async void CopyScreenDiagnostics(){
        DiagnosticsSnapshot snapshot=diagnosticsService.Capture();await CopyDiagnosticText(DiagnosticsService.FormatFull(snapshot,DiagnosticAccountDisplayName,DiagnosticPeerDisplayName),"Screen-share diagnostics copied.");
    }
    bool loadingVoiceDevices,voiceDeviceHandlersBound;
    void LoadVoiceDevices(){LoadVoiceDevices(false);}
    void LoadVoiceDevices(bool preserveSelection){var input=Find<ComboBox>("VoiceInput");var output=Find<ComboBox>("VoiceOutput");var oldInput=preserveSelection?input.SelectedItem as VoiceDevice:null;var oldOutput=preserveSelection?output.SelectedItem as VoiceDevice:null;loadingVoiceDevices=true;var devices=core.VoiceDevices();VoicePreferenceStore.ResolveDevices(voicePreferences,devices);input.ItemsSource=devices.inputs;output.ItemsSource=devices.outputs;input.SelectedItem=oldInput==null?devices.inputs.FirstOrDefault(device=>device.id==voicePreferences.InputDeviceId&&device.name==voicePreferences.InputDeviceName):(devices.inputs.FirstOrDefault(device=>device.id==oldInput.id&&device.name==oldInput.name)??devices.inputs.FirstOrDefault(device=>device.name==oldInput.name));output.SelectedItem=oldOutput==null?devices.outputs.FirstOrDefault(device=>device.id==voicePreferences.OutputDeviceId&&device.name==voicePreferences.OutputDeviceName):(devices.outputs.FirstOrDefault(device=>device.id==oldOutput.id&&device.name==oldOutput.name)??devices.outputs.FirstOrDefault(device=>device.name==oldOutput.name));if(input.SelectedItem==null&&input.Items.Count>0)input.SelectedIndex=0;if(output.SelectedItem==null&&output.Items.Count>0)output.SelectedIndex=0;if(!voiceDeviceHandlersBound){input.SelectionChanged+=(s,e)=>SelectVoiceDevices();output.SelectionChanged+=(s,e)=>SelectVoiceDevices();voiceDeviceHandlersBound=true;}loadingVoiceDevices=false;SelectVoiceDevices();}
    void RefreshVoiceDevices(){if(model.VoiceStateCode!=0){Notify("End the active call before refreshing audio devices.");return;}LoadVoiceDevices(true);voiceAudioError="";model.Status="Audio device list refreshed.";RefreshVoiceUi();}
    void SelectVoiceDevices(){if(loadingVoiceDevices)return;var input=Find<ComboBox>("VoiceInput").SelectedItem as VoiceDevice;var output=Find<ComboBox>("VoiceOutput").SelectedItem as VoiceDevice;if(input!=null&&output!=null){core.SelectVoiceDevices(input.id,output.id);voicePreferences.InputDeviceId=input.id;voicePreferences.InputDeviceName=input.name;voicePreferences.OutputDeviceId=output.id;voicePreferences.OutputDeviceName=output.name;voicePreferenceStore.Save(voicePreferences);}}
    void EndVoiceCall(){if(!String.IsNullOrEmpty(model.VoiceCallId))core.EndVoiceCall(model.VoiceCallId);}
    bool voiceMuted,voiceDeafened;
    void ToggleVoiceMute(){if(!core.SetVoiceMuted(!model.VoiceMuted))Notify("Mute is unavailable until voice media is connected.");}
    void ToggleVoiceDeafen(){if(!core.SetVoiceDeafened(!model.VoiceDeafened))Notify("Deafen is unavailable until voice media is connected.");}
    void ShowVoiceDiagnostics(){var panel=Find<StackPanel>("ContextVoice");bool opening=panel.Visibility!=Visibility.Visible;panel.Visibility=opening?Visibility.Visible:Visibility.Collapsed;Find<Expander>("VoiceAdvancedExpander").IsExpanded=false;Find<TextBox>("VoiceDiagnosticsText").Text="Expand to load the bounded diagnostic snapshot.";}
    async void RefreshVoiceDiagnosticsText(){if(Find<StackPanel>("ContextVoice").Visibility!=Visibility.Visible||!Find<Expander>("VoiceAdvancedExpander").IsExpanded)return;string native=await Task.Run(()=>core.VoiceDiagnostics());bool active=model.VoiceStateCode!=0;string peer=active?model.VoicePeerId:recentVoicePeerId;string peerLabel=DiagnosticPeerDisplayName(peer);var input=Find<ComboBox>("VoiceInput").SelectedItem as VoiceDevice;var output=Find<ComboBox>("VoiceOutput").SelectedItem as VoiceDevice;var text="call_id="+(active?model.VoiceCallId:recentVoiceCallId)+"\ncall_lifetime="+(active?model.VoiceCallLifetime:0)+"\ncall_type=voice\nlogical_peer="+(String.IsNullOrEmpty(peerLabel)?"unavailable":peerLabel)+"\nexpected_peer_id="+(String.IsNullOrEmpty(peer)?"none":peer)+"\ncall_state="+(active?model.VoiceState:recentVoiceState)+"\nmedia_generation="+model.VoiceMediaGeneration+"\nmedia_state="+model.VoiceMediaState+"\ncontrol_available="+model.VoiceControlAvailable+"\nmedia_route="+VoiceConnectionMethod()+"\ninput_device="+(input==null?"none":input.name)+"\noutput_device="+(output==null?"none":output.name)+"\nmuted="+model.VoiceMuted+"\ndeafened="+model.VoiceDeafened+"\n"+(native??"media_state=unavailable");if(text.Length>8192)text=text.Substring(0,8192);if(Find<StackPanel>("ContextVoice").Visibility==Visibility.Visible&&Find<Expander>("VoiceAdvancedExpander").IsExpanded)Find<TextBox>("VoiceDiagnosticsText").Text=text;}
    void ShowIncomingVoiceCall(){if(incomingVoiceWindow!=null||model.VoiceStateCode!=2)return;var conversation=model.FindByPeer(model.VoicePeerId);string friend=conversation==null?"Verified friend":conversation.Name;var dialog=new Window{Title="Incoming Voice Call",Owner=window,Width=430,SizeToContent=SizeToContent.Height,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),FontFamily=new FontFamily("Segoe UI"),Resources=window.Resources,ShowInTaskbar=false};var panel=new StackPanel{Margin=new Thickness(26)};dialog.Content=panel;panel.Children.Add(new TextBlock{Text="Incoming voice call",FontSize=24,FontWeight=FontWeights.SemiBold});panel.Children.Add(new TextBlock{Text=friend+" · verified PeerChat identity",Foreground=Brush("#A1AEBB"),Margin=new Thickness(0,7,0,20)});var row=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right};var decline=new Button{Content="Decline",Margin=new Thickness(0,0,8,0)};var answer=new Button{Content="Answer",Background=Brush("#275AA7")};decline.Click+=(s,e)=>{core.DeclineVoiceCall(model.VoiceCallId);dialog.Close();};answer.Click+=(s,e)=>{AnswerIncomingVoiceCall(dialog);};row.Children.Add(decline);row.Children.Add(answer);panel.Children.Add(row);dialog.Closed+=(s,e)=>{if(incomingVoiceWindow==dialog)incomingVoiceWindow=null;};incomingVoiceWindow=dialog;try{MessageBeep(0xFFFFFFFF);}catch{}dialog.Show();dialog.Activate();}
    async void AnswerIncomingVoiceCall(Window dialog){string call=model.VoiceCallId;if(!await EnsureMediaIceConfigurationAsync()||call!=model.VoiceCallId||!core.AnswerVoiceCall(call)){Notify("Voice media relay is unavailable. Messaging remains connected.");core.DeclineVoiceCall(call);}dialog.Close();}
    void HandleVoiceEvent(CoreEvent e){string state=e.text??"Idle";int stateCode=e.callState;string name=(model.FindByPeer(e.peer)==null?"Verified friend":model.FindByPeer(e.peer).Name);if(!String.IsNullOrEmpty(e.detail))recentVoiceCallId=e.detail;if(!String.IsNullOrEmpty(e.peer))recentVoicePeerId=e.peer;if(stateCode!=0)recentVoiceState=state;voiceMuted=e.callMuted;voiceDeafened=e.callDeafened;if(stateCode==1||stateCode==2){voiceAudioError="";voiceStartedUtc=DateTime.UtcNow;voiceEndedUtc=DateTime.MinValue;voiceLastConnectedUtc=DateTime.MinValue;recentVoiceMuted=recentVoiceDeafened=false;}RecordConnection("voice",model.FindByPeer(e.peer),"call="+(e.detail??"")+"; lifetime="+e.callLifetime+"; media_generation="+e.mediaGeneration+"; media_state="+e.mediaState+"; control="+e.callControlAvailable+"; state="+state);if(stateCode!=0&&!String.IsNullOrEmpty(e.detail)){activeVoiceDiagnosticId=e.detail;activeVoiceDiagnosticStage=state+" · media="+e.mediaState;UpdateDiagnosticOperation("Voice",e.detail,activeVoiceDiagnosticStage,e.mediaGeneration,diagnosticsService.Capture());}if(stateCode==8)RetainFailure("Voice stopped — Media Error","Voice",e.detail,"voice_media",state,activeVoiceDiagnosticStage,e.mediaGeneration,null);if(stateCode==2)ShowIncomingVoiceCall();if(stateCode==5&&e.mediaState==2){if(voiceConnectedUtc==DateTime.MinValue)voiceConnectedUtc=DateTime.UtcNow;voiceLastConnectedUtc=voiceConnectedUtc;voiceTimer.Start();}if(stateCode==0){voiceTimer.Stop();voiceConnectedUtc=DateTime.MinValue;voiceEndedUtc=DateTime.UtcNow;voiceMuted=voiceDeafened=false;voiceAudioError="";if(!String.IsNullOrEmpty(activeVoiceDiagnosticId))CompleteDiagnosticOperation("Voice",activeVoiceDiagnosticId);activeVoiceDiagnosticId="";activeVoiceDiagnosticStage="";if(incomingVoiceWindow!=null){incomingVoiceWindow.Close();incomingVoiceWindow=null;}LoadVoiceDevices(true);}model.Status=stateCode==0?"Voice call ended · normal messaging ready":"Voice call with "+name+" · "+state+(String.IsNullOrEmpty(voiceAudioError)?"":" · Audio unavailable: "+voiceAudioError);RefreshVoiceUi();}
    void RefreshVoiceUi(){
        bool active=model.VoiceStateCode!=0,connected=model.VoiceStateCode==5&&model.VoiceMediaState==2;
        Find<TextBlock>("MediaConnectionMethod").Text="Connection: "+VoiceConnectionMethod();Find<TextBlock>("MediaSecurityMode").Text="Security: DTLS-SRTP";Find<TextBlock>("MediaConnectionState").Text="State: "+VoiceConnectionState();
        Find<Border>("VoiceCard").Visibility=active?Visibility.Visible:Visibility.Collapsed;
        if(!active){Find<StackPanel>("ContextVoice").Visibility=Visibility.Collapsed;Find<Expander>("VoiceAdvancedExpander").IsExpanded=false;}
        Find<ComboBox>("VoiceInput").IsEnabled=!active;Find<ComboBox>("VoiceOutput").IsEnabled=!active;Find<Button>("VoiceRefreshDevices").IsEnabled=!active;
        string userState=model.VoiceMediaState==3||model.VoiceMediaState==4?"Voice connection interrupted · Reconnecting…":model.VoiceStateCode==5&&model.VoiceMediaState==2?"Connected":model.VoiceStateCode==1||model.VoiceStateCode==2?"Ringing":model.VoiceStateCode==6?"Ending":model.VoiceStateCode==8?"Media failed":"Connecting";Find<TextBlock>("VoiceState").Text=userState+(String.IsNullOrEmpty(voiceAudioError)?"":" · Audio unavailable");Find<TextBlock>("VoiceDuration").Text="00:00";
        if(voiceConnectedUtc!=DateTime.MinValue){var duration=DateTime.UtcNow-voiceConnectedUtc;Find<TextBlock>("VoiceDuration").Text=((int)duration.TotalMinutes).ToString("00")+":"+duration.Seconds.ToString("00");}
        var input=Find<ComboBox>("VoiceInput").SelectedItem as VoiceDevice;var output=Find<ComboBox>("VoiceOutput").SelectedItem as VoiceDevice;Find<TextBlock>("VoiceDeviceSummary").Text="Mic: "+(input==null?"Unavailable":input.name)+" · Output: "+(output==null?"Unavailable":output.name);Find<Button>("VoiceMute").IsEnabled=connected;Find<Button>("VoiceMute").Content=model.VoiceMuted?"Unmute":"Mute";Find<Button>("VoiceDeafen").IsEnabled=connected;Find<Button>("VoiceDeafen").Content=model.VoiceDeafened?"Undeafen":"Deafen";Find<Button>("VoiceHangUp").IsEnabled=active&&model.VoiceStateCode!=6;
    }
    void RefreshVoiceTelemetry(){
        var telemetry=core.VoiceTelemetry();bool connected=model.VoiceStateCode==5&&model.VoiceMediaState==2;
        double input=connected&&!model.VoiceMuted&&telemetry.inputAvailable?telemetry.input:0;double output=connected&&!model.VoiceDeafened&&telemetry.outputAvailable?telemetry.output:0;
        Find<ProgressBar>("VoiceInputMeter").Value=MeterValue(input);Find<ProgressBar>("VoiceOutputMeter").Value=MeterValue(output);
        Find<TextBlock>("VoiceInputStatus").Text=!connected?"Mic: No Signal":model.VoiceMuted?"Mic: Muted":!telemetry.inputAvailable?"Mic: Device Error":input>=0.001?"Mic: Active":"Mic: No Signal";
        Find<TextBlock>("VoiceOutputStatus").Text=!connected?"Output: No Incoming Audio":model.VoiceDeafened?"Output: Deafened":!telemetry.outputAvailable?"Output: Device Error":output>=0.001?"Output: Receiving":"Output: No Incoming Audio";
    }
    static double MeterValue(double rms){if(rms<=0.001)return 0;double decibels=20*Math.Log10(Math.Min(1,rms));return Math.Max(0,Math.Min(100,(decibels+60)*100/60));}
    VoiceDiagnosticsData CaptureVoiceDiagnosticsData(){bool active=model.VoiceStateCode!=0;string peer=active&&!String.IsNullOrEmpty(model.VoicePeerId)?model.VoicePeerId:recentVoicePeerId;var conversation=model.FindByPeer(peer);var input=Find<ComboBox>("VoiceInput").SelectedItem as VoiceDevice;var output=Find<ComboBox>("VoiceOutput").SelectedItem as VoiceDevice;return new VoiceDiagnosticsData{Native=core.VoiceDiagnostics(),Active=active,CallId=active&&!String.IsNullOrEmpty(model.VoiceCallId)?model.VoiceCallId:recentVoiceCallId,PeerId=peer,VerifiedPeerId=conversation!=null&&conversation.Contact!=null&&conversation.Contact.verified?conversation.Contact.peer:"not verified/available",CallState=active?model.VoiceState:recentVoiceState,Lifetime=active?model.VoiceCallLifetime:0,MediaGeneration=model.VoiceMediaGeneration,MediaState=model.VoiceMediaState,MediaRoute=VoiceConnectionMethod(),ControlAvailable=model.VoiceControlAvailable,InputDevice=input==null?"none":input.name+" ("+input.id+")",OutputDevice=output==null?"none":output.name+" ("+output.id+")",Muted=active?model.VoiceMuted:recentVoiceMuted,Deafened=active?model.VoiceDeafened:recentVoiceDeafened,StartedUtc=voiceStartedUtc==DateTime.MinValue?"unknown":voiceStartedUtc.ToString("O"),ConnectedUtc=(active?voiceConnectedUtc:voiceLastConnectedUtc)==DateTime.MinValue?"unknown":(active?voiceConnectedUtc:voiceLastConnectedUtc).ToString("O"),EndedUtc=voiceEndedUtc==DateTime.MinValue?"not ended/unknown":voiceEndedUtc.ToString("O"),LatestAudioError=latestVoiceAudioError};}
    async void CopyVoiceDiagnostics(){await CopyDiagnosticText(DiagnosticsService.FormatFull(diagnosticsService.Capture(),DiagnosticAccountDisplayName,DiagnosticPeerDisplayName),"Voice diagnostics copied.");}
    async void CopyConnectionDetails(){await CopyDiagnosticText(DiagnosticsService.FormatSummary(diagnosticsService.Capture(),DiagnosticAccountDisplayName,DiagnosticPeerDisplayName),"Connection summary copied.");}
    async Task CopyDiagnosticText(string details,string success){for(int attempt=0;attempt<6;attempt++){bool copied=false;try{Clipboard.SetText(DiagnosticsPrivacyFilter.Redact(details));copied=true;}catch(COMException){}if(copied){model.Status=success;Refresh();return;}await Task.Delay(100);}model.Status="Clipboard is busy. Try again from Connection & Diagnostics.";Refresh();}
    void ViewConnectionLog(){ShowDiagnosticsWindow(null);}
    void EditShareableEndpoint(){var fields=Dialog("Shareable endpoint",new[]{"Host/IP to put in Copy My ID"},new[]{shareHost},"Save endpoint","Choose a hostname, LAN/VPN IPv4, or forwarded endpoint your friend can reach. This does not configure your router.");if(fields!=null){string host=fields[0].Trim();if(host.Length==0||host.Length>253||host.Any(Char.IsWhiteSpace)||host.Any(Char.IsControl)){Notify("Enter a valid hostname or IPv4 address.");return;}shareHost=host;shareHostManuallyConfigured=true;}}
    void Older(){
        var viewer=Find<ScrollViewer>("MessageScroll");
        if(model.Groups){
            var group=model.SelectedGroup;if(group==null||group.HistoryLoading||group.HistoryExhausted)return;
            long before=group.Messages.Where(message=>message.Data.logical>0).Select(message=>message.Data.logical).DefaultIfEmpty(long.MaxValue).Min();
            group.HistoryLoading=true;historyScrollKey="group:"+group.Id;historyScrollOffset=viewer.VerticalOffset;historyScrollExtent=viewer.ExtentHeight;core.GroupHistory(group.Id,before);return;
        }
        var conversation=model.Selected;if(conversation==null||conversation.HistoryLoading||conversation.HistoryExhausted)return;
        string peer=(new[]{conversation.Contact==null?"":conversation.Contact.peer}.Concat(conversation.KnownPeerIds)).Where(value=>!String.IsNullOrEmpty(value)).Distinct(StringComparer.Ordinal).FirstOrDefault(value=>!conversation.HistoryExhaustedPeers.Contains(value));
        if(String.IsNullOrEmpty(peer)){conversation.HistoryExhausted=true;Refresh();return;}
        long dmBefore=conversation.Messages.Where(message=>message.Data.sequence>0).Select(message=>message.Data.sequence).DefaultIfEmpty(long.MaxValue).Min();
        conversation.HistoryLoading=true;conversation.LoadingHistoryPeer=peer;conversation.RequestedPeerHistory.Add(peer);historyScrollKey=conversation.ConversationId;historyScrollOffset=viewer.VerticalOffset;historyScrollExtent=viewer.ExtentHeight;core.History(peer,dmBefore);
    }
    void Send(){if(!Find<Button>("Send").IsEnabled)return;string text=Find<TextBox>("Composer").Text;if(String.IsNullOrWhiteSpace(text))return;bool queued=model.Groups&&model.SelectedGroup!=null?core.SendGroup(model.SelectedGroup.Id,text):model.Selected!=null&&core.Send(model.Selected.Contact.peer,text);if(queued){Find<TextBox>("Composer").Clear();Find<TextBox>("Composer").Focus();}else Notify("Message could not be queued. Try again.");}
    void Notify(string text){Notify(text,null);}
    void Notify(string text,Window owner){Dialog("PeerChat",new string[0],new string[0],"OK",text,false,owner);}
    bool Confirm(string title,string text,string accept){return Dialog(title,new string[0],new string[0],accept,text,true)!=null;}
    string[] Dialog(string title,string[] labels,string[] values,string accept,string description,bool keepOnTop=false,Window owner=null){
        var dialog=new Window{Title=title,Owner=owner??window,Width=510,SizeToContent=SizeToContent.Height,MaxHeight=750,ResizeMode=ResizeMode.NoResize,WindowStartupLocation=WindowStartupLocation.CenterOwner,Topmost=keepOnTop,ShowInTaskbar=false,ShowActivated=true,Background=Brush("#101820"),Foreground=Brush("#EDF1F5"),FontFamily=new FontFamily("Segoe UI"),FontSize=15,Resources=window.Resources};
        var stack=new StackPanel{Margin=new Thickness(26)};dialog.Content=stack;
        stack.Children.Add(new TextBlock{Text=title,FontSize=24,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,0,0,16)});
        stack.Children.Add(new TextBlock{Text=description,TextWrapping=TextWrapping.Wrap,Foreground=Brush("#A1AEBB"),Margin=new Thickness(0,0,0,20)});
        var fields=new List<TextBox>();for(int index=0;index<labels.Length;index++){stack.Children.Add(new TextBlock{Text=labels[index],Margin=new Thickness(0,0,0,7)});var field=new TextBox{Text=values[index],MaxLength=labels[index].Contains("PeerChat")?1400:253,Margin=new Thickness(0,0,0,16)};fields.Add(field);stack.Children.Add(field);}
        var actions=new StackPanel{Orientation=Orientation.Horizontal,HorizontalAlignment=HorizontalAlignment.Right};var cancel=new Button{Content="Cancel",IsCancel=true,Margin=new Thickness(0,0,8,0)};var save=new Button{Content=accept,Background=Brush("#275AA7")};cancel.Click+=(s,e)=>dialog.DialogResult=false;save.Click+=(s,e)=>dialog.DialogResult=true;actions.Children.Add(cancel);actions.Children.Add(save);stack.Children.Add(actions);dialog.Loaded+=(s,e)=>{dialog.Activate();if(fields.Count>0)fields[0].Focus();else cancel.Focus();};return dialog.ShowDialog()==true?fields.Select(f=>f.Text).ToArray():null;
    }
    public void Run(Application app){app.Run(window);}
    static LocalProfileInfo ChooseLocalProfile(LocalProfileManager manager,string requestedId){
        var catalog=manager.Initialize();
        if(!String.IsNullOrEmpty(requestedId)){var requested=catalog.Profiles.FirstOrDefault(p=>p.Id==requestedId);if(requested==null)throw new InvalidOperationException("Requested local profile was not found.");manager.Select(requested.Id);return requested;}
        LocalProfileInfo result=null;var primary=Brush("#EDF1F5");var muted=Brush("#A1AEBB");var secondary=Brush("#B5C0CB");var disabled=Brush("#758390");var dialog=new Window{Title="Choose local profile",Width=620,Height=590,MinHeight=500,WindowStartupLocation=WindowStartupLocation.CenterScreen,Background=Brush("#101820"),Foreground=primary,FontFamily=new FontFamily("Segoe UI"),FontSize=15};dialog.Resources.Add("PeerChatPrimaryText",primary);dialog.Resources.Add("PeerChatMutedText",muted);var textBlockStyle=new Style(typeof(TextBlock));textBlockStyle.Setters.Add(new Setter(TextBlock.ForegroundProperty,primary));dialog.Resources.Add(typeof(TextBlock),textBlockStyle);var buttonStyle=new Style(typeof(Button));buttonStyle.Setters.Add(new Setter(Control.ForegroundProperty,primary));buttonStyle.Setters.Add(new Setter(Control.BackgroundProperty,Brush("#19222B")));buttonStyle.Setters.Add(new Setter(Control.BorderBrushProperty,Brush("#303D49")));buttonStyle.Setters.Add(new Setter(Control.BorderThicknessProperty,new Thickness(1)));buttonStyle.Setters.Add(new Setter(Control.PaddingProperty,new Thickness(12,8,12,8)));buttonStyle.Setters.Add(new Setter(Control.CursorProperty,Cursors.Hand));var buttonDisabled=new Trigger{Property=UIElement.IsEnabledProperty,Value=false};buttonDisabled.Setters.Add(new Setter(Control.ForegroundProperty,disabled));buttonDisabled.Setters.Add(new Setter(UIElement.OpacityProperty,0.65));buttonStyle.Triggers.Add(buttonDisabled);dialog.Resources.Add(typeof(Button),buttonStyle);var textStyle=new Style(typeof(TextBox));textStyle.Setters.Add(new Setter(Control.ForegroundProperty,primary));textStyle.Setters.Add(new Setter(Control.BackgroundProperty,Brush("#151D25")));textStyle.Setters.Add(new Setter(Control.BorderBrushProperty,Brush("#344351")));textStyle.Setters.Add(new Setter(Control.BorderThicknessProperty,new Thickness(1)));textStyle.Setters.Add(new Setter(Control.PaddingProperty,new Thickness(11,8,11,8)));textStyle.Setters.Add(new Setter(TextBox.CaretBrushProperty,Brush("#FFFFFF")));dialog.Resources.Add(typeof(TextBox),textStyle);var itemStyle=new Style(typeof(ListBoxItem));itemStyle.Setters.Add(new Setter(Control.ForegroundProperty,primary));itemStyle.Setters.Add(new Setter(Control.BackgroundProperty,Brush("Transparent")));itemStyle.Setters.Add(new Setter(Control.BorderBrushProperty,Brush("Transparent")));itemStyle.Setters.Add(new Setter(Control.HorizontalContentAlignmentProperty,HorizontalAlignment.Stretch));dialog.Resources.Add(typeof(ListBoxItem),itemStyle);var contextStyle=new Style(typeof(ContextMenu));contextStyle.Setters.Add(new Setter(Control.ForegroundProperty,primary));contextStyle.Setters.Add(new Setter(Control.BackgroundProperty,Brush("#111920")));contextStyle.Setters.Add(new Setter(Control.BorderBrushProperty,Brush("#344351")));dialog.Resources.Add(typeof(ContextMenu),contextStyle);var menuStyle=new Style(typeof(MenuItem));menuStyle.Setters.Add(new Setter(Control.ForegroundProperty,primary));menuStyle.Setters.Add(new Setter(Control.BackgroundProperty,Brush("#111920")));menuStyle.Setters.Add(new Setter(Control.PaddingProperty,new Thickness(11,8,11,8)));var menuDisabled=new Trigger{Property=UIElement.IsEnabledProperty,Value=false};menuDisabled.Setters.Add(new Setter(Control.ForegroundProperty,disabled));menuStyle.Triggers.Add(menuDisabled);dialog.Resources.Add(typeof(MenuItem),menuStyle);
        var root=new Grid{Margin=new Thickness(26)};root.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});root.RowDefinitions.Add(new RowDefinition());root.RowDefinitions.Add(new RowDefinition{Height=GridLength.Auto});dialog.Content=root;
        var heading=new StackPanel();heading.Children.Add(new TextBlock{Text="Choose a local profile",FontSize=26,FontWeight=FontWeights.SemiBold,Foreground=primary});heading.Children.Add(new TextBlock{Text="Each profile has its own Peer ID, trust, encrypted history, contacts, account session, and settings.",TextWrapping=TextWrapping.Wrap,Foreground=muted,Margin=new Thickness(0,7,0,16)});root.Children.Add(heading);
        var list=new ListBox{Padding=new Thickness(0),Margin=new Thickness(0,0,0,14),Background=Brush("Transparent"),Foreground=primary,BorderThickness=new Thickness(0)};Grid.SetRow(list,1);root.Children.Add(list);
        var footer=new StackPanel();var editor=new Grid{Visibility=Visibility.Collapsed,Margin=new Thickness(0,0,0,10)};editor.ColumnDefinitions.Add(new ColumnDefinition());editor.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});editor.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});var name=new TextBox{Margin=new Thickness(0,0,7,0),MaxLength=64};var commit=new Button{Content="Create Profile",Background=Brush("#275AA7"),Foreground=primary,IsEnabled=false,Margin=new Thickness(0,0,7,0)};var cancelEdit=new Button{Content="Cancel"};Grid.SetColumn(commit,1);Grid.SetColumn(cancelEdit,2);editor.Children.Add(name);editor.Children.Add(commit);editor.Children.Add(cancelEdit);name.TextChanged+=(s,e)=>commit.IsEnabled=!String.IsNullOrWhiteSpace(name.Text);footer.Children.Add(editor);var actionRow=new Grid();actionRow.ColumnDefinitions.Add(new ColumnDefinition());actionRow.ColumnDefinitions.Add(new ColumnDefinition{Width=GridLength.Auto});var newProfile=new Button{Content="＋ New Profile",HorizontalAlignment=HorizontalAlignment.Left,Background=Brush("#111920"),BorderBrush=Brush("#25313C")};var open=new Button{Content="Open Profile",Padding=new Thickness(20,10,20,10),IsDefault=true,Background=Brush("#275AA7"),HorizontalContentAlignment=HorizontalAlignment.Center};Grid.SetColumn(open,1);actionRow.Children.Add(newProfile);actionRow.Children.Add(open);footer.Children.Add(actionRow);Grid.SetRow(footer,2);root.Children.Add(footer);
        Func<LocalProfileInfo> selectedProfile=()=>{var item=list.SelectedItem as ListBoxItem;return item==null?null:item.Tag as LocalProfileInfo;};var overflowButtons=new Dictionary<ListBoxItem,Button>();Action rebuild=null;Action<LocalProfileInfo> removeProfile=profile=>{if(MessageBox.Show(dialog,"Delete local profile '"+profile.Name+"'?\n\nIts Peer ID, trust, encrypted history, contacts, account session, endpoints, and settings will be removed from PeerChat. A recoverable copy is retained locally.","Delete Local Profile",MessageBoxButton.YesNo,MessageBoxImage.Warning)!=MessageBoxResult.Yes)return;try{manager.Remove(profile.Id);rebuild();}catch(Exception error){MessageBox.Show(dialog,error.Message,"PeerChat",MessageBoxButton.OK,MessageBoxImage.Information);}};string editMode="";LocalProfileInfo editProfile=null;Action<string,LocalProfileInfo> showEditor=(mode,profile)=>{editMode=mode;editProfile=profile;name.Text=profile==null?"":profile.Name;commit.Content=mode=="rename"?"Save Name":"Create Profile";editor.Visibility=Visibility.Visible;open.IsDefault=false;commit.IsDefault=true;name.Focus();name.SelectAll();};rebuild=()=>{var current=manager.Load();list.Items.Clear();overflowButtons.Clear();ListBoxItem selectedItem=null;foreach(var profile in current.Profiles){string initials=String.Join("",profile.Name.Split(new[]{' '},StringSplitOptions.RemoveEmptyEntries).Take(2).Select(part=>Char.ToUpperInvariant(part[0]).ToString()));if(initials.Length==0)initials="P";DateTime used;string last=DateTime.TryParse(profile.LastUsedUtc,out used)?"Last used "+used.ToLocalTime().ToString("g"):"Not opened yet";bool wasSelected=profile.Id==current.SelectedProfileId;ProfileAccountLink linked=null;try{linked=new ProfileAccountStore(manager.ProfilePath(profile.Id)).Load();}catch{}string account=linked==null?"No linked account":"@"+linked.Username;var text=new StackPanel{Margin=new Thickness(12,2,6,2)};text.Children.Add(new TextBlock{Text=profile.Name,FontSize=17,FontWeight=FontWeights.SemiBold,Foreground=primary});text.Children.Add(new TextBlock{Text=account,FontSize=12,Foreground=secondary,Margin=new Thickness(0,3,0,0)});text.Children.Add(new TextBlock{Text=last,FontSize=11,Foreground=disabled,Margin=new Thickness(0,3,0,0)});var card=new Grid{Margin=new Thickness(10,9,8,9)};card.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(46)});card.ColumnDefinitions.Add(new ColumnDefinition());card.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(38)});card.Children.Add(new Border{Width=40,Height=40,CornerRadius=new CornerRadius(20),Background=Brush(wasSelected?"#315F9E":"#29455F"),Child=new TextBlock{Text=initials,FontSize=15,FontWeight=FontWeights.SemiBold,Foreground=primary,HorizontalAlignment=HorizontalAlignment.Center,VerticalAlignment=VerticalAlignment.Center}});Grid.SetColumn(text,1);card.Children.Add(text);var overflow=new Button{Content="…",Padding=new Thickness(8,4,8,7),HorizontalContentAlignment=HorizontalAlignment.Center,VerticalAlignment=VerticalAlignment.Center,Visibility=Visibility.Collapsed,Background=Brush("#16212A"),BorderBrush=Brush("#2B3A46")};var menu=new ContextMenu{Background=Brush("#111920"),Foreground=primary,BorderBrush=Brush("#344351"),BorderThickness=new Thickness(1)};var rename=new MenuItem{Header="Rename",Foreground=primary};var delete=new MenuItem{Header="Delete",Foreground=Brush("#FF9B9B")};rename.Click+=(s,e)=>showEditor("rename",profile);delete.Click+=(s,e)=>removeProfile(profile);menu.Items.Add(rename);menu.Items.Add(new Separator());menu.Items.Add(delete);overflow.Click+=(s,e)=>{menu.PlacementTarget=overflow;menu.IsOpen=true;};Grid.SetColumn(overflow,2);card.Children.Add(overflow);var item=new ListBoxItem{Content=card,Tag=profile,Foreground=primary,Padding=new Thickness(0),Margin=new Thickness(3,2,3,2),HorizontalContentAlignment=HorizontalAlignment.Stretch,Background=Brush(wasSelected?"#152536":"Transparent"),BorderBrush=Brush(wasSelected?"#31506B":"Transparent"),BorderThickness=new Thickness(1)};overflowButtons[item]=overflow;list.Items.Add(item);if(wasSelected)selectedItem=item;}list.SelectedItem=selectedItem??list.Items.Cast<object>().FirstOrDefault();};
        Action updateSelection=()=>{foreach(var pair in overflowButtons){bool selected=Object.ReferenceEquals(pair.Key,list.SelectedItem);pair.Value.Visibility=selected?Visibility.Visible:Visibility.Collapsed;pair.Key.Background=Brush(selected?"#152536":"Transparent");pair.Key.BorderBrush=Brush(selected?"#31506B":"Transparent");}};list.SelectionChanged+=(s,e)=>updateSelection();Action openSelected=()=>{var selected=selectedProfile();if(selected==null)return;manager.Select(selected.Id);result=manager.Load().Profiles.First(p=>p.Id==selected.Id);dialog.DialogResult=true;};open.Click+=(s,e)=>openSelected();list.MouseDoubleClick+=(s,e)=>openSelected();newProfile.Click+=(s,e)=>showEditor("new",null);cancelEdit.Click+=(s,e)=>{editor.Visibility=Visibility.Collapsed;commit.IsDefault=false;open.IsDefault=true;};commit.Click+=(s,e)=>{if(String.IsNullOrWhiteSpace(name.Text))return;if(editMode=="rename"&&editProfile!=null)manager.Rename(editProfile.Id,name.Text);else manager.Create(name.Text);editor.Visibility=Visibility.Collapsed;commit.IsDefault=false;open.IsDefault=true;rebuild();updateSelection();};rebuild();updateSelection();dialog.ShowDialog();return result;
    }
    static void ExecuteProfileMaintenance(LocalProfileManager manager,ProfileMaintenanceRequest request){
        Exception last=null;
        // The replacement process normally races only the prior process' final lock release.
        // Wait briefly for that known graceful shutdown, but never bypass a real other-process lock.
        for(int attempt=0;attempt<12;attempt++){
            try{
                manager.ExecuteMaintenance(request,(previous,fresh)=>{using(var maintenanceCore=new CoreAdapter()){if(String.IsNullOrEmpty(fresh))return maintenanceCore.ProfilePeerId(previous);maintenanceCore.RebuildProfileIdentity(previous,fresh);return maintenanceCore.ProfilePeerId(fresh);}});
                return;
            }catch(InvalidOperationException error){
                if(error.Message==null||error.Message.IndexOf("currently open in another PeerChat process",StringComparison.OrdinalIgnoreCase)<0)throw;
                last=error;if(attempt==11)break;System.Threading.Thread.Sleep(Math.Min(1000,100*(attempt+1)));
            }
        }
        throw last??new InvalidOperationException("This profile is currently open in another PeerChat process. Close it before continuing.");
    }
    [STAThread] public static int Main(string[] args){
        string startupRoot=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"PeerChat");
        try{
            string installRoot=startupRoot,directory=null,profileId=null,healthToken=null,maintenanceRequestId=null;ushort port=0;string capture=null;bool compact=args.Contains("--compact");
            for(int index=0;index+1<args.Length;index++){if(args[index]=="--data-dir")directory=args[++index];else if(args[index]=="--install-root"){installRoot=Path.GetFullPath(args[++index]);startupRoot=installRoot;}else if(args[index]=="--profile-id")profileId=args[++index];else if(args[index]=="--port")port=UInt16.Parse(args[++index]);else if(args[index]=="--capture")capture=args[++index];else if(args[index]=="--peerchat-update-health-token")healthToken=args[++index];else if(args[index]=="--profile-maintenance")maintenanceRequestId=args[++index];}
            // The chooser is intentionally shown before Application.Run and before CoreAdapter
            // construction. Keep WPF alive when that temporary last window closes; otherwise WPF
            // permanently begins shutdown and silently refuses to run the main window.
            var app=new Application{ShutdownMode=ShutdownMode.OnExplicitShutdown};LocalProfileManager manager=null;LocalProfileInfo selected=null;TraceStartup(installRoot,"bootstrap-start");
            if(directory==null){manager=new LocalProfileManager(installRoot);if(!String.IsNullOrEmpty(maintenanceRequestId)){var request=manager.ReadMaintenanceRequest(maintenanceRequestId);TraceStartup(installRoot,"profile-maintenance-start action="+request.Action+" profile="+request.ProfileId);ExecuteProfileMaintenance(manager,request);manager.CompleteMaintenanceRequest(request);profileId=request.ProfileId;TraceStartup(installRoot,"profile-maintenance-complete action="+request.Action+" profile="+request.ProfileId);}TraceStartup(installRoot,"chooser-open");selected=ChooseLocalProfile(manager,profileId);if(selected==null){TraceStartup(installRoot,"chooser-cancelled");app.Shutdown();return 0;}directory=manager.ProfilePath(selected.Id);TraceStartup(installRoot,"profile-selected id="+selected.Id+" path="+directory);}
            var profileLease=LocalProfileLease.TryAcquire(directory);if(profileLease==null){TraceStartup(installRoot,"profile-already-open path="+directory);MessageBox.Show("This local profile is already open in another PeerChat window. Choose a different profile or close the existing window first.","PeerChat profile already open",MessageBoxButton.OK,MessageBoxImage.Information);app.Shutdown(2);return 2;}
        var desktop=new DesktopApplication(directory,port,capture,compact,manager,selected,healthToken);app.ShutdownMode=ShutdownMode.OnMainWindowClose;TraceStartup(installRoot,"main-run-enter");desktop.Run(app);TraceStartup(installRoot,"main-run-exit");app.Shutdown(0);profileLease.Dispose();
            // The window close path has already joined/released the native core. Terminate the
            // packaged host explicitly so dormant WPF infrastructure cannot leave a windowless
            // process that collides with a restart replacement.
            Environment.Exit(0);return 0;
        }catch(Exception error){try{TraceStartup(startupRoot,"startup-failed type="+error.GetType().Name+" message="+(error.Message??"").Replace('\r',' ').Replace('\n',' '));}catch{}MessageBox.Show(error.ToString(),"PeerChat startup error");return 1;}
    }
}
}

