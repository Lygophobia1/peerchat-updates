package com.peerchat.android.platform.tls;

import com.peerchat.android.*;
import static com.peerchat.android.platform.tls.ReliableProfileProof.*;
import com.peerchat.android.platform.tls.ReliableProfileProof.Record;
import com.peerchat.android.platform.tls.ReliableProfileProof.Reader;

/** Audit-only reproduction using unchanged production JNI and private host fixtures.
 * Accepted capability replies are synthetic; no live server or user profiles.
 * Characterizes the defect, rather than asserting it has been repaired. */
public final class CacheEnrollmentGenerationAudit {
 static void require(boolean value,String message){if(!value)throw new AssertionError(message);}
 static void scope(NativeDmAttachmentSide side,long generation)throws Exception{
  Record friends=new Record(1);friends.num(1);friends.num(2);friends.str(PeerTls.peerId(side.remote[0]));
  friends.num(side.remote.length);for(byte[] key:side.remote)friends.str(PeerTls.peerId(key));
  require(ReliableBridge.friends(side.handle,1,generation,friends.bytes())==0,"friend generation");
  require(MailboxBridge.configureEnrollment(side.handle,1,generation,utf8(side.server),utf8(side.pin))==0,"scope");
  Record request=new Record(30);request.num(1);request.num(generation);
  require(TransferBridge.request(side.handle,11,request.bytes())!=null,"cache scope");
 }
 static byte[] capabilityReply(NativeDmAttachmentSide side){
  byte[] command=TransferBridge.request(side.handle,12,new byte[0]);require(command!=null&&command.length>0,"capability command");
  Reader r=new Reader(command,30);require(r.str().equals("PeerChat/DMCacheCommand/v1"),"command domain");
  long generation=r.num();String id=r.str();require(r.str().equals("cache_dm_capability"),"capability operation");
  Record reply=new Record(30);reply.str("PeerChat/DMCacheReply/v1");reply.num(generation);reply.str(id);reply.str("accepted");
  reply.num(2);reply.num(1);reply.num(0);reply.num(0);reply.num(0);reply.num(0);reply.str("");
  reply.num(0);reply.num(0);reply.num(0);reply.num(0);reply.num(0);reply.str("");reply.num(0);
  require(reply.bytes().length==212,"same 212-byte capability envelope as physical failure");return reply.bytes();
 }
 public static void main(String[] args)throws Exception{
  Class.forName(ReliableProfileProof.class.getName());
  NativeDmAttachmentSide side=new NativeDmAttachmentSide(args[0]);
  try{
   side.friends(new byte[][]{new PeerTlsProof.Identity().pub,new PeerTlsProof.Identity().pub});
   scope(side,1);
   require(TransferBridge.request(side.handle,13,capabilityReply(side))==null,"capability before local registration must reproduce rejection");
   System.out.println("REPRODUCED startup: capability before accepted mailbox binding rejected");
   scope(side,2);
   require(MailboxBridge.registered(side.handle,side.binding)==0,"initial fixture binding acceptance");
   require(TransferBridge.request(side.handle,13,capabilityReply(side))!=null,"initial registered binding succeeds");
   System.out.println("PASS initial accepted local binding: 212-byte capability reply applied");
   scope(side,3);
   require(TransferBridge.request(side.handle,13,capabilityReply(side))==null,"generation change must reproduce rejection");
   Reader diagnostic=new Reader(TransferBridge.request(side.handle,5,side.command()),30);require(diagnostic.num()>0,"diagnostic row");
   String failure=diagnostic.str();require(failure.contains("operation=13 stage=cache_reply_validation_apply")&&failure.contains("input_bytes=212"),"physical failure signature");
   System.out.println("REPRODUCED unchanged account/device + new authentication generation: "+failure);
   scope(side,4);
   byte[] current=MailboxBridge.registration(side.handle,true);require(current!=null,"same private key remains available");
   require(MailboxBridge.registered(side.handle,current)==0,"fixture repeats existing accepted-binding operation");
   require(TransferBridge.request(side.handle,13,capabilityReply(side))!=null,"reconciliation restores capability");
   System.out.println("PASS control: existing binding reconciliation restores capability without new keys or relaxed validation");
   require(TransferBridge.request(side.handle,13,capabilityReplyAfterReset(side,5))==null,"repeat reset reproduces");
   System.out.println("REPRODUCED second generation reset; HOST JNI/CNG/SQLite fixture, no phone/server access");
  }finally{side.core.destroy(side.handle);for(long key:NativeDmAttachmentSide.KEYS.values())NativeDmAttachmentSide.cng("destroy",new Class<?>[]{long.class},key);NativeDmAttachmentSide.KEYS.clear();}
 }
 static byte[] capabilityReplyAfterReset(NativeDmAttachmentSide side,long generation)throws Exception{scope(side,generation);return capabilityReply(side);}
}
