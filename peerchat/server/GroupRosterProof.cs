using System;
using System.Security.Cryptography;
using System.Text;

namespace PeerChat.Server {
    internal sealed class GroupDeviceProof {
        public string GroupId {get;private set;} public string LegacyGroupId {get;private set;}
        public string Actor {get;private set;} public string Target {get;private set;}
        public long Account {get;private set;} public long Generation {get;private set;}
        public bool Revoke {get;private set;} public string Id {get;private set;}
        public string Encoded {get;private set;} public string EventId {get;private set;}
        private GroupDeviceProof(){}
        static void Check(bool condition){if(!condition)throw new InvalidOperationException("Invalid signed group device proof.");}
        static void Header(byte[] data){Check(data.Length>=4&&data[0]=='P'&&data[1]=='T'&&data[2]==1&&data[3]==5);}
        static void Number(System.IO.BinaryWriter writer,ulong value){writer.Write(value);}
        static void Bytes(System.IO.BinaryWriter writer,byte[] value){Number(writer,(ulong)value.Length);writer.Write(value);}
        static void Text(System.IO.BinaryWriter writer,string value){Bytes(writer,Encoding.UTF8.GetBytes(value));}
        static bool Verify(byte[] input,byte[] signature,byte[] key){
            if(signature.Length!=64||key.Length!=72)return false;
            try{using(var sha=SHA256.Create()){
#if PEERCHAT_PORTABLE
                return PortablePlatform.VerifyHash(key,sha.ComputeHash(input),signature);
#else
                using(var imported=CngKey.Import(key,CngKeyBlobFormat.EccPublicBlob))
                using(var verifier=new ECDsaCng(imported))return verifier.VerifyHash(sha.ComputeHash(input),signature);
#endif
            }}catch(CryptographicException){return false;}
        }
        public static GroupDeviceProof Parse(string encoded){
            Check(!String.IsNullOrEmpty(encoded)&&encoded.Length<=8192);
            byte[] wire;try{wire=Convert.FromBase64String(encoded);}catch(FormatException){throw new InvalidOperationException("Invalid signed group device proof.");}
            Check(wire.Length<=4096);Header(wire);int p=4;
            Check(GroupRosterProof.Text(wire,ref p,64)=="PeerChat/group-device-approval/v1");
            string group=GroupRosterProof.Text(wire,ref p,32),legacy=GroupRosterProof.Text(wire,ref p,32);
            ulong generation=GroupRosterProof.Number(wire,ref p),account=GroupRosterProof.Number(wire,ref p);
            string actor=GroupRosterProof.Text(wire,ref p,64);byte[] actorKey=GroupRosterProof.Bytes(wire,ref p,72);
            string target=GroupRosterProof.Text(wire,ref p,64);byte[] targetKey=GroupRosterProof.Bytes(wire,ref p,72);
            byte[] targetSignature=GroupRosterProof.Bytes(wire,ref p,64),eventWire=GroupRosterProof.Bytes(wire,ref p,2048);
            int signedEnd=p;byte[] signature=GroupRosterProof.Bytes(wire,ref p,64);Check(p==wire.Length);
            Check(GroupRosterProof.Id(group,32)&&GroupRosterProof.Id(legacy,32)&&GroupRosterProof.Id(actor,64)&&
                GroupRosterProof.Id(target,64)&&actor!=target&&account>0&&account<=long.MaxValue&&generation>0&&generation<long.MaxValue);
            using(var sha=SHA256.Create()){
                Check(GroupRosterProof.Hex(sha.ComputeHash(actorKey))==actor&&GroupRosterProof.Hex(sha.ComputeHash(targetKey))==target);
                byte[] identityInput;using(var buffer=new System.IO.MemoryStream())using(var writer=new System.IO.BinaryWriter(buffer)){
                    writer.Write(new byte[]{(byte)'P',(byte)'T',1,5});Text(writer,"PeerChat/group-device-identity/v1");
                    Number(writer,account);Text(writer,target);Bytes(writer,targetKey);identityInput=buffer.ToArray();
                }
                Check(Verify(identityInput,targetSignature,targetKey));
                byte[] signed=new byte[signedEnd];Buffer.BlockCopy(wire,0,signed,0,signed.Length);Check(Verify(signed,signature,actorKey));
                Header(eventWire);int e=4;string eventId=GroupRosterProof.Text(eventWire,ref e,64);int unsignedStart=e;
                Check(GroupRosterProof.Text(eventWire,ref e,32)==legacy);ulong kind=GroupRosterProof.Number(eventWire,ref e);
                ulong order=GroupRosterProof.Number(eventWire,ref e);GroupRosterProof.Number(eventWire,ref e);
                Check(order>0&&(kind==10||kind==11)&&GroupRosterProof.Number(eventWire,ref e)==account&&
                    GroupRosterProof.Text(eventWire,ref e,64)==actor&&GroupRosterProof.Number(eventWire,ref e)==account&&
                    GroupRosterProof.Text(eventWire,ref e,64)==target);
                Check(GroupRosterProof.Text(eventWire,ref e,0)=="");byte[] subjectKey=GroupRosterProof.Bytes(eventWire,ref e,72);
                Check(kind==10?Convert.ToBase64String(subjectKey)==Convert.ToBase64String(targetKey):subjectKey.Length==0);
                Check(GroupRosterProof.Text(eventWire,ref e,128)==(kind==10?"PeerChat/group-device-authorization/v1":"PeerChat/group-device-revocation/v1"));
                Check(GroupRosterProof.Text(eventWire,ref e,0)=="");int unsignedEnd=e;
                byte[] eventSignature=GroupRosterProof.Bytes(eventWire,ref e,64);Check(e==eventWire.Length);
                byte[] unsigned=new byte[4+unsignedEnd-unsignedStart];Buffer.BlockCopy(eventWire,0,unsigned,0,4);Buffer.BlockCopy(eventWire,unsignedStart,unsigned,4,unsignedEnd-unsignedStart);
                Check(GroupRosterProof.Id(eventId,64)&&GroupRosterProof.Hex(sha.ComputeHash(unsigned))==eventId&&Verify(unsigned,eventSignature,actorKey));
                return new GroupDeviceProof{GroupId=group,LegacyGroupId=legacy,Account=(long)account,Generation=(long)generation,
                    Actor=actor,Target=target,Revoke=kind==11,Id=GroupRosterProof.Hex(sha.ComputeHash(signed)),
                    Encoded=Convert.ToBase64String(wire),EventId=eventId};
            }
        }
    }
    // Verifies the signed, content-free Create event of a newly created group.
    // Registration starts an owner-only authoritative server roster; an old
    // client-owned group is never imported by scanning local history.
    internal sealed class GroupRosterProof {
        public string LegacyGroupId { get; private set; }
        public string OwnerIdentity { get; private set; }
        public long OwnerAccountId { get; private set; }
        public string ServerGroupId { get; private set; }
        public string CreateEventId { get; private set; }

        internal static ulong Number(byte[] data,ref int offset){
            if(data.Length-offset<8)throw new InvalidOperationException("Invalid group creation proof.");
            ulong value=0;for(int i=0;i<8;i++)value|=(ulong)data[offset++]<<(i*8);return value;
        }
        internal static byte[] Bytes(byte[] data,ref int offset,int limit){
            ulong length=Number(data,ref offset);
            if(length>(ulong)limit||length>(ulong)(data.Length-offset))throw new InvalidOperationException("Invalid group creation proof.");
            byte[] result=new byte[(int)length];Buffer.BlockCopy(data,offset,result,0,result.Length);offset+=result.Length;return result;
        }
        internal static string Text(byte[] data,ref int offset,int limit){return new UTF8Encoding(false,true).GetString(Bytes(data,ref offset,limit));}
        internal static string Hex(byte[] data){var text=new StringBuilder(data.Length*2);foreach(byte value in data)text.Append(value.ToString("x2"));return text.ToString();}
        internal static bool Id(string value,int length){if(value==null||value.Length!=length)return false;foreach(char c in value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}

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
