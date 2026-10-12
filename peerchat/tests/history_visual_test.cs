using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Automation;
using PeerChatDesktop;
class HistoryVisualTest {
 static BindingFlags flags=BindingFlags.NonPublic|BindingFlags.Instance;
 static void Check(bool v,string reason){if(!v)throw new Exception(reason);}
 static System.Collections.Generic.IEnumerable<DependencyObject> Walk(DependencyObject v){for(int i=0;i<VisualTreeHelper.GetChildrenCount(v);i++){var c=VisualTreeHelper.GetChild(v,i);yield return c;foreach(var n in Walk(c))yield return n;}}
 static void Set(object target,string field,object value){target.GetType().GetField(field,flags|BindingFlags.Public).SetValue(target,value);}
 static void Save(Window window,string path){window.UpdateLayout();var image=new RenderTargetBitmap((int)window.ActualWidth,(int)window.ActualHeight,96,96,PixelFormats.Pbgra32);image.Render(window);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(image));using(var stream=File.Create(path))encoder.Save(stream);}
 [STAThread]static int Main(string[] args){int result=0;Directory.CreateDirectory(args[1]);var app=new Application();var desktop=new DesktopApplication(args[0],0);var main=(Window)typeof(DesktopApplication).GetField("window",flags).GetValue(desktop);main.ShowInTaskbar=false;main.ShowActivated=false;main.Left=-20000;main.WindowStartupLocation=WindowStartupLocation.Manual;
 main.Loaded+=async(s,e)=>{try{
  await Task.Delay(200);((System.Windows.Threading.DispatcherTimer)typeof(DesktopApplication).GetField("timer",flags).GetValue(desktop)).Stop();((System.Windows.Threading.DispatcherTimer)typeof(DesktopApplication).GetField("voiceTimer",flags).GetValue(desktop)).Stop();
  var owner=new DiscoveryClient("127.0.0.1",1,""){AccountId=8};Set(desktop,"discovery",owner);Set(desktop,"archiveLoadedOwner",owner);Set(desktop,"authenticatedFriendSnapshotGeneration",1UL);Set(desktop,"historyAccount",8L);Set(desktop,"historyGeneration",1UL);Set(desktop,"historyCommandInFlight",true);
  var assembly=typeof(DesktopApplication).Assembly;var dt=assembly.GetType("PeerChatDesktop.ArchiveEnrollmentExecution+Device");var local=Activator.CreateInstance(dt);var remote=Activator.CreateInstance(dt);string a=new string('a',64),b=new string('b',64);
  foreach(var d in new[]{local,remote}){Set(d,"PeerId",d==local?a:b);foreach(string f in new[]{"Authorized","MayRead","MayServe","GrantedRead","GrantedServe","Automatic"})Set(d,f,true);}Set(local,"Local",true);var rows=Array.CreateInstance(dt,2);rows.SetValue(local,0);rows.SetValue(remote,1);Set(desktop,"historyDevices",rows);new HistoryDeviceNames(args[0]).Set(8,b,"VMTEST");
  var pt=assembly.GetType("PeerChatDesktop.ArchiveEnrollmentExecution+SyncProgress");var p=Activator.CreateInstance(pt);Set(p,"State","History synchronized");Set(p,"Enabled",true);Set(p,"Complete",true);Set(p,"Received",22L);Set(p,"Conversations",3L);Set(p,"LastSuccessful",1791740000000L);Set(p,"Source",b);Set(desktop,"archiveProgress",p);
  Exception failure=null;main.Dispatcher.BeginInvoke((Action)(async()=>{Window settings=null;try{
   settings=main.OwnedWindows.Cast<Window>().Single(w=>w.Title=="Settings");settings.WindowStartupLocation=WindowStartupLocation.Manual;settings.Left=-20000;settings.Top=0;settings.Width=1920;settings.Height=1080;await Task.Delay(100);settings.UpdateLayout();
   bool modern=Walk(settings).OfType<Border>().Any(c=>AutomationProperties.GetName(c)=="History sync status card");Save(settings,Path.Combine(args[1],"history-wide.png"));
   if(modern){var card=Walk(settings).OfType<Border>().Single(c=>AutomationProperties.GetName(c)=="History sync status card");Check(card.ActualWidth<=720&&card.ActualWidth>500,"Compact width not honored");var bar=Walk(settings).OfType<ProgressBar>().Single(c=>AutomationProperties.GetName(c)=="History sync progress");Check(bar.Value==100&&!bar.IsIndeterminate,"Completed bar incorrect");Check(Walk(settings).OfType<TextBlock>().Any(t=>t.Text.Contains("All conversations up to date")),"Complete title missing");
    // Restored terminal state has no in-memory complete flag after a worker restart.
    Set(p,"Complete",false);typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags).Invoke(desktop,null);Check(Walk(settings).OfType<TextBlock>().Any(t=>t.Text.Contains("All conversations up to date")),"Restart terminal state lost");
    Set(p,"State","Connecting to archive");Set(p,"Running",true);Set(p,"Complete",true);Set(p,"Received",0L);typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags).Invoke(desktop,null);settings.UpdateLayout();Check(bar.IsIndeterminate&&bar.Value==0,"Connecting inherited completion");Save(settings,Path.Combine(args[1],"history-connecting.png"));
    Set(p,"Complete",false);Set(p,"State","Synchronizing history");for(long n=1;n<=3;n++){Set(p,"Received",n*4);typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags).Invoke(desktop,null);Check(Walk(settings).OfType<TextBlock>().Any(t=>t.Text.Contains((n*4)+" new messages received")),"Live batch missing");}
    Set(p,"Running",false);Set(p,"State","Archive data rejected; synchronization stopped");typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags).Invoke(desktop,null);Check(Walk(settings).OfType<TextBlock>().Any(t=>t.Text.Contains("Last verified: 12 messages")),"Failure erased committed count");
    Set(p,"Complete",true);Set(p,"State","History synchronized");Set(p,"Received",0L);typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags).Invoke(desktop,null);Check(Walk(settings).OfType<TextBlock>().Any(t=>t.Text=="No new messages — up to date"),"Empty rescan misleading");
    var device=Walk(settings).OfType<Button>().Single(c=>AutomationProperties.GetName(c)=="History device VMTEST");device.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();Check(Walk(settings).OfType<Button>().Any(c=>Object.Equals(c.Content,"Manage")&&c.IsVisible),"Device expansion failed");Save(settings,Path.Combine(args[1],"history-expanded.png"));
   }
   if(modern){Set(remote,"ApprovalKnown",true);Set(remote,"PairAuthorized",false);Set(remote,"MayAuthorize",true);var pending=Array.CreateInstance(dt,2);pending.SetValue(local,0);pending.SetValue(remote,1);Set(desktop,"historyDevices",pending);typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags).Invoke(desktop,null);settings.UpdateLayout();
    var pill=Walk(settings).OfType<Button>().Single(c=>AutomationProperties.GetName(c)=="Authorize history device VMTEST");Check(pill.IsVisible&&pill.Focusable&&pill.IsTabStop,"Authorize keyboard action missing");var row=(DockPanel)pill.Parent;var arrow=row.Children.OfType<TextBlock>().First(t=>t.Text=="›"||t.Text=="⌄");Check(pill.TranslatePoint(new Point(pill.ActualWidth,0),row).X<=arrow.TranslatePoint(new Point(0,0),row).X,"Authorize is not left of chevron");Check(((SolidColorBrush)pill.Background).Color==Color.FromRgb(40,107,193),"Authorize accent changed");Save(settings,Path.Combine(args[1],"history-authorize.png"));
    Set(remote,"MayAuthorize",false);var denied=Array.CreateInstance(dt,2);denied.SetValue(local,0);denied.SetValue(remote,1);Set(desktop,"historyDevices",denied);typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags).Invoke(desktop,null);settings.UpdateLayout();Check(!Walk(settings).OfType<Button>().Any(c=>AutomationProperties.GetName(c)=="Authorize history device VMTEST"),"Ineligible authorizer offered action");Check(Walk(settings).OfType<TextBlock>().Any(t=>t.Text.Contains("Approve from an enabled trusted history device")),"Ineligible next step missing");
   }
   settings.Width=920;settings.Height=780;await Task.Delay(100);settings.UpdateLayout();Save(settings,Path.Combine(args[1],"history-small.png"));if(modern)Check(Walk(settings).OfType<Border>().Single(c=>AutomationProperties.GetName(c)=="History sync status card").ActualWidth<720,"Narrow column did not shrink");
   Console.WriteLine("PASS: "+(modern?"compact rendered layouts, terminal/restart/connecting/failure/no-new/live states and device expansion":"before-change rendered reference")+". UI fixture only; no physical transfer claim.");
  }catch(Exception error){failure=error;}finally{if(settings!=null)settings.Close();}}));
  typeof(DesktopApplication).GetMethod("SettingsPage",flags).Invoke(desktop,new object[]{"Privacy & Security"});if(failure!=null)throw failure;
 }catch(Exception error){Console.Error.WriteLine(error);result=1;}finally{Set(desktop,"discovery",null);main.Close();}};app.Run(main);return result;}
}
