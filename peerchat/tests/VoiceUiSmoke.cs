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
    [STAThread] static int Main(string[] args){
        int result=0;var app=new Application();var desktop=new DesktopApplication(args[0],UInt16.Parse(args[1]));
        var window=(Window)typeof(DesktopApplication).GetField("window",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);
        window.ShowInTaskbar=false;window.ShowActivated=false;window.WindowStartupLocation=WindowStartupLocation.Manual;window.Left=-20000;
        window.Loaded+=async(s,e)=>{try{
            await Task.Delay(900);window.UpdateLayout();
            var input=window.FindName("VoiceInput") as ComboBox;var output=window.FindName("VoiceOutput") as ComboBox;
            var inputMeter=window.FindName("VoiceInputMeter") as ProgressBar;var outputMeter=window.FindName("VoiceOutputMeter") as ProgressBar;
            var advanced=(Expander)window.FindName("VoiceAdvancedExpander");var inline=(StackPanel)window.FindName("ContextVoice");var connectionContext=(StackPanel)window.FindName("ContextConnection");var voiceCard=(Border)window.FindName("VoiceCard");
            Require(input!=null&&output!=null&&inputMeter!=null&&outputMeter!=null,"Voice selectors or meters are missing");
			var version=window.FindName("VersionText") as TextBlock;Require(version!=null&&version.Text=="App version: "+PeerChat.VersionInfo.Display,"Sidebar version is not projected from the packaged version metadata");
            bool settingsVerified=false;var settingsTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(80)};settingsTimer.Tick+=(sender,tickArgs)=>{var settings=window.OwnedWindows.Cast<Window>().FirstOrDefault(item=>item.Title=="Settings");if(settings==null)return;settingsTimer.Stop();try{foreach(var page in new[]{"General","Privacy & Security","Voice","Video","Screen Sharing","Network"}){var navigation=FindButton(settings,page);Require(navigation!=null,"Settings navigation is missing "+page);navigation.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();}var updates=FindButton(settings,"Updates");Require(updates!=null,"Updates navigation is missing");updates.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();Require(FindButton(settings,"Check for Updates")!=null,"Updates content is not on its own page");var advancedPage=FindButton(settings,"Advanced");Require(advancedPage!=null,"Advanced navigation is missing");advancedPage.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();Require(FindButton(settings,"◉   Direct P2P Connection")!=null,"Advanced content did not return after navigation");settingsVerified=true;}finally{settings.Close();}};settingsTimer.Start();Press(window,"Settings");Require(settingsVerified,"Settings navigation test did not complete");
            Require(!advanced.IsExpanded&&inline.Visibility==Visibility.Collapsed&&connectionContext.Visibility==Visibility.Visible,"Inline diagnostics were visible by default or replaced the side-panel context");
            Require(IsInside(inline,voiceCard),"Call diagnostics are not hosted inside the conversation call strip");
            var model=(ChatViewModel)typeof(DesktopApplication).GetField("model",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);var refresh=typeof(DesktopApplication).GetMethod("RefreshVoiceUi",BindingFlags.Instance|BindingFlags.NonPublic);
            model.VoiceState="Connected · Direct WebRTC";model.VoiceCallId="ui-call";refresh.Invoke(desktop,null);
            Require(voiceCard.Visibility==Visibility.Visible&&((TextBlock)window.FindName("VoiceState")).Text=="Connected","Active call did not expand into the header strip");
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
            model.VoiceState="Idle";model.VoiceCallId="";refresh.Invoke(desktop,null);Require(voiceCard.Visibility==Visibility.Collapsed&&inline.Visibility==Visibility.Collapsed,"Ending a call did not collapse the header strip and inline diagnostics");
            Press(window,"VoiceRefreshDevices");var newInput=input.SelectedItem as VoiceDevice;var newOutput=output.SelectedItem as VoiceDevice;
            Require(newInput!=null&&newOutput!=null&&newInput.id==oldInput.id&&newInput.name==oldInput.name&&newOutput.id==oldOutput.id&&newOutput.name==oldOutput.name,"Refresh Devices did not preserve valid selections");
            File.WriteAllText(args[2],"PASS: Settings navigation/pages, inline capped call diagnostics, header call-strip lifecycle, light dark-theme text, device controls, meters, and refresh selection persistence.");
        }catch(Exception error){result=1;File.WriteAllText(args[2],"FAIL: "+error);}finally{window.Close();}};
        app.Run(window);return result;
    }
}
