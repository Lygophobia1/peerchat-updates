using System;
using System.Security.Cryptography;
using System.Text;

namespace PeerChat.Server {
    // Verifies the signed, content-free Create event of a newly created group.
    // Registration starts an owner-only authoritative server roster; an old
    // client-owned group is never imported by scanning local history.
    internal sealed class GroupRosterProof {
        public string LegacyGroupId { get; private set; }
        public string OwnerIdentity { get; private set; }
        public long OwnerAccountId { get; private set; }
        public string ServerGroupId { get; private set; }
        public string CreateEventId { get; private set; }

        static ulong Number(byte[] data,ref int offset){
            if(data.Length-offset<8)throw new InvalidOperationException("Invalid group creation proof.");
            ulong value=0;for(int i=0;i<8;i++)value|=(ulong)data[offset++]<<(i*8);return value;
        }
        static byte[] Bytes(byte[] data,ref int offset,int limit){
            ulong length=Number(data,ref offset);
            if(length>(ulong)limit||length>(ulong)(data.Length-offset))throw new InvalidOperationException("Invalid group creation proof.");
            byte[] result=new byte[(int)length];Buffer.BlockCopy(data,offset,result,0,result.Length);offset+=result.Length;return result;
        }
        static string Text(byte[] data,ref int offset,int limit){return new UTF8Encoding(false,true).GetString(Bytes(data,ref offset,limit));}
        static string Hex(byte[] data){var text=new StringBuilder(data.Length*2);foreach(byte value in data)text.Append(value.ToString("x2"));return text.ToString();}
        static bool Id(string value,int length){if(value==null||value.Length!=length)return false;foreach(char c in value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}

        public static GroupRosterProof Parse(string encoded){
            if(String.IsNullOrEmpty(encoded)||encoded.Length>4096)throw new InvalidOperationException("Invalid group creation proof.");
            byte[] wire;try{wire=Convert.FromBase64String(encoded);}catch(FormatException){throw new InvalidOperationException("Invalid group creation proof.");}
            if(wire.Length<12||wire.Length>2048||wire[0]!='P'||wire[1]!='T'||wire[2]!=1||wire[3]!=5)throw new InvalidOperationException("Invalid group creation proof.");
            int cursor=4;string eventId=Text(wire,ref cursor,64);int unsignedStart=cursor;
            string legacyId=Text(wire,ref cursor,32);ulong kind=Number(wire,ref cursor),order=Number(wire,ref cursor);
            Number(wire,ref cursor); // signed creation timestamp is not a server clock
            ulong actor=Number(wire,ref cursor);string actorIdentity=Text(wire,ref cursor,64);
            ulong subject=Number(wire,ref cursor);string subjectIdentity=Text(wire,ref cursor,64);
            Text(wire,ref cursor,64);byte[] publicKey=Bytes(wire,ref cursor,512);
            string name=Text(wire,ref cursor,320);string message=Text(wire,ref cursor,0);
            int unsignedEnd=cursor;byte[] signature=Bytes(wire,ref cursor,128);
            if(cursor!=wire.Length||!Id(eventId,64)||!Id(legacyId,32)||!Id(actorIdentity,64)||
               kind!=1||order!=1||actor==0||actor>long.MaxValue||actor!=subject||
               subjectIdentity!=actorIdentity||name.Length==0||name.Length>80||message.Length!=0||
               signature.Length!=64||publicKey.Length<8)throw new InvalidOperationException("Invalid group creation proof.");
            byte[] unsigned=new byte[4+unsignedEnd-unsignedStart];Buffer.BlockCopy(wire,0,unsigned,0,4);Buffer.BlockCopy(wire,unsignedStart,unsigned,4,unsignedEnd-unsignedStart);
            using(var sha=SHA256.Create()){
                byte[] digest=sha.ComputeHash(unsigned);
                if(Hex(digest)!=eventId||Hex(sha.ComputeHash(publicKey))!=actorIdentity)throw new InvalidOperationException("Invalid group creation proof.");
                try{
#if PEERCHAT_PORTABLE
                    if(!PortablePlatform.VerifyHash(publicKey,digest,signature))throw new InvalidOperationException("Invalid group creation proof.");
#else
                    using(var key=CngKey.Import(publicKey,CngKeyBlobFormat.EccPublicBlob))
                    using(var verifier=new ECDsaCng(key))
                        if(!verifier.VerifyHash(digest,signature))throw new InvalidOperationException("Invalid group creation proof.");
#endif
                }catch(CryptographicException){throw new InvalidOperationException("Invalid group creation proof.");}
                string serverId=Hex(sha.ComputeHash(Encoding.ASCII.GetBytes("PeerChat group roster v1|"+legacyId+"|"+actorIdentity))).Substring(0,32);
                return new GroupRosterProof{LegacyGroupId=legacyId,OwnerIdentity=actorIdentity,OwnerAccountId=(long)actor,ServerGroupId=serverId,CreateEventId=eventId};
            }
        }
    }
}
