using System;
using System.Linq;
using System.Security.Cryptography;

namespace PeerChat.Server {
// Public signed routing/integrity evidence only. No filename, plaintext digest,
// MIME type, attachment key or encrypted-manifest plaintext reaches the server.
internal sealed class AttachmentCacheDescriptor {
 internal const long MaximumPlainBytes=256L*1024*1024;
 internal string Id {get;private set;}
 internal string Group {get;private set;}
 internal string Message {get;private set;}
 internal string Sender {get;private set;}
 internal long Account {get;private set;}
 internal long Epoch {get;private set;}
 internal long PlainBytes {get;private set;}
 internal long CipherBytes {get;private set;}
 internal string CipherDigest {get;private set;}
 internal byte[] Wire {get;private set;}
 AttachmentCacheDescriptor(){}
 internal static AttachmentCacheDescriptor Parse(byte[] wire){
  try{
   if(wire==null||wire.Length>2048||wire.Length<4||wire[0]!=80||wire[1]!=84||wire[2]!=1||wire[3]!=5)throw new InvalidOperationException();
   int at=4;if(GroupRosterProof.Text(wire,ref at,64)!="PeerChat/group-cache-upload/v1")throw new InvalidOperationException();
   var result=new AttachmentCacheDescriptor{Id=GroupRosterProof.Text(wire,ref at,32),Group=GroupRosterProof.Text(wire,ref at,32),Message=GroupRosterProof.Text(wire,ref at,32),Sender=GroupRosterProof.Text(wire,ref at,64)};
   ulong account=GroupRosterProof.Number(wire,ref at),epoch=GroupRosterProof.Number(wire,ref at),plain=GroupRosterProof.Number(wire,ref at),cipher=GroupRosterProof.Number(wire,ref at);
   var digest=GroupRosterProof.Bytes(wire,ref at,32);var key=GroupRosterProof.Bytes(wire,ref at,72);int signedEnd=at;var signature=GroupRosterProof.Bytes(wire,ref at,64);
   if(at!=wire.Length||!MailboxProtocol.Id(result.Id,32)||!MailboxProtocol.Id(result.Group,32)||!MailboxProtocol.Id(result.Message,32)||!MailboxProtocol.Id(result.Sender,64)||account==0||account>long.MaxValue||epoch==0||epoch>long.MaxValue||plain>(ulong)MaximumPlainBytes||cipher!=20+plain+Math.Max(1UL,(plain+32767)/32768)*16||digest.Length!=32||key.Length!=72||signature.Length!=64||!MailboxProtocol.Verify(wire.Take(signedEnd).ToArray(),signature,key,result.Sender))throw new InvalidOperationException();
   result.Account=(long)account;result.Epoch=(long)epoch;result.PlainBytes=(long)plain;result.CipherBytes=(long)cipher;result.CipherDigest=String.Concat(digest.Select(b=>b.ToString("x2")));result.Wire=(byte[])wire.Clone();return result;
  }catch(Exception error){if(error is OutOfMemoryException)throw;throw new InvalidOperationException("Invalid signed encrypted attachment descriptor.");}
 }
}
internal sealed class CacheRecipient {
 internal readonly long Account;internal readonly string Device,Membership;
 internal CacheRecipient(long account,string device,string membership){if(account<=0||!MailboxProtocol.Id(device,64)||!MailboxProtocol.Id(membership,32))throw new InvalidOperationException("Invalid cache recipient binding.");Account=account;Device=device;Membership=membership;}
}
}
