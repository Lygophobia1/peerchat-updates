package com.peerchat.android.platform.tls;

import com.peerchat.android.*;
import com.peerchat.android.platform.OpaqueRecipientHpke;
import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import static com.peerchat.android.platform.tls.ReliableProfileProof.*;
import com.peerchat.android.platform.tls.ReliableProfileProof.Record;
import com.peerchat.android.platform.tls.ReliableProfileProof.Reader;

/** TEST ONLY. Production JNI/owners and production opaque HPKE run against
 * Windows SQLite/DPAPI/CNG fixtures. This is not Android POSIX or Keystore. */
public final class NativeDmAttachmentSide {
 static native int trustFixturePeer(long handle,byte[] publicKey,boolean verified);
 static native byte[] profileFixtureDiagnostic(Object facade);
 static final Base64.Encoder ENCODE=Base64.getEncoder();static final Base64.Decoder DECODE=Base64.getDecoder();
 static final Map<String,Long> KEYS=new HashMap<>();
 static Object cng(String operation,Class<?>[] types,Object... args){try{Method method=Class.forName("NativeCngHpkeFixture").getDeclaredMethod(operation,types);method.setAccessible(true);return method.invoke(null,args);}catch(Exception error){throw new IllegalStateException("Isolated CNG operation failed",error);}}
 static byte[] pub(long key){return (byte[])cng("publicKey",new Class<?>[]{long.class},key);}
 static long key(byte[] alias){Long value=KEYS.get(new String(alias,StandardCharsets.US_ASCII));if(value==null)throw new IllegalStateException("Missing isolated opaque key");return value;}
 public static final class Facade {
  final ReliableProfileProof.Facade profile,registry;
  Facade(PeerTlsProof.Identity identity,String directory){profile=new ReliableProfileProof.Facade(identity,directory);registry=new ReliableProfileProof.Facade(identity,directory+"-recipient-registry");}
  public byte[] publicKey(){return profile.publicKey();}public byte[] sign(byte[] bytes)throws Exception{return profile.sign(bytes);}
  public PeerTls createPeerTls(boolean incoming,byte[] pin)throws Exception{return profile.createPeerTls(incoming,pin);}
  public byte[] attachmentDirectory(){return profile.attachmentDirectory();}
  public Object keyRegistryProvider(){return registry;}
  public void beginMailboxRegistration(){}
  public byte[] recipientAlias(int generation){return utf8("peerchat.test.dm.recipient."+generation);}
  public boolean recipientSupported(){return true;}public boolean recipientExists(byte[] alias){return KEYS.containsKey(new String(alias,StandardCharsets.US_ASCII));}
  public byte[] recipientCreate(byte[] alias){String name=new String(alias,StandardCharsets.US_ASCII);PeerTlsProof.check(!KEYS.containsKey(name));long value=(Long)cng("create",new Class<?>[]{String.class},"PeerChat.Recipient.test.dm-jni."+UUID.randomUUID());KEYS.put(name,value);return pub(value);}
  public byte[] recipientPublic(byte[] alias){return pub(key(alias));}
  public byte[] recipientAgree(byte[] alias,byte[] point){return (byte[])cng("agree",new Class<?>[]{long.class,byte[].class},key(alias),point);}
  public byte[] hpkeSeal(byte[] recipient,byte[] info,byte[] aad,byte[] plain)throws Exception{return OpaqueRecipientHpke.seal(recipient,info,aad,plain);}
  public byte[] hpkeOpen(byte[] alias,byte[] expected,byte[] enc,byte[] info,byte[] aad,byte[] cipher)throws Exception{return OpaqueRecipientHpke.open(new OpaqueRecipientHpke.Recipient(){public byte[] publicKey(){return recipientPublic(alias);}public byte[] agree(byte[] point){return recipientAgree(alias,point);}},expected,enc,info,aad,cipher);}
  public byte[] protect(byte[] bytes,byte[] label){return profile.protect(bytes,label);}public byte[] unprotect(byte[] bytes,byte[] label){return profile.unprotect(bytes,label);}
  public void execute(byte[] query){profile.execute(query);}public long prepare(byte[] query){return profile.prepare(query);}public void bindBytes(long stmt,int index,byte[] value,boolean blob){profile.bindBytes(stmt,index,value,blob);}public void bindNumber(long stmt,int index,long value){profile.bindNumber(stmt,index,value);}public boolean row(long stmt){return profile.row(stmt);}public byte[] bytes(long stmt,int index){return profile.bytes(stmt,index);}public byte[] text(long stmt,int index){return profile.text(stmt,index);}public long number(long stmt,int index){return profile.number(stmt,index);}public void finish(long stmt){profile.finish(stmt);}public void close(){profile.close();}
 }
 final String directory;final PeerTlsProof.Identity identity=new PeerTlsProof.Identity();final NativeBridge core=new NativeBridge();
 long handle;byte[] binding;byte[][] remote;String server="fixture.local:7443",pin="f".repeat(64);
 NativeDmAttachmentSide(String directory)throws Exception{this.directory=directory;open();}
 void open()throws Exception{handle=core.create();if(handle==0)throw new AssertionError("JNI owner creation failed");int status=ProfileBridge.bind(handle,new Facade(identity,directory));if(status!=0)throw new AssertionError("JNI opaque profile bind status="+status+" diagnostic="+new String(profileFixtureDiagnostic(new Facade(identity,directory+"-diagnostic")),StandardCharsets.UTF_8));}
 void friends(byte[][] publics)throws Exception{remote=publics;Record r=new Record(1);r.num(1);r.num(2);r.str(PeerTls.peerId(publics[0]));r.num(publics.length);for(byte[] value:publics)r.str(PeerTls.peerId(value));PeerTlsProof.check(ReliableBridge.friends(handle,1,1,r.bytes())==0);for(byte[] value:publics)PeerTlsProof.check(trustFixturePeer(handle,value,true)==0);configure();binding=MailboxBridge.registration(handle,true);PeerTlsProof.check(binding!=null);}
 void configure(){PeerTlsProof.check(MailboxBridge.configureEnrollment(handle,1,1,utf8(server),utf8(pin))==0);}
 void session(String server,String pin){this.server=server;this.pin=pin;configure();PeerTlsProof.check(MailboxBridge.registered(handle,binding)==0);Record r=new Record(30);r.num(1);r.num(1);PeerTlsProof.check(TransferBridge.request(handle,11,r.bytes())!=null);}
 byte[] command(String... fields){Record r=new Record(30);for(String value:fields)r.str(value);return r.bytes();}
 void requireNative(byte[] value){if(value!=null)return;Reader r=new Reader(TransferBridge.request(handle,5,command()),30);long count=r.num();String diagnostic=count>0?r.str():"missing";throw new AssertionError("JNI request rejected: "+diagnostic);}
 void restart()throws Exception{PeerTlsProof.check(core.destroy(handle)==0);open();friends(remote);session(server,pin);}
 static void emit(byte[] value){System.out.println(ENCODE.encodeToString(value==null||value.length==0?new byte[]{0}:value));System.out.flush();}
 public static void main(String[] args)throws Exception{
  Class.forName(ReliableProfileProof.class.getName());NativeDmAttachmentSide side=new NativeDmAttachmentSide(args[0]);emit(side.identity.pub);
  try(BufferedReader input=new BufferedReader(new InputStreamReader(System.in,StandardCharsets.US_ASCII))){String line;while((line=input.readLine())!=null){PeerTlsProof.check(line.length()<=140000);String[] fields=line.split("\\|",-1);byte[] response=new byte[]{1};switch(fields[0]){
   case "F":side.friends(new byte[][]{DECODE.decode(fields[1]),DECODE.decode(fields[2])});response=side.binding;break;
   case "S":side.session("localhost:"+fields[1],fields[2]);break;
   case "C":PeerTlsProof.check(ReliableBridge.requests(side.handle)!=null);response=TransferBridge.request(side.handle,12,new byte[0]);PeerTlsProof.check(response!=null);break;
   case "P":{Record r=new Record(30);r.num(1);r.num(1);response=TransferBridge.request(side.handle,11,r.bytes());break;}
   case "R":response=new byte[]{(byte)(TransferBridge.request(side.handle,13,DECODE.decode(fields[1]))!=null?1:0)};break;
   case "U":PeerTlsProof.check(trustFixturePeer(side.handle,side.remote[1],false)==0);break;
   case "B":PeerTlsProof.check(trustFixturePeer(side.handle,side.remote[1],true)==0);break;
   case "O":{Reader d=new Reader(TransferBridge.request(side.handle,0,side.command()),30);Path directory=Path.of(d.str());d.end();Path original=Path.of(new String(DECODE.decode(fields[1]),StandardCharsets.UTF_8)).toAbsolutePath().normalize();PeerTlsProof.check(original.startsWith(Path.of(side.directory).toAbsolutePath().getParent()));String name="jni-日本-📷.bin",file=UUID.randomUUID().toString().replace("-","")+"-"+name;try(InputStream in=Files.newInputStream(original);FileOutputStream out=new FileOutputStream(directory.resolve(file).toFile())){byte[] block=new byte[32768];long total=0;int n;while((n=in.read(block))>=0){total+=n;PeerTlsProof.check(total<=256L*1024*1024);out.write(block,0,n);}out.getFD().sync();}response=TransferBridge.request(side.handle,10,side.command(PeerTls.peerId(side.remote[0]),file,name));side.requireNative(response);break;}
   case "N":response=MailboxBridge.receive(side.handle,DECODE.decode(fields[1]));break;
   case "A":PeerTlsProof.check(TransferBridge.request(side.handle,2,side.command(fields[1]))!=null);break;
   case "K":PeerTlsProof.check(TransferBridge.request(side.handle,3,side.command(fields[1]))!=null);break;
   case "V":response=TransferBridge.request(side.handle,4,side.command());PeerTlsProof.check(response!=null);break;
   case "X":side.restart();break;
   case "Z":PeerTlsProof.check(side.core.destroy(side.handle)==0);side.open();break;
   case "L":{Record r=new Record(30);r.num(1);r.str(side.server);r.str(side.pin);r.str("");r.str("");response=TransferBridge.request(side.handle,15,r.bytes());side.requireNative(response);break;}
   case "T":response=ReliableBridge.queueLocal(side.handle,utf8(PeerTls.peerId(side.remote[0])),DECODE.decode(fields[1]));PeerTlsProof.check(response!=null);break;
   case "H":response=ReliableBridge.history(side.handle,utf8(PeerTls.peerId(side.remote[0])),Long.MAX_VALUE);PeerTlsProof.check(response!=null);break;
   case "Q":emit(response);return;
   default:throw new AssertionError("Unknown isolated JNI control operation");
  }emit(response);}}
  finally{PeerTlsProof.check(side.core.destroy(side.handle)==0);for(long key:KEYS.values())cng("destroy",new Class<?>[]{long.class},key);KEYS.clear();}
 }
}
