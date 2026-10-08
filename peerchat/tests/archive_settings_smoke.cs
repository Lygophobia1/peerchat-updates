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
            if(item!=null&&AutomationProperties.GetName(item)==name)return item;
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
                savePolicy.Invoke(desktop,new object[]{true});Check((bool)loadPolicy.Invoke(desktop,null));
                string policy=(string)typeof(DesktopApplication).GetProperty("MailboxPolicyPath",flags).GetValue(desktop,null);
                File.WriteAllBytes(policy,new byte[]{1,2,3});Check(!(bool)loadPolicy.Invoke(desktop,null)); // Corruption fails closed.
                savePolicy.Invoke(desktop,new object[]{false});
                dialog=new Window{Owner=window,Width=700,Height=650,ShowInTaskbar=false,ShowActivated=false,Left=-20000,WindowStartupLocation=WindowStartupLocation.Manual};
                var panel=(StackPanel)typeof(DesktopApplication).GetMethod("ArchiveSettingsPanel",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(desktop,new object[]{dialog});
                dialog.Content=new ScrollViewer{Content=panel};dialog.Show();dialog.UpdateLayout();
                Check(Find(dialog,"Archive device selector") is ComboBox);
                foreach(var label in new[]{"Refresh archive devices","Trust archive authorizer","Authorize history access","Revoke access","Disable authorizer","Synchronize from selected archive","Pause synchronization"})Check(Find(dialog,label) is Button);
                var selector=(ComboBox)Find(dialog,"Archive device selector");Check(selector.Items.Count==0);
                ((Button)Find(dialog,"Refresh archive devices")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="Sign in and establish authenticated server presence first."));
                Check(selector.Items.Count==0&&panel.IsEnabled);
                Console.WriteLine("PASS: packaged WPF archive Settings controls render; no automatic enrollment, no unsigned-in RPC/consent, no archive state owned by WPF. Host smoke only.");
            }catch(Exception error){Console.Error.WriteLine(error);result=1;}
            finally{if(dialog!=null)dialog.Close();window.Close();}
        };
        app.Run(window);return result;
    }
}
