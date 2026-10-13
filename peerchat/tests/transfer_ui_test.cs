using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using PeerChatDesktop;
class TransferUiTest {
    static void Require(bool b,string message){if(!b)throw new Exception(message);}
    static bool HasText(DependencyObject root,string text){
        var label=root as TextBlock;if(label!=null&&label.Text.Contains(text))return true;
        var button=root as Button;if(button!=null&&Convert.ToString(button.Content).Contains(text))return true;
        for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);++i)if(HasText(VisualTreeHelper.GetChild(root,i),text))return true;
        return false;
    }
    [STAThread] static int Main(string[] args){
        var measured=new CoreAdapter.FileTransferData{state=3,size=100000,done=50000,statisticsKnown=true,statisticsBytes=50000,durationMs=2000,rateBits=80000,etaReliable=true};
        string stats=ChatAttachmentProjection.Statistics(measured);Require(stats.Contains("Current 0.08 Mbps")&&stats.Contains("Average 0.20 Mbps")&&stats.Contains("ETA 5 s"),"Acknowledged byte statistics formatting");
        measured.rateBits=0;measured.etaReliable=false;Require(!ChatAttachmentProjection.Statistics(measured).Contains("ETA"),"Stall ETA must be hidden");
        measured.state=4;measured.done=measured.statisticsBytes=100000;stats=ChatAttachmentProjection.Statistics(measured);Require(stats.Contains("Average 0.40 Mbps")&&!stats.Contains("Current"),"Completed summary retained without live rate");
        int result=1;string profile=Path.Combine(args[0],"profile");Directory.CreateDirectory(profile);
        var app=new Application();var desktop=new DesktopApplication(profile,0);
        var field=typeof(DesktopApplication).GetField("window",BindingFlags.Instance|BindingFlags.NonPublic);
        var window=(Window)field.GetValue(desktop);window.ShowInTaskbar=false;window.ShowActivated=false;window.Left=-20000;
        window.Loaded+=(s,e)=>{
            try {
                var attach=(Button)window.FindName("Attachment");Require(attach!=null,"Attachment control missing");
                Require(attach.IsEnabled&&attach.IsHitTestVisible&&attach.Focusable,
                    "Paperclip cannot be activated by a user");
                Require(Convert.ToString(attach.ToolTip)=="Attach file to this conversation","Paperclip must target conversation");
                var scroll=(ScrollViewer)window.FindName("MessageScroll");Require(((Grid)scroll.Parent).AllowDrop,"Empty-conversation overlay hides drop target");
                Require(((TextBox)window.FindName("Composer")).MaxLength==0,"Oversized paste silently truncated by composer");
                typeof(DesktopApplication).GetMethod("ShowFileTransfers",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(desktop,null);
                typeof(DesktopApplication).GetMethod("ShowFileTransfers",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(desktop,null);
                var timer=new DispatcherTimer{Interval=TimeSpan.FromSeconds(2)};
                timer.Tick+=(sender,unused)=>{
                    timer.Stop();
                    try{
                        var transfers=window.OwnedWindows.Cast<Window>().Where(item=>item.Title=="PeerChat — File transfers").ToArray();
                        Require(transfers.Length==1,"Attachment opened duplicate transfer views");
                        var view=transfers[0];view.Width=420;view.Height=320;view.UpdateLayout();
                        Require(HasText(view,"Live encrypted transfer")&&HasText(view,"256 MiB")&&HasText(view,"Send file"),"Transfer controls/limits missing");
                        Require(HasText(view,"No file transfers"),"Native empty snapshot did not project");
                        var color=(view.Background as SolidColorBrush).Color;Require(color.R<40&&color.G<40,"Dark theme not preserved");
                        view.Close();result=0;Console.WriteLine("PASS: Conversation paperclip target and one secondary dark transfer view, native empty snapshot, limits/send action, minimum layout, close.");
                    }catch(Exception error){Console.Error.WriteLine(error);}
                    window.Close();
                };timer.Start();
            }catch(Exception error){Console.Error.WriteLine(error);window.Close();}
        };
        app.Run(window);return result;
    }
}
