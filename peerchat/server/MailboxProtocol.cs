using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;

namespace PeerChat.Server {
// Strict content-free validation. RFC 9180 operations and plaintext exist only
// at endpoints. The wire uses standards suite IDs, never vendor structures.
internal static class MailboxProtocol {
    internal const int MaxWire=70000;
    internal const long MaxTtl=7L*24*60*60*1000;
    internal static bool Id(string value,int length){return value!=null&&value.Length==length&&value.All(c=>c>='0'&&c<='9'||c>='a'&&c<='f');}
    internal static void Require(bool ok){if(!ok)throw new InvalidOperationException("Invalid mailbox protocol object.");}
    internal static string Digest(byte[] bytes){using(var sha=SHA256.Create())return String.Concat(sha.ComputeHash(bytes).Select(x=>x.ToString("x2")));}
    internal static byte[] Decode(string encoded,int maximum){Require(encoded!=null&&encoded.Length<=((maximum+2)/3)*4);var bytes=Convert.FromBase64String(encoded);Require(bytes.Length<=maximum);return bytes;}
    internal sealed class Reader {
        internal readonly byte[] Wire;internal int Offset;
        internal Reader(byte[] wire,string domain){Wire=wire;var expected=Encoding.ASCII.GetBytes(domain+"\0");Require(wire!=null&&wire.Length<=MaxWire&&wire.Length>=expected.Length&&wire.Take(expected.Length).SequenceEqual(expected));Offset=expected.Length;}
        internal ulong Number(int width){Require(width>0&&width<=8&&width<=Wire.Length-Offset);ulong result=0;for(int i=0;i<width;i++)result=(result<<8)|Wire[Offset++];return result;}
        internal long Positive(){ulong value=Number(8);Require(value>0&&value<=long.MaxValue);return (long)value;}
        internal byte[] Field(int maximum){ulong size=Number(4);Require(size<=(ulong)maximum&&size<=(ulong)(Wire.Length-Offset));byte[] value=new byte[(int)size];Buffer.BlockCopy(Wire,Offset,value,0,value.Length);Offset+=value.Length;return value;}
        internal string Text(int maximum){return new UTF8Encoding(false,true).GetString(Field(maximum));}
        internal void Suite(){Require(Number(2)==1&&Number(2)==0x10&&Number(2)==1&&Number(2)==1);}
        internal void End(){Require(Offset==Wire.Length);}
        internal byte[] Prefix(){return Wire.Take(Offset).ToArray();}
    }
    internal static bool Verify(byte[] original,byte[] signature,byte[] identity,string device){
        try {
            if(identity==null||identity.Length!=72||signature==null||signature.Length!=64||Digest(identity)!=device)return false;
            // Existing PeerChat identity is ECDSA P-256 CNG public blob / P1363 signature.
            if(BitConverter.ToUInt32(identity,0)!=0x31534345||BitConverter.ToUInt32(identity,4)!=32)return false;
            using(var key=CngKey.Import(identity,CngKeyBlobFormat.EccPublicBlob))
            using(var verify=new ECDsaCng(key))
            using(var sha=SHA256.Create())return verify.VerifyHash(sha.ComputeHash(original),signature);
        }catch(CryptographicException){return false;}
    }
    internal sealed class Binding {
        internal long Account,Generation,Created;
        internal string Device,Key,Predecessor;
        internal byte[] Public,Signature,Original,Wire;
        internal bool Allowed;
        internal static Binding Parse(byte[] wire){
            var r=new Reader(wire,"PeerChat/device-recipient-binding/v1");r.Suite();var b=new Binding();b.Wire=wire;
            b.Account=r.Positive();b.Device=r.Text(64);b.Key=r.Text(32);b.Generation=r.Positive();b.Created=r.Positive();b.Predecessor=r.Text(32);b.Public=r.Field(65);
            ulong consent=r.Number(1);Require(consent<=1);b.Allowed=consent==1;b.Original=r.Prefix();b.Signature=r.Field(64);r.End();
            Require(Id(b.Device,64)&&Id(b.Key,32)&&(b.Predecessor.Length==0||Id(b.Predecessor,32))&&b.Public.Length==65&&b.Public[0]==4&&b.Signature.Length==64);
            // Validate on-curve before accepting the signed encryption descriptor.
            var publicBlob=new byte[72];Buffer.BlockCopy(BitConverter.GetBytes(0x314b4345u),0,publicBlob,0,4);Buffer.BlockCopy(BitConverter.GetBytes(32u),0,publicBlob,4,4);Buffer.BlockCopy(b.Public,1,publicBlob,8,64);
            using(var publicKey=CngKey.Import(publicBlob,CngKeyBlobFormat.EccPublicBlob)){}
            return b;
        }
    }
    internal sealed class Envelope {
        internal long Sender,Recipient,Generation,Created,Expires,Order;
        internal string SenderDevice,RecipientDevice,Key,Message;
        internal byte[] Wire,Original,Signature;
        internal static Envelope Parse(byte[] wire){
            var r=new Reader(wire,"PeerChat/mailbox-device-signature/v1");var h=new Reader(r.Field(4096),"PeerChat/mailbox-envelope/v1");h.Suite();var e=new Envelope();e.Wire=wire;
            e.Sender=h.Positive();e.SenderDevice=h.Text(64);e.Recipient=h.Positive();e.RecipientDevice=h.Text(64);e.Key=h.Text(32);e.Generation=h.Positive();e.Message=h.Text(32);
            e.Created=h.Positive();e.Expires=h.Positive();e.Order=h.Positive();Require(h.Number(1)==1);h.End();
            var enc=r.Field(65);var cipher=r.Field(65552);e.Original=r.Prefix();e.Signature=r.Field(64);r.End();
            Require(e.Sender!=e.Recipient&&e.SenderDevice!=e.RecipientDevice&&Id(e.SenderDevice,64)&&Id(e.RecipientDevice,64)&&Id(e.Key,32)&&Id(e.Message,32)&&
                e.Expires>e.Created&&e.Expires-e.Created<=MaxTtl&&enc.Length==65&&enc[0]==4&&cipher.Length>=16&&e.Signature.Length==64);
            return e;
        }
    }
    internal sealed class Revocation {
        internal long Account,Generation;internal string Device,Key;internal byte[] Original,Signature;
        internal static Revocation Parse(byte[] wire){
            var r=new Reader(wire,"PeerChat/device-recipient-revoke/v1");Require(r.Number(2)==1);var v=new Revocation();
            v.Account=r.Positive();v.Device=r.Text(64);v.Key=r.Text(32);v.Generation=r.Positive();v.Original=r.Prefix();v.Signature=r.Field(64);r.End();
            Require(Id(v.Device,64)&&Id(v.Key,32)&&v.Signature.Length==64);return v;
        }
    }
    internal sealed class Receipt {
        internal long Recipient;internal string Device,SenderDevice,Message,DigestHex;internal byte[] Wire,Original,Signature;
        internal static Receipt Parse(byte[] wire){
            var r=new Reader(wire,"PeerChat/mailbox-recipient-ACK/v1");var a=new Receipt();a.Wire=wire;
            a.Recipient=r.Positive();a.Device=r.Text(64);a.SenderDevice=r.Text(64);a.Message=r.Text(32);var digest=r.Field(32);
            a.Original=r.Prefix();a.Signature=r.Field(64);r.End();a.DigestHex=String.Concat(digest.Select(x=>x.ToString("x2")));
            Require(Id(a.Device,64)&&Id(a.SenderDevice,64)&&Id(a.Message,32)&&digest.Length==32&&a.Signature.Length==64);return a;
        }
    }
}
}
