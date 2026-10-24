using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using PeerChatDesktop;

class VoiceSettingsUiTest {
    static void Require(bool condition,string message){if(!condition)throw new Exception(message);}
    static T Find<T>(DependencyObject root,string name) where T:DependencyObject {
        if(root is T&&(AutomationProperties.GetName(root)==name||(root is ContentControl && Object.Equals(((ContentControl)root).Content,name))))return (T)root;
        for(int i=0;i<VisualTreeHelper.GetChildrenCount(root);++i){var result=Find<T>(VisualTreeHelper.GetChild(root,i),name);if(result!=null)return result;}
        return null;
    }
    [STAThread] static int Main(string[] args){
        int result=1,visits=0;
        string profile=Path.Combine(args[0],"profile");
        Directory.CreateDirectory(profile);
        var app=new Application();
        var desktop=new DesktopApplication(profile,0);
        var window=(Window)typeof(DesktopApplication).GetField("window",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);
        window.ShowInTaskbar=false;window.ShowActivated=false;window.Left=-20000;
        window.Loaded+=(sender,unused)=>{
            var timer=new DispatcherTimer{Interval=TimeSpan.FromMilliseconds(100)};
            timer.Tick+=(s,e)=>{
                Window settings=window.OwnedWindows.Cast<Window>().FirstOrDefault(item=>item.Title=="Settings");
                if(settings==null)return;
                timer.Stop();
                try {
                    var voice=Find<Button>(settings,"Settings navigation Voice");Require(voice!=null,"Voice navigation missing");
                    voice.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));settings.UpdateLayout();
                    var automatic=Find<CheckBox>(settings,"Automatic Noise Control");Require(automatic!=null,"Automatic Noise Control switch missing");
                    var autoLevel=Find<ListBox>(settings,"Automatic Noise Control level");Require(autoLevel!=null,"Automatic levels missing");
                    var ns=Find<CheckBox>(settings,"Noise Suppression enabled");var agc=Find<CheckBox>(settings,"Automatic Gain Control enabled");
                    var nsStrength=Find<ListBox>(settings,"Noise Suppression strength");var agcStrength=Find<ListBox>(settings,"Automatic Gain Control strength");
                    var master=Find<CheckBox>(settings,"Enable Audio Processing");var aec=Find<CheckBox>(settings,"Echo Cancellation enabled");
                    Require(ns!=null&&agc!=null&&nsStrength!=null&&agcStrength!=null&&master!=null,"Switches or discrete strength choices missing");
                    Require(String.Join("|",nsStrength.Items.Cast<string>())=="Low|Balanced|High"&&String.Join("|",agcStrength.Items.Cast<string>())=="Gentle|Balanced|Strong","Wrong friendly choices");
                    Require(ns.Template.FindName("SwitchThumb",ns)!=null&&agc.Template.FindName("SwitchThumb",agc)!=null,"On/Off switch presentation missing");
                    Require(aec!=null&&aec.IsEnabled,"AEC switch unavailable");
                    if(visits==0){
                        Require(automatic.IsChecked==true&&!ns.IsEnabled&&!nsStrength.IsEnabled,"Automatic mode must own noise processing");
                        Require(autoLevel.SelectedIndex==1&&autoLevel.IsEnabled,"Medium default missing");autoLevel.SelectedIndex=2;
                        automatic.IsChecked=false;Require(!autoLevel.IsEnabled,"Automatic level still enabled while Off");Require(ns.IsEnabled,"Original processing fallback unavailable");
                        nsStrength.SelectedIndex=2;agcStrength.SelectedIndex=0;aec.IsChecked=true;
                        ns.IsChecked=false;agc.IsChecked=false;settings.UpdateLayout();
                        Require(!nsStrength.IsEnabled&&!agcStrength.IsEnabled&&nsStrength.Opacity<1&&agcStrength.Opacity<1,"OFF strengths were not disabled/grayed");
                        Require(nsStrength.SelectedIndex==2&&agcStrength.SelectedIndex==0,"OFF discarded strength");
                        Require(((TextBlock)ns.Template.FindName("SwitchState",ns)).Text=="OFF","OFF switch text incorrect");
                    } else {
                        Require(automatic.IsChecked==false,"Automatic mode preference did not persist");Require(aec.IsChecked==true,"AEC preference did not persist");Require(ns.IsChecked==false&&agc.IsChecked==false&&nsStrength.SelectedIndex==2&&agcStrength.SelectedIndex==0,"Reopening lost saved OFF strengths");
                        ns.IsChecked=true;agc.IsChecked=true;settings.UpdateLayout();
                        Require(nsStrength.IsEnabled&&agcStrength.IsEnabled&&nsStrength.SelectedIndex==2&&agcStrength.SelectedIndex==0,"ON did not restore saved strength");
                        Require(((TextBlock)ns.Template.FindName("SwitchState",ns)).Text=="ON","ON switch text incorrect");
                        master.IsChecked=false;Require(!ns.IsEnabled&&!agc.IsEnabled&&!aec.IsEnabled&&!nsStrength.IsEnabled&&!agcStrength.IsEnabled,"Master OFF did not disable processing controls");
                        master.IsChecked=true;Require(nsStrength.IsEnabled&&agcStrength.IsEnabled,"Master ON did not restore controls");
                        automatic.IsChecked=true;Require(!ns.IsEnabled&&!nsStrength.IsEnabled&&agc.IsEnabled&&aec.IsEnabled,"Automatic control must preserve AGC/AEC and disable duplicate NS");
                        automatic.IsChecked=false;Require(nsStrength.IsEnabled,"Returning to original processing lost settings");
                        result=0;
                    }
                    visits++;
                } catch(Exception error){Console.Error.WriteLine("FAIL: "+error);result=1;visits=2;}
                finally{settings.Close();}
            };
            for(int iteration=0;iteration<2&&visits<2;++iteration){timer.Start();((Button)window.FindName("Settings")).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));}
            window.Close();
        };
        app.Run(window);
        if(result==0)Console.WriteLine("PASS: actual Voice settings switches, discrete presets, OFF gray/disable, saved strength, reopen/re-enable, master toggle, and persisted AEC switch.");
        return result;
    }
}
