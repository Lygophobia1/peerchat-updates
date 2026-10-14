using System;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using PeerChatDesktop;
class MediaDiagnosticsTest {
    static void Check(bool value,[System.Runtime.CompilerServices.CallerLineNumber]int line=0){if(!value)throw new Exception("Media summary regression at line "+line);}
    [STAThread] static int Main(string[] args){
        var empty=GroupTelemetryProjection.Create("{}",new string[0]);Check(!empty.VoiceActive&&!empty.ScreenActive&&empty.Text.Contains("N/A"));
        string participant="{\"sourceKey\":\"verified-device\",\"connected\":true,\"turn\":true,\"state\":\"Connected\",\"sendPayloadBits\":1000000,\"receivePayloadBits\":2000000}";
        var metrics=GroupTelemetryProjection.Create("{\"active\":true,\"routes\":["+participant+"]}",new[]{"{\"active\":true,\"encoderCount\":1,\"capturedFps\":30,\"encodedFps\":29,\"routes\":["+participant+","+participant+","+participant+"]}"});
        Check(metrics.VoiceActive&&metrics.ScreenActive&&metrics.UploadMbps==4&&metrics.DownloadMbps==8&&metrics.EncodedFps==29&&metrics.Transport.Contains("TURN")&&metrics.Participants.Contains("verified-device"));
        Check(GroupTelemetryProjection.Create("malformed",new[]{"invalid"}).Text.Contains("N/A"));
        var voice=MediaPerformanceSummary.Voice("rtp_arrival_jitter_ms=4.2\nrtp_missing_estimate=2\nneteq_current_delay_ms=40\nneteq_target_delay_ms=60\nperf_apm_capture=count:8;last_us:100;max_us:300;mean_us:120;deadline_misses:0");
        Check(voice.Contains("4.2")&&voice.Contains("estimate")&&voice.Contains("40 ms")&&voice.Contains("300 µs")&&voice.Contains("unavailable"));
        var screen=MediaPerformanceSummary.Screen("rendered_fps=29\ndecoder_queue_depth=8\ndecoder_oldest_queue_age_ms=230\ndropped_decode_queue=1\nawaiting_recovery_idr=true\nperf_screen_present=count:2;max_us:80000");
        Check(screen.Contains("230 ms")&&screen.Contains("80000 µs")&&screen.Contains("true"));
        Check(MediaPerformanceSummary.Screen(null).Contains("Unavailable"));
        Check(MediaPerformanceSummary.ProcessSnapshot().Contains("process_cpu_total_ms="));
        var group=new GroupDiagnosticsData{GroupId="group-a",Name="Test group",Members=new System.Collections.Generic.List<GroupDiagnosticsMember>(),RoomVoiceState="received_packets=2",RoomScreenState="presented_frames=1"};
        var frozen=new DiagnosticsSnapshot{SnapshotId="frozen",TimestampUtc="old",Sections=new System.Collections.Generic.List<DiagnosticsSection>{new DiagnosticsSection{Name="Voice",Details="DM voice unchanged"}},Groups=new System.Collections.Generic.List<GroupDiagnosticsData>{group}};
        frozen=DiagnosticsService.RefreshGroupView(frozen,new[]{group},DateTime.UtcNow);frozen.SnapshotId="frozen";frozen.TimestampUtc="old";
        var live=DiagnosticsService.RefreshGroupView(frozen,new[]{group},DateTime.UtcNow);
        group.RoomVoiceState="received_packets=20";group.RoomScreenState="presented_frames=30";
        var next=DiagnosticsService.RefreshGroupView(live,new[]{group},DateTime.UtcNow);
        Check(DiagnosticsService.FormatFull(DiagnosticsService.ProjectGroup(live,"group-a","Voice")).Contains("received_packets=2"));
        Check(DiagnosticsService.FormatFull(DiagnosticsService.ProjectGroup(next,"group-a","Voice")).Contains("received_packets=20"));
        Check(DiagnosticsService.FormatFull(DiagnosticsService.ProjectGroup(next,"group-a","Screen Share")).Contains("presented_frames=30"));
        Check(next.Sections[0].Details=="DM voice unchanged"&&frozen.SnapshotId=="frozen"&&frozen.TimestampUtc=="old"&&frozen.Groups[0].RoomVoiceState=="received_packets=2");
        next.Groups[0].RoomVoiceState="changed";Check(live.Groups[0].RoomVoiceState=="received_packets=2");
        Check(DiagnosticsService.FormatFull(DiagnosticsService.ProjectGroup(DiagnosticsService.RefreshGroupView(next,null,DateTime.UtcNow),"group-a","Voice")).Contains("unavailable"));
        var samples=new SampleDiagnosticLayoutProvider();
        foreach(DiagnosticSection section in Enum.GetValues(typeof(DiagnosticSection)))foreach(DiagnosticExampleState state in Enum.GetValues(typeof(DiagnosticExampleState))){
            if(section==DiagnosticSection.GroupCalls)continue;
            var sample=samples.Read(new DiagnosticContext(section,"stable-test-id"),state);Check(sample.Sample&&sample.Banner.Contains("SAMPLE")&&sample.Context.StableId=="stable-test-id");Check(sample.Pages.Select(p=>p.Tab).SequenceEqual(DiagnosticNavigation.Tabs(section)));
            if(state==DiagnosticExampleState.Unavailable||state==DiagnosticExampleState.Disconnected)Check(sample.Pages.SelectMany(p=>p.Panels).All(p=>p.Samples.Length==0));
        }
        bool rejected=false;try{samples.Read(new DiagnosticContext(DiagnosticSection.GroupCalls,"test"),DiagnosticExampleState.Active);}catch(InvalidOperationException){rejected=true;}Check(rejected);
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
            // Presentation fixture only, not a real group media acceptance claim.
            var displayed=new GroupModel{Data=new GroupData{id=new string('a',32),name="Before",members=new GroupMemberData[0],invites=new GroupMemberData[0]}};
            model.GroupChats[displayed.Id]=displayed;model.Groups=true;model.SelectedGroup=displayed;
            typeof(DesktopApplication).GetMethod("RefreshGroupDetails",flags).Invoke(desktop,new object[]{true});typeof(DesktopApplication).GetField("groupTelemetry",flags).SetValue(desktop,metrics);
            var openGraphs=typeof(DesktopApplication).GetMethod("OpenGroupDiagnostics",flags);openGraphs.Invoke(desktop,null);openGraphs.Invoke(desktop,null);
            var graphWindow=(Window)typeof(DesktopApplication).GetField("groupDiagnosticWindow",flags).GetValue(desktop);Check(graphWindow!=null&&app.Windows.Cast<Window>().Count(w=>w==graphWindow)==1);
            var tabs=(TabControl)typeof(DesktopApplication).GetField("groupDiagnosticTabs",flags).GetValue(desktop);Check(tabs.Items.Count==6);graphWindow.Width=540;graphWindow.Height=420;graphWindow.UpdateLayout();Check(tabs.ActualWidth>0);graphWindow.Close();Check(typeof(DesktopApplication).GetField("groupDiagnosticWindow",flags).GetValue(desktop)==null);
            openGraphs.Invoke(desktop,null);graphWindow=(Window)typeof(DesktopApplication).GetField("groupDiagnosticWindow",flags).GetValue(desktop);graphWindow.UpdateLayout();var visual=(FrameworkElement)graphWindow.Content;var bitmap=new System.Windows.Media.Imaging.RenderTargetBitmap((int)visual.ActualWidth,(int)visual.ActualHeight,96,96,System.Windows.Media.PixelFormats.Pbgra32);bitmap.Render(visual);var png=new System.Windows.Media.Imaging.PngBitmapEncoder();png.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));using(var image=System.IO.File.Create(System.IO.Path.Combine(args[0],"group-diagnostics.png")))png.Save(image);graphWindow.Close();
            var openContext=typeof(DesktopApplication).GetMethod("OpenContextDiagnostics",flags);string originalCall=model.VoiceCallId;
            foreach(DiagnosticSection section in Enum.GetValues(typeof(DiagnosticSection))){
                openContext.Invoke(desktop,new object[]{section,"stable-context-test"});graphWindow=(Window)typeof(DesktopApplication).GetField("groupDiagnosticWindow",flags).GetValue(desktop);var same=graphWindow;
                openContext.Invoke(desktop,new object[]{section,"stable-context-test"});Check(Object.ReferenceEquals(same,typeof(DesktopApplication).GetField("groupDiagnosticWindow",flags).GetValue(desktop)));
                tabs=(TabControl)typeof(DesktopApplication).GetField("groupDiagnosticTabs",flags).GetValue(desktop);Check(tabs.Items.Count==DiagnosticNavigation.Tabs(section).Length);
                foreach(TabItem tab in tabs.Items){tabs.SelectedItem=tab;graphWindow.UpdateLayout();Check(tab.Content!=null);}
                graphWindow.Width=540;graphWindow.Height=420;graphWindow.UpdateLayout();Check(tabs.ActualWidth>0);graphWindow.Width=1400;graphWindow.Height=900;graphWindow.UpdateLayout();
                var nav=(TabControl)typeof(DesktopApplication).GetField("diagnosticNavigation",flags).GetValue(desktop);Check(nav.Items.Count==4&&nav.SelectedIndex==(int)section&&nav.Focusable&&nav.ItemContainerStyle!=null&&tabs.ItemContainerStyle!=null);
                var selector=(ComboBox)typeof(DesktopApplication).GetField("diagnosticExamples",flags).GetValue(desktop);Check((selector.Visibility==Visibility.Collapsed)==(section==DiagnosticSection.GroupCalls));
                if(section!=DiagnosticSection.GroupCalls)foreach(DiagnosticExampleState state in Enum.GetValues(typeof(DiagnosticExampleState))){selector.SelectedItem=state;graphWindow.UpdateLayout();Check(((string)typeof(DesktopApplication).GetMethod("CurrentDiagnosticExport",flags).Invoke(desktop,null)).Contains("SAMPLE"));}
                if(section!=DiagnosticSection.GroupCalls)selector.SelectedItem=DiagnosticExampleState.Active;
                tabs.SelectedIndex=0;graphWindow.UpdateLayout();var surface=(FrameworkElement)graphWindow.Content;var picture=new System.Windows.Media.Imaging.RenderTargetBitmap((int)surface.ActualWidth,(int)surface.ActualHeight,96,96,System.Windows.Media.PixelFormats.Pbgra32);picture.Render(surface);var encoding=new System.Windows.Media.Imaging.PngBitmapEncoder();encoding.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(picture));using(var file=System.IO.File.Create(System.IO.Path.Combine(args[0],section+".png")))encoding.Save(file);
                foreach(double scale in new[]{1.25,1.5,2.0}){graphWindow.Width=960;graphWindow.Height=720;surface.LayoutTransform=new System.Windows.Media.ScaleTransform(scale,scale);graphWindow.UpdateLayout();Check(tabs.ActualWidth>0&&surface.ActualHeight>0);}surface.LayoutTransform=System.Windows.Media.Transform.Identity;graphWindow.UpdateLayout();
                var primary=(TabItem)nav.SelectedItem;Check(primary.Focus());
                Check(primary.ActualHeight>=44);
                Check(primary.MoveFocus(new System.Windows.Input.TraversalRequest(System.Windows.Input.FocusNavigationDirection.Next)));
                // Keyboard traversal can select the adjacent context tab; restore this fixture's section.
                nav.SelectedIndex=(int)section;graphWindow.UpdateLayout();
                foreach(TabItem item in tabs.Items){tabs.SelectedItem=item;graphWindow.UpdateLayout();Check(item.ActualHeight>=38);}tabs.SelectedIndex=0;
                if(section==DiagnosticSection.FileTransfers){tabs.SelectedIndex=2;graphWindow.UpdateLayout();Check(((TabItem)tabs.SelectedItem).Header.ToString()=="Pipeline");typeof(DesktopApplication).GetMethod("RefreshDiagnosticLayout",flags).Invoke(desktop,null);Check(((TabItem)tabs.SelectedItem).Header.ToString()=="Pipeline");}
            }
            Check(model.VoiceCallId==originalCall);graphWindow.Close();
            var attachment=new AttachmentCard{Transfer=new CoreAdapter.FileTransferData{id="stable-transfer-id",name="fixture.bin",size=1048576,state=3}};AttachmentActionEventArgs selectedTransfer=null;attachment.AddHandler(AttachmentCard.ActionEvent,new RoutedEventHandler((sender,eventArgs)=>selectedTransfer=(AttachmentActionEventArgs)eventArgs));var entry=attachment.Children.OfType<WrapPanel>().Single().Children.OfType<Button>().Single(b=>Object.Equals(b.Content,"Open Diagnostics"));entry.RaiseEvent(new RoutedEventArgs(Button.ClickEvent));Check(selectedTransfer!=null&&selectedTransfer.Action=="diagnostics"&&selectedTransfer.Transfer.id=="stable-transfer-id");
            typeof(DesktopApplication).GetMethod("ShowDiagnosticsWindow",flags).Invoke(desktop,new object[]{null});
            var diagnostics=app.Windows.Cast<Window>().Single(w=>w.Title=="Connection & Diagnostics");
            Func<System.Windows.DependencyObject,System.Collections.Generic.IEnumerable<System.Windows.DependencyObject>> walk=null;
            walk=o=>Enumerable.Range(0,System.Windows.Media.VisualTreeHelper.GetChildrenCount(o)).SelectMany(i=>{var child=System.Windows.Media.VisualTreeHelper.GetChild(o,i);return new[]{child}.Concat(walk(child));});
            await Task.Delay(100);var details=walk(diagnostics).OfType<TextBox>().Single();Check(details.Text.Contains("Before"));
            displayed.Data.name="After live refresh";await Task.Delay(1300);Check(details.Text.Contains("After live refresh"));
            typeof(DesktopApplication).GetField("authenticatedFriendSnapshotGeneration",flags).SetValue(desktop,(ulong)2);
            await Task.Delay(1300);Check(details.Text.Contains("unavailable"));diagnostics.Close();await Task.Delay(100);
            Console.WriteLine("PASS: bounded media summaries, honest unavailable telemetry; copied live group counters, immutable failure snapshots; packaged WPF live group refresh/context invalidation and repeated details open/close.");
        }catch(Exception error){Console.Error.WriteLine(error);result=1;}finally{window.Close();}};
        app.Run(window);return result;
    }
}
