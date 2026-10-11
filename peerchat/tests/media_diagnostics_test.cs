using System;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using PeerChatDesktop;
class MediaDiagnosticsTest {
    static void Check(bool value){if(!value)throw new Exception("Media summary regression");}
    [STAThread] static int Main(string[] args){
        var voice=MediaPerformanceSummary.Voice("rtp_arrival_jitter_ms=4.2\nrtp_missing_estimate=2\nneteq_current_delay_ms=40\nneteq_target_delay_ms=60\nperf_apm_capture=count:8;last_us:100;max_us:300;mean_us:120;deadline_misses:0");
        Check(voice.Contains("4.2")&&voice.Contains("estimate")&&voice.Contains("40 ms")&&voice.Contains("300 µs")&&voice.Contains("unavailable"));
        var screen=MediaPerformanceSummary.Screen("rendered_fps=29\ndecoder_queue_depth=8\ndecoder_oldest_queue_age_ms=230\ndropped_decode_queue=1\nawaiting_recovery_idr=true\nperf_screen_present=count:2;max_us:80000");
        Check(screen.Contains("230 ms")&&screen.Contains("80000 µs")&&screen.Contains("true"));
        Check(MediaPerformanceSummary.Screen(null).Contains("Unavailable"));
        Check(MediaPerformanceSummary.ProcessSnapshot().Contains("process_cpu_total_ms="));
        int result=0;var app=new Application();var desktop=new DesktopApplication(args[0],0);
        var flags=BindingFlags.Instance|BindingFlags.NonPublic;
        var window=(Window)typeof(DesktopApplication).GetField("window",flags).GetValue(desktop);
        window.ShowInTaskbar=false;window.ShowActivated=false;window.Left=-20000;window.WindowStartupLocation=WindowStartupLocation.Manual;
        window.Loaded+=async(s,e)=>{try{
            await Task.Delay(700);
            // UI projection fixture only, not a claim of a real call. Idle UI
            // correctly collapses details on each timer tick.
            var model=(ChatViewModel)typeof(DesktopApplication).GetField("model",flags).GetValue(desktop);
            model.VoiceStateCode=5;model.VoiceState="Connected";
            typeof(DesktopApplication).GetMethod("RefreshVoiceUi",flags).Invoke(desktop,null);
            var show=typeof(DesktopApplication).GetMethod("ShowVoiceDiagnostics",flags);
            var panel=(StackPanel)window.FindName("ContextVoice");
            for(int i=0;i<3;i++){show.Invoke(desktop,null);await Task.Delay(200);Check(panel.Children.OfType<TextBlock>().Count(t=>Object.Equals(t.Tag,"media performance"))==1);Check(panel.Children.OfType<TextBlock>().Single(t=>Object.Equals(t.Tag,"media performance")).Text.Contains("NetEq"));show.Invoke(desktop,null);}
            var screenPanel=(StackPanel)typeof(DesktopApplication).GetField("screenConnectionPanel",flags).GetValue(desktop);
            Check(screenPanel.Children.OfType<TextBlock>().Count(t=>Object.Equals(t.Tag,"media performance"))==1);
            Console.WriteLine("PASS: bounded media summaries, unavailable telemetry honest, CPU/GC snapshot; packaged WPF repeated details open/close, one summary per owner.");
        }catch(Exception error){Console.Error.WriteLine(error);result=1;}finally{window.Close();}};
        app.Run(window);return result;
    }
}
