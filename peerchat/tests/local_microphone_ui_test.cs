using System;
using System.Linq;
using System.Reflection;
using System.Runtime.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Threading;
using PeerChatDesktop;
class LocalMicrophoneUiTest {
 static void Check(bool value,string reason){if(!value)throw new Exception(reason);}
 static void Set(object o,string name,object value){typeof(DesktopApplication).GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).SetValue(o,value);}
 static void Open(object o){typeof(DesktopApplication).GetMethod("ShowLocalMicrophoneTest",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(o,null);}
 [STAThread] static int Main(){try{
  var app=new Application{ShutdownMode=ShutdownMode.OnExplicitShutdown};
  using(var core=new CoreAdapter()){
   var state=core.MicrophoneTest(0);Check(!state.recording&&!state.available&&state.frames==0,"Snapshot opened or retained microphone");
   state=core.MicrophoneTest(1,new VoicePreferenceData{InputVolume=201});Check(!state.recording&&!state.available&&!String.IsNullOrEmpty(state.error),"Invalid settings not rejected before device open");
   core.MicrophoneTest(5);state=core.MicrophoneTest(99);Check(!String.IsNullOrEmpty(state.error),"Unknown operation accepted");
   Window owner;using(var stream=typeof(CoreAdapter).Assembly.GetManifestResourceStream("Main.xaml"))owner=(Window)XamlReader.Load(stream);owner.Show();
   var ui=(DesktopApplication)FormatterServices.GetUninitializedObject(typeof(DesktopApplication));Set(ui,"window",owner);Set(ui,"core",core);Set(ui,"model",new ChatViewModel());Set(ui,"voicePreferences",new VoicePreferenceData());
   owner.Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle,new Action(()=>{var dialog=app.Windows.OfType<Window>().Single(w=>w.Title=="Microphone comparison — local only");var panel=(StackPanel)dialog.Content;var controls=panel.Children.OfType<WrapPanel>().Single().Children.OfType<Button>().ToArray();Check(controls.Select(b=>b.Content.ToString()).SequenceEqual(new[]{"Record 8 seconds","Stop recording","Listen raw","Listen processed","Clear"}),"Local comparison controls");Check(!core.MicrophoneTest(0).recording,"Dialog auto-started capture");controls.Last().RaiseEvent(new RoutedEventArgs(Button.ClickEvent));dialog.Close();}));Open(ui);
   Check(!core.MicrophoneTest(0).available,"Dialog close retained samples");
   owner.Dispatcher.BeginInvoke(DispatcherPriority.ApplicationIdle,new Action(()=>Set(ui,"profileSessionGeneration",1UL)));Open(ui);
   Check(!core.MicrophoneTest(0).recording&&!core.MicrophoneTest(0).available,"Profile change retained preview");owner.Close();
  }
  Console.WriteLine("PASS: packaged local-only ABI, no automatic recording, invalid/unknown operation rejection, WPF controls, clear/close/profile invalidation. No microphone opened.");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
