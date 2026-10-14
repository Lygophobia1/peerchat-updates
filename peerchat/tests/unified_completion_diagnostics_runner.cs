using System;
using System.Reflection;
using System.Windows;
using PeerChatDesktop;
class UnifiedCompletionDiagnosticsRunner {
    [STAThread]static int Main(string[] args){int result=0;var app=new Application();var desktop=new DesktopApplication(args[0],0);var window=(Window)typeof(DesktopApplication).GetField("window",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(desktop);window.ShowInTaskbar=false;window.ShowActivated=false;window.Left=-20000;window.WindowStartupLocation=WindowStartupLocation.Manual;window.Loaded+=async(s,e)=>{try{await System.Threading.Tasks.Task.Delay(700);await UnifiedCompletionDiagnosticsTest.Run(desktop,args[0]);}catch(Exception error){Console.Error.WriteLine(error);result=1;}finally{window.Close();app.Shutdown();}};app.Run(window);return result;}
}
