using System;
using System.Linq;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace PeerChatDesktop {
public partial class DesktopApplication {
    bool transfersDeveloperPreview=Environment.GetEnvironmentVariable("PEERCHAT_DIAGNOSTICS_TRANSFERS_PREVIEW")=="1";DateTime transferSnapshotUtc;TransferDiagnosticIdentity transferDiagnosticIdentity;CoreAdapter.FileTransferData[] transferDiagnosticRows=new CoreAdapter.FileTransferData[0];string transferDiagnosticScope="",transferDiagnosticCounters="";
    readonly TransferDiagnosticHistory transferDiagnosticHistory=new TransferDiagnosticHistory();readonly List<Action> transferViewRefreshes=new List<Action>();
    DiagnosticUiSnapshot transferViewSnapshot;bool transferMegabytes,transferFollow=true,transferAdvanced;int transferGraphSeconds=300;DateTime nextTransferViewUtc;
    string TransferScope(){return "profile:"+(localProfile==null?"local":localProfile.Id)+"/profile-generation:"+profileSessionGeneration+"/local-device:"+(model.OwnId??"")+"/account:"+(discovery==null?0:discovery.AccountId)+"/authentication-generation:"+authenticatedFriendSnapshotGeneration;}
    void CacheTransferDiagnostics(CoreAdapter.FileTransferData[] rows,string counters){transferSnapshotUtc=DateTime.UtcNow;transferDiagnosticRows=rows;transferDiagnosticCounters=counters;transferDiagnosticScope=TransferScope();}
    void OpenTransferDiagnostics(CoreAdapter.FileTransferData row){
        var next=new TransferDiagnosticIdentity(TransferScope(),row);if(transferDiagnosticIdentity==null||transferDiagnosticIdentity.Key!=next.Key)transferDiagnosticHistory.Clear();transferDiagnosticIdentity=next;OpenContextDiagnostics(DiagnosticSection.FileTransfers,row.id);
    }
    DiagnosticUiSnapshot ReadTransferDiagnostics(){
        string scope=TransferScope();var rows=scope==transferDiagnosticScope&&(DateTime.UtcNow-transferSnapshotUtc).TotalSeconds<=3?transferDiagnosticRows:new CoreAdapter.FileTransferData[0];string route="N/A — exact transfer route binding not exposed";
        // Associated control owner is useful, but is NOT proof of this file's exact route binding.
        string associated="No active associated peer control session";
        var selected=transferDiagnosticIdentity==null?null:rows.FirstOrDefault(r=>transferDiagnosticIdentity.Matches(scope,r));
        var conversation=selected==null?null:model.FindByPeer(selected.peer);
        if(conversation!=null){var owner=connectionAttempts.For(conversation);if(owner.HasLiveSession)associated="Authenticated peer control owner: epoch="+owner.SessionEpoch+" winner="+owner.LiveConnectionId+" source="+owner.LiveSource+" route="+owner.LiveRoute+" (not an exact transfer route binding)";}
        return TransferDiagnosticProjection.Read(transferDiagnosticIdentity,scope,rows,scope==transferDiagnosticScope?transferDiagnosticCounters:"",route,transferMegabytes,transferDiagnosticHistory,associated);
    }
    void BindTransferViewRefresh(Window owner){
        EventHandler refresh=(s,e)=>{
            if(!owner.IsVisible||diagnosticContext.Section!=DiagnosticSection.FileTransfers||transfersDeveloperPreview||DateTime.UtcNow<nextTransferViewUtc)return;nextTransferViewUtc=DateTime.UtcNow.AddSeconds(1);
            var row=transferDiagnosticScope==TransferScope()&&(DateTime.UtcNow-transferSnapshotUtc).TotalSeconds<=3&&transferDiagnosticIdentity!=null?transferDiagnosticRows.SingleOrDefault(r=>transferDiagnosticIdentity.Matches(TransferScope(),r)):null;
            if(row!=null)transferDiagnosticHistory.Observe(transferDiagnosticIdentity,row,DateTime.UtcNow);else transferDiagnosticHistory.Clear();
            // Frozen display preferences never permit stale/profile-switched data to remain visible.
            if(transferFollow||row==null){transferViewSnapshot=ReadTransferDiagnostics();foreach(var update in transferViewRefreshes.ToArray())update();diagnosticBanner.Text=transferViewSnapshot.Banner;}RefreshDiagnosticFooter();
        };
        owner.Loaded+=(s,e)=>timer.Tick+=refresh;owner.Closed+=(s,e)=>{timer.Tick-=refresh;transferViewRefreshes.Clear();transferDiagnosticHistory.Clear();transferDiagnosticIdentity=null;};
    }
    void RenderTransferDiagnostics(){
        transferViewRefreshes.Clear();var snapshot=ReadTransferDiagnostics();transferViewSnapshot=snapshot;diagnosticBanner.Text=snapshot.Banner;
        for(int i=0;i<snapshot.Pages.Length;i++){
            int page=i;var content=new StackPanel{Margin=new Thickness(8)};
            for(int j=0;j<snapshot.Pages[i].Panels.Length;j++){
                int panel=j;var data=snapshot.Pages[i].Panels[j];var body=new StackPanel{Margin=new Thickness(10)};
                for(int k=0;k<data.Metrics.Length;k++){
                    int metric=k;var line=new Grid{Margin=new Thickness(0,diagnosticCompact?2:5,0,diagnosticCompact?2:5)};line.ColumnDefinitions.Add(new ColumnDefinition());line.ColumnDefinitions.Add(new ColumnDefinition());
                    line.Children.Add(new TextBlock{Text=data.Metrics[k].Label,TextWrapping=TextWrapping.Wrap,FontSize=diagnosticTextSize,Foreground=Brush("#A8B9C8")});var value=new TextBlock{Text=data.Metrics[k].Value,TextWrapping=TextWrapping.Wrap,FontSize=diagnosticTextSize,Foreground=Brush("#E7F2FA")};Grid.SetColumn(value,1);line.Children.Add(value);body.Children.Add(line);
                    transferViewRefreshes.Add(()=>value.Text=transferViewSnapshot.Pages[page].Panels[panel].Metrics[metric].Value);
                }
                if(data.Title=="Confirmed throughput graph")body.Children.Add(TransferGraph());
                var title=new TextBlock{Text=data.Title,FontSize=15,FontWeight=FontWeights.SemiBold,Foreground=Brush("#83CCFC"),Margin=new Thickness(10)};var card=new StackPanel();if(data.Advanced)card.Children.Add(new Expander{Header=title,Content=body,IsExpanded=transferAdvanced});else{card.Children.Add(title);card.Children.Add(body);}content.Children.Add(new Border{Child=card,Background=Brush("#111F2B"),BorderBrush=Brush("#2B4355"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(8),Margin=new Thickness(0,0,0,10)});
            }
            if(snapshot.Pages[i].Tab=="Settings"){
                var units=new ComboBox{ItemsSource=new[]{"Mbps","MB/s"},SelectedIndex=transferMegabytes?1:0,Margin=new Thickness(10),ToolTip="Payload rate units only"};units.SelectionChanged+=(s,e)=>{transferMegabytes=units.SelectedIndex==1;RenderTransferDiagnostics();};content.Children.Add(units);
                var range=new ComboBox{ItemsSource=new[]{"Last minute","Last 5 minutes"},SelectedIndex=transferGraphSeconds==60?0:1,Margin=new Thickness(10)};range.SelectionChanged+=(s,e)=>{transferGraphSeconds=range.SelectedIndex==0?60:300;};content.Children.Add(range);
                var follow=new CheckBox{Content="Follow live snapshots",IsChecked=transferFollow,Margin=new Thickness(10)};follow.Click+=(s,e)=>transferFollow=follow.IsChecked==true;content.Children.Add(follow);
                var advanced=new CheckBox{Content="Expand advanced measurements",IsChecked=transferAdvanced,Margin=new Thickness(10)};advanced.Click+=(s,e)=>{transferAdvanced=advanced.IsChecked==true;RenderTransferDiagnostics();};content.Children.Add(advanced);
            }
            ((TabItem)groupDiagnosticTabs.Items[i]).Content=new ScrollViewer{Content=content,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};
        }
    }
    FrameworkElement TransferGraph(){
        var canvas=new Canvas{Height=110,Margin=new Thickness(8),Background=Brush("#0C1720"),ToolTip="Confirmed native payload rate, observed ~once per second; no fabricated pre-open history"};
        Action draw=()=>{canvas.Children.Clear();var points=transferDiagnosticHistory.Samples.Where(p=>(DateTime.UtcNow-p.Utc).TotalSeconds<=transferGraphSeconds).ToArray();if(points.Length<2){canvas.Children.Add(new TextBlock{Text="Waiting for observed samples — not measured before opening",Foreground=Brush("#A8B9C8")});return;}double max=Math.Max(.001,points.Max(p=>p.Mbps));var line=new System.Windows.Shapes.Polyline{Stroke=Brush("#65D8AA"),StrokeThickness=2};double span=Math.Max(1,(points.Last().Utc-points.First().Utc).TotalSeconds);foreach(var p in points)line.Points.Add(new Point((p.Utc-points.First().Utc).TotalSeconds/span*Math.Max(1,canvas.ActualWidth),90-70*p.Mbps/max));canvas.Children.Add(line);canvas.Children.Add(new TextBlock{Text=(max/(transferMegabytes?8:1)).ToString("0.###")+(transferMegabytes?" MB/s":" Mbps")+" peak · "+points.Length+" observed samples"+(transferDiagnosticHistory.Paused?" · paused":""),Foreground=Brush("#83CCFC")});};canvas.Loaded+=(s,e)=>draw();canvas.SizeChanged+=(s,e)=>draw();transferViewRefreshes.Add(draw);return canvas;
    }
}
}
