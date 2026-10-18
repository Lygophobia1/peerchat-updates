using System;
using System.Globalization;
using System.Linq;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
namespace PeerChatDesktop {
public sealed class TransferTuning {
 // New-profile defaults explicitly authorized by the client-integration milestone.
 public int chunkBytes=65536,window=16;public long bitsPerSecond,downloadBitsPerSecond;
 public bool Valid {get{return (chunkBytes==32768||chunkBytes==65536)&&new[]{1,4,8,16}.Contains(window)&&bitsPerSecond>=0&&bitsPerSecond<=10000000000L&&downloadBitsPerSecond>=0&&downloadBitsPerSecond<=10000000000L;}}
 public string Wire {get{if(!Valid)throw new InvalidOperationException("Invalid file transfer settings.");return chunkBytes+","+window+","+bitsPerSecond.ToString(CultureInfo.InvariantCulture)+","+downloadBitsPerSecond.ToString(CultureInfo.InvariantCulture);}}
}
public static class TransferTuningEditor {
 public static StackPanel Create(TransferTuning value,Action<TransferTuning> save,bool profileDefaults=true){
  var panel=new StackPanel{Margin=new Thickness(20)};
  panel.Children.Add(new TextBlock{Text="File transfer settings",FontSize=22});
  panel.Children.Add(new TextBlock{Text="High performance requests 64 KiB × 16. Older peers negotiate Standard safely. Existing custom settings are preserved. Speed limits are aggregate ceilings in Mbps (not MB/s); 0 means unlimited. Receiver limits pace durable ACK credit within the bounded negotiated window.",TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,12,0,12)});
  var preset=new ComboBox{ItemsSource=new[]{"Standard","Fast","High performance","Custom"},Margin=new Thickness(0,4,0,10)};
  var chunk=new ComboBox{ItemsSource=new[]{32768,65536},SelectedItem=value.chunkBytes,Margin=new Thickness(0,4,0,10)};
  var window=new ComboBox{ItemsSource=new[]{1,4,8,16},SelectedItem=value.window,Margin=new Thickness(0,4,0,10)};
  var cap=new TextBox{Text=(value.bitsPerSecond/1000000m).ToString(CultureInfo.CurrentCulture),Margin=new Thickness(0,4,0,10)};
  var download=new TextBox{Text=(value.downloadBitsPerSecond/1000000m).ToString(CultureInfo.CurrentCulture),IsEnabled=profileDefaults,Margin=new Thickness(0,4,0,10)};
  panel.Children.Add(new TextBlock{Text="Performance preset"});panel.Children.Add(preset);panel.Children.Add(new TextBlock{Text="Chunk size in bytes"});panel.Children.Add(chunk);panel.Children.Add(new TextBlock{Text="Maximum outstanding chunks"});panel.Children.Add(window);panel.Children.Add(new TextBlock{Text="Upload limit in Mbps · 0 = unlimited"});panel.Children.Add(cap);
  panel.Children.Add(new TextBlock{Text=profileDefaults?"Download limit in Mbps · 0 = unlimited":"Download ceiling is controlled by profile defaults"});panel.Children.Add(download);
  bool updating=false;Action match=()=>{if(updating)return;updating=true;int c=(int)chunk.SelectedItem,w=(int)window.SelectedItem;preset.SelectedIndex=c==32768&&w==1?0:c==32768&&w==4?1:c==65536&&w==16?2:3;updating=false;};
  chunk.SelectionChanged+=(s,e)=>match();window.SelectionChanged+=(s,e)=>match();preset.SelectionChanged+=(s,e)=>{if(updating||preset.SelectedIndex==3)return;updating=true;chunk.SelectedItem=preset.SelectedIndex==2?65536:32768;window.SelectedItem=preset.SelectedIndex==0?1:preset.SelectedIndex==1?4:16;updating=false;};match();
  var status=new TextBlock{TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,8,0,8)};panel.Children.Add(status);
  var apply=new Button{Content="Save settings",Padding=new Thickness(12,8,12,8),HorizontalAlignment=HorizontalAlignment.Left};panel.Children.Add(apply);
  apply.Click+=(s,e)=>{decimal mbps,down;if(!Decimal.TryParse(cap.Text,NumberStyles.Number,CultureInfo.CurrentCulture,out mbps)||mbps<0||mbps>10000||!Decimal.TryParse(download.Text,NumberStyles.Number,CultureInfo.CurrentCulture,out down)||down<0||down>10000){status.Text="Enter speed limits between 0 and 10000 Mbps.";return;}try{save(new TransferTuning{chunkBytes=(int)chunk.SelectedItem,window=(int)window.SelectedItem,bitsPerSecond=(long)(mbps*1000000),downloadBitsPerSecond=(long)(down*1000000)});status.Text="Saved. Aggregate ceilings and receiver backpressure apply; peer capabilities determine actual framing.";}catch{status.Text="Could not save all protected settings. Reopen to check retained values, then retry when storage is available.";}};
  return panel;
 }
}
public partial class DesktopApplication {
 async void ShowTransferSettings(string id=""){
  var profile=profileSessionGeneration;
  try{var value=await Task.Run(()=>new JavaScriptSerializer().Deserialize<TransferTuning>(core.FileTransfer(12,id:id)));if(profile!=profileSessionGeneration||closing)return;
   var owner=Application.Current.Windows.OfType<Window>().LastOrDefault(w=>w.IsActive)??window;
   var panel=TransferTuningEditor.Create(value,p=>{if(profile!=profileSessionGeneration)throw new InvalidOperationException("Profile changed");core.FileTransfer(13,id:id,path:p.Wire);},String.IsNullOrEmpty(id));
   panel.Children.Insert(1,new TextBlock{Text=String.IsNullOrEmpty(id)?"Profile defaults · upload cap shared by every outgoing transfer":"This transfer only · cannot bypass the aggregate upload ceiling",TextWrapping=TextWrapping.Wrap});
   if(String.IsNullOrEmpty(id)){
    var preference=new AttachmentSendPreferenceStore(directory);
    var sending=new StackPanel{Margin=new Thickness(0,12,0,16)};
    sending.Children.Add(new TextBlock{Text="Default DM attachment sending mode"});
    var mode=new ComboBox{ItemsSource=new[]{"Direct Transfer","Cached Transfer"},SelectedIndex=preference.Load()==DmAttachmentDeliveryMode.EncryptedCache?1:0,Margin=new Thickness(0,6,0,6)};sending.Children.Add(mode);
    sending.Children.Add(new TextBlock{Text="Direct sends to the selected verified online device (including over relay). Cached sends to the authorized account audience for offline retrieval. You can override this choice for each send. Receiving both stays automatic. Group attachments continue using the encrypted cache.",TextWrapping=TextWrapping.Wrap});
    var result=new TextBlock{TextWrapping=TextWrapping.Wrap};
    var saveMode=new Button{Content="Save sending preference",HorizontalAlignment=HorizontalAlignment.Left,Margin=new Thickness(0,8,0,4)};
    saveMode.Click+=(s,e)=>{try{if(profile!=profileSessionGeneration||closing)throw new InvalidOperationException();preference.Save(mode.SelectedIndex==1?DmAttachmentDeliveryMode.EncryptedCache:DmAttachmentDeliveryMode.OnlineDevice);result.Text="Sending preference saved for this profile.";}catch{result.Text="Could not save the sending preference. Your existing preference is retained.";}};
    sending.Children.Add(saveMode);sending.Children.Add(result);panel.Children.Insert(2,sending);
   }
   var dialog=new Window{Title="PeerChat file transfers",Owner=owner,Width=560,Height=580,MinWidth=420,MinHeight=350,Background=Brush("#111920"),Foreground=Brush("#EDF1F5"),Resources=window.Resources,Content=new ScrollViewer{VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Content=panel}};dialog.ShowDialog();
  }catch{if(!closing)Notify("File transfer settings are unavailable in this profile.");}
 }
}
}
