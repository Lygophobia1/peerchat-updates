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
    static void Check(bool value,[System.Runtime.CompilerServices.CallerLineNumber]int line=0){if(!value)throw new Exception("Archive Settings smoke failed at line "+line);}
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
                // Real startup regression: a previous authenticated refresh has
                // cached devices, but a new Settings panel starts at account 0.
                var deviceType=typeof(DesktopApplication).Assembly.GetType("PeerChatDesktop.ArchiveEnrollmentExecution+Device");
                var cached=Activator.CreateInstance(deviceType);deviceType.GetField("PeerId").SetValue(cached,new string('a',64));
                var cachedRows=Array.CreateInstance(deviceType,1);cachedRows.SetValue(cached,0);
                typeof(DesktopApplication).GetField("historyDevices",flags).SetValue(desktop,cachedRows);
                typeof(DesktopApplication).GetField("historyAccount",flags).SetValue(desktop,8L);
                typeof(DesktopApplication).GetField("historyGeneration",flags).SetValue(desktop,1UL);
                for(int cycle=0;cycle<32;cycle++){
                    var startup=(StackPanel)typeof(DesktopApplication).GetMethod("ArchiveSettingsPanel",flags).Invoke(desktop,new object[]{dialog});
                    Check(startup.Children.OfType<Expander>().Any());
                }
                var panel=(StackPanel)typeof(DesktopApplication).GetMethod("ArchiveSettingsPanel",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(desktop,new object[]{dialog});
                panel.Children.Add(toggle);
                dialog.Content=new ScrollViewer{Content=panel};dialog.Show();dialog.UpdateLayout();
                var switchState=(TextBlock)toggle.Template.FindName("SwitchState",toggle);
                Check(switchState.Text=="OFF");toggle.IsChecked=true;dialog.UpdateLayout();Check(switchState.Text=="ON");
                toggle.IsChecked=false;dialog.UpdateLayout();Check(switchState.Text=="OFF");
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="THIS DEVICE"));
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="OTHER DEVICES"));
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="SYNC STATUS"));
                Check(panel.Children.OfType<CheckBox>().All(c=>c==toggle||Object.Equals(c.Content,"History Sync"))); // Mailbox remains separate; no raw history permissions.
                Check(Find(dialog,"Archive device selector")==null);
                Check(panel.Children.OfType<CheckBox>().Single(c=>Object.Equals(c.Content,"History Sync")).Template!=null);
                var security=panel.Children.OfType<Expander>().Single();Check(!security.IsExpanded);security.IsExpanded=true;dialog.UpdateLayout();
                var fileSection=((StackPanel)security.Content).Children.OfType<Expander>().Single();fileSection.IsExpanded=true;dialog.UpdateLayout();
                Check(Find(dialog,"Historical attachment selector") is ComboBox);
                foreach(var label in new[]{"Refresh attachments","Next attachment page","Download selected attachment…"})Check(Find(dialog,label) is Button);
                Check(!((Button)Find(dialog,"Next attachment page")).IsEnabled);
                foreach(var label in new[]{"Add device","Pause","Refresh history devices"})Check(Find(dialog,label) is Button);
                Check(!panel.Children.OfType<Button>().Any(b=>Object.Equals(b.Content,"Refresh history devices")));
                ((Button)Find(dialog,"Refresh history devices")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                Check(panel.Children.OfType<TextBlock>().Any(t=>t.Text=="Sign in and establish authenticated server presence first."));
                Check(panel.IsEnabled);
                // Aggregate permissions can already be ON under another issuer.
                // The new action is explicit, not an automatic Save side effect.
                var own=Activator.CreateInstance(deviceType);deviceType.GetField("PeerId").SetValue(own,new string('b',64));deviceType.GetField("Local").SetValue(own,true);
                deviceType.GetField("GrantedRead").SetValue(cached,true);deviceType.GetField("GrantedServe").SetValue(cached,true);
                var consentRows=Array.CreateInstance(deviceType,2);consentRows.SetValue(own,0);consentRows.SetValue(cached,1);
                typeof(DesktopApplication).GetField("historyDevices",flags).SetValue(desktop,consentRows);
                foreach(bool enabledAuthorizer in new[]{true,false}){
                    deviceType.GetField("ApprovedIssuer").SetValue(own,enabledAuthorizer);
                    Exception modalFailure=null;
                    window.Dispatcher.BeginInvoke((Action)(()=>{
                        Window focus=null;try{
                            focus=dialog.OwnedWindows.Cast<Window>().Single(w=>w.Title=="New history device");focus.UpdateLayout();
                            var focusBody=(StackPanel)((ScrollViewer)focus.Content).Content;Check(!focusBody.Children.OfType<CheckBox>().Any());
                            var custom=focusBody.Children.OfType<Expander>().Single();Check(!custom.IsExpanded&&((StackPanel)custom.Content).Children.OfType<CheckBox>().Count()==3);custom.IsExpanded=true;focus.UpdateLayout();
                            var explicitGrant=Find(focus,"Authorize selected permissions from this device") as Button;
                            Check((explicitGrant!=null)==enabledAuthorizer);
                            if(explicitGrant!=null){explicitGrant.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(focus.IsVisible&&focus.IsEnabled&&panel.IsEnabled);}
                        }catch(Exception error){modalFailure=error;}finally{if(focus!=null)focus.Close();}
                    }));
                    ((Button)Find(dialog,"Add device")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
                    if(modalFailure!=null)throw modalFailure;
                }
                var names=new HistoryDeviceNames(args[0]);string first=new string('a',64),second=new string('b',64);
                Check(names.Get(8,first,"Unnamed device")=="Unnamed device");
                Check(names.Get(0,first,"This PC")=="This PC");
                Check(names.Get(8,"unsupported scope","Unnamed device")=="Unnamed device");
                Check(names.Get(8,null,"Unnamed device")=="Unnamed device");
                names.Set(8,first,"Work VM");names.Set(8,second,"Work VM");names.Set(9,first,"Other account");
                for(int cycle=0;cycle<32;cycle++){var reopen=new HistoryDeviceNames(args[0]);Check(reopen.Get(8,first,"")=="Work VM"&&reopen.Get(8,second,"")=="Work VM"&&reopen.Get(9,first,"")=="Other account");}
                names.Set(8,first,"Main PC");Check(names.Get(8,first,"")=="Main PC"&&names.Get(8,second,"")=="Work VM");
                bool bad=false;try{names.Set(8,"Work VM","Replacement identity");}catch{bad=true;}Check(bad);
                bad=false;try{names.Set(0,first,"Invalid account");}catch(InvalidDataException){bad=true;}Check(bad);
                var metadataPath=Path.Combine(args[0],"history-device-names.dat");var protectedNames=File.ReadAllBytes(metadataPath);
                var malformed=System.Text.Encoding.UTF8.GetBytes("{\"8:"+first+"\":\"bad\\nname\"}");
                File.WriteAllBytes(metadataPath,System.Security.Cryptography.ProtectedData.Protect(malformed,System.Text.Encoding.UTF8.GetBytes("PeerChat/HistoryNames/v1"),System.Security.Cryptography.DataProtectionScope.CurrentUser));
                Check(names.Get(8,first,"Unnamed device")=="Unnamed device");
                File.WriteAllBytes(metadataPath,new byte[]{1,2,3});
                Check(names.Get(8,first,"Unnamed device")=="Unnamed device"&&(bool)typeof(HistoryDeviceNames).GetProperty("DetailsUnavailable").GetValue(names,null));
                bad=false;try{names.Set(8,first,"Do not overwrite corruption");}catch(InvalidDataException){bad=true;}Check(bad);
                Check(File.ReadAllBytes(metadataPath).SequenceEqual(new byte[]{1,2,3}));
                typeof(DesktopApplication).GetMethod("ArchiveSettingsPanel",flags).Invoke(desktop,new object[]{dialog});
                File.WriteAllBytes(metadataPath,protectedNames);Check(names.Get(8,first,"")=="Main PC"&&!(bool)typeof(HistoryDeviceNames).GetProperty("DetailsUnavailable").GetValue(names,null));
                var assembly=typeof(DesktopApplication).Assembly;var progressType=assembly.GetType("PeerChatDesktop.ArchiveEnrollmentExecution+SyncProgress");
                Func<string,bool,bool,long,long?,HistoryProgressView> project=(state,running,complete,count,total)=>{var p=Activator.CreateInstance(progressType);progressType.GetField("State").SetValue(p,state);progressType.GetField("Running").SetValue(p,running);progressType.GetField("Complete").SetValue(p,complete);progressType.GetField("Processed").SetValue(p,count);progressType.GetField("Received").SetValue(p,count);progressType.GetField("Conversations").SetValue(p,4L);return (HistoryProgressView)typeof(HistoryProgressView).GetMethod("Create",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,new object[]{p,"Work VM",new string[0],total});};
                foreach(var pair in new[]{new[]{"Connecting to archive","Connecting"},new[]{"Synchronizing history","Downloading history"},new[]{"Archive route unavailable","Waiting for device"},new[]{"Archive authorization unavailable","Authorization required"},new[]{"Paused","Paused"},new[]{"History synchronization is off","Permission disabled"},new[]{"Archive data rejected","Failed"}})Check(project(pair[0],true,false,842,null).State==pair[1]);
                var download=project("Synchronizing history",true,false,50,null);Check(download.Indeterminate&&download.Direction.Contains("Work VM")&&download.Counters.Contains("50 new messages")&&download.Counters.Contains("4 conversations"));
                var known=project("Synchronizing history",true,false,50,100);Check(!known.Indeterminate&&known.Percent==50);Check(!project("History synchronized",false,true,50,null).Active);
                // Actual rendered progress events with no active voice timer.
                ((System.Windows.Threading.DispatcherTimer)typeof(DesktopApplication).GetField("timer",flags).GetValue(desktop)).Stop();
                var fakeOwner=new DiscoveryClient("127.0.0.1",1,""){AccountId=8};
                typeof(DesktopApplication).GetField("discovery",flags).SetValue(desktop,fakeOwner);
                typeof(DesktopApplication).GetField("archiveLoadedOwner",flags).SetValue(desktop,fakeOwner);
                typeof(DesktopApplication).GetField("authenticatedFriendSnapshotGeneration",flags).SetValue(desktop,1UL);
                typeof(DesktopApplication).GetField("historyAccount",flags).SetValue(desktop,8L);
                typeof(DesktopApplication).GetField("historyGeneration",flags).SetValue(desktop,1UL);
                typeof(DesktopApplication).GetField("historyCommandInFlight",flags).SetValue(desktop,true); // No test RPC/auth.
                var snapshot=Activator.CreateInstance(progressType);progressType.GetField("State").SetValue(snapshot,"Synchronizing history");progressType.GetField("Source").SetValue(snapshot,first);progressType.GetField("Running").SetValue(snapshot,true);progressType.GetField("Enabled").SetValue(snapshot,true);
                typeof(DesktopApplication).GetField("archiveProgress",flags).SetValue(desktop,snapshot);
                var live=(StackPanel)typeof(DesktopApplication).GetMethod("ArchiveSettingsPanel",flags).Invoke(desktop,new object[]{dialog});dialog.Content=new ScrollViewer{Content=live};dialog.UpdateLayout();await Task.Delay(30); // Process WPF Loaded/Unloaded subscription boundaries.
                var voiceClock=(System.Windows.Threading.DispatcherTimer)typeof(DesktopApplication).GetField("voiceTimer",flags).GetValue(desktop);voiceClock.Stop();
                var publish=typeof(DesktopApplication).GetMethod("PublishHistoryPresentation",flags);
                for(long batch=1;batch<=4;batch++){progressType.GetField("Received").SetValue(snapshot,batch*8);publish.Invoke(desktop,null);dialog.UpdateLayout();Check(live.Children.OfType<TextBlock>().Any(t=>t.Text.Contains((batch*8)+" new messages received")));Check(((ProgressBar)Find(dialog,"History sync progress")).IsIndeterminate);}
                progressType.GetField("Running").SetValue(snapshot,false);progressType.GetField("Complete").SetValue(snapshot,true);progressType.GetField("State").SetValue(snapshot,"History synchronized");progressType.GetField("LastSuccessful").SetValue(snapshot,1791740000000L);publish.Invoke(desktop,null);dialog.UpdateLayout();
                Check(live.Children.OfType<TextBlock>().Any(t=>t.Text.Contains("Last successful sync:")&&!t.Text.Contains("Not recorded")));
                var currentCallback=typeof(DesktopApplication).GetMethod("IsHistoryCallbackCurrent",flags);long epoch=(long)typeof(DesktopApplication).GetField("archiveUiEpoch",flags).GetValue(desktop);
                Check((bool)currentCallback.Invoke(desktop,new object[]{fakeOwner,1UL,epoch}));
                Check(!(bool)currentCallback.Invoke(desktop,new object[]{fakeOwner,2UL,epoch}));Check(!(bool)currentCallback.Invoke(desktop,new object[]{new DiscoveryClient("127.0.0.1",1,""),1UL,epoch}));
                typeof(DesktopApplication).GetField("archiveUiEpoch",flags).SetValue(desktop,epoch+1);Check(!(bool)currentCallback.Invoke(desktop,new object[]{fakeOwner,1UL,epoch}));
                dialog.Content=null;dialog.UpdateLayout();await Task.Delay(30);Check(typeof(DesktopApplication).GetField("HistoryPresentationChanged",flags).GetValue(desktop)==null||((Delegate)typeof(DesktopApplication).GetField("HistoryPresentationChanged",flags).GetValue(desktop)).GetInvocationList().Length<=1);
                typeof(DesktopApplication).GetField("discovery",flags).SetValue(desktop,null);typeof(DesktopApplication).GetField("historyCommandInFlight",flags).SetValue(desktop,false);
                Console.WriteLine("PASS: explicit mailbox OFF protected persistence, corruption fails closed, rendered ON/OFF slider states; archive Settings render without automatic enrollment or unsigned-in consent. Host smoke only.");
            }catch(Exception error){Console.Error.WriteLine(error);result=1;}
            finally{if(dialog!=null)dialog.Close();window.Close();}
        };
        app.Run(window);return result;
    }
}
