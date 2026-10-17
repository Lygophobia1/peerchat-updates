package com.peerchat.android.platform.tls;

import com.peerchat.android.*;
import java.nio.file.*;
import java.util.*;
import static com.peerchat.android.platform.tls.ReliableProfileProof.*;
import com.peerchat.android.platform.tls.ReliableProfileProof.Record;

/** TEST ONLY: production JNI/TransferManager/direct TLS guard with real host
 * files, SQLite and DPAPI. No Android POSIX/Keystore/physical runtime claim. */
public final class DirectFileCompatibilityProof {
 static native int verifiedFixturePeer(long handle,byte[] publicKey,boolean verified);
 static byte[] command(long account,long generation,long recipient,String peer,boolean commit,String path,String name){Record w=new Record(30);w.num(account);w.num(generation);w.num(recipient);w.str(peer);w.num(commit?1:0);w.str(path);w.str(name);return w.bytes();}
 static byte[] probe(Side side,long account,long generation,long recipient){return TransferBridge.request(side.handle,16,command(account,generation,recipient,side.other,false,"",""));}
 static void falseProbe(byte[] wire){Reader r=new Reader(wire,30);PeerTlsProof.check(r.num()==0);r.end();}
 static void scope(Side side,long generation){PeerTlsProof.check(MailboxBridge.configureEnrollment(side.handle,10,generation,utf8("fixture.local:7443"),utf8("f".repeat(64)))==0);}
 public static void main(String[] args)throws Exception{
  Class.forName(ReliableProfileProof.class.getName());PeerTlsProof.Identity ia=new PeerTlsProof.Identity(),ib=new PeerTlsProof.Identity();
  try(Side a=new Side(ia,args[0]+"/a",PeerTls.peerId(ib.pub),10,20);Side b=new Side(ib,args[0]+"/b",PeerTls.peerId(ia.pub),20,10)){
   scope(a,1);PeerTlsProof.check(verifiedFixturePeer(a.handle,ib.pub,true)==0);falseProbe(probe(a,10,1,20));
   PeerTlsProof.check(TransferBridge.request(a.handle,16,command(10,1,20,a.other,true,"unused","unused.bin"))==null);
   a.attach(false);b.attach(true);exchange(a,b);
   PeerTlsProof.check(verifiedFixturePeer(a.handle,ib.pub,false)==0);PeerTlsProof.check(probe(a,10,1,20)==null);
   PeerTlsProof.check(verifiedFixturePeer(a.handle,ib.pub,true)==0);Reader ready=new Reader(probe(a,10,1,20),30);PeerTlsProof.check(ready.num()==1);ready.end();
   PeerTlsProof.check(probe(a,11,1,20)==null&&probe(a,10,2,20)==null&&probe(a,10,1,21)==null);
   byte[] payload=new byte[256*1024+3];new Random(174).nextBytes(payload);String name="direct-日本-📷.bin",file=UUID.randomUUID().toString().replace("-","")+"-"+name;
   Files.write(a.facade.attachmentPath.toPath().resolve(file),payload);
   Reader offer=new Reader(TransferBridge.request(a.handle,16,command(10,1,20,a.other,true,file,name)),30);String id=offer.str();offer.end();boolean accepted=false,complete=false;
   for(int i=0;i<2500;i++){NativeTransferProof.pump(a,b);for(NativeTransferProof.Row row:NativeTransferProof.rows(b))if(row.id.equals(id)){
    if(row.state==2&&!accepted){PeerTlsProof.check(TransferBridge.request(b.handle,2,NativeTransferProof.command(id))!=null);accepted=true;}
    if(row.state==4){PeerTlsProof.check(accepted&&Arrays.equals(payload,Files.readAllBytes(Path.of(row.path))));complete=true;break;}
   }if(complete)break;Thread.sleep(2);}PeerTlsProof.check(complete);
   Record friends=new Record(1);friends.num(1);friends.num(20);friends.str(a.other);friends.num(1);friends.str(a.other);PeerTlsProof.check(ReliableBridge.friends(a.handle,10,2,friends.bytes())==0);scope(a,2);
   // The old TLS endpoint must not masquerade as a new-generation route.
   falseProbe(probe(a,10,2,20));PeerTlsProof.check(TransferBridge.request(a.handle,16,command(10,2,20,a.other,true,file,name))==null);
  }
  System.out.println("PASS production JNI explicit online direct compatibility: offline/unverified/wrong-account/wrong-recipient/stale-input-generation/stale-live-route rejected; current exact verified direct-file route transfers Unicode bytes through existing TransferManager with no cache capability gate. HOST only; no Android POSIX/Keystore/device claim.");
 }
}
