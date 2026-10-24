using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using PeerChatDesktop;
// Real desktop owners and pinned TLS. New profiles must send a cached file
// before any ordinary message or manual trust action has initialized the peer.
class FreshCachedContactTest {
 const BindingFlags Private=BindingFlags.Instance|BindingFlags.NonPublic;
 static object Get(object o,string n){return o.GetType().GetField(n,Private).GetValue(o);}
 static void Set(object o,string n,object v){o.GetType().GetField(n,Private).SetValue(o,v);}
 static object Call(object o,string n,params object[] a){return o.GetType().GetMethod(n,Private).Invoke(o,a);}
 static ChatViewModel Model(DesktopApplication a){return (ChatViewModel)Get(a,"model");}
 static CoreAdapter Core(DesktopApplication a){return (CoreAdapter)Get(a,"core");}
 static Window Window(DesktopApplication a){return (Window)Get(a,"window");}
 static void Check(bool b,string s){if(!b)throw new Exception(s);}
 static async Task Wait(Func<bool> done,string stage,int seconds=60){var end=DateTime.UtcNow.AddSeconds(seconds);while(DateTime.UtcNow<end){if(done())return;await Task.Delay(100);}throw new Exception("Timeout: "+stage);}
 static T Visual<T>(DependencyObject root,Func<T,bool> predicate) where T:DependencyObject {var typed=root as T;if(typed!=null&&predicate(typed))return typed;for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);++i){var found=Visual<T>(VisualTreeHelper.GetChild(root,i),predicate);if(found!=null)return found;}return null;}
 static DesktopApplication Open(string path,ushort port){var app=new DesktopApplication(path,port);Set(app,"discoveryRestored",true);var w=Window(app);w.ShowInTaskbar=false;w.ShowActivated=false;w.Left=-20000;w.WindowStartupLocation=WindowStartupLocation.Manual;w.Show();return app;}
 static async Task<DiscoveryClient> Register(DesktopApplication app,string name,string host,string pin,int port){
  var service=new DiscoveryClient(host,port,pin);string peer=Model(app).OwnId;
  await Task.Run(()=>{service.Register(name,"isolated-fixture-password",peer);service.RefreshSessionInfo();service.ReportPresence(peer,"",0,"",0,false);});
  Set(app,"discovery",service);Set(app,"discoverySessionGeneration",(ulong)Get(app,"discoverySessionGeneration")+1);
  Core(app).SetGroupAccount(service.AccountId,service.Username);Core(app).LoadGroups();
  var handler=(Action<DiscoveryEvent>)Delegate.CreateDelegate(typeof(Action<DiscoveryEvent>),app,typeof(DesktopApplication).GetMethod("HandleDiscoveryEvent",Private));
  service.StartEventChannel(peer,handler);Call(app,"SaveDiscoverySession");Call(app,"RefreshFriendState");return service;
 }
 static async Task<DmAttachmentDeliveryMode> Select(DesktopApplication app){
  bool selected=false;var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(50)};
  timer.Tick+=(s,e)=>{foreach(Window dialog in Window(app).OwnedWindows){if(dialog.Title!="Attachment delivery")continue;var choices=Visual<ComboBox>(dialog,x=>true);var proceed=Visual<Button>(dialog,x=>Equals(x.Content,"Continue"));if(choices==null||proceed==null)continue;choices.SelectedIndex=1;selected=true;timer.Stop();proceed.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));return;}};
  timer.Start();try{var result=await (Task<DmAttachmentDeliveryMode>)Call(app,"SelectDmAttachmentDelivery",Model(app).Selected);Check(selected,"Delivery choice was not exercised");return result;}finally{timer.Stop();}
 }
 static string Selection(string path){using(var stream=new MemoryStream())using(var w=new BinaryWriter(stream,Encoding.UTF8)){w.Write(new byte[]{80,84,1,30});foreach(var field in new[]{Encoding.UTF8.GetBytes("PeerChat/DownloadSelection/v1"),Encoding.UTF8.GetBytes(path),new byte[0]}){w.Write((ulong)field.Length);w.Write(field);}return Convert.ToBase64String(stream.ToArray());}}
 static async Task Send(DesktopApplication from,DesktopApplication to,string root,string label){
  Console.WriteLine("Selecting cached delivery "+label);Check(await Select(from)==DmAttachmentDeliveryMode.EncryptedCache,"Cached choice failed");Console.WriteLine("Selected cached delivery "+label);
  var data=Enumerable.Range(0,131089).Select(i=>(byte)(i*37)).ToArray();string source=Path.Combine(root,label+"-日本.bin"),destination=Path.Combine(root,label+"-received.bin");File.WriteAllBytes(source,data);
  string id=await Task.Run(()=>Core(from).FileTransfer(21,Model(from).Selected.Contact.peer,Model(from).Selected.AccountId.ToString(),source));
  await Wait(()=>Core(to).DmAttachments().items.Any(x=>x.id==id&&!x.sending),"recipient offer "+label);
  Core(to).FileTransfer(22,id:id,path:Selection(destination));
  await Wait(()=>File.Exists(destination)&&Core(to).DmAttachments().items.Any(x=>x.id==id&&x.plaintextVerified),"verified output "+label,120);
  Check(File.ReadAllBytes(destination).SequenceEqual(data),"Plaintext mismatch "+label);
  Console.WriteLine("PASS exact cached attachment "+label);
 }
 static async Task Run(string[] args){
  var first=Open(Path.Combine(args[0],"first"),29541);var second=Open(Path.Combine(args[0],"second"),29542);
  try{
   await Wait(()=>Model(first).Listening&&Model(second).Listening,"native startup");
   var a=await Register(first,"fresh-cache-a",args[1],args[2],Int32.Parse(args[3]));var b=await Register(second,"fresh-cache-b",args[1],args[2],Int32.Parse(args[3]));
   await Task.Run(()=>{Check(a.SendFriendRequest(b.AccountId)=="pending","Friend request");Check(b.RespondToFriendRequest(a.AccountId,true)=="friends","Friend acceptance");});
   Call(first,"RefreshFriendState");Call(second,"RefreshFriendState");
   await Wait(()=>Model(first).Conversations.Values.Any(x=>x.AccountId==b.AccountId)&&Model(second).Conversations.Values.Any(x=>x.AccountId==a.AccountId),"friend projections");
   var af=await Task.Run(()=>a.GetFriendState().Friends.Single());var bf=await Task.Run(()=>b.GetFriendState().Friends.Single());
   Call(first,"OpenFriendConversation",af,false,"fresh cached test");Call(second,"OpenFriendConversation",bf,false,"fresh cached test");
   Check(!Model(first).Selected.Contact.verified&&!Model(second).Selected.Contact.verified,"Fixture already had verified pins");
   Check(Model(first).Selected.Messages.Count==0&&Model(second).Selected.Messages.Count==0,"Fixture had prior messages");
   Console.WriteLine("PASS fresh friendship has no device pins or prior messages");Console.WriteLine("Waiting for cache capabilities");
   for(int attempt=0;attempt<4;++attempt){bool readyA=await (Task<bool>)Call(first,"EnsureDmAttachmentReady",false);bool readyB=await (Task<bool>)Call(second,"EnsureDmAttachmentReady",false);Console.WriteLine("cache setup="+readyA+"/"+readyB+" first="+Get(first,"groupAttachmentCapabilityDiagnostic")+" second="+Get(second,"groupAttachmentCapabilityDiagnostic"));if(readyA&&readyB)break;await Task.Delay(1000);}Check(Core(first).DmAttachments().supported&&Core(second).DmAttachments().supported,"cache capability registration failed");
   await Send(first,second,args[0],"first-to-second");await Send(second,first,args[0],"second-to-first");
   Check(Model(first).Selected.Messages.All(x=>x.Data==null||String.IsNullOrEmpty(x.Data.text)),"Unexpected ordinary message");
  }finally{Window(first).Close();Window(second).Close();}
 }
 [STAThread] static int Main(string[] args){int exit=0;var application=new Application{ShutdownMode=ShutdownMode.OnExplicitShutdown};var watchdog=new DispatcherTimer{Interval=TimeSpan.FromSeconds(15)};int ticks=0;watchdog.Tick+=(s,e)=>{Console.WriteLine("live stage tick="+(++ticks));foreach(Window w in application.Windows){Console.WriteLine("window="+w.Title);var status=Visual<TextBlock>(w,x=>x.Text!=null&&(x.Text.Contains("queued")||x.Text.Contains("Verify")||x.Text.Contains("attachment")));if(status!=null)Console.WriteLine("status="+status.Text);}if(ticks>=16){Console.Error.WriteLine("Fresh contact fixture timed out");Environment.Exit(2);}};watchdog.Start();application.Dispatcher.BeginInvoke(new Action(async()=>{try{await Run(args);Console.WriteLine("PASS fresh-contact cached-first delivery in both directions");}catch(Exception e){exit=1;Console.Error.WriteLine(e);}finally{application.Shutdown();}}));application.Run();Environment.Exit(exit);return exit;}
}
