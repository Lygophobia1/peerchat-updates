#if PEERCHAT_PORTABLE
using ECDsaCng = PortableTestKey;
#endif
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using PeerChat.Server;

internal static class GroupRosterProofTest {
    static void Require(bool condition,string message){if(!condition)throw new Exception(message);}
    static string Hex(byte[] data){var text=new StringBuilder(data.Length*2);foreach(byte value in data)text.Append(value.ToString("x2"));return text.ToString();}
    static void Number(Stream stream,ulong value){for(int i=0;i<8;i++)stream.WriteByte((byte)(value>>(8*i)));}
    static void Bytes(Stream stream,byte[] data){Number(stream,(ulong)data.Length);stream.Write(data,0,data.Length);}
    static void Text(Stream stream,string data){Bytes(stream,new UTF8Encoding(false,true).GetBytes(data));}
    internal static byte[] CreateProof(ECDsaCng owner,string group,long account){
        byte[] publicKey=owner.Key.Export(CngKeyBlobFormat.EccPublicBlob);
        string identity;using(var sha=SHA256.Create())identity=Hex(sha.ComputeHash(publicKey));
        var unsigned=new MemoryStream();unsigned.WriteByte((byte)'P');unsigned.WriteByte((byte)'T');unsigned.WriteByte(1);unsigned.WriteByte(5);
        Text(unsigned,group);Number(unsigned,1);Number(unsigned,1);Number(unsigned,12345);Number(unsigned,(ulong)account);Text(unsigned,identity);Number(unsigned,(ulong)account);Text(unsigned,identity);Text(unsigned,"Owner");Bytes(unsigned,publicKey);Text(unsigned,"Existing group");Text(unsigned,"");
        byte[] digest;using(var sha=SHA256.Create())digest=sha.ComputeHash(unsigned.ToArray());
        byte[] signature=owner.SignHash(digest);
        var complete=new MemoryStream();complete.WriteByte((byte)'P');complete.WriteByte((byte)'T');complete.WriteByte(1);complete.WriteByte(5);Text(complete,Hex(digest));byte[] body=unsigned.ToArray();complete.Write(body,4,body.Length-4);Bytes(complete,signature);return complete.ToArray();
    }
    static void Rejected(byte[] value){try{GroupRosterProof.Parse(Convert.ToBase64String(value));throw new Exception("Altered group roster proof was accepted.");}catch(InvalidOperationException){}}
    public static int Main(){try{
        const string legacy="0123456789abcdef0123456789abcdef";
        using(var first=new ECDsaCng(256))using(var second=new ECDsaCng(256)){
            byte[] source=CreateProof(first,legacy,19);var proof=GroupRosterProof.Parse(Convert.ToBase64String(source));
            Require(proof.LegacyGroupId==legacy&&proof.OwnerAccountId==19&&proof.OwnerIdentity.Length==64&&proof.ServerGroupId.Length==32,"Valid owner proof was not decoded.");
            var other=GroupRosterProof.Parse(Convert.ToBase64String(CreateProof(second,legacy,20)));
            Require(other.ServerGroupId!=proof.ServerGroupId,"A different owner could squat on the same server Group ID.");
            var tampered=(byte[])source.Clone();tampered[tampered.Length-1]^=1;Rejected(tampered);
            tampered=(byte[])source.Clone();tampered[28]^=1;Rejected(tampered);
            Rejected(new byte[]{0,1,2});
        }
        Console.WriteLine("PASS: signed legacy Create proof binds the server roster to its owner device; tampering and namespace squatting rejected.");return 0;
    }catch(Exception error){Console.Error.WriteLine("FAIL: "+error.Message);return 1;}}
}
