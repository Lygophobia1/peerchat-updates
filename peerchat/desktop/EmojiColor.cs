using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
namespace PeerChatDesktop {
// Trusted embedded PNGs only. Unicode remains the sole message/draft identity.
public static class EmojiColor {
    const int CacheLimit=128;
    static readonly Dictionary<string,BitmapSource> hot=new Dictionary<string,BitmapSource>();
    static readonly Dictionary<string,WeakReference> visible=new Dictionary<string,WeakReference>();
    static readonly Queue<string> order=new Queue<string>();
    static readonly object gate=new object();
    static ZipArchive archive;
    public static string LastDecodeFailure {get;private set;}
    public static int CachedCount {get{lock(gate)return hot.Count;}}
    public static string Key(string text){var parts=new List<string>();for(int i=0;i<text.Length;i++){int cp=Char.IsSurrogatePair(text,i)?Char.ConvertToUtf32(text,i++):text[i];if(cp==0xfe0e)return null;if(cp!=0xfe0f)parts.Add(cp.ToString("x",System.Globalization.CultureInfo.InvariantCulture));}return String.Join("-",parts);}
    public static BitmapSource Get(string text){if(String.IsNullOrEmpty(text)||(text.Length==1&&text[0]<0x00a9))return null;string key=Key(text);if(key==null)return null;lock(gate){
        BitmapSource bitmap;if(hot.TryGetValue(key,out bitmap))return bitmap;
        WeakReference weak;if(visible.TryGetValue(key,out weak))bitmap=weak.Target as BitmapSource;
        if(bitmap==null){try{if(archive==null){var resource=typeof(EmojiColor).Assembly.GetManifestResourceStream("EmojiColor.zip");if(resource==null)return null;archive=new ZipArchive(resource,ZipArchiveMode.Read);}
            var entry=archive.GetEntry(key+".png");if(entry==null||entry.Length>256*1024)return null;
            using(var stream=entry.Open())using(var buffer=new MemoryStream()){stream.CopyTo(buffer);buffer.Position=0;var image=new BitmapImage();image.BeginInit();image.CacheOption=BitmapCacheOption.OnLoad;image.DecodePixelWidth=64;image.StreamSource=buffer;image.EndInit();image.Freeze();bitmap=image;}
            visible[key]=new WeakReference(bitmap);
        }catch(Exception error){LastDecodeFailure=error.GetType().Name;return null;}}
        if(hot.Count==CacheLimit)hot.Remove(order.Dequeue());hot[key]=bitmap;order.Enqueue(key);return bitmap;
    }}
    public static FrameworkElement Glyph(string text,double size){var source=Get(text);if(source==null)return new TextBlock{Text=text,FontFamily=new FontFamily("Segoe UI Emoji, Segoe UI Symbol"),FontSize=size};var image=new Image{Source=source,Width=size,Height=size,Stretch=Stretch.Uniform,Tag=text};AutomationProperties.SetName(image,text);return image;}
    public static void Append(TextBlock block,string text,double size=18){
        var boundaries=EmojiEditing.Boundaries(text);var plain=new StringBuilder();int images=0;
        for(int i=0;i<boundaries.Length-1;i++){string cluster=text.Substring(boundaries[i],boundaries[i+1]-boundaries[i]);var bitmap=images<256?Get(cluster):null;
            if(bitmap==null){plain.Append(cluster);continue;}
            if(plain.Length>0){block.Inlines.Add(new Run(plain.ToString()));plain.Clear();}
            var image=Glyph(cluster,size);block.Inlines.Add(new InlineUIContainer(image){BaselineAlignment=BaselineAlignment.TextBottom,Tag=cluster});images++;
        }
        if(plain.Length>0)block.Inlines.Add(new Run(plain.ToString()));
    }
    public static TextBlock Preview(string text,int limit=4096){text=text??"";if(text.Length>limit){string prefix=text.Substring(0,Math.Min(text.Length,limit+128));int cut=EmojiEditing.Boundaries(prefix).Last(x=>x<=limit);text=text.Substring(0,cut)+"\n… Open full for the complete, selectable Unicode text.";}var block=new TextBlock{TextWrapping=TextWrapping.Wrap,FontSize=16};AutomationProperties.SetName(block,text);Append(block,text);return block;}
    public static void DraftPreview(Panel panel,string text){panel.Children.Clear();string prefix=(text??"");if(prefix.Length>2048)prefix=prefix.Substring(0,2048);var bounds=EmojiEditing.Boundaries(prefix);for(int i=0;i<bounds.Length-1&&panel.Children.Count<24;i++){string cluster=prefix.Substring(bounds[i],bounds[i+1]-bounds[i]);if(Get(cluster)==null)continue;var glyph=Glyph(cluster,22);glyph.Margin=new Thickness(0,0,5,0);panel.Children.Add(glyph);}panel.Visibility=panel.Children.Count==0?Visibility.Collapsed:Visibility.Visible;}
}
}
