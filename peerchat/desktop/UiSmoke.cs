// Test-only WPF control exercise. Uses real isolated profiles and actual Node
// events; it never seeds fake online, delivery or message state into the GUI.
using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using PeerChatDesktop;
class UiSmoke {
 static void Require(bool value,string message){if(!value)throw new Exception(message);}
 static void Press(Window window,string name){((Button)window.FindName(name)).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));}
 static Button FindButton(DependencyObject root,string text){for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++){var child=VisualTreeHelper.GetChild(root,i);var button=child as Button;if(button!=null&&Convert.ToString(button.Content).StartsWith(text))return button;var nested=FindButton(child,text);if(nested!=null)return nested;}return null;}
 static void SubmitNextDialog(Window owner,string[] values,string buttonText){
  var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(100)};timer.Tick+=(s,e)=>{if(owner.OwnedWindows.Count==0)return;var dialog=owner.OwnedWindows[0];var stack=dialog.Content as StackPanel;if(stack==null)return;timer.Stop();int index=0;foreach(var child in stack.Children){var field=child as TextBox;if(field!=null&&index<values.Length)field.Text=values[index++];}var actions=stack.Children[stack.Children.Count-1] as StackPanel;foreach(Button button in actions.Children)if((string)button.Content==buttonText){button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));return;}throw new Exception("Dialog action not found");};timer.Start();
 }
 static void Capture(Window window,string path){window.UpdateLayout();var content=(FrameworkElement)window.Content;var bitmap=new RenderTargetBitmap((int)content.ActualWidth,(int)content.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(content);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));using(var file=File.Create(path))encoder.Save(file);}
 [STAThread] static int Main(string[] args){int result=0;var app=new Application();var desktop=new DesktopApplication(args[0],UInt16.Parse(args[1]));var window=(Window)typeof(DesktopApplication).GetField("window",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);
 window.ShowInTaskbar=false;window.ShowActivated=false;window.WindowStartupLocation=WindowStartupLocation.Manual;window.Left=-20000;
 window.Loaded+=async(s,e)=>{try{
  await Task.Delay(2500);
  var list=(ListBox)window.FindName("ContactList");var input=(TextBox)window.FindName("Composer");var messages=(ItemsControl)window.FindName("Messages");
  var send=(Button)window.FindName("Send");var attachment=(Button)window.FindName("Attachment");
  Require(list.Items.Count>0,"Saved contacts not shown");Require(messages.Items.Count>=2,"Saved history not shown");Require(send.IsEnabled,"Send is not enabled");
  Require(input.IsEnabled&&!input.IsReadOnly&&input.Focusable&&input.IsHitTestVisible,"Composer cannot accept direct P2P input");
  Require(!attachment.IsEnabled&&Convert.ToString(attachment.ToolTip).Contains("not available"),"Attachment must be honestly disabled");
  foreach(var button in new[]{send,attachment}){var iconRow=button.Content as StackPanel;Require(iconRow!=null&&iconRow.Children.Count>0&&iconRow.Children[0] is System.Windows.Shapes.Path,"Action vector icon missing");var bounds=((System.Windows.Shapes.Path)iconRow.Children[0]).Data.Bounds;Require(bounds.Width>8&&bounds.Height>8,"Action vector icon geometry is invalid");}
  input.Text="preserved draft";Press(window,"Groups");Require(messages.ItemsSource==null&&!input.IsEnabled,"Empty Groups view must hide DMs and disable composer");Capture(window,args[2]+"-groups.png");Press(window,"DM");Require(input.Text=="preserved draft"&&messages.Items.Count>=2,"DM switch lost state (draft='"+input.Text+"', messages="+messages.Items.Count+")");
  var search=(TextBox)window.FindName("Search");search.Text="no such contact";Require(list.Items.Count==0,"Search did not filter");search.Clear();Require(list.Items.Count>0,"Search did not restore contacts");
  // Clipboard is desktop-global. Only one test process owns this check.
  if(args[1]=="29431"){Press(window,"CopyId");await Task.Delay(700);string copied="";for(int attempt=0;attempt<5;attempt++){try{copied=Clipboard.GetText();break;}catch(System.Runtime.InteropServices.COMException){}await Task.Delay(100);}Require(copied.StartsWith("P2PCHAT1:"),"Copy My ID did not copy an invite");}
  SubmitNextDialog(window,new string[0],"Cancel");Press(window,"Profile");
  SubmitNextDialog(window,new string[0],"Cancel");Press(window,"Manual");
  SubmitNextDialog(window,new string[0],"Cancel");Press(window,"AddFriend");
  bool advancedManualOpened=false;var settingsTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(100)};settingsTimer.Tick+=(sender,args2)=>{var settings=window.OwnedWindows.Cast<Window>().FirstOrDefault(w=>w.Title=="Settings");if(settings==null)return;var direct=FindButton(settings,"◉   Direct P2P Connection");if(direct==null)return;settingsTimer.Stop();var manualTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(100)};manualTimer.Tick+=(nestedSender,nestedArgs)=>{var manualDialog=settings.OwnedWindows.Cast<Window>().FirstOrDefault(w=>w.Title=="Manual Connect");if(manualDialog==null)return;advancedManualOpened=true;manualTimer.Stop();var cancel=FindButton(manualDialog,"Cancel");if(cancel==null)throw new Exception("Manual Connect cancel action missing");cancel.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.Close();};manualTimer.Start();direct.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));};settingsTimer.Start();Press(window,"Settings");Require(advancedManualOpened,"Settings Direct P2P did not open its usable connection dialog");
  string uniqueText="WPF control test "+args[3]+" from "+args[1]+" — https://example.com/";
  input.Text=uniqueText;Press(window,"Send");Require(input.Text=="","Send did not clear composer");
  bool received=false,delivered=false;
  for(int attempt=0;attempt<100;attempt++){await Task.Delay(100);foreach(MessageModel message in messages.Items){if(message.Outgoing&&message.Text==uniqueText&&message.Data.delivery==2)delivered=true;if(!message.Outgoing&&message.Text.Contains(args[3]))received=true;}if(received&&delivered)break;}
  Require(received,"Did not receive the other WPF process's new message");Require(delivered,"New message delivery state not displayed");await Task.Delay(1000);
  Require(window.FindName("CompactConnectionStatus") is TextBlock&&window.FindName("LastConnectionFailure") is TextBlock,"Compact connection status is missing");Require(window.FindName("CopyConnectionDetails") is Button&&window.FindName("ViewConnectionLog") is Button,"Connection diagnostics actions are missing");
  Require(window.FindName("VoiceCall") is Button&&window.FindName("VoiceCard") is Border&&window.FindName("VoiceMute") is Button&&window.FindName("VoiceDeafen") is Button&&window.FindName("VoiceHangUp") is Button,"Voice call controls are missing");Require(window.FindName("VoiceInput") is ComboBox&&window.FindName("VoiceOutput") is ComboBox&&window.FindName("VoiceRefreshDevices") is Button&&window.FindName("CopyVoiceDiagnostics") is Button,"Voice device selectors/diagnostics are missing");Press(window,"VoiceRefreshDevices");Require(((ComboBox)window.FindName("VoiceInput")).Items.Count>0&&((ComboBox)window.FindName("VoiceOutput")).Items.Count>0,"Refreshing voice devices cleared the default choices");
  if(args[1]=="29431"){Press(window,"CopyConnectionDetails");await Task.Delay(300);Require(Clipboard.GetText().StartsWith("PeerChat connection details"),"Copy Details did not copy a safe troubleshooting block");Press(window,"CopyVoiceDiagnostics");await Task.Delay(300);Require(Clipboard.GetText().StartsWith("PeerChat voice diagnostics")&&Clipboard.GetText().Contains("capture_frames="),"Copy Voice Diagnostics did not copy its bounded media snapshot");}
  bool logOpened=false;var logTimer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(100)};logTimer.Tick+=(sender,args2)=>{var logWindow=window.OwnedWindows.Cast<Window>().FirstOrDefault(w=>w.Title=="Connection Log");if(logWindow==null)return;logOpened=true;logTimer.Stop();logWindow.Close();};logTimer.Start();Press(window,"ViewConnectionLog");Require(logOpened,"Read-only connection log did not open");
  Press(window,"Older");Press(window,"Reconnect");await Task.Delay(300);Capture(window,args[2]+"-full.png");window.Width=1120;window.Height=740;await Task.Delay(300);Capture(window,args[2]+"-compact.png");
  File.WriteAllText(args[2]+".txt","PASS: packaged WPF controls; direct P2P messaging, voice controls/device refresh, bounded Copy Voice Diagnostics, reconnect, compact status, safe Copy Details/log, profile history, and normal close.");
 }catch(Exception error){result=1;File.WriteAllText(args[2]+".txt","FAIL: "+error);}finally{window.Close();}};
 app.Run(window);return result;
 }
}
