using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Reflection;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Automation;
using PeerChatDesktop;

// Real WPF handlers + native profile workers/signatures. Only authenticated
// server RPC metadata transport is an isolated fixture; no production TLS bypass.
class HistoryFirstEnrollmentUiTest {
 static BindingFlags F=BindingFlags.Instance|BindingFlags.NonPublic;
 static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
 static object Get(object o,string name){return o.GetType().GetField(name,F).GetValue(o);}
 static void Set(object o,string name,object value){o.GetType().GetField(name,F).SetValue(o,value);}
 static IEnumerable<DependencyObject> Walk(DependencyObject o){for(int i=0;i<VisualTreeHelper.GetChildrenCount(o);i++){var child=VisualTreeHelper.GetChild(o,i);yield return child;foreach(var n in Walk(child))yield return n;}}
 static Button Button(Window w,string label){return Walk(w).OfType<Button>().SingleOrDefault(b=>AutomationProperties.GetName(b)==label||Object.Equals(b.Content,label));}
 static void Pill(Window w,Button button,string image){w.UpdateLayout();Check(button.IsVisible&&button.Focusable&&button.IsTabStop,"Pill is not visible/keyboard reachable");var row=(DockPanel)button.Parent;var arrow=row.Children.OfType<TextBlock>().Single(t=>t.Text=="›"||t.Text=="⌄");Check(button.TranslatePoint(new Point(button.ActualWidth,0),row).X<=arrow.TranslatePoint(new Point(0,0),row).X,"Pill not immediately left of chevron");Check(((SolidColorBrush)button.Background).Color==Color.FromRgb(40,107,193),"Pill not blue");var bitmap=new RenderTargetBitmap((int)w.ActualWidth,(int)w.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(w);var png=new PngBitmapEncoder();png.Frames.Add(BitmapFrame.Create(bitmap));using(var file=File.Create(image))png.Save(file);}
 static async Task Until(Func<bool> ready,string reason){for(int i=0;i<200;i++){if(ready())return;await Task.Delay(25);}throw new Exception(reason);}
 static byte[] Command(uint op){using(var s=new MemoryStream())using(var w=new BinaryWriter(s)){w.Write(new byte[]{80,84,1,31});var b=Encoding.UTF8.GetBytes("PeerChat/ArchiveApplication/v1");w.Write((long)b.Length);w.Write(b);w.Write(1L);w.Write((long)op);return s.ToArray();}}
 static string Text(BinaryReader r){long n=r.ReadInt64();Check(n>=0&&n<=2048,"Fixture bound");return Encoding.UTF8.GetString(r.ReadBytes((int)n));}
 static byte[] Payload(byte[] wire){using(var r=new BinaryReader(new MemoryStream(wire))){r.ReadBytes(4);long n=r.ReadInt64();Check(n>0&&n<2048,"Fixture payload bound");return r.ReadBytes((int)n);}}
 static string Device(byte[] descriptor){using(var r=new BinaryReader(new MemoryStream(Payload(descriptor)))){r.ReadBytes(4);Text(r);r.ReadInt64();Check(r.ReadInt64()==8,"Fixture account");return Text(r);}}
 sealed class ServerFixture {
  readonly object gate=new object();public readonly Dictionary<string,byte[]> Descriptors=new Dictionary<string,byte[]>(),Grants=new Dictionary<string,byte[]>();public bool FailPublication;
  public Dictionary<string,object> Rpc(string op,Dictionary<string,object> fields){lock(gate){var reply=new Dictionary<string,object>{{"ok",true},{"archiveVersion",1}};
   if(op=="archive_descriptor_register"){var proof=Convert.FromBase64String((string)fields["proof"]);Descriptors[Device(proof)]=proof;reply["state"]="accepted";}
   else if(op=="archive_grant_register"){if(FailPublication)throw new IOException("Test-only publication interruption");var proof=Convert.FromBase64String((string)fields["proof"]);using(var r=new BinaryReader(new MemoryStream(Payload(proof)))){r.ReadBytes(4);Text(r);r.ReadInt64();Check(r.ReadInt64()==8,"Fixture grant account");string issuer=Text(r),device=Text(r);Grants[issuer+"/"+device]=proof;}reply["state"]="accepted";}
   else {Check(op=="archive_authorization_state","Unexpected fixture operation");reply["accountId"]=8;reply["serverEpoch"]="isolated-first-enrollment-ui";reply["devices"]=Descriptors.Values.Select(d=>new Dictionary<string,object>{{"descriptor",Convert.ToBase64String(d)}}).ToArray();reply["grants"]=Grants.Values.Select(g=>new Dictionary<string,object>{{"grant",Convert.ToBase64String(g)},{"currentlyEligible",true}}).ToArray();}return reply;
  }}
 }
 static void Respond(Window main,string title,string accept){main.Dispatcher.BeginInvoke((Action)(async()=>{await Until(()=>main.OwnedWindows.Cast<Window>().Any(w=>w.Title==title),"Confirmation missing: "+title);var w=main.OwnedWindows.Cast<Window>().Single(x=>x.Title==title);w.UpdateLayout();Button(w,accept).RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));}));}
 static void StopTimers(DesktopApplication app){((System.Windows.Threading.DispatcherTimer)Get(app,"timer")).Stop();((System.Windows.Threading.DispatcherTimer)Get(app,"voiceTimer")).Stop();}
 static Window Panel(DesktopApplication app,DiscoveryClient owner,ServerFixture server){var main=(Window)Get(app,"window");var core=(CoreAdapter)Get(app,"core");var w=new Window{Owner=main,Width=900,Height=850,ShowInTaskbar=false,ShowActivated=false,Left=-20000,WindowStartupLocation=WindowStartupLocation.Manual};
  Set(app,"discovery",owner);Set(app,"authenticatedFriendSnapshotGeneration",1UL);Set(app,"mailboxContextAccount",8L);
  var factory=new Func<DiscoveryClient,string,ArchiveEnrollmentExecution>((d,p)=>new ArchiveEnrollmentExecution((op,input)=>core.Archive(op,input),server.Rpc));
  var content=(StackPanel)typeof(DesktopApplication).GetMethod("BuildHistorySettingsPanel",F).Invoke(app,new object[]{w,factory});w.Content=new ScrollViewer{Content=content};w.Show();return w;
 }
 [STAThread]public static int Main(string[] args){int result=0;var application=new Application();var a=new DesktopApplication(Path.Combine(args[0],"first"),0);var ma=(Window)Get(a,"window");ma.Left=-20000;ma.ShowInTaskbar=false;ma.ShowActivated=false;ma.WindowStartupLocation=WindowStartupLocation.Manual;
  ma.Loaded+=async(s,e)=>{DesktopApplication b=null;Window wa=null,wb=null,mb=null;try{
   await Task.Delay(900);StopTimers(a);b=new DesktopApplication(Path.Combine(args[0],"second"),0);mb=(Window)Get(b,"window");mb.Left=-20000;mb.ShowInTaskbar=false;mb.ShowActivated=false;mb.Show();await Task.Delay(500);StopTimers(b);
   var ca=(CoreAdapter)Get(a,"core");var cb=(CoreAdapter)Get(b,"core");Check(ca.SetAuthenticatedFriends(8,1,new FriendConversationBinding[0])&&cb.SetAuthenticatedFriends(8,1,new FriendConversationBinding[0]),"Native authenticated contexts");
   ca.SetNetworkContext(1,"isolated-enrollment-ui");cb.SetNetworkContext(1,"isolated-enrollment-ui");
   var da=ca.Archive(1,Command(1));var db=cb.Archive(1,Command(1));string pa=Device(da),pb=Device(db);((ChatViewModel)Get(a,"model")).OwnId=pa;((ChatViewModel)Get(b,"model")).OwnId=pb;
   var server=new ServerFixture();server.Descriptors[pa]=da;server.Descriptors[pb]=db;var oa=new DiscoveryClient("127.0.0.1",1,""){AccountId=8};var ob=new DiscoveryClient("127.0.0.1",1,""){AccountId=8};
   wa=Panel(a,oa,server);await Until(()=>Button(wa,"Set up history device This PC")!=null,"First-use pill missing");Check(Button(wa,"Set up History Sync")!=null,"Prominent Set up missing");var setup=Button(wa,"Set up history device This PC");Check(setup.Focusable&&setup.IsTabStop,"Setup keyboard access");
   Pill(wa,setup,Path.Combine(args[0],"first-use-set-up.png"));
   Respond(ma,"Set up History Sync","Cancel");setup.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));await Task.Delay(100);Check(server.Grants.Count==0&&!((ArchiveEnrollmentExecution.Device[])Get(a,"historyDevices")).Single(d=>d.Local).ApprovedIssuer,"Cancel enrolled device");
   server.FailPublication=true;Respond(ma,"Set up History Sync","Set up");setup.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));await Until(()=>!(bool)Get(a,"historyCommandInFlight"),"Failed setup did not finish");Check(server.Grants.Count==0,"Failed publish claimed registered grant");
   server.FailPublication=false; // Existing durable signed proof is retried by the normal publication path, not reissued.
   var ea=new ArchiveEnrollmentExecution((op,input)=>ca.Archive(op,input),server.Rpc);Check(ea.PublishPending(8,1,pa)==1,"Setup proof lost on interruption");wa.Close();wa=Panel(a,oa,server);
   await Until(()=>((ArchiveEnrollmentExecution.Device[])Get(a,"historyDevices")).Any(d=>d.Local&&d.LocalGrantRead&&d.LocalGrantServe),"Setup did not verify ON/ON");Check(server.Grants.Count==1,"Setup duplicate grant");
   string targetName="New device "+pb.Substring(0,8);var rows=(ArchiveEnrollmentExecution.Device[])Get(a,"historyDevices");var remote=rows.Single(d=>d.PeerId==pb);Check(remote.MayAuthorize&&remote.NeedsApproval,"Second device not authorizable");
   var authorize=Walk(wa).OfType<Button>().Single(x=>Object.Equals(x.Content,"Authorize"));Check(authorize.IsVisible,"Authorize hidden in Advanced");
   Pill(wa,authorize,Path.Combine(args[0],"second-device-authorize.png"));
   Exception modal=null;ma.Dispatcher.BeginInvoke((Action)(async()=>{try{await Until(()=>wa.OwnedWindows.Cast<Window>().Any(x=>x.Title=="New history device"),"Authorization dialog missing");var focus=wa.OwnedWindows.Cast<Window>().Single(x=>x.Title=="New history device");var body=(StackPanel)((ScrollViewer)focus.Content).Content;var advanced=body.Children.OfType<Expander>().Single();var switches=((StackPanel)advanced.Content).Children.OfType<CheckBox>().ToArray();Check(switches[0].IsChecked==true&&switches[1].IsChecked==true,"Normal grant not ON/ON");Respond(ma,"Confirm history device access","Confirm");focus.UpdateLayout();Button(focus,"Authorize device").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));}catch(Exception error){modal=error;}}));
   authorize.RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));if(modal!=null)throw modal;await Until(()=>server.Grants.Count==2&&!(bool)Get(a,"historyCommandInFlight"),"UI did not issue signed second-device grant");Check(!Walk(wa).OfType<Button>().Any(x=>Object.Equals(x.Content,"Authorize")),"Approved row retained Authorize");remote=((ArchiveEnrollmentExecution.Device[])Get(a,"historyDevices")).Single(d=>d.PeerId==pb);Check(remote.PairAuthorized&&remote.LocalGrantRead&&remote.LocalGrantServe,"Actual grant permissions wrong");
   wb=Panel(b,ob,server);await Until(()=>Button(wb,"Approve history device This PC")!=null,"Received authorization confirmation hidden");Check(Button(wb,"Set up history device This PC")==null,"Already-enrolled account offered bootstrap");Respond(mb,"Approve History Sync","Approve");Button(wb,"Approve history device This PC").RaiseEvent(new RoutedEventArgs(System.Windows.Controls.Button.ClickEvent));await Until(()=>!(bool)Get(b,"historyCommandInFlight")&&((ArchiveEnrollmentExecution.Device[])Get(b,"historyDevices")).Any(d=>d.Local&&d.GrantedRead&&d.GrantedServe),"Recipient approval not verified");
   var eb=new ArchiveEnrollmentExecution((op,input)=>cb.Archive(op,input),server.Rpc);Check(eb.Progress(8,1).Running,"Eligible automatic sync did not start");Check(server.Grants.Count==2,"Recipient approval silently issued another grant");
   ea.Refresh(8,1,pa);ea.Consent(8,1,7,pa,0,true);wa.Close();wa=Panel(a,oa,server);await Until(()=>!(bool)Get(a,"historyCommandInFlight"),"Disabled state refresh");Check(!Walk(wa).OfType<Button>().Any(x=>Object.Equals(x.Content,"Authorize")||Object.Equals(x.Content,"Set up")),"Disabled authorizer silently reenrolled");
   Console.WriteLine("PASS: real first-use WPF Set up/Cancel/Confirm handlers, native signed ON/ON self enrollment, interrupted durable publication retry, visible Authorize second-device grant, Approved projection, explicit recipient authorizer confirmation and eligible automatic sync. Isolated authenticated RPC fixture; no physical acceptance claim.");
  }catch(Exception error){Console.Error.WriteLine(error);result=1;}finally{Set(a,"discovery",null);if(b!=null)Set(b,"discovery",null);if(wa!=null)wa.Close();if(wb!=null)wb.Close();if(mb!=null)mb.Close();ma.Close();}};application.Run(ma);return result;
 }
}
