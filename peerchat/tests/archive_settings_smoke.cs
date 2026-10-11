using System;
using System.Linq;
using System.Reflection;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Automation;
using System.Windows.Media;
using PeerChatDesktop;

static class ArchiveSettingsSmoke {
    static void Check(bool value){if(!value)throw new Exception("Archive Settings smoke failed");}
    static FrameworkElement Find(DependencyObject root,string name){
        for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);i++){
            var child=VisualTreeHelper.GetChild(root,i);var item=child as FrameworkElement;
            if(item!=null&&(AutomationProperties.GetName(item)==name||(item is Button&&Object.Equals(((Button)item).Content,name))))return item;
            var found=Find(child,name);if(found!=null)return found;
        }return null;
    }
    [STAThread] public static int Main(string[] args){
        int result=0;var app=new Application();var desktop=new DesktopApplication(args[0],0);
        var window=(Window)typeof(DesktopApplication).GetField("window",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);
        window.ShowInTaskbar=false;window.ShowActivated=false;window.Left=-20000;window.WindowStartupLocation=WindowStartupLocation.Manual;
        window.Loaded+=async(s,e)=>{
            Window dialog=null;
            try{
                await Task.Delay(900);
                var flags=BindingFlags.Instance|BindingFlags.NonPublic;
                var history=new ChatViewModel();history.Apply(new CoreEvent{kind=0,peer=new string('b',64),text="Reader"});
                var original=new MessageData{id=new string('1',32),sender=new string('c',64),text="Archived local-account message",delivery=1,localAuthor=true};
                history.Apply(new CoreEvent{kind=6,peer=new string('a',64),messages=new[]{original}});
                var rendered=history.Get(new string('a',64)).Messages.Single();
                Check(rendered.Outgoing&&rendered.Data.sender==original.sender&&rendered.Data.delivery==1);
                var failureText=typeof(DesktopApplication).GetMethod("AccountFailureMessage",BindingFlags.Static|BindingFlags.NonPublic);
                Check(((string)failureText.Invoke(null,new object[]{new DiscoveryRequestException("device_account_mismatch"),true,true})).Contains("identity is already linked"));
                Check(((string)failureText.Invoke(null,new object[]{new System.Security.Authentication.AuthenticationException("test-only"),true,false})).Contains("TLS identity validation failed"));
                Check(((string)failureText.Invoke(null,new object[]{new IOException("test-only"),true,true})).Contains("authentication succeeded"));
                var loadPolicy=typeof(DesktopApplication).GetMethod("LoadMailboxPolicy",flags);
                var savePolicy=typeof(DesktopApplication).GetMethod("SaveMailboxPolicy",flags);
                Check((bool)loadPolicy.Invoke(desktop,null)); // Missing profile value defaults ON.
                savePolicy.Invoke(desktop,new object[]{false});
                foreach(string accept in new[]{"Sign in","Create account"}){
                    string expected=accept=="Sign in"?"Create Account":"Back to Sign In";
                    window.Dispatcher.BeginInvoke((Action)(()=>{
                        var accountDialog=window.OwnedWindows.Cast<Window>().Single(w=>w.Title=="Isolated account navigation");
                        var content=(StackPanel)accountDialog.Content;
                        var change=content.Children.OfType<Button>().Single(b=>Object.Equals(b.Content,expected));
                        Check(change.IsEnabled);change.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                    }));
                    var navigation=(string[])typeof(DesktopApplication).GetMethod("AccountDialog",flags).Invoke(desktop,new object[]{"Isolated account navigation",accept,"",false});
                    Check(navigation!=null&&navigation.Length==0); // Navigation only, no registration/login/identity mutation.
                }
                for(int cycle=0;cycle<32;cycle++)Check(!(bool)loadPolicy.Invoke(desktop,null));
                var toggle=(CheckBox)typeof(DesktopApplication).GetMethod("VoiceProcessingSwitch",flags).Invoke(desktop,new object[]{"Offline encrypted mailbox storage",(bool)loadPolicy.Invoke(desktop,null)});
                Check(toggle.IsChecked==false&&toggle.Template!=null);
                toggle.IsChecked=true;savePolicy.Invoke(desktop,new object[]{toggle.IsChecked==true});Check((bool)loadPolicy.Invoke(desktop,null));
                toggle.IsChecked=false;savePolicy.Invoke(desktop,new object[]{toggle.IsChecked==true});Check(!(bool)loadPolicy.Invoke(desktop,null));
                savePolicy.Invoke(desktop,new object[]{true});Check((bool)loadPolicy.Invoke(desktop,null));
                string policy=(string)typeof(DesktopApplication).GetProperty("MailboxPolicyPath",flags).GetValue(desktop,null);
                File.WriteAllBytes(policy,new byte[]{1,2,3});Check(!(bool)loadPolicy.Invoke(desktop,null)); // Corruption fails closed.
                savePolicy.Invoke(desktop,new object[]{false});
                dialog=new Window{Owner=window,Width=700,Height=650,ShowInTaskbar=false,ShowActivated=false,Left=-20000,WindowStartupLocation=WindowStartupLocation.Manual};
                var panel=(StackPanel)typeof(DesktopApplication).GetMethod("ArchiveSettingsPanel",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(desktop,new object[]{dialog});
                panel.Children.Add(toggle);
                dialog.Content=new ScrollViewer{Content=panel};dialog.Show();dialog.UpdateLayout();
                var switchState=(TextBlock)toggle.Template.FindName("SwitchState",toggle);
                Check(switchState.Text=="OFF");toggle.IsChecked=true;dialog.UpdateLayout();Check(switchState.Text=="ON");
                toggle.IsChecked=false;dialog.UpdateLayout();Check(switchState.Text=="OFF");
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="THIS DEVICE"));
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="YOUR HISTORY DEVICES"));
                Check(Find(dialog,"Archive device selector")==null);
                Check(panel.Children.OfType<CheckBox>().Single(c=>Object.Equals(c.Content,"History Sync")).Template!=null);
                var security=panel.Children.OfType<Expander>().Single();Check(!security.IsExpanded);security.IsExpanded=true;dialog.UpdateLayout();
                var fileSection=((StackPanel)security.Content).Children.OfType<Expander>().Single();fileSection.IsExpanded=true;dialog.UpdateLayout();
                Check(Find(dialog,"Historical attachment selector") is ComboBox);
                foreach(var label in new[]{"Refresh attachments","Next attachment page","Download selected attachment…"})Check(Find(dialog,label) is Button);
                Check(!((Button)Find(dialog,"Next attachment page")).IsEnabled);
                foreach(var label in new[]{"+ Authorize a device","Pause","Refresh history devices"})Check(Find(dialog,label) is Button);
                Check(!panel.Children.OfType<Button>().Any(b=>Object.Equals(b.Content,"Refresh history devices")));
                ((Button)Find(dialog,"Refresh history devices")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="Sign in and establish authenticated server presence first."));
                Check(panel.IsEnabled);
                var names=new HistoryDeviceNames(args[0]);string first=new string('a',64),second=new string('b',64);
                names.Set(8,first,"Work VM");names.Set(8,second,"Work VM");names.Set(9,first,"Other account");
                for(int cycle=0;cycle<32;cycle++){var reopen=new HistoryDeviceNames(args[0]);Check(reopen.Get(8,first,"")=="Work VM"&&reopen.Get(8,second,"")=="Work VM"&&reopen.Get(9,first,"")=="Other account");}
                names.Set(8,first,"Main PC");Check(names.Get(8,first,"")=="Main PC"&&names.Get(8,second,"")=="Work VM");
                bool bad=false;try{names.Set(8,"Work VM","Replacement identity");}catch{bad=true;}Check(bad);
                var assembly=typeof(DesktopApplication).Assembly;var progressType=assembly.GetType("PeerChatDesktop.ArchiveEnrollmentExecution+SyncProgress");
                Func<string,bool,bool,long,long?,HistoryProgressView> project=(state,running,complete,count,total)=>{var p=Activator.CreateInstance(progressType);progressType.GetField("State").SetValue(p,state);progressType.GetField("Running").SetValue(p,running);progressType.GetField("Complete").SetValue(p,complete);progressType.GetField("Processed").SetValue(p,count);progressType.GetField("Conversations").SetValue(p,4L);return (HistoryProgressView)typeof(HistoryProgressView).GetMethod("Create",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,new object[]{p,"Work VM",new string[0],total});};
                foreach(var pair in new[]{new[]{"Connecting to archive","Connecting"},new[]{"Synchronizing history","Downloading history"},new[]{"Archive route unavailable","Waiting for device"},new[]{"Archive authorization unavailable","Authorization required"},new[]{"Paused","Paused"},new[]{"History synchronization is off","Permission disabled"},new[]{"Archive data rejected","Failed"}})Check(project(pair[0],true,false,842,null).State==pair[1]);
                var download=project("Synchronizing history",true,false,50,null);Check(download.Indeterminate&&download.Direction.Contains("Work VM")&&download.Counters.Contains("50 messages")&&download.Counters.Contains("4 conversations"));
                var known=project("Synchronizing history",true,false,50,100);Check(!known.Indeterminate&&known.Percent==50);Check(!project("History synchronized",false,true,50,null).Active);
                Console.WriteLine("PASS: explicit mailbox OFF protected persistence, corruption fails closed, rendered ON/OFF slider states; archive Settings render without automatic enrollment or unsigned-in consent. Host smoke only.");
            }catch(Exception error){Console.Error.WriteLine(error);result=1;}
            finally{if(dialog!=null)dialog.Close();window.Close();}
        };
        app.Run(window);return result;
    }
}
