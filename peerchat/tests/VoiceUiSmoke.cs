using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using PeerChatDesktop;

class VoiceUiSmoke {
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
    static void Press(Window window,string name){((Button)window.FindName(name)).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));}
    static bool IsInside(DependencyObject child,DependencyObject ancestor){for(var current=child;current!=null;current=VisualTreeHelper.GetParent(current))if(current==ancestor)return true;return false;}
    static Button FindButton(DependencyObject root,string text){for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++){var child=VisualTreeHelper.GetChild(root,i);var button=child as Button;if(button!=null&&Convert.ToString(button.Content).StartsWith(text))return button;var nested=FindButton(child,text);if(nested!=null)return nested;}return null;}
    static FrameworkElement FindNamedControl(DependencyObject root,string name){for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++){var child=VisualTreeHelper.GetChild(root,i);var element=child as FrameworkElement;if(element!=null&&AutomationProperties.GetName(element)==name)return element;var nested=FindNamedControl(child,name);if(nested!=null)return nested;}return null;}
    [STAThread] static int Main(string[] args){
        var screenPreferences=new ScreenSharePreferenceStore(args[0]);
        screenPreferences.Save(new ScreenSharePreferenceData{FramesPerSecond=60,BitrateBitsPerSecond=16000000,CaptureCursor=false});
        var loadedScreenPreferences=screenPreferences.Load();
        Require(loadedScreenPreferences.FramesPerSecond==60&&loadedScreenPreferences.BitrateBitsPerSecond==16000000&&!loadedScreenPreferences.CaptureCursor,"Profile-scoped screen preferences did not survive reload");
        int result=0;var app=new Application();var desktop=new DesktopApplication(args[0],UInt16.Parse(args[1]));
        var window=(Window)typeof(DesktopApplication).GetField("window",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);
        window.ShowInTaskbar=false;window.ShowActivated=false;window.WindowStartupLocation=WindowStartupLocation.Manual;window.Left=-20000;
        window.Loaded+=async(s,e)=>{try{
            await Task.Delay(900);window.UpdateLayout();
            var input=window.FindName("VoiceInput") as ComboBox;var output=window.FindName("VoiceOutput") as ComboBox;
            var inputMeter=window.FindName("VoiceInputMeter") as ProgressBar;var outputMeter=window.FindName("VoiceOutputMeter") as ProgressBar;
            var advanced=(Expander)window.FindName("VoiceAdvancedExpander");var inline=(StackPanel)window.FindName("ContextVoice");var connectionContext=(StackPanel)window.FindName("ContextConnection");var voiceCard=(Border)window.FindName("VoiceCard");
            var rightPane=(Border)window.FindName("RightDetailsPane");var rightColumn=(ColumnDefinition)window.FindName("RightPaneColumn");var chatColumn=(ColumnDefinition)window.FindName("ChatColumn");var securityContent=(StackPanel)window.FindName("SecurityPanelContent");var connectionContent=(StackPanel)window.FindName("ConnectionPanelContent");var screenConnection=(StackPanel)typeof(DesktopApplication).GetField("screenConnectionPanel",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);
            Require(input!=null&&output!=null&&inputMeter!=null&&outputMeter!=null,"Voice selectors or meters are missing");
            Require(rightPane!=null&&rightPane.Visibility==Visibility.Visible&&rightColumn.ActualWidth>200&&securityContent.IsVisible&&connectionContent.IsVisible,"The right details pane and its Security/Connection content should be open by default");
            Require(((TextBlock)window.FindName("ReliableConnectionMethod")).Text.Length>0&&((TextBlock)window.FindName("Security")).Text.Length>0,"Default-open pane omitted current connection/security details");
            Require(screenConnection!=null&&IsInside(screenConnection,connectionContent),"Screen-media connection details were lost from the Connection section");
            var callButton=(Button)window.FindName("VoiceCall");var settingsButton=(Button)window.FindName("Settings");var paneButton=(Button)window.FindName("ToggleRightPane");var headerActions=callButton.Parent as Panel;
            Require(headerActions!=null&&settingsButton.Parent==headerActions&&paneButton.Parent==headerActions,"Call, Settings, and pane controls are not in the shared header action area");
            Require(callButton.ToolTip as string=="Call"&&settingsButton.ToolTip as string=="Settings","Compact header actions lost their tooltips");
            Require(((StackPanel)callButton.Content).Children.Count==1&&((StackPanel)callButton.Content).Children[0] is System.Windows.Shapes.Path,"Call action is not icon-only");
            Require(((StackPanel)settingsButton.Content).Children.Count==1&&((StackPanel)settingsButton.Content).Children[0] is System.Windows.Shapes.Path,"Settings action is not a gear icon-only control");
            Require(headerActions.Children.IndexOf(callButton)==headerActions.Children.Count-3&&headerActions.Children.IndexOf(settingsButton)==headerActions.Children.Count-2&&headerActions.Children.IndexOf(paneButton)==headerActions.Children.Count-1,"Header actions are not ordered Call, Settings, pane chevron at the right edge");
            Require(Convert.ToString(paneButton.Content)=="‹","Open pane does not show the retract chevron");
            var screenStrip=(Border)typeof(DesktopApplication).GetField("screenStrip",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);var diagnosticsButton=(Button)typeof(DesktopApplication).GetField("screenDiagnosticsButton",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);var stopButton=(Button)typeof(DesktopApplication).GetField("screenEndButton",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);var screenRow=screenStrip.Child as Grid;var screenActions=screenRow==null?null:screenRow.Children.Cast<UIElement>().OfType<Panel>().SingleOrDefault(panel=>IsInside(diagnosticsButton,panel)&&IsInside(stopButton,panel));
            Require(screenRow!=null&&screenActions!=null&&Grid.GetColumn(screenActions)==1&&screenActions.HorizontalAlignment==HorizontalAlignment.Right&&screenActions.Children.Contains(diagnosticsButton)&&screenActions.Children.Contains(stopButton),"Screen-share status/actions are not separated into left details and right-aligned buttons");
            double openChatWidth=chatColumn.ActualWidth,openComposerWidth=((TextBox)window.FindName("Composer")).ActualWidth;Press(window,"ToggleRightPane");window.UpdateLayout();
            Require(rightPane.Visibility==Visibility.Collapsed&&rightColumn.ActualWidth<1&&chatColumn.ActualWidth>openChatWidth+250&&Convert.ToString(paneButton.Content)=="›","Collapsing the right pane did not reclaim its width for chat or show the restore chevron");
            var composer=(TextBox)window.FindName("Composer");Require(composer.IsVisible&&composer.ActualWidth>openComposerWidth+200&&composer.TextWrapping==TextWrapping.Wrap,"Composer did not expand cleanly with the chat area");
            Press(window,"ToggleRightPane");window.UpdateLayout();
            Require(rightPane.Visibility==Visibility.Visible&&rightColumn.ActualWidth>200&&securityContent.IsVisible&&connectionContent.IsVisible&&Math.Abs(chatColumn.ActualWidth-openChatWidth)<2&&Convert.ToString(paneButton.Content)=="‹","Reopening the pane did not restore its contents and original chat width");
            Require(FindButton(window,"Share Screen")!=null,"DM header is missing screen sharing action");
			var version=window.FindName("VersionText") as TextBlock;Require(version!=null&&version.Text=="App version: "+PeerChat.VersionInfo.Display,"Sidebar version is not projected from the packaged version metadata");
            bool settingsVerified=false;var settingsTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(80)};settingsTimer.Tick+=(sender,tickArgs)=>{
                var settings=window.OwnedWindows.Cast<Window>().FirstOrDefault(item=>item.Title=="Settings");if(settings==null)return;settingsTimer.Stop();
                try{
                    foreach(var page in new[]{"General","Privacy & Security","Voice","Video","Screen Sharing","Network"}){
                        var navigation=FindButton(settings,page);Require(navigation!=null,"Settings navigation is missing "+page);
                        navigation.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();
                        if(page=="Voice")Require(FindNamedControl(settings,"Voice settings input device") is ComboBox&&FindNamedControl(settings,"Voice settings output device") is ComboBox&&FindNamedControl(settings,"Voice input volume") is Slider&&FindNamedControl(settings,"Voice output volume") is Slider,"Voice settings page is missing working device or volume controls");
                        if(page=="Screen Sharing")Require(FindNamedControl(settings,"Screen share frame rate") is ComboBox&&FindNamedControl(settings,"Screen share bitrate") is ComboBox&&FindNamedControl(settings,"Screen share cursor") is CheckBox,"Screen sharing settings are missing working quality or cursor controls");
                    }
                    var updates=FindButton(settings,"Updates");Require(updates!=null,"Updates navigation is missing");updates.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();Require(FindButton(settings,"Check for Updates")!=null,"Updates content is not on its own page");
                    var advancedPage=FindButton(settings,"Advanced");Require(advancedPage!=null,"Advanced navigation is missing");advancedPage.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();Require(FindButton(settings,"◉   Direct P2P Connection")!=null,"Advanced content did not return after navigation");settingsVerified=true;
                }finally{settings.Close();}
            };settingsTimer.Start();Press(window,"Settings");Require(settingsVerified,"Settings navigation test did not complete");
            Require(!advanced.IsExpanded&&inline.Visibility==Visibility.Collapsed&&connectionContext.Visibility==Visibility.Visible,"Inline diagnostics were visible by default or replaced the side-panel context");
            Require(IsInside(inline,voiceCard),"Call diagnostics are not hosted inside the conversation call strip");
            var model=(ChatViewModel)typeof(DesktopApplication).GetField("model",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);var refresh=typeof(DesktopApplication).GetMethod("RefreshVoiceUi",BindingFlags.Instance|BindingFlags.NonPublic);var refreshAll=typeof(DesktopApplication).GetMethod("Refresh",BindingFlags.Instance|BindingFlags.NonPublic);var routeConversation=model.Get(new string('f',64));routeConversation.Contact.verified=true;routeConversation.Online=true;routeConversation.ConnectionState="Ready";routeConversation.ReliableRoute=3;routeConversation.ReliableEndpoint="relay:"+routeConversation.Contact.peer;model.Selected=routeConversation;refreshAll.Invoke(desktop,null);Require(callButton.IsVisible,"Existing Call availability was lost for a verified online friend");double originalWindowWidth=window.Width;window.Width=Math.Max(window.MinWidth,1120);window.UpdateLayout();Require(callButton.IsVisible&&settingsButton.IsVisible&&paneButton.IsVisible&&composer.IsVisible&&composer.ActualWidth>0,"Header or composer is clipped at compact supported width");window.Width=originalWindowWidth;window.UpdateLayout();Require(((TextBlock)window.FindName("ReliableConnectionMethod")).Text=="Connection: PeerChat Server Relay"&&((TextBlock)window.FindName("ReliableSecurityMode")).Text=="Security: End-to-end peer TLS"&&((TextBlock)window.FindName("ReliableRemoteRoute")).Text.StartsWith("Remote route: relay:"),"Authoritative reliable relay route was not rendered separately from voice media");routeConversation.ReliableRoute=1;routeConversation.ReliableEndpoint="192.168.1.17:65417";refreshAll.Invoke(desktop,null);Require(((TextBlock)window.FindName("ReliableConnectionMethod")).Text=="Connection: Direct LAN"&&((TextBlock)window.FindName("ReliableRemoteRoute")).Text=="Remote route: 192.168.1.17:65417","Authoritative Direct LAN route was not rendered");
            model.Apply(new CoreEvent{kind=13,callLifetime=3,mediaGeneration=7,mediaState=2,callState=5,text="Connected",detail="previous-call"});model.Apply(new CoreEvent{kind=13,callLifetime=3,mediaGeneration=6,mediaState=5,callState=8,text="stale failure",detail="previous-call"});Require(model.VoiceStateCode==5&&model.VoiceMediaGeneration==7,"Stale old-generation media event changed authoritative UI state");model.Apply(new CoreEvent{kind=13,callLifetime=3,mediaGeneration=7,mediaState=6,callState=0,text="Idle",detail="previous-call"});Require(model.VoiceStateCode==0,"Current-generation call close was rejected by the UI lifetime guard");
            model.VoiceState="Connected · Direct WebRTC";model.VoiceStateCode=5;model.VoiceMediaState=2;model.VoiceMediaRoute=2;model.VoiceMediaGeneration=4;model.VoiceControlAvailable=true;model.VoiceCallId="ui-call";model.VoiceCallLifetime=4;refresh.Invoke(desktop,null);
            Require(voiceCard.Visibility==Visibility.Visible&&((TextBlock)window.FindName("VoiceState")).Text=="Connected","Active call did not expand into the header strip");
            Require(((TextBlock)window.FindName("MediaConnectionMethod")).Text=="Connection: Direct ICE / NAT traversal"&&((TextBlock)window.FindName("MediaSecurityMode")).Text=="Security: DTLS-SRTP"&&((TextBlock)window.FindName("MediaConnectionState")).Text=="State: Connected","Authoritative voice route/security state was not rendered separately");
            model.VoiceMediaState=4;refresh.Invoke(desktop,null);Require(((TextBlock)window.FindName("VoiceState")).Text.StartsWith("Voice connection interrupted · Reconnecting")&&((TextBlock)window.FindName("MediaConnectionMethod")).Text=="Connection: Recovering","Media recovery state was inferred or hidden instead of rendered explicitly");model.VoiceMediaState=2;refresh.Invoke(desktop,null);
            Press(window,"VoiceDetails");Require(inline.Visibility==Visibility.Visible&&connectionContext.Visibility==Visibility.Visible&&!advanced.IsExpanded,"Call Details did not open inline while preserving the contextual side panel");
            advanced.IsExpanded=true;await Task.Delay(150);window.UpdateLayout();
            var diagnosticsScroll=advanced.Content as ScrollViewer;
            Require(diagnosticsScroll!=null&&diagnosticsScroll.MaxHeight<=300&&diagnosticsScroll.VerticalScrollBarVisibility==ScrollBarVisibility.Auto,"Inline diagnostics are not height-capped with internal scrolling");
            Require(input.Background.ToString()!="#FFFFFFFF"&&input.Foreground.ToString()!="#FF000000"&&input.Template!=null,"Voice ComboBox did not receive the dark theme");
            Require(input.Focusable&&input.IsTabStop&&!String.IsNullOrEmpty(AutomationProperties.GetName(input)),"Voice ComboBox lost keyboard or accessibility behavior");
            input.ApplyTemplate();Require(input.Template.FindName("PART_Popup",input)!=null,"Themed ComboBox is missing its accessible popup part");
            input.IsDropDownOpen=true;await Task.Delay(80);input.UpdateLayout();var popupItem=input.ItemContainerGenerator.ContainerFromIndex(0) as ComboBoxItem;
            Require(popupItem!=null&&popupItem.Foreground.ToString()!="#FF000000"&&popupItem.Template!=null,"Voice dropdown items did not receive the dark theme");input.IsDropDownOpen=false;
            Require(!inputMeter.IsHitTestVisible&&!outputMeter.IsHitTestVisible&&inputMeter.Maximum==100&&outputMeter.Maximum==100,"Audio meters behave like controls instead of read-only indicators");
            var meterValue=typeof(DesktopApplication).GetMethod("MeterValue",BindingFlags.Static|BindingFlags.NonPublic);
            Require(meterValue!=null&&(double)meterValue.Invoke(null,new object[]{0.0034})>15&&(double)meterValue.Invoke(null,new object[]{0.0005})==0,"Quiet decoded speech is not mapped onto the readable dB meter range");
            var oldInput=input.SelectedItem as VoiceDevice;var oldOutput=output.SelectedItem as VoiceDevice;Require(oldInput!=null&&oldOutput!=null,"Initial voice device choices are missing");
            model.VoiceState="Idle";model.VoiceStateCode=0;model.VoiceCallId="";refresh.Invoke(desktop,null);Require(voiceCard.Visibility==Visibility.Collapsed&&inline.Visibility==Visibility.Collapsed,"Ending a call did not collapse the header strip and inline diagnostics");
            Press(window,"VoiceRefreshDevices");var newInput=input.SelectedItem as VoiceDevice;var newOutput=output.SelectedItem as VoiceDevice;
            Require(newInput!=null&&newOutput!=null&&newInput.id==oldInput.id&&newInput.name==oldInput.name&&newOutput.id==oldOutput.id&&newOutput.name==oldOutput.name,"Refresh Devices did not preserve valid selections");
            File.WriteAllText(args[2],"PASS: compact Call/Settings/pane header actions, Settings navigation, chevron-driven right-pane retract/restore, chat/composer expansion and compact-width fit, right-aligned screen-share actions, Security/Connection details, authoritative route labels, media-generation UI rejection, call strip, and voice-device controls.");
        }catch(Exception error){result=1;File.WriteAllText(args[2],"FAIL: "+error);}finally{window.Close();}};
        app.Run(window);return result;
    }
}
