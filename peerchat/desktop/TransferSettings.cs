using System;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
namespace PeerChatDesktop {
public sealed class TransferTuning {
 public int chunkBytes=32768,window=1;public long bitsPerSecond;
 public bool Valid {get{return (chunkBytes==32768||chunkBytes==65536)&&new[]{1,4,8,16}.Contains(window)&&bitsPerSecond>=0&&bitsPerSecond<=10000000000L;}}
 public string Wire {get{if(!Valid)throw new InvalidOperationException("Invalid file transfer settings.");return chunkBytes+","+window+","+bitsPerSecond.ToString(CultureInfo.InvariantCulture);}}
}
public static class TransferTuningEditor {
 public static StackPanel Create(TransferTuning value,Action<TransferTuning> save){
  var panel=new StackPanel{Margin=new Thickness(20)};
  panel.Children.Add(new TextBlock{Text="File transfer settings",FontSize=22});
  panel.Children.Add(new TextBlock{Text="Standard preserves 32 KiB × 1 compatibility. Advanced settings are requests: older peers use Standard. Chunk/window changes apply on the next connection negotiation; speed caps apply to subsequent payload submissions.",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,12,0,12)});
  var preset=new ComboBox{ItemsSource=new[]{"Standard","Fast","High performance","Custom"},Margin=new Thickness(0,4,0,10)};
  var chunk=new ComboBox{ItemsSource=new[]{32768,65536},SelectedItem=value.chunkBytes,Margin=new Thickness(0,4,0,10)};
  var window=new ComboBox{ItemsSource=new[]{1,4,8,16},SelectedItem=value.window,Margin=new Thickness(0,4,0,10)};
  var cap=new TextBox{Text=(value.bitsPerSecond/1000000m).ToString(CultureInfo.CurrentCulture),Margin=new Thickness(0,4,0,10)};
  panel.Children.Add(new TextBlock{Text="Performance preset"});panel.Children.Add(preset);panel.Children.Add(new TextBlock{Text="Chunk size in bytes"});panel.Children.Add(chunk);panel.Children.Add(new TextBlock{Text="Maximum outstanding chunks"});panel.Children.Add(window);panel.Children.Add(new TextBlock{Text="Upload limit in Mbps · 0 = unlimited"});panel.Children.Add(cap);
  bool updating=false;Action match=()=>{if(updating)return;updating=true;int c=(int)chunk.SelectedItem,w=(int)window.SelectedItem;preset.SelectedIndex=c==32768&&w==1?0:c==32768&&w==4?1:c==65536&&w==8?2:3;updating=false;};
  chunk.SelectionChanged+=(s,e)=>match();window.SelectionChanged+=(s,e)=>match();preset.SelectionChanged+=(s,e)=>{if(updating||preset.SelectedIndex==3)return;updating=true;chunk.SelectedItem=preset.SelectedIndex==2?65536:32768;window.SelectedItem=preset.SelectedIndex==0?1:preset.SelectedIndex==1?4:8;updating=false;};match();
  var status=new TextBlock{TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,8,0,8)};panel.Children.Add(status);
  var apply=new Button{Content="Save settings",Padding=new Thickness(12,8,12,8),HorizontalAlignment=HorizontalAlignment.Left};panel.Children.Add(apply);
  apply.Click+=(s,e)=>{decimal mbps;if(!Decimal.TryParse(cap.Text,NumberStyles.Number,CultureInfo.CurrentCulture,out mbps)||mbps<0||mbps>10000){status.Text="Enter an upload limit between 0 and 10000 Mbps.";return;}try{save(new TransferTuning{chunkBytes=(int)chunk.SelectedItem,window=(int)window.SelectedItem,bitsPerSecond=(long)(mbps*1000000)});status.Text="Saved. The aggregate ceiling always applies; peer capabilities determine actual framing.";}catch{status.Text="Could not save protected transfer settings. No success was assumed; retry when local storage is available.";}};
  return panel;
 }
}
public partial class DesktopApplication {
 async void ShowTransferSettings(string id=""){
  var profile=profileSessionGeneration;
  try{var value=await Task.Run(()=>new JavaScriptSerializer().Deserialize<TransferTuning>(core.FileTransfer(12,id:id)));if(profile!=profileSessionGeneration||closing)return;
   var owner=Application.Current.Windows.OfType<Window>().LastOrDefault(w=>w.IsActive)??window;
   var panel=TransferTuningEditor.Create(value,p=>{if(profile!=profileSessionGeneration)throw new InvalidOperationException("Profile changed");core.FileTransfer(13,id:id,path:p.Wire);});
   panel.Children.Insert(1,new TextBlock{Text=String.IsNullOrEmpty(id)?"Profile defaults · upload cap shared by every outgoing transfer":"This transfer only · cannot bypass the aggregate upload ceiling",TextWrapping=TextWrapping.Wrap});
   var dialog=new Window{Title="PeerChat file transfers",Owner=owner,Width=560,Height=580,MinWidth=420,MinHeight=350,Background=Brush("#111920"),Foreground=Brush("#EDF1F5"),Resources=window.Resources,Content=new ScrollViewer{VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Content=panel}};dialog.ShowDialog();
  }catch{if(!closing)Notify("File transfer settings are unavailable in this profile.");}
 }
}
}
