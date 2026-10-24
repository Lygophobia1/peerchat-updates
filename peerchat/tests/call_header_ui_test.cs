using System;using System.IO;using System.Reflection;using System.Windows;using System.Windows.Controls;using System.Windows.Media;using System.Windows.Media.Imaging;using PeerChatDesktop;
class CallHeaderTest {
static BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic;
static void Check(bool ok,string why){if(!ok)throw new Exception(why);}
static void Set(object o,string field,object value){o.GetType().GetField(field,flags).SetValue(o,value);}
static object Get(object o,string field){return o.GetType().GetField(field,flags).GetValue(o);}
static void Invoke(object o,string method){o.GetType().GetMethod(method,flags).Invoke(o,null);}
[STAThread]static int Main(string[] args){int result=1;var app=new Application();var desktop=new DesktopApplication(Path.Combine(args[0],"isolated-call-header-profile"),0);var w=(Window)Get(desktop,"window");w.ShowInTaskbar=false;w.ShowActivated=false;w.Left=-20000;
w.Loaded+=async(sender,eventArgs)=>{try{
await (System.Threading.Tasks.Task)Get(desktop,"startupTask");
var started=System.Diagnostics.Stopwatch.StartNew();while((bool)Get(desktop,"busy")){Check(started.ElapsedMilliseconds<15000,"Startup did not finish");await System.Threading.Tasks.Task.Delay(20);}
var m=(ChatViewModel)Get(desktop,"model");Action refresh=()=>{Invoke(desktop,"RefreshVoiceUi");w.UpdateLayout();};
// Idle phone with a saved account identity must permit preparation before TLS
// verification, while offline/unfriended/legacy unverified contacts stay gated.
var savedSelected=m.Selected;var savedListening=m.Listening;var savedGroups=m.Groups;
m.Listening=true;m.Groups=false;m.VoiceStateCode=0;
m.Selected=new ConversationModel{AccountId=42,IsFriend=true,Contact=new ContactData{peer=new string('b',64),verified=false}};
Check(m.CanPrepareDirectCall(true,false),"Idle friend cannot start wake/preparation");
Check(!m.CanPrepareDirectCall(false,false)&&!m.CanPrepareDirectCall(true,true),"Signed-out/busy preparation allowed");
m.Selected.IsFriend=false;Check(!m.CanPrepareDirectCall(true,false),"Removed friend could prepare call");m.Selected.IsFriend=true;
m.VoiceStateCode=5;Check(!m.CanPrepareDirectCall(true,false),"Second active call permitted");m.VoiceStateCode=0;
var mockDiscovery=new DiscoveryClient("127.0.0.1",1,new string('a',64));mockDiscovery.SessionToken="test-only-not-transmitted";
Set(desktop,"discovery",mockDiscovery);Invoke(desktop,"Refresh");
Check(((Button)w.FindName("VoiceCall")).IsEnabled,"Real Call button still requires live phone/event connection");Set(desktop,"discovery",null);
m.Selected.AccountId=0;Check(!m.CanPrepareDirectCall(true,false),"Unverified legacy peer could prepare call");
m.Selected=savedSelected;m.Listening=savedListening;m.Groups=savedGroups;
Func<string,string> text=n=>((TextBlock)w.FindName(n)).Text;
Set(desktop,"callHeaderName","JonahPhone");Set(desktop,"voiceStartPending",true);refresh();
Check(((Border)w.FindName("VoiceCard")).Visibility==Visibility.Visible&&text("VoiceState")=="Preparing…","Preparation must be immediately visible");
Check(((TextBlock)w.FindName("VoiceDuration")).Visibility==Visibility.Collapsed,"No preconnection timer");
Invoke(desktop,"EndVoiceCall");Check((bool)Get(desktop,"voicePreparationCancelled"),"Cancel must invalidate preparation");Set(desktop,"voiceStartPending",false);Set(desktop,"voicePreparationCancelled",false);
Set(desktop,"pendingHeaderCall","test-only");refresh();Check(text("VoiceState")=="Calling…","No gap before native event");Set(desktop,"pendingHeaderCall","");
m.VoiceStateCode=1;refresh();Check(text("VoiceState")=="Calling…","Outgoing state");
m.VoiceStateCode=3;refresh();Check(text("VoiceState")=="Answered · Connecting…","Negotiating must not show ringing");
m.VoiceStateCode=2;refresh();Check(((Button)w.FindName("VoiceAccept")).Visibility==Visibility.Visible,"Incoming accept control");
m.VoiceStateCode=5;m.VoiceMediaState=2;refresh();Check(text("VoiceState")=="Connected","Connected state");Check(((Button)w.FindName("VoiceHangUp")).ActualHeight==((Button)w.FindName("VoiceMute")).ActualHeight,"Proportional end button");
Set(desktop,"callHeaderCollapsed",true);refresh();Check(((StackPanel)w.FindName("VoiceExpanded")).Visibility==Visibility.Collapsed&&((Button)w.FindName("VoiceDetails")).IsVisible,"Collapsed diagnostics remain available");
Set(desktop,"callHeaderCollapsed",false);m.VoiceStateCode=0;m.VoiceMediaState=0;Set(desktop,"callHeaderFailure","Couldn’t connect");refresh();Check(text("VoiceState")=="Couldn’t connect","Failure retained");
Set(desktop,"callHeaderFailure","");var group=Get(desktop,"groupVoice");var ctor=typeof(GroupVoiceRoomStatus).GetConstructors(flags)[0];Set(group,"status",ctor.Invoke(new object[]{"test-group","test-room","In voice · alone","",true,false,false}));refresh();Check(text("VoiceState")=="Waiting for participants","Alone is not connected");Check((string)((Button)w.FindName("VoiceHangUp")).Content=="Leave call","Unified group leave");
Set(group,"status",ctor.Invoke(new object[]{"test-group","test-room","In voice · connected","",true,false,false}));refresh();Check(text("VoiceState")=="Connected","Group connected");
Set(group,"status",ctor.Invoke(new object[]{"","","Not joined","",false,false,false}));Set(desktop,"voiceStartPending",true);refresh();
w.Width=1120;w.Height=740;w.UpdateLayout();var bitmap=new RenderTargetBitmap((int)w.ActualWidth,(int)w.ActualHeight,96,96,PixelFormats.Pbgra32);bitmap.Render(w);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(bitmap));using(var stream=File.Create(Path.Combine(args[0],"call-header-render.png")))encoder.Save(stream);
Set(desktop,"voiceStartPending",false);m.VoiceStateCode=0;refresh();Console.WriteLine("PASS: preparation, cancellation, pending offer, outgoing, answer, incoming, connected, proportional controls, collapse/diagnostics, failure, group waiting/connected, minimum-window render");result=0;
}catch(Exception ex){Console.Error.WriteLine(ex);}finally{w.Close();app.Shutdown();}};app.Run(w);return result;}
}
