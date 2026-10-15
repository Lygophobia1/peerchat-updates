using System;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace PeerChatDesktop {
public static class ClipboardAttachments {
    public const int MaximumPixels=16*1024*1024,MaximumPngBytes=16*1024*1024;
    public static bool HasImage(IDataObject data){return data!=null&&!data.GetDataPresent(DataFormats.UnicodeText)&&!data.GetDataPresent(DataFormats.Text)&&(data.GetDataPresent(DataFormats.Bitmap)||data.GetDataPresent(DataFormats.Dib));}
    public static BitmapSource ReadImage(IDataObject data){
        if(data==null)throw new InvalidDataException("Clipboard image unavailable.");
        var bitmap=data.GetData(DataFormats.Bitmap) as BitmapSource;
        if(bitmap==null){
            object dib=data.GetData(DataFormats.Dib);byte[] bytes=dib as byte[];
            if(bytes==null&&dib is MemoryStream){var stream=(MemoryStream)dib;if(stream.Length<=64L*1024*1024)bytes=stream.ToArray();}
            if(bytes==null)throw new InvalidDataException("Clipboard bitmap unavailable.");
            bitmap=ReadDib(bytes);
        }
        Validate(bitmap);bitmap=bitmap.Clone();bitmap.Freeze();return bitmap;
    }
    public static byte[] EncodePng(BitmapSource image){
        Validate(image);var encoder=new PngBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(image));
        using(var output=new MemoryStream()){encoder.Save(output);if(output.Length>MaximumPngBytes)throw new InvalidDataException("Clipboard images are limited to 16 MiB. Save the image and attach the file instead.");return output.ToArray();}
    }
    public static BitmapSource Preview(BitmapSource image){double scale=Math.Min(1,Math.Min(320.0/image.PixelWidth,160.0/image.PixelHeight));var preview=new TransformedBitmap(image,new ScaleTransform(scale,scale));preview.Freeze();return preview;}
    static void Validate(BitmapSource image){if(image==null||image.PixelWidth<=0||image.PixelHeight<=0||image.PixelWidth>16384||image.PixelHeight>16384||(long)image.PixelWidth*image.PixelHeight>MaximumPixels)throw new InvalidDataException("Clipboard images are limited to 16 million pixels.");}
    static BitmapSource ReadDib(byte[] dib){
        if(dib.Length<40||dib.Length>64L*1024*1024)throw new InvalidDataException("Clipboard DIB is invalid or too large.");
        uint header=BitConverter.ToUInt32(dib,0);if(header<40||header>dib.Length||header>124)throw new InvalidDataException("Clipboard DIB header is invalid.");
        int width=BitConverter.ToInt32(dib,4),height=BitConverter.ToInt32(dib,8);ushort planes=BitConverter.ToUInt16(dib,12),bits=BitConverter.ToUInt16(dib,14);uint compression=BitConverter.ToUInt32(dib,16),colors=BitConverter.ToUInt32(dib,32);
        if(width<=0||height==0||height==Int32.MinValue||width>16384||Math.Abs(height)>16384||(long)width*Math.Abs((long)height)>MaximumPixels||planes!=1||!new[]{1,4,8,16,24,32}.Contains((int)bits)||(compression!=0&&compression!=3))throw new InvalidDataException("Clipboard DIB format is unsupported.");
        long palette=bits<=8?(colors==0?1L<<bits:colors)*4:0;long masks=compression==3&&header==40?12:0;long offset=14L+header+masks+palette;
        if(offset>Int32.MaxValue||offset-14>dib.Length)throw new InvalidDataException("Clipboard DIB pixel offset is invalid.");
        long stride=((long)width*bits+31)/32*4;if(stride*Math.Abs((long)height)>dib.Length-(offset-14))throw new InvalidDataException("Clipboard DIB pixels are truncated.");
        using(var stream=new MemoryStream(checked(dib.Length+14))){using(var writer=new BinaryWriter(stream,System.Text.Encoding.ASCII,true)){writer.Write((byte)'B');writer.Write((byte)'M');writer.Write(checked((uint)(dib.Length+14)));writer.Write(0u);writer.Write(checked((uint)offset));writer.Write(dib);}stream.Position=0;var decoder=BitmapDecoder.Create(stream,BitmapCreateOptions.PreservePixelFormat,BitmapCacheOption.OnLoad);var result=decoder.Frames[0];Validate(result);result.Freeze();return result;}
    }
}
public sealed class DirectImageDraft {
    public readonly string Conversation,Peer,Name;public readonly ulong Profile;public readonly object Owner;public readonly byte[] Png;public readonly BitmapSource Preview;
    DirectImageDraft(string conversation,string peer,ulong profile,object owner,byte[] png,BitmapSource preview){Conversation=conversation;Peer=peer;Profile=profile;Owner=owner;Png=png;Preview=preview;Name="Clipboard-"+DateTime.Now.ToString("yyyyMMdd-HHmmss")+".png";}
    public static DirectImageDraft Create(string conversation,string peer,ulong profile,object owner,BitmapSource image){return new DirectImageDraft(conversation,peer,profile,owner,ClipboardAttachments.EncodePng(image),ClipboardAttachments.Preview(image));}
    public bool Matches(string conversation,string peer,ulong profile,object owner){return Conversation==conversation&&Peer==peer&&Profile==profile&&Owner==owner;}
}
public partial class DesktopApplication {
    DirectImageDraft directImageDraft;bool directImageDraftPreparing,directImageDraftSending;
    void BindClipboardAttachments(){
        var composer=Find<TextBox>("Composer");
        composer.PreviewKeyDown+=(s,e)=>{
            if(e.Key!=Key.V||(Keyboard.Modifiers&ModifierKeys.Control)==0||(Keyboard.Modifiers&ModifierKeys.Alt)!=0)return;
            IDataObject data;try{data=Clipboard.GetDataObject();}catch(System.Runtime.InteropServices.COMException){return;}
            if(!ClipboardAttachments.HasImage(data))return;
            e.Handled=true;_ = PrepareClipboardImage(data);
        };
        DataObject.AddPastingHandler(composer,(s,e)=>{if(!ClipboardAttachments.HasImage(e.DataObject))return;e.CancelCommand();_ = PrepareClipboardImage(e.DataObject);});
        window.Closed+=(s,e)=>directImageDraft=null;
    }
    async Task PrepareClipboardImage(IDataObject data){
            if(model.Groups){await PrepareGroupClipboardImage(data);return;}
            var conversation=model.Selected;if(conversation==null||conversation.Contact==null||!conversation.Contact.verified){Notify("Select a verified direct conversation to paste an image.");return;}
            if(directImageDraftPreparing||directImageDraftSending){Notify("Please wait for the current image draft.");return;}
            if(directImageDraft!=null&&!directImageDraft.Matches(conversation.ConversationId,conversation.Contact.peer,profileSessionGeneration,discovery)){Notify("An image draft is waiting in another conversation. Send or remove it before pasting another image.");return;}
            string target=conversation.ConversationId,peer=conversation.Contact.peer;ulong generation=profileSessionGeneration;var owner=discovery;directImageDraftPreparing=true;RefreshGroupImageDraft();
            try{var image=ClipboardAttachments.ReadImage(data);var draft=await Task.Run(()=>DirectImageDraft.Create(target,peer,generation,owner,image));if(!closing&&generation==profileSessionGeneration&&owner==discovery){directImageDraft=draft;RefreshGroupImageDraft();}}
            catch(Exception error){if(!closing&&generation==profileSessionGeneration&&owner==discovery)Notify("Image draft could not be prepared: "+error.Message);}
            finally{directImageDraftPreparing=false;if(!closing)RefreshGroupImageDraft();}
    }
    bool DirectDraftVisible(){return directImageDraft!=null&&!model.Groups&&model.Selected!=null&&model.Selected.Contact!=null&&directImageDraft.Matches(model.Selected.ConversationId,model.Selected.Contact.peer,profileSessionGeneration,discovery);}
    async Task<bool> SendDirectImageDraft(){
        if(model.Groups)return false;
        if(directImageDraftPreparing||directImageDraftSending)return true;
        var draft=directImageDraft;if(draft==null||!DirectDraftVisible())return false;
        if(localProfile==null||profileManager==null){Notify("Image source storage is unavailable. Your draft was kept.");return true;}
        ulong generation=profileSessionGeneration;var owner=discovery;string profileId=localProfile.Id;
        directImageDraftSending=true;RefreshGroupImageDraft();string path=null,sourceDirectory=null;bool accepted=false;
        try{
            sourceDirectory=Path.Combine(profileManager.ProfilePath(profileId),"ClipboardAttachments",Guid.NewGuid().ToString("N"));Directory.CreateDirectory(sourceDirectory);
            path=Path.Combine(sourceDirectory,draft.Name);
            using(var file=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.None)){await file.WriteAsync(draft.Png,0,draft.Png.Length);file.Flush(true);}
            if(closing||generation!=profileSessionGeneration||owner!=discovery||localProfile.Id!=profileId)return true;
            await Task.Run(()=>core.FileTransfer(1,draft.Peer,"",path));accepted=true;
            if(directImageDraft==draft){directImageDraft=null;RefreshGroupImageDraft();}
            await RefreshChatAttachments();
        }catch(Exception error){if(!closing)Notify("Image could not be durably queued. Your draft was kept. "+error.Message);}
        finally{if(!accepted){if(path!=null)try{File.Delete(path);}catch{}if(sourceDirectory!=null)try{Directory.Delete(sourceDirectory);}catch{}}directImageDraftSending=false;if(!closing)RefreshGroupImageDraft();}
        return true;
    }
}
}
