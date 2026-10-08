using System;
using System.IO;
using System.Text;
using System.Runtime.InteropServices;

static class ArchiveApplicationBoundaryTest {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr LoadLibraryEx(string path,IntPtr file,uint flags);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr ui_create();
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern void ui_destroy(IntPtr h);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_start(IntPtr h,string directory,ushort port,string name);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] static extern int ui_authenticated_friends(IntPtr h,ulong account,ulong generation,string peers);
    [DllImport("PeerChat.Core.dll",CallingConvention=CallingConvention.Cdecl)] static extern int ui_archive(IntPtr h,uint op,byte[] input,uint length,[Out] byte[] output,uint capacity);
    static void Check(bool condition){if(!condition)throw new Exception("Archive C ABI regression");}
    sealed class Wire:IDisposable {
        MemoryStream stream=new MemoryStream();BinaryWriter writer;
        public Wire(uint op){writer=new BinaryWriter(stream);writer.Write(new byte[]{80,84,1,31});Text("PeerChat/ArchiveApplication/v1");Number(1);Number(op);}
        public void Number(ulong value){writer.Write(value);}
        public void Text(string value){Bytes(Encoding.UTF8.GetBytes(value));}
        public void Bytes(byte[] value){Number((ulong)value.Length);writer.Write(value);}
        public byte[] Value(){return stream.ToArray();}
        public void Dispose(){writer.Dispose();stream.Dispose();}
    }
    sealed class Record {
        byte[] bytes;int at=4;
        public Record(byte[] value){bytes=value;Check(value.Length>=4&&value[0]==80&&value[1]==84&&value[2]==1&&value[3]==31);}
        public ulong Number(){Check(at<=bytes.Length-8);ulong value=BitConverter.ToUInt64(bytes,at);at+=8;return value;}
        public byte[] Bytes(){ulong size=Number();Check(size<=(ulong)(bytes.Length-at));var value=new byte[(int)size];Buffer.BlockCopy(bytes,at,value,0,value.Length);at+=value.Length;return value;}
        public string Text(){return Encoding.UTF8.GetString(Bytes());}
        public void End(){Check(at==bytes.Length);}
    }
    static byte[] Request(IntPtr h,uint op,Wire wire,bool reject=false){
        var bytes=wire.Value();var output=new byte[262144];int size=ui_archive(h,op,bytes,(uint)bytes.Length,output,(uint)output.Length);
        if(reject){Check(size<0);return null;}Check(size>0&&size<=output.Length);Array.Resize(ref output,size);return output;
    }
    static byte[] Simple(IntPtr h,uint op,bool reject=false){using(var wire=new Wire(op))return Request(h,op,wire,reject);}
    static void Metadata(IntPtr h,string peer,byte[] descriptor,byte[] grant,ulong generation,ulong revision,ulong account=8,bool reject=false){
        using(var wire=new Wire(2)){
            wire.Number(account);wire.Number(generation);wire.Number(revision);wire.Text(peer);wire.Text("isolated-native-boundary-fixture");
            wire.Number(1);wire.Bytes(descriptor);wire.Number(grant==null?0UL:1UL);
            if(grant!=null){wire.Bytes(grant);wire.Number(1);}Request(h,2,wire,reject);
        }
    }
    static void Views(IntPtr h,string peer,bool approved,bool serving){
        var record=new Record(Simple(h,3));Check(record.Text()=="PeerChat/ArchiveApplication/v1"&&record.Number()==1&&record.Number()==3&&record.Number()==1);
        Check(record.Text()==peer&&record.Number()==1&&record.Number()==(approved?1UL:0UL)&&record.Number()==0&&record.Number()==(serving?1UL:0UL));record.End();
    }
    public static int Main(string[] args){try{
        Check(args.Length==2&&LoadLibraryEx(Path.Combine(args[0],"PeerChat.Core.dll"),IntPtr.Zero,0x1100)!=IntPtr.Zero);
        IntPtr handle=ui_create();Check(handle!=IntPtr.Zero);byte[] descriptor=null,grant=null;string peer=null;
        try{
            for(int cycle=0;cycle<5;++cycle){
                Check(ui_start(handle,args[1],0,"Isolated archive test")==1);
                Simple(handle,1,true); // No cached prior-profile authentication.
                Check(ui_authenticated_friends(handle,8,1,"")==1);
                var current=Simple(handle,1);
                if(descriptor==null){
                    descriptor=current;var outer=new Record(descriptor);var payload=outer.Bytes();outer.Bytes();outer.End();
                    var record=new Record(payload);Check(record.Text()=="PeerChat/ArchiveDeviceDescriptor/v1"&&record.Number()==1&&record.Number()==8);peer=record.Text();
                }else Check(Convert.ToBase64String(current)==Convert.ToBase64String(descriptor));
                Metadata(handle,peer,descriptor,grant,1,1);
                Views(handle,peer,cycle>0,cycle>0);
                if(cycle==0){
                    using(var approve=new Wire(4)){approve.Text(peer);approve.Number(0);Request(handle,4,approve,true);}
                    Views(handle,peer,false,false);
                    using(var approve=new Wire(4)){approve.Text(peer);approve.Number(1);Request(handle,4,approve);}
                    using(var authorize=new Wire(5)){authorize.Text(peer);authorize.Number(2);authorize.Number(1);grant=Request(handle,5,authorize);}
                    Views(handle,peer,true,false); // Unpublished local signature is NOT permission.
                    Metadata(handle,peer,descriptor,grant,1,2);Views(handle,peer,true,true);
                }
                Metadata(handle,peer,descriptor,grant,1,3,9,true);Simple(handle,3,true);
                Metadata(handle,peer,descriptor,grant,1,4);Views(handle,peer,true,true);
                Check(ui_authenticated_friends(handle,0,2,"")==1);Simple(handle,3,true);
                Check(ui_authenticated_friends(handle,8,3,"")==1);Simple(handle,3,true); // Fresh login still needs fresh metadata.
                Metadata(handle,peer,descriptor,grant,3,1);Views(handle,peer,true,true);
            }
        }finally{ui_destroy(handle);}
        Console.WriteLine("PASS: packaged Windows archive C ABI, five actual Node profile-worker restarts, exact registration identity, explicit consent, publication gating, account rejection, logout/relogin and fresh metadata. HOST ONLY");
        return 0;
    }catch(Exception e){Console.Error.WriteLine(e.Message);return 1;}}
}
