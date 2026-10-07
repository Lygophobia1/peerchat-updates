using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace PeerChat.Server {
    internal static class PortablePlatform {
        sealed class Signals:IDisposable {
            readonly Action stop;
            readonly PosixSignalRegistration term,interrupt;
            readonly ConsoleCancelEventHandler console;
            internal Signals(Action stop){this.stop=stop;
                if(OperatingSystem.IsWindows()){
                    console=(sender,e)=>{e.Cancel=true;stop();};Console.CancelKeyPress+=console;
                }else{
                    term=PosixSignalRegistration.Create(PosixSignal.SIGTERM,c=>{c.Cancel=true;stop();});
                    interrupt=PosixSignalRegistration.Create(PosixSignal.SIGINT,c=>{c.Cancel=true;stop();});
                }
            }
            public void Dispose(){if(console!=null)Console.CancelKeyPress-=console;if(term!=null)term.Dispose();if(interrupt!=null)interrupt.Dispose();}
        }
        internal static IDisposable ShutdownSignals(Action stop){return new Signals(stop);}
        internal static void InitializeSqlite(){
            NativeLibrary.SetDllImportResolver(typeof(PortablePlatform).Assembly,(name,assembly,path)=>{
                if(name!="winsqlite3.dll"||OperatingSystem.IsWindows())return IntPtr.Zero;
                return NativeLibrary.Load("libsqlite3.so.0",assembly,path);
            });
        }
        static ECDsa PublicKey(byte[] blob,bool agreement=false){
            uint magic=agreement?0x314b4345u:0x31534345u;
            if(blob==null||blob.Length!=72||BitConverter.ToUInt32(blob,0)!=magic||BitConverter.ToUInt32(blob,4)!=32)
                throw new CryptographicException("Invalid canonical P-256 public blob");
            byte[] x=new byte[32],y=new byte[32];Buffer.BlockCopy(blob,8,x,0,32);Buffer.BlockCopy(blob,40,y,0,32);
            // Platform crypto validates the point. Never alter identity blob/Peer ID.
            return ECDsa.Create(new ECParameters{Curve=ECCurve.NamedCurves.nistP256,Q=new ECPoint{X=x,Y=y}});
        }
        internal static bool VerifyHash(byte[] blob,byte[] hash,byte[] signature){
            if(hash==null||hash.Length!=32||signature==null||signature.Length!=64)return false;
            try{using(var key=PublicKey(blob))return key.VerifyHash(hash,signature,DSASignatureFormat.IeeeP1363FixedFieldConcatenation);}
            catch(CryptographicException){return false;}
        }
        internal static void ValidateAgreementPublic(byte[] blob){using(var key=PublicKey(blob,true)){}}
        internal static byte[] RsaSign(byte[] key,byte[] message){
            using(var rsa=RSA.Create()){int consumed;rsa.ImportPkcs8PrivateKey(key,out consumed);
                if(consumed!=key.Length)throw new CryptographicException("Trailing RSA key data");
                return rsa.SignData(message,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);}
        }
        internal static void ValidateSecretFile(string path,int maximum=65536){
            if(!Path.IsPathRooted(path)||new FileInfo(path).LinkTarget!=null)throw new InvalidOperationException("Secret requires an absolute non-symlink file");
            var info=new FileInfo(path);if(info.Length<1||info.Length>maximum)throw new InvalidOperationException("Invalid secret file size");
            if(!OperatingSystem.IsWindows()){
                var mode=File.GetUnixFileMode(path);
                if((mode&(UnixFileMode.GroupRead|UnixFileMode.GroupWrite|UnixFileMode.GroupExecute|
                    UnixFileMode.OtherRead|UnixFileMode.OtherWrite|UnixFileMode.OtherExecute))!=0)
                    throw new InvalidOperationException("Secret file must be service-owned mode 0600 or stricter");
            }
            }
        internal static string ReadSecret(string path,int maximum=65536){ValidateSecretFile(path,maximum);return File.ReadAllText(path,new UTF8Encoding(false,true));
        }
        // Modern Windows continues using the same CurrentUser DPAPI OS API.
        // Linux never accepts a DPAPI blob or silently substitutes plaintext.
        [StructLayout(LayoutKind.Sequential)] struct Blob {public int Length;public IntPtr Data;}
        [DllImport("crypt32.dll",SetLastError=true)] static extern bool CryptProtectData(ref Blob input,IntPtr description,IntPtr entropy,IntPtr reserved,IntPtr prompt,uint flags,out Blob output);
        [DllImport("crypt32.dll",SetLastError=true)] static extern bool CryptUnprotectData(ref Blob input,IntPtr description,IntPtr entropy,IntPtr reserved,IntPtr prompt,uint flags,out Blob output);
        [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
        internal static byte[] Dpapi(byte[] value,bool protect){
            if(!OperatingSystem.IsWindows())throw new PlatformNotSupportedException("DPAPI requires Windows; configure an external Linux secret file");
            var input=new Blob{Length=value.Length,Data=Marshal.AllocHGlobal(value.Length)};Blob output=new Blob();
            try{Marshal.Copy(value,0,input.Data,value.Length);
                bool ok=protect?CryptProtectData(ref input,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,1,out output):
                    CryptUnprotectData(ref input,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,IntPtr.Zero,1,out output);
                if(!ok)throw new CryptographicException(Marshal.GetLastWin32Error());
                var result=new byte[output.Length];Marshal.Copy(output.Data,result,0,result.Length);return result;
            }finally{Marshal.FreeHGlobal(input.Data);if(output.Data!=IntPtr.Zero)LocalFree(output.Data);}
        }
    }
}
