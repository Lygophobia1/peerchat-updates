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
    TabControl diagnosticNavigation;ComboBox diagnosticExamples;Style diagnosticSecondaryTabs;TextBlock diagnosticBanner;bool diagnosticCompact;double diagnosticTextSize=13;
    // Window-local styles: do not change tabs or dropdowns elsewhere in PeerChat.
    static Style DiagnosticTabStyle(bool primary){
        string xaml=@"<Style xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='TabItem'>
          <Setter Property='Foreground' Value='#B7C9D9'/><Setter Property='Background' Value='#13222F'/><Setter Property='BorderBrush' Value='#263B4D'/>
          <Setter Property='FontSize' Value='SIZE'/><Setter Property='FontWeight' Value='SemiBold'/><Setter Property='Padding' Value='PAD'/><Setter Property='MinHeight' Value='HEIGHT'/><Setter Property='Margin' Value='0,0,8,8'/>
          <Setter Property='Template'><Setter.Value><ControlTemplate TargetType='TabItem'>
            <Border x:Name='Pill' CornerRadius='RADIUS' Background='{TemplateBinding Background}' BorderBrush='{TemplateBinding BorderBrush}' BorderThickness='1' Padding='{TemplateBinding Padding}'>
              <ContentPresenter ContentSource='Header' VerticalAlignment='Center' HorizontalAlignment='Center' RecognizesAccessKey='True' TextElement.Foreground='{TemplateBinding Foreground}'/>
            </Border><ControlTemplate.Triggers>
              <Trigger Property='IsMouseOver' Value='True'><Setter TargetName='Pill' Property='Background' Value='#243E53'/><Setter Property='Foreground' Value='#FFFFFF'/></Trigger>
              <Trigger Property='IsSelected' Value='True'><Setter TargetName='Pill' Property='Background' Value='ACTIVE'/><Setter TargetName='Pill' Property='BorderBrush' Value='#68ABEF'/><Setter Property='Foreground' Value='#FFFFFF'/></Trigger>
              <Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='Pill' Property='BorderBrush' Value='#CEE8FF'/></Trigger>
              <Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='0.45'/></Trigger>
            </ControlTemplate.Triggers>
          </ControlTemplate></Setter.Value></Setter>
        </Style>";
        return (Style)System.Windows.Markup.XamlReader.Parse(xaml.Replace("SIZE",primary?"14":"13").Replace("PAD",primary?"18,10":"15,8").Replace("HEIGHT",primary?"44":"38").Replace("RADIUS",primary?"12":"9").Replace("ACTIVE",primary?"#245DA8":"#23445F"));
    }
    static ControlTemplate DiagnosticTabContainer(bool primary){
        string xaml=primary?@"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='TabControl'><ItemsPresenter/></ControlTemplate>":@"<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='TabControl'><DockPanel LastChildFill='True'><ItemsPresenter DockPanel.Dock='Top'/><ContentPresenter x:Name='PART_SelectedContentHost' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' ContentSource='SelectedContent' Margin='0,4,0,0'/></DockPanel></ControlTemplate>";
        return (ControlTemplate)System.Windows.Markup.XamlReader.Parse(xaml);
    }
    static ItemsPanelTemplate DiagnosticTabPanel(){return (ItemsPanelTemplate)System.Windows.Markup.XamlReader.Parse("<ItemsPanelTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'><WrapPanel IsItemsHost='True'/></ItemsPanelTemplate>");}
    void AddDiagnosticEntry(StackPanel panel,DiagnosticSection section,Func<string> identity){var button=new Button{Content="Open Diagnostics",Margin=new Thickness(0,6,0,6),Padding=new Thickness(10,6,10,6),HorizontalAlignment=HorizontalAlignment.Left};button.Click+=(s,e)=>OpenContextDiagnostics(section,identity());panel.Children.Add(button);}
    void BuildDiagnosticNavigation(DockPanel root){
        root.Background=Brush("#0D1720");var shell=new StackPanel{Margin=new Thickness(0,0,0,12)};DockPanel.SetDock(shell,Dock.Top);root.Children.Add(shell);
        shell.Children.Add(new TextBlock{Text="Diagnostics & Graphs",FontSize=22,FontWeight=FontWeights.SemiBold,Margin=new Thickness(0,0,0,10)});
        diagnosticSecondaryTabs=DiagnosticTabStyle(false);
        diagnosticNavigation=new TabControl{Focusable=true,ItemContainerStyle=DiagnosticTabStyle(true),Template=DiagnosticTabContainer(true),ItemsPanel=DiagnosticTabPanel(),ToolTip="Diagnostics context"};foreach(DiagnosticSection section in Enum.GetValues(typeof(DiagnosticSection)))diagnosticNavigation.Items.Add(new TabItem{Header=DiagnosticNavigation.Label(section)});shell.Children.Add(diagnosticNavigation);
        System.Windows.Automation.AutomationProperties.SetName(diagnosticNavigation,"Diagnostics context tabs");
        var row=new WrapPanel{Margin=new Thickness(0,2,0,0)};shell.Children.Add(row);
        diagnosticExamples=new ComboBox{MinWidth=145,Margin=new Thickness(0,0,10,6),ToolTip="Sample layout state (never changes the real operation)"};foreach(var state in Enum.GetValues(typeof(DiagnosticExampleState)))diagnosticExamples.Items.Add(state);diagnosticExamples.SelectedIndex=0;row.Children.Add(diagnosticExamples);
        BuildConnectionPreviewSelector(row);
        var compact=new CheckBox{Content="Compact cards",VerticalAlignment=VerticalAlignment.Center,IsChecked=diagnosticCompact,Margin=new Thickness(0,4,12,6)};compact.Checked+=(s,e)=>{diagnosticCompact=true;RefreshDiagnosticLayout();};compact.Unchecked+=(s,e)=>{diagnosticCompact=false;RefreshDiagnosticLayout();};row.Children.Add(compact);
        row.Children.Add(new TextBlock{Text="Text size",VerticalAlignment=VerticalAlignment.Center,Margin=new Thickness(0,4,8,6),Foreground=Brush("#A8B9C8")});var size=new Slider{Minimum=12,Maximum=18,VerticalAlignment=VerticalAlignment.Center,Value=diagnosticTextSize,Width=100,ToolTip="Display text size",Margin=new Thickness(0,3,0,6)};size.ValueChanged+=(s,e)=>{diagnosticTextSize=e.NewValue;RefreshDiagnosticLayout();};row.Children.Add(size);
        diagnosticBanner=new TextBlock{TextWrapping=TextWrapping.Wrap,Foreground=Brush("#83CCFC"),Margin=new Thickness(0,4,0,0)};shell.Children.Add(diagnosticBanner);
        diagnosticNavigation.SelectionChanged+=(s,e)=>{var section=(DiagnosticSection)diagnosticNavigation.SelectedIndex;if(section!=diagnosticContext.Section)diagnosticContext=new DiagnosticContext(section,"");RefreshDiagnosticLayout();};
        diagnosticExamples.SelectionChanged+=(s,e)=>{diagnosticExampleState=(DiagnosticExampleState)diagnosticExamples.SelectedItem;RefreshDiagnosticLayout();};
    }
    void OpenContextDiagnostics(DiagnosticSection section,string stableId){
        if(section==DiagnosticSection.HistorySync)SelectHistoryDiagnosticScope();if(section==DiagnosticSection.GroupCalls)SelectGroupDiagnosticIdentity(stableId);if(section==DiagnosticSection.DirectMessageCalls&&!dmDeveloperPreview)SelectDmDiagnosticIdentity(stableId);diagnosticContext=new DiagnosticContext(section,stableId);if(groupDiagnosticWindow==null)OpenGroupDiagnostics();
        diagnosticNavigation.SelectedIndex=(int)section;RefreshDiagnosticLayout();if(section==DiagnosticSection.GroupCalls){nextGroupTelemetry=DateTime.MinValue;RefreshGroupTelemetry();}groupDiagnosticWindow.Activate();
    }
    void RefreshDiagnosticFooter(){
        if(groupDiagnosticWindow==null||groupDiagnosticWindow.Content==null)return;
        bool group=diagnosticContext.Section==DiagnosticSection.GroupCalls;bool file=diagnosticContext.Section==DiagnosticSection.FileTransfers&&!transfersDeveloperPreview;bool dm=diagnosticContext.Section==DiagnosticSection.DirectMessageCalls&&!dmDeveloperPreview;bool live=group||file||dm||diagnosticContext.Section==DiagnosticSection.HistorySync||diagnosticContext.Section==DiagnosticSection.Connections&&!connectionsDeveloperPreview;
        bool active=group&&(groupTelemetry.ScreenActive||groupTelemetry.VoiceActive);
        bool graphAvailable=dm?dmHistory.Points.Count>1&&dmHistory.Points.Any(p=>p.VoiceEncoded.HasValue||p.VoiceRtp.HasValue||p.CaptureFps.HasValue||p.PresentedFps.HasValue):file?transferDiagnosticHistory.Samples.Count>1:active&&(graphsPaused||groupSamples.Any(p=>p.EncodedFps.HasValue||p.UploadMbps.HasValue||p.DownloadMbps.HasValue));
        foreach(var button in ((DockPanel)groupDiagnosticWindow.Content).Children.OfType<WrapPanel>().SelectMany(p=>p.Children.OfType<Button>())){
            if(Object.Equals(button.Tag,"Copy Diagnostics"))button.Content=live?"Copy Diagnostics":"Copy sample preview";
            if(Object.Equals(button.Tag,"Export…"))button.Content=live?"Export…":"Export sample preview…";
            if(Object.Equals(button.Tag,"Pause graphs")){button.IsEnabled=graphAvailable;button.Content=(dm?dmGraphsPaused:file?transferDiagnosticHistory.Paused:graphsPaused)?"Resume graphs":"Pause graphs";button.ToolTip=dm?"Pause only observed DM graph history; calls and live counters continue.":file?"Pause only observed transfer graph samples; the transfer continues.":!group?(live?"N/A — this section has no connected live graph measurements.":"No live graphs in sample-preview sections."):!active?"No active live Group Calls graphs.":graphAvailable?"Pause or resume existing live graph samples.":"Live graph measurements are not available yet.";ToolTipService.SetShowOnDisabled(button,true);}
        }
    }
    void RefreshDiagnosticLayout(){
        if(groupDiagnosticTabs==null||diagnosticNavigation==null||diagnosticNavigation.SelectedIndex<0)return;
        bool history=diagnosticContext.Section==DiagnosticSection.HistorySync;bool live=diagnosticContext.Section==DiagnosticSection.GroupCalls;bool connections=diagnosticContext.Section==DiagnosticSection.Connections;diagnosticExamples.Visibility=history||live||connections||diagnosticContext.Section==DiagnosticSection.FileTransfers&&!transfersDeveloperPreview||diagnosticContext.Section==DiagnosticSection.DirectMessageCalls&&!dmDeveloperPreview?Visibility.Collapsed:Visibility.Visible;connectionPreviewExamples.Visibility=connections&&connectionsDeveloperPreview?Visibility.Visible:Visibility.Collapsed;connectionPreviewStatePanel.Visibility=connections&&connectionsDeveloperPreview?Visibility.Visible:Visibility.Collapsed;
        RefreshDiagnosticFooter();
        diagnosticBanner.Text=live?"LIVE GROUP TELEMETRY · existing native counters · unavailable measurements are N/A":"SAMPLE / LAYOUT PREVIEW · not live measurements";
        if(!String.IsNullOrEmpty(diagnosticContext.StableId))diagnosticBanner.Text+="\nSelected context: "+diagnosticContext.StableId;
        groupDiagnosticWindow.Title="PeerChat — "+DiagnosticNavigation.Label(diagnosticContext.Section)+" Diagnostics";
        var selected=groupDiagnosticTabs.SelectedItem as TabItem;string selectedName=selected==null?"Overview":Convert.ToString(selected.Header);
        groupDiagnosticTabs.Items.Clear();foreach(string tab in DiagnosticNavigation.Tabs(diagnosticContext.Section)){var item=new TabItem{Header=tab};groupDiagnosticTabs.Items.Add(item);if(tab==selectedName)groupDiagnosticTabs.SelectedItem=item;}if(groupDiagnosticTabs.SelectedIndex<0)groupDiagnosticTabs.SelectedIndex=0;
        if(live){UpdateGroupDiagnosticWindow();return;}
        if(history){RenderHistoryDiagnostics();return;}
        if(connections){RenderConnectionPreview();return;}
        if(diagnosticContext.Section==DiagnosticSection.FileTransfers&&!transfersDeveloperPreview){RenderTransferDiagnostics();return;}
        if(diagnosticContext.Section==DiagnosticSection.DirectMessageCalls&&!dmDeveloperPreview){RenderDmDiagnostics();return;}
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
        if(diagnosticContext.Section==DiagnosticSection.Connections)return ConnectionPreviewQuery.Export(ReadConnectionsView());
        if(diagnosticContext.Section==DiagnosticSection.HistorySync){UpdateHistoryDiagnosticSnapshot();return HistoryDiagnosticProjection.Export(historyDiagnosticView);}
        if(diagnosticContext.Section==DiagnosticSection.FileTransfers&&!transfersDeveloperPreview)return TransferDiagnosticProjection.Export(ReadTransferDiagnostics());
        if(diagnosticContext.Section==DiagnosticSection.DirectMessageCalls&&!dmDeveloperPreview){dmDiagnosticInput=CaptureDmDiagnosticInput();return DmDiagnosticProjection.Export(ReadDmDiagnostics());}
        var snapshot=diagnosticLayoutProvider.Read(diagnosticContext,diagnosticExampleState);
        return snapshot.Banner+"\nSection: "+DiagnosticNavigation.Label(diagnosticContext.Section)+"\nContext: "+diagnosticContext.StableId+"\n"+String.Join("\n",snapshot.Pages.SelectMany(p=>p.Panels.Select(panel=>p.Tab+" / "+panel.Title+"\n"+String.Join("\n",panel.Metrics.Select(m=>m.Label+": "+m.Value)))));
    }
}
}
