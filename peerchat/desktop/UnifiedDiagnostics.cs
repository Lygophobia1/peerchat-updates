using System;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
namespace PeerChatDesktop {
public partial class DesktopApplication {
    IDiagnosticLayoutProvider diagnosticLayoutProvider=new SampleDiagnosticLayoutProvider();
    DiagnosticContext diagnosticContext=new DiagnosticContext(DiagnosticSection.GroupCalls,"");
    DiagnosticExampleState diagnosticExampleState=DiagnosticExampleState.Active;
    ComboBox diagnosticNavigation,diagnosticExamples;TextBlock diagnosticBanner;bool diagnosticCompact;double diagnosticTextSize=13;
    void AddDiagnosticEntry(StackPanel panel,DiagnosticSection section,Func<string> identity){var button=new Button{Content="Open Diagnostics",Margin=new Thickness(0,6,0,6),Padding=new Thickness(10,6,10,6),HorizontalAlignment=HorizontalAlignment.Left};button.Click+=(s,e)=>OpenContextDiagnostics(section,identity());panel.Children.Add(button);}
    void BuildDiagnosticNavigation(DockPanel root){
        root.Background=Brush("#0D1720");var shell=new StackPanel{Margin=new Thickness(0,0,0,12)};DockPanel.SetDock(shell,Dock.Top);root.Children.Add(shell);
        shell.Children.Add(new TextBlock{Text="Diagnostics & Graphs",FontSize=22,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,0,0,10)});
        var row=new WrapPanel();shell.Children.Add(row);
        diagnosticNavigation=new ComboBox{MinWidth=225,Foreground=Brush("#EDF1F5"),Background=Brush("#172A38"),Margin=new Thickness(0,0,10,6),ToolTip="Diagnostics section"};foreach(DiagnosticSection section in Enum.GetValues(typeof(DiagnosticSection)))diagnosticNavigation.Items.Add(new ComboBoxItem{Content=new TextBlock{Text=DiagnosticNavigation.Label(section),Foreground=Brush("#EDF1F5")},Foreground=Brush("#EDF1F5"),Background=Brush("#172A38")});row.Children.Add(diagnosticNavigation);
        diagnosticExamples=new ComboBox{MinWidth=145,Margin=new Thickness(0,0,10,6),ToolTip="Sample layout state (never changes the real operation)"};foreach(var state in Enum.GetValues(typeof(DiagnosticExampleState)))diagnosticExamples.Items.Add(state);diagnosticExamples.SelectedIndex=0;row.Children.Add(diagnosticExamples);
        var compact=new CheckBox{Content="Compact cards",IsChecked=diagnosticCompact,Margin=new Thickness(0,4,12,6)};compact.Checked+=(s,e)=>{diagnosticCompact=true;RefreshDiagnosticLayout();};compact.Unchecked+=(s,e)=>{diagnosticCompact=false;RefreshDiagnosticLayout();};row.Children.Add(compact);
        row.Children.Add(new TextBlock{Text="Text size",Margin=new Thickness(0,4,8,6),Foreground=Brush("#A8B9C8")});var size=new Slider{Minimum=12,Maximum=18,Value=diagnosticTextSize,Width=100,ToolTip="Display text size",Margin=new Thickness(0,3,0,6)};size.ValueChanged+=(s,e)=>{diagnosticTextSize=e.NewValue;RefreshDiagnosticLayout();};row.Children.Add(size);
        diagnosticBanner=new TextBlock{TextWrapping=TextWrapping.Wrap,Foreground=Brush("#83CCFC"),Margin=new Thickness(0,4,0,0)};shell.Children.Add(diagnosticBanner);
        diagnosticNavigation.SelectionChanged+=(s,e)=>{var section=(DiagnosticSection)diagnosticNavigation.SelectedIndex;if(section!=diagnosticContext.Section)diagnosticContext=new DiagnosticContext(section,"");RefreshDiagnosticLayout();};
        diagnosticExamples.SelectionChanged+=(s,e)=>{diagnosticExampleState=(DiagnosticExampleState)diagnosticExamples.SelectedItem;RefreshDiagnosticLayout();};
    }
    void OpenContextDiagnostics(DiagnosticSection section,string stableId){
        diagnosticContext=new DiagnosticContext(section,stableId);if(groupDiagnosticWindow==null)OpenGroupDiagnostics();
        diagnosticNavigation.SelectedIndex=(int)section;RefreshDiagnosticLayout();groupDiagnosticWindow.Activate();
    }
    void RefreshDiagnosticLayout(){
        if(groupDiagnosticTabs==null||diagnosticNavigation==null||diagnosticNavigation.SelectedIndex<0)return;
        bool live=diagnosticContext.Section==DiagnosticSection.GroupCalls;diagnosticExamples.Visibility=live?Visibility.Collapsed:Visibility.Visible;
        diagnosticBanner.Text=live?"LIVE GROUP TELEMETRY · existing native counters · unavailable measurements are N/A":"SAMPLE / LAYOUT PREVIEW · not live measurements";
        if(!String.IsNullOrEmpty(diagnosticContext.StableId))diagnosticBanner.Text+="\nSelected context: "+diagnosticContext.StableId;
        groupDiagnosticWindow.Title="PeerChat — "+DiagnosticNavigation.Label(diagnosticContext.Section)+" Diagnostics";
        var selected=groupDiagnosticTabs.SelectedItem as TabItem;string selectedName=selected==null?"Overview":Convert.ToString(selected.Header);
        groupDiagnosticTabs.Items.Clear();foreach(string tab in DiagnosticNavigation.Tabs(diagnosticContext.Section)){var item=new TabItem{Header=tab,Background=Brush("#14222E"),Foreground=Brush("#83CCFC")};groupDiagnosticTabs.Items.Add(item);if(tab==selectedName)groupDiagnosticTabs.SelectedItem=item;}if(groupDiagnosticTabs.SelectedIndex<0)groupDiagnosticTabs.SelectedIndex=0;
        if(live){UpdateGroupDiagnosticWindow();return;}
        var snapshot=diagnosticLayoutProvider.Read(diagnosticContext,diagnosticExampleState);diagnosticBanner.Text=snapshot.Banner+(String.IsNullOrEmpty(diagnosticContext.StableId)?"":"\nSelected context: "+diagnosticContext.StableId);
        for(int i=0;i<snapshot.Pages.Length;i++){
            var content=new StackPanel{Margin=new Thickness(8)};
            foreach(var panel in snapshot.Pages[i].Panels){
                var body=new StackPanel{Margin=new Thickness(10)};foreach(var metric in panel.Metrics){var line=new Grid{Margin=new Thickness(0,diagnosticCompact?2:5,0,diagnosticCompact?2:5)};line.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(1,GridUnitType.Star)});line.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(1,GridUnitType.Star)});line.Children.Add(new TextBlock{Text=metric.Label,TextWrapping=TextWrapping.Wrap,FontSize=diagnosticTextSize,Foreground=Brush("#A8B9C8")});var value=new TextBlock{Text=metric.Value,TextWrapping=TextWrapping.Wrap,FontSize=diagnosticTextSize,Foreground=Brush("#E7F2FA"),ToolTip=metric.Explanation};Grid.SetColumn(value,1);line.Children.Add(value);body.Children.Add(line);}
                if(panel.Title.Contains("graph"))body.Children.Add(DiagnosticSampleGraph(panel.Samples));
                var title=new TextBlock{Text=panel.Title,FontSize=15,FontWeight=FontWeights.SemiBold,Foreground=Brush("#83CCFC"),Margin=new Thickness(10)};
                var card=new StackPanel();if(panel.Advanced)card.Children.Add(new Expander{Header=title,Content=body,IsExpanded=false});else{card.Children.Add(title);card.Children.Add(body);}
                content.Children.Add(new Border{Child=card,Background=Brush("#111F2B"),BorderBrush=Brush("#2B4355"),BorderThickness=new Thickness(1),CornerRadius=new CornerRadius(8),Margin=new Thickness(0,0,0,10)});
            }
            ((TabItem)groupDiagnosticTabs.Items[i]).Content=new ScrollViewer{Content=content,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};
        }
    }
    FrameworkElement DiagnosticSampleGraph(double?[] values){
        if(values.Length==0||!values.Any(v=>v.HasValue))return new TextBlock{Text="Graph unavailable · N/A",Margin=new Thickness(8),Foreground=Brush("#A8B9C8")};
        var canvas=new Canvas{Height=95,Background=Brush("#0C1720"),Margin=new Thickness(8),ToolTip="Sample graph · arbitrary layout values, not observed throughput or latency"};
        Action draw=()=>{canvas.Children.Clear();if(graphsPaused){canvas.Children.Add(new TextBlock{Text="Graph preview paused",Foreground=Brush("#83CCFC")});return;}var line=new System.Windows.Shapes.Polyline{Stroke=Brush("#65D8AA"),StrokeThickness=2};double max=Math.Max(1,values.Where(v=>v.HasValue).Max(v=>v.Value));for(int i=0;i<values.Length;i++)if(values[i].HasValue)line.Points.Add(new Point(i*Math.Max(1,canvas.ActualWidth)/Math.Max(1,values.Length-1),85-75*values[i].Value/max));canvas.Children.Add(line);canvas.Children.Add(new TextBlock{Text="SAMPLE",Foreground=Brush("#83CCFC")});};canvas.Loaded+=(s,e)=>draw();canvas.SizeChanged+=(s,e)=>draw();return canvas;
    }
    string CurrentDiagnosticExport(){
        if(diagnosticContext.Section==DiagnosticSection.GroupCalls)return GroupDiagnosticExport();
        var snapshot=diagnosticLayoutProvider.Read(diagnosticContext,diagnosticExampleState);
        return snapshot.Banner+"\nSection: "+DiagnosticNavigation.Label(diagnosticContext.Section)+"\nContext: "+diagnosticContext.StableId+"\n"+String.Join("\n",snapshot.Pages.SelectMany(p=>p.Panels.Select(panel=>p.Tab+" / "+panel.Title+"\n"+String.Join("\n",panel.Metrics.Select(m=>m.Label+": "+m.Value)))));
    }
}
}
