using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using PeerChatDesktop;
class TransferTuningUiTest {
 static void Check(bool b,string text){if(!b)throw new Exception(text);}
 [STAThread] static int Main(){try{
  new Application{ShutdownMode=ShutdownMode.OnExplicitShutdown};TransferTuning saved=null;
  var original=new TransferTuning{chunkBytes=65536,window=8,bitsPerSecond=1250000,downloadBitsPerSecond=5000000};
  var panel=TransferTuningEditor.Create(original,x=>saved=x);var combos=panel.Children.OfType<ComboBox>().ToArray();var caps=panel.Children.OfType<TextBox>().ToArray();var cap=caps[0];var button=panel.Children.OfType<Button>().Single();
  Check(combos.Length==3&&combos[0].SelectedIndex==3,"Custom initialization reset");Check(saved==null,"Opening saved settings");
  button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(saved.Wire=="65536,8,1250000,5000000","Custom values changed");
  combos[0].SelectedIndex=0;button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(saved.chunkBytes==32768&&saved.window==1,"Standard preset");
  combos[0].SelectedIndex=1;button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(saved.chunkBytes==32768&&saved.window==4,"Fast preset");
  combos[0].SelectedIndex=2;button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(saved.chunkBytes==65536&&saved.window==16,"High preset");
  Check(saved.downloadBitsPerSecond==5000000,"Changing framing reset download cap");
  Check(new TransferTuning().Wire=="65536,16,0,0","New profile defaults are not high-performance unlimited");
  var prior=saved;cap.Text="-1";button.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(Object.ReferenceEquals(saved,prior),"Invalid cap saved");
  Check(!new TransferTuning{window=3}.Valid&&!new TransferTuning{chunkBytes=131072}.Valid,"Unsupported tuning accepted");
  Console.WriteLine("PASS tuning UI: preserved custom values, no auto-save, Standard/Fast/High, invalid cap/size/window, stable wire representation");return 0;
 }catch(Exception e){Console.Error.WriteLine(e);return 1;}}
}
