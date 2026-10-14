using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
namespace PeerChatDesktop {
public sealed class EmojiUsage {
    readonly string path; readonly byte[] entropy;
    public readonly List<string> Recent=new List<string>();
    public readonly Dictionary<string,int> Counts=new Dictionary<string,int>();
    public readonly HashSet<string> Favorites=new HashSet<string>(StringComparer.Ordinal);
    public EmojiUsage(string directory,string profile){
        path=String.IsNullOrEmpty(directory)?null:Path.Combine(directory,"emoji-usage.v1");
        entropy=Encoding.UTF8.GetBytes("PeerChat/emoji-usage/v1/"+profile);
        try{
            if(path==null||!File.Exists(path)||new FileInfo(path).Length>65536)return;
            var rows=Encoding.UTF8.GetString(ProtectedData.Unprotect(File.ReadAllBytes(path),entropy,DataProtectionScope.CurrentUser)).Split('\n');
            if(rows[0]!="PeerChat/emoji-usage/v1")return;
            foreach(var row in rows.Skip(1).Take(512)){
                var parts=row.Split('\t'); int count;
                if(parts.Length!=2)continue;
                if(parts[0]=="F"){if(EmojiCatalog.Contains(parts[1]))Favorites.Add(parts[1]);continue;}
                if(EmojiCatalog.Contains(parts[0])&&Int32.TryParse(parts[1],out count)&&count>0&&!Counts.ContainsKey(parts[0])){
                    Counts[parts[0]]=Math.Min(count,1000000);if(Recent.Count<24)Recent.Add(parts[0]);
                }
            }
        }catch{/* Supplemental preferences never block messages. */}
    }
    public void Used(string text){
        if(!EmojiCatalog.Contains(text))return;
        Recent.Remove(text);Recent.Insert(0,text);if(Recent.Count>24)Recent.RemoveAt(24);
        int n;Counts.TryGetValue(text,out n);Counts[text]=Math.Min(n+1,1000000);Save();
    }
    public void ToggleFavorite(string text){if(!EmojiCatalog.Contains(text))return;if(!Favorites.Remove(text))Favorites.Add(text);Save();}
    void Save(){
        if(path==null)return;string temp=null;
        try{
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            var ordered=Recent.Concat(Counts.Keys.Except(Recent).OrderBy(x=>x,StringComparer.Ordinal));
            var rows=ordered.Select(x=>x+"\t"+Counts[x]).Concat(Favorites.OrderBy(x=>x,StringComparer.Ordinal).Select(x=>"F\t"+x));
            var body="PeerChat/emoji-usage/v1\n"+String.Join("\n",rows);
            temp=path+"."+Guid.NewGuid().ToString("N")+".tmp";
            File.WriteAllBytes(temp,ProtectedData.Protect(Encoding.UTF8.GetBytes(body),entropy,DataProtectionScope.CurrentUser));
            if(File.Exists(path))File.Replace(temp,path,null);else File.Move(temp,path);temp=null;
        }catch{/* Missing/corrupt preferences are not authoritative message state. */}
        finally{if(temp!=null)try{File.Delete(temp);}catch{}}
    }
}
public sealed class EmojiPicker : Window {
    public string Selection {get;private set;}
    public Func<string,bool> Insert;
    readonly TextBox search=new TextBox();readonly ComboBox category=new ComboBox(),tone=new ComboBox();
    readonly UniformGrid grid=new UniformGrid{Columns=6,VerticalAlignment=VerticalAlignment.Top};
    readonly TextBlock status=new TextBlock();readonly ScrollViewer scroll=new ScrollViewer();readonly EmojiUsage usage;
    public IEnumerable<Button> EntryButtons {get{return grid.Children.OfType<Grid>().Select(tile=>tile.Children.OfType<Button>().First());}}
    public EmojiPicker(Window owner,EmojiUsage preferences){
        usage=preferences;Owner=owner;Title="Emoji";Width=430;Height=490;MinWidth=380;MinHeight=360;
        WindowStartupLocation=WindowStartupLocation.CenterOwner;ShowInTaskbar=false;
        Background=new SolidColorBrush(Color.FromRgb(17,25,32));Foreground=Brushes.White;FontFamily=new FontFamily("Segoe UI, Segoe UI Emoji");FontSize=14;
        Resources=new ResourceDictionary{MergedDictionaries={owner.Resources}};
        var toggleStyle=new Style(typeof(ToggleButton));toggleStyle.Setters.Add(new Setter(Control.ForegroundProperty,Brushes.White));Resources[typeof(ToggleButton)]=toggleStyle;
        var panel=new DockPanel{Margin=new Thickness(14)};Content=panel;
        var top=new StackPanel();DockPanel.SetDock(top,Dock.Top);panel.Children.Add(top);
        var header=new DockPanel{Margin=new Thickness(0,0,0,10)};
        var close=new Button{Content="×",Padding=new Thickness(9,2,9,2),FontSize=20,ToolTip="Close emoji picker (Escape)"};AutomationProperties.SetName(close,"Close emoji picker");close.Click+=(s,e)=>Close();DockPanel.SetDock(close,Dock.Right);header.Children.Add(close);
        header.Children.Add(new TextBlock{Text="Find an emoji",FontSize=20,FontWeight=FontWeights.SemiBold,VerticalAlignment=VerticalAlignment.Center});top.Children.Add(header);
        search.ToolTip="Search names and keywords";AutomationProperties.SetName(search,"Search emojis");search.Margin=new Thickness(0,0,0,10);top.Children.Add(search);
        var controls=new Grid();controls.ColumnDefinitions.Add(new ColumnDefinition());controls.ColumnDefinitions.Add(new ColumnDefinition{Width=new GridLength(150)});
        foreach(var c in new[]{"All","Favorites","Recent","Frequent"}.Concat(EmojiCatalog.All.Select(e=>e.Category).Distinct()))category.Items.Add(c);
        category.SelectedIndex=0;AutomationProperties.SetName(category,"Emoji category");controls.Children.Add(category);
        Grid.SetColumn(tone,1);tone.Margin=new Thickness(8,0,0,0);
        foreach(var name in new[]{"Default tone","Light","Medium-light","Medium","Medium-dark","Dark"})tone.Items.Add(name);
        tone.SelectedIndex=0;AutomationProperties.SetName(tone,"Skin tone");controls.Children.Add(tone);top.Children.Add(controls);
        DockPanel.SetDock(status,Dock.Bottom);status.Margin=new Thickness(0,10,0,0);status.FontSize=11;status.TextWrapping=TextWrapping.Wrap;panel.Children.Add(status);
        scroll.VerticalScrollBarVisibility=ScrollBarVisibility.Auto;scroll.HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled;scroll.Margin=new Thickness(0,10,0,0);scroll.Content=grid;panel.Children.Add(scroll);
        KeyboardNavigation.SetDirectionalNavigation(grid,KeyboardNavigationMode.Contained);
        search.TextChanged+=(s,e)=>Refresh();category.SelectionChanged+=(s,e)=>Refresh();tone.SelectionChanged+=(s,e)=>Refresh();
        PreviewKeyDown+=(s,e)=>{if(e.Key==Key.Escape){Close();e.Handled=true;}else if(search.IsKeyboardFocusWithin&&(e.Key==Key.Down||e.Key==Key.Enter)&&EntryButtons.Any()){EntryButtons.First().Focus();e.Handled=true;}};
        Loaded+=(s,e)=>{Refresh();search.Focus();};Deactivated+=(s,e)=>{if(IsVisible)Close();};
    }
    void Refresh(){
        grid.Children.Clear();string selected=category.SelectedItem as string;
        var rows=EmojiCatalog.Search(search.Text,new[]{"All","Favorites","Recent","Frequent"}.Contains(selected)?null:selected).ToArray();
        IEnumerable<string> texts;
        if(selected=="Recent"||selected=="Frequent"||selected=="Favorites"){
            var allowed=new HashSet<string>(rows.SelectMany(e=>Enumerable.Range(0,e.SkinTone?6:1).Select(e.Variant)));
            texts=(selected=="Recent"?(IEnumerable<string>)usage.Recent:selected=="Favorites"?usage.Favorites.OrderBy(x=>x,StringComparer.Ordinal):usage.Counts.OrderByDescending(p=>p.Value).ThenBy(p=>p.Key,StringComparer.Ordinal).Select(p=>p.Key)).Where(allowed.Contains);
            if(selected!="Favorites")texts=texts.Take(24);
        }else texts=rows.Select(e=>e.Variant(Math.Max(0,tone.SelectedIndex)));
        foreach(string text in texts){
            var entry=EmojiCatalog.All.First(e=>Enumerable.Range(0,e.SkinTone?6:1).Any(t=>e.Variant(t)==text));
            var tile=new Grid{Margin=new Thickness(2)};
            var button=new Button{Content=EmojiColor.Glyph(text,28),MinWidth=44,Height=54,Padding=new Thickness(3,3,14,3),ToolTip=entry.Name,Tag=text};
            AutomationProperties.SetName(button,entry.Name+" "+text);
            button.Click+=(s,e)=>{if(Insert!=null&&!Insert(text))return;Selection=text;usage.Used(text);};tile.Children.Add(button);
            var star=new Button{Content=usage.Favorites.Contains(text)?"★":"☆",Foreground=Brushes.Gold,FontSize=13,Padding=new Thickness(1),Width=18,Height=22,HorizontalAlignment=HorizontalAlignment.Right,VerticalAlignment=VerticalAlignment.Top,ToolTip="Star/unstar favorite (does not insert)"};
            AutomationProperties.SetName(star,(usage.Favorites.Contains(text)?"Remove ":"Add ")+entry.Name+" favorite");
            star.Click+=(s,e)=>{double offset=scroll.VerticalOffset;usage.ToggleFavorite(text);star.Content=usage.Favorites.Contains(text)?"★":"☆";AutomationProperties.SetName(star,(usage.Favorites.Contains(text)?"Remove ":"Add ")+entry.Name+" favorite");e.Handled=true;
                if(selected=="Favorites"){Refresh();scroll.UpdateLayout();scroll.ScrollToVerticalOffset(offset);}};
            tile.Children.Add(star);grid.Children.Add(tile);
        }
        status.Text=grid.Children.Count==0?(selected=="Favorites"?"Star emojis to keep them here. Favorites never count as an insertion.":"No matches. Try another category or keyword."):"Select as many as you like · ☆ favorites · Escape / × / outside click to close.";
    }
}
public static class EmojiComposer {
    public static void Insert(TextBox composer,string text,int start,int length){int from,to;EmojiEditing.Range(composer.Text,start,length,out from,out to);composer.BeginChange();try{composer.Select(from,to-from);composer.SelectedText=text;composer.CaretIndex=from+text.Length;}finally{composer.EndChange();}}
    public static bool Delete(TextBox box,bool backwards){int from,to;string text=box.Text;int caret=box.SelectionStart;if(box.SelectionLength>0)EmojiEditing.Range(text,caret,box.SelectionLength,out from,out to);else{var bounds=EmojiEditing.Boundaries(text);if(backwards){if(caret==0)return false;from=bounds.Last(b=>b<caret);to=bounds.First(b=>b>=caret);}else{if(caret==text.Length)return false;from=bounds.Last(b=>b<=caret);to=bounds.First(b=>b>caret);}}if(to-from<=1)return false;box.BeginChange();try{box.Select(from,to-from);box.SelectedText="";box.CaretIndex=from;}finally{box.EndChange();}return true;}
}
public partial class DesktopApplication {
    EmojiPicker emojiPicker;
    void OpenEmojiPicker(){
        var composer=Find<TextBox>("Composer");if(!composer.IsEnabled||composer.IsReadOnly)return;
        if(emojiPicker!=null){emojiPicker.Activate();return;}
        var profileGeneration=profileSessionGeneration;var selected=model.Selected;var selectedGroup=model.SelectedGroup;bool groups=model.Groups;
        string expected=composer.Text;int start=composer.SelectionStart,length=composer.SelectionLength;
        string usageDirectory=profileManager!=null&&localProfile!=null?profileManager.ProfilePath(localProfile.Id):directory;
        var usage=new EmojiUsage(usageDirectory,localProfile==null?"legacy:"+Path.GetFullPath(directory):localProfile.Id);
        var picker=new EmojiPicker(window,usage);emojiPicker=picker;
        picker.Insert=text=>{
            if(composer.Text!=expected||profileGeneration!=profileSessionGeneration||selected!=model.Selected||selectedGroup!=model.SelectedGroup||groups!=model.Groups||!composer.IsEnabled){picker.Close();return false;}
            EmojiComposer.Insert(composer,text,start,length);start=composer.CaretIndex;length=0;expected=composer.Text;return true;
        };
        picker.Closed+=(s,e)=>{if(ReferenceEquals(emojiPicker,picker))emojiPicker=null;window.Dispatcher.BeginInvoke(DispatcherPriority.Input,new Action(()=>{if(window.IsActive)composer.Focus();}));};
        picker.Show();
    }
}
}
