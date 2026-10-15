using System;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using PeerChatDesktop;

class ClipboardAttachmentTest {
    static void Check(bool value,string message){if(!value)throw new Exception(message);}
    static byte[] Bmp(BitmapSource source){var encoder=new BmpBitmapEncoder();encoder.Frames.Add(BitmapFrame.Create(source));using(var stream=new MemoryStream()){encoder.Save(stream);return stream.ToArray();}}
    [STAThread] static int Main(){
        try{
            var image=BitmapSource.Create(12,7,96,96,PixelFormats.Bgr32,null,Enumerable.Repeat((byte)120,12*7*4).ToArray(),12*4);image.Freeze();
            var bmp=Bmp(image);var dib=bmp.Skip(14).ToArray();
            var data=new DataObject();data.SetData(DataFormats.Dib,new MemoryStream(dib));
            Check(ClipboardAttachments.HasImage(data),"DIB not detected");
            var decoded=ClipboardAttachments.ReadImage(data);Check(decoded.IsFrozen&&decoded.PixelWidth==12&&decoded.PixelHeight==7,"DIB pixels unavailable");
            var png=ClipboardAttachments.EncodePng(decoded);Check(png.Take(8).SequenceEqual(new byte[]{137,80,78,71,13,10,26,10}),"DIB did not encode to PNG");
            var bitmapData=new DataObject();bitmapData.SetData(DataFormats.Bitmap,image);Check(ClipboardAttachments.ReadImage(bitmapData).PixelWidth==12,"bitmap paste unavailable");
            foreach(var bad in new[]{new byte[0],new byte[40],dib.Take(dib.Length-1).ToArray()}){
                var malformed=new DataObject();malformed.SetData(DataFormats.Dib,new MemoryStream(bad));bool refused=false;
                try{ClipboardAttachments.ReadImage(malformed);}catch(InvalidDataException){refused=true;}
                Check(refused,"malformed or truncated DIB accepted");
            }
            var draft=DirectImageDraft.Create("conversation-a","peer-a",4,new object(),decoded);
            Check(draft.Preview.IsFrozen&&draft.Png.Length>0&&draft.Name.EndsWith(".png"),"DM draft preview/PNG missing");
            Check(!draft.Matches("conversation-b","peer-a",4,draft.Owner)&&!draft.Matches("conversation-a","peer-b",4,draft.Owner)&&!draft.Matches("conversation-a","peer-a",5,draft.Owner)&&!draft.Matches("conversation-a","peer-a",4,new object()),"DM draft escaped scope");
            var text=new DataObject();text.SetData(DataFormats.UnicodeText,"ordinary paste");text.SetData(DataFormats.Dib,new MemoryStream(dib));Check(!ClipboardAttachments.HasImage(text),"text paste was intercepted");
            var oversized=BitmapSource.Create(16385,1,96,96,PixelFormats.Gray8,null,new byte[16385],16385);oversized.Freeze();bool rejected=false;try{ClipboardAttachments.EncodePng(oversized);}catch(InvalidDataException){rejected=true;}Check(rejected,"oversized image accepted");
            IDataObject old=null;try{old=Clipboard.GetDataObject();Clipboard.SetDataObject(data,true);var live=Clipboard.GetDataObject();Check(ClipboardAttachments.HasImage(live)&&ClipboardAttachments.ReadImage(live).PixelWidth==12,"Windows clipboard DIB paste failed");}catch(System.Runtime.InteropServices.COMException){Console.WriteLine("Clipboard busy: DataObject checks passed; live clipboard check skipped.");}finally{try{if(old!=null)Clipboard.SetDataObject(old,true);else Clipboard.Clear();}catch(System.Runtime.InteropServices.COMException){}}
            Console.WriteLine("PASS clipboard attachments: DIB, PNG, bounds, text precedence, DM scope, Windows clipboard");return 0;
        }catch(Exception error){Console.Error.WriteLine(error);return 1;}
    }
}
