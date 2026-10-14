using System;
using System.Linq;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace PeerChatDesktop {
public partial class DesktopApplication {
    bool dmDeveloperPreview=Environment.GetEnvironmentVariable("PEERCHAT_DIAGNOSTICS_DM_PREVIEW")=="1",dmGraphsPaused,dmAdvanced;int dmGraphSeconds=300;
    DmDiagnosticIdentity dmDiagnosticIdentity;DmDiagnosticInput dmDiagnosticInput;DiagnosticUiSnapshot dmViewSnapshot;
    readonly DmDiagnosticHistory dmHistory=new DmDiagnosticHistory();readonly List<Action> dmViewRefreshes=new List<Action>();DateTime nextDmViewUtc;
    // Test-only replacement of an input boundary; production always reads the existing collectors.
    Func<DmDiagnosticInput> dmDiagnosticTestInput=null;
    // Friend snapshot revision changes on metadata refresh; it is not an authentication lifetime.
    string DmDiagnosticScope(){return "profile:"+(localProfile==null?"local":localProfile.Id)+"/profile-generation:"+profileSessionGeneration+"/local-device:"+(model.OwnId??"")+"/account:"+(discovery==null?0:discovery.AccountId)+"/authentication-session:"+discoverySessionGeneration;}
    DmDiagnosticInput CaptureDmDiagnosticInput(){
        try{return CaptureDmDiagnosticInputCore();}catch(Exception error){return new DmDiagnosticInput{Scope=DmDiagnosticScope(),Control="Diagnostic snapshot unavailable: "+error.GetType().Name};}
    }
    DmDiagnosticInput CaptureDmDiagnosticInputCore(){
        if(dmDiagnosticTestInput!=null)return dmDiagnosticTestInput();
        var voice=CaptureVoiceDiagnosticsData();string peer=dmDiagnosticIdentity!=null?dmDiagnosticIdentity.Peer:voice.Active?voice.PeerId:activeScreenPeer;var conversation=model.FindByPeer(peer);string control="No active associated authenticated peer-control session";
        if(conversation!=null){var d=connectionAttempts.For(conversation);if(d.HasLiveSession)control="Authenticated peer-control TLS owner epoch="+d.SessionEpoch+" winner="+d.LiveConnectionId+" route="+d.LiveRoute+" — separate from WebRTC media; not historical call-start route";}
        return new DmDiagnosticInput{Scope=DmDiagnosticScope(),Voice=voice,Name=conversation==null?"Peer":conversation.Name,ScreenId=activeScreenId,ScreenPeer=activeScreenPeer,ScreenLifetime=activeScreenLifetime,ScreenGeneration=activeScreenGeneration,ScreenState=activeScreenState,ScreenRaw=core.ScreenShareDiagnostics(),Control=control};
    }
    void SelectDmDiagnosticIdentity(string id){var input=CaptureDmDiagnosticInput();var selected=DmDiagnosticIdentity.Select(input,id);if(dmDiagnosticIdentity==null||selected==null||dmDiagnosticIdentity.Key!=selected.Key){dmHistory.Clear();dmGraphsPaused=false;}dmDiagnosticIdentity=selected;dmDiagnosticInput=input;}
    DiagnosticUiSnapshot ReadDmDiagnostics(){if(dmDiagnosticInput==null)dmDiagnosticInput=CaptureDmDiagnosticInput();return DmDiagnosticProjection.Read(dmDiagnosticIdentity,dmDiagnosticInput,dmHistory);}
    void BindDmViewRefresh(Window owner){
        EventHandler refresh=(s,e)=>{
            if(!owner.IsVisible||diagnosticContext.Section!=DiagnosticSection.DirectMessageCalls||dmDeveloperPreview||DateTime.UtcNow<nextDmViewUtc)return;nextDmViewUtc=DateTime.UtcNow.AddSeconds(1);
            dmDiagnosticInput=CaptureDmDiagnosticInput();if(dmDiagnosticIdentity==null&&String.IsNullOrEmpty(diagnosticContext.StableId))dmDiagnosticIdentity=DmDiagnosticIdentity.Select(dmDiagnosticInput,"");if(dmDiagnosticIdentity!=null){if(dmDiagnosticIdentity.Scope!=dmDiagnosticInput.Scope){dmHistory.Clear();dmGraphsPaused=false;}else{dmHistory.Paused=dmGraphsPaused;dmHistory.Observe(dmDiagnosticIdentity,dmDiagnosticInput,DateTime.UtcNow);}}
            dmViewSnapshot=ReadDmDiagnostics();diagnosticBanner.Text=dmViewSnapshot.Banner;foreach(var action in dmViewRefreshes.ToArray())action();RefreshDiagnosticFooter();
        };
        owner.Loaded+=(s,e)=>timer.Tick+=refresh;owner.Closed+=(s,e)=>{timer.Tick-=refresh;dmViewRefreshes.Clear();dmHistory.Clear();dmDiagnosticInput=null;dmDiagnosticIdentity=null;dmGraphsPaused=false;};
    }
    void RenderDmDiagnostics(){
        dmViewRefreshes.Clear();dmDiagnosticInput=CaptureDmDiagnosticInput();if(dmDiagnosticIdentity!=null&&dmDiagnosticIdentity.Scope!=dmDiagnosticInput.Scope)dmHistory.Clear();if(dmDiagnosticIdentity==null)dmDiagnosticIdentity=DmDiagnosticIdentity.Select(dmDiagnosticInput,diagnosticContext.StableId);dmViewSnapshot=ReadDmDiagnostics();diagnosticBanner.Text=dmViewSnapshot.Banner;
        for(int i=0;i<dmViewSnapshot.Pages.Length;i++){
            int page=i;var content=new StackPanel{Margin=new Thickness(8)};
            for(int j=0;j<dmViewSnapshot.Pages[i].Panels.Length;j++){
                int panel=j;var data=dmViewSnapshot.Pages[i].Panels[j];var body=new StackPanel{Margin=new Thickness(10)};
                for(int k=0;k<data.Metrics.Length;k++){
                    int metric=k;var line=new Grid{Margin=new Thickness(0,diagnosticCompact?2:5,0,diagnosticCompact?2:5)};line.ColumnDefinitions.Add(new ColumnDefinition());line.ColumnDefinitions.Add(new ColumnDefinition());line.Children.Add(new TextBlock{Text=data.Metrics[k].Label,TextWrapping=TextWrapping.Wrap,FontSize=diagnosticTextSize,Foreground=Brush("#A8B9C8")});var value=new TextBlock{Text=data.Metrics[k].Value,TextWrapping=TextWrapping.Wrap,FontSize=diagnosticTextSize,Foreground=Brush("#E7F2FA")};Grid.SetColumn(value,1);line.Children.Add(value);body.Children.Add(line);dmViewRefreshes.Add(()=>value.Text=dmViewSnapshot.Pages[page].Panels[panel].Metrics[metric].Value);
                }
                if(data.Title=="Voice observed rate graph")body.Children.Add(DmGraph(false));if(data.Title=="Screen observed FPS graph")body.Children.Add(DmGraph(true));
                var title=new TextBlock{Text=data.Title,FontSize=15,FontWeight=FontWeights.SemiBold,Foreground=Brush("#83CCFC"),Margin=new Thickness(10)};var card=new StackPanel();if(data.Advanced)card.Children.Add(new Expander{Header=title,Content=body,IsExpanded=dmAdvanced});else{card.Children.Add(title);card.Children.Add(body);}content.Children.Add(new Border{Child=card,Background=Brush("#111F2B"),BorderBrush=Brush("#2B4355"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(8),Margin=new Thickness(0,0,0,10)});
            }
            if(dmViewSnapshot.Pages[i].Tab=="Settings"){
                var range=new ComboBox{ItemsSource=new[]{"Last minute","Last 5 minutes"},SelectedIndex=dmGraphSeconds==60?0:1,Margin=new Thickness(10)};range.SelectionChanged+=(s,e)=>dmGraphSeconds=range.SelectedIndex==0?60:300;content.Children.Add(range);
                var advanced=new CheckBox{Content="Expand advanced local audio measurements",IsChecked=dmAdvanced,Margin=new Thickness(10)};advanced.Click+=(s,e)=>{dmAdvanced=advanced.IsChecked==true;RenderDmDiagnostics();};content.Children.Add(advanced);
            }
            ((TabItem)groupDiagnosticTabs.Items[i]).Content=new ScrollViewer{Content=content,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};
        }
    }
    FrameworkElement DmGraph(bool screen){
        var canvas=new Canvas{Height=125,Margin=new Thickness(8),Background=Brush("#0C1720"),ToolTip=screen?"Distinct capture/encode/received access-unit/presented FPS":"Local encoded Opus and incoming observed RTP Mbps, not wire bitrate"};
        Action draw=()=>{canvas.Children.Clear();var points=dmHistory.Points.Where(p=>(DateTime.UtcNow-p.Utc).TotalSeconds<=dmGraphSeconds).ToArray();Func<DmDiagnosticPoint,double?>[] series=screen?new Func<DmDiagnosticPoint,double?>[]{p=>p.CaptureFps,p=>p.EncodeFps,p=>p.ReceivedAuFps,p=>p.PresentedFps}:new Func<DmDiagnosticPoint,double?>[]{p=>p.VoiceEncoded,p=>p.VoiceRtp};if(points.Length<2||!points.Any(p=>series.Any(f=>f(p).HasValue))){canvas.Children.Add(new TextBlock{Text="Waiting for samples / N/A — no fabricated history",Foreground=Brush("#A8B9C8")});return;}double max=Math.Max(.001,points.SelectMany(p=>series.Select(f=>f(p)??0)).Max()),seconds=Math.Max(1,(points.Last().Utc-points.First().Utc).TotalSeconds);string[] colors={"#65D8AA","#58B7FF","#D780FF","#FFC16D"};for(int index=0;index<series.Length;index++){var line=new System.Windows.Shapes.Polyline{Stroke=Brush(colors[index]),StrokeThickness=2};foreach(var p in points){var value=series[index](p);if(value.HasValue)line.Points.Add(new Point((p.Utc-points.First().Utc).TotalSeconds/seconds*Math.Max(1,canvas.ActualWidth),100-75*value.Value/max));else{if(line.Points.Count>0)canvas.Children.Add(line);line=new System.Windows.Shapes.Polyline{Stroke=Brush(colors[index]),StrokeThickness=2};}}canvas.Children.Add(line);}canvas.Children.Add(new TextBlock{Text=(screen?"FPS: green capture / blue encode / purple received AU / amber presented":"Mbps: green encoded Opus / blue received RTP")+(dmGraphsPaused?" · paused":""),Foreground=Brush("#83CCFC"),TextWrapping=TextWrapping.Wrap});};canvas.Loaded+=(s,e)=>draw();canvas.SizeChanged+=(s,e)=>draw();dmViewRefreshes.Add(draw);return canvas;
    }
}
}
