package com.peerchat.android.platform.tls;

import com.peerchat.android.*;
import java.nio.file.*;
import java.util.*;
import static com.peerchat.android.platform.tls.ReliableProfileProof.*;
import com.peerchat.android.platform.tls.ReliableProfileProof.Record;

/** Real production JNI callbacks, authenticated Java TLS and protected journals.
 * Host file adapter uses Windows primitives, not Android POSIX or Keystore. */
public final class NativeTransferProof {
    static native byte[] markArchive(long handle,byte[] id);
    static byte[] command(String... fields){Record r=new Record(30);for(String s:fields)r.str(s);return r.bytes();}
    static void initArchive(Side side){Record r=new Record(31);r.str("PeerChat/ArchiveApplication/v1");r.num(1);r.num(1);PeerTlsProof.check(ArchiveBridge.request(side.handle,1,r.bytes())!=null);}
    static final class Row {String id,path,error;long state,done,size;boolean sending;}
    static List<Row> rows(Side side){Reader r=new Reader(TransferBridge.request(side.handle,4,command()),30);long n=r.num();List<Row> result=new ArrayList<>();for(int i=0;i<n;i++){Row v=new Row();v.id=r.str();r.str();r.str();v.size=r.num();v.state=r.num();v.sending=r.num()!=0;v.done=r.num();r.num();v.error=r.str();v.path=r.str();result.add(v);}return result;}
    static String offer(Side side,byte[] bytes)throws Exception{
        Reader directory=new Reader(TransferBridge.request(side.handle,0,command()),30);Path root=Path.of(directory.str());directory.end();
        String name="native-\u65e5\u672c\u8a9e.bin",cached=UUID.randomUUID().toString().replace("-","")+"-"+name;
        try(java.io.FileOutputStream out=new java.io.FileOutputStream(root.resolve(cached).toFile())){out.write(bytes);out.getFD().sync();}
        Reader r=new Reader(TransferBridge.request(side.handle,1,command(side.other,cached,name)),30);String id=r.str();r.end();return id;
    }
    static int largest;
    static void deliver(Side target,byte[] encrypted){if(encrypted.length==0)return;largest=Math.max(largest,encrypted.length);
        byte[] value=ReliableBridge.exchange(target.handle,target.peer,0,encrypted);
        if(value==null){Reader d=new Reader(TransferBridge.request(target.handle,5,command()),30);long n=d.num();for(int i=0;i<n;i++)System.out.println(d.str());throw new AssertionError("Production callback rejected encrypted input_bytes="+encrypted.length);}}
    static void pump(Side a,Side b){a.events();b.events();deliver(b,a.operation(1));deliver(a,b.operation(1));}
    static void timing(Side side,String id,boolean receiver){
        Reader d=new Reader(TransferBridge.request(side.handle,5,command()),30);long count=d.num();boolean found=false,nativeTiming=false;
        for(int i=0;i<count;i++){
            String line=d.str();
            if(line.startsWith("native_reliable_exchange_failure"))nativeTiming=line.contains("native_receive_gate_wait_us=")&&line.contains("native_output_processing_us=");
            if(line.startsWith("transfer="+id+" ")){
                String field=receiver?"receiver_samples":"ack_samples";
                PeerTlsProof.check(java.util.regex.Pattern.compile(" "+field+"=[1-9][0-9]* ").matcher(line).find());
                PeerTlsProof.check(!receiver||java.util.regex.Pattern.compile(" receiver_ack_samples=[1-9][0-9]* ").matcher(line).find());
                PeerTlsProof.check(line.length()<=2048);System.out.println("HOST_FIXTURE_TIMING "+line);found=true;
            }
        }
        d.end();PeerTlsProof.check(found&&nativeTiming);
    }
    static void transfer(Side a,Side b,int count)throws Exception{
        byte[] payload=new byte[count];new Random(91+count).nextBytes(payload);String id=offer(a,payload);boolean accepted=false;
        for(int i=0;i<2000;i++){pump(a,b);for(Row row:rows(b))if(row.id.equals(id)){
            if(!accepted&&row.state==2){PeerTlsProof.check(TransferBridge.request(b.handle,2,command(id))!=null);accepted=true;}
            if(row.state==4){PeerTlsProof.check(accepted&&row.done==count&&!row.path.isEmpty());PeerTlsProof.check(Arrays.equals(payload,Files.readAllBytes(Path.of(row.path))));
                for(int j=0;j<5;j++)pump(a,b);timing(a,id,false);timing(b,id,true);System.out.println("PASS production JNI file bytes="+count+" largest_tls_output="+largest);return;}
        }Thread.sleep(2);}throw new AssertionError("Transfer did not complete");
    }
    static void rejected(String directory,int mode)throws Exception{
        PeerTlsProof.Identity ia=new PeerTlsProof.Identity(),ib=new PeerTlsProof.Identity();
        try(Side a=new Side(ia,directory+"/a",PeerTls.peerId(ib.pub),10,20);Side b=new Side(ib,directory+"/b",PeerTls.peerId(ia.pub),20,10)){
            initArchive(a);a.attach(false);b.attach(true);exchange(a,b);String id="a".repeat(32);
            if(mode==0)PeerTlsProof.check(markArchive(a.handle,utf8(id))!=null);
            Record r=new Record(30);r.num(mode==1||mode==4?3:2);r.num(2);r.str(mode==3?"private-invalid-id":id);r.num(mode==2?0:1);r.num(0);
            byte[] plain=r.bytes();byte[] framed=java.nio.ByteBuffer.allocate(plain.length+4).putInt(plain.length).put(plain).array();
            if(mode==4){Record valid=new Record(30);valid.num(1);valid.num(6);valid.str("b".repeat(32));byte[] prefix=valid.bytes();framed=java.nio.ByteBuffer.allocate(4+prefix.length+framed.length).putInt(prefix.length).put(prefix).put(framed).array();}
            b.facade.lastTls.send(framed);byte[] encrypted=b.facade.lastTls.takeOutput();
            PeerTlsProof.check(ReliableBridge.exchange(a.handle,a.peer,0,encrypted)==null);
            Reader d=new Reader(TransferBridge.request(a.handle,5,command()),30);long count=d.num();String failure="";for(int i=0;i<count;i++){String line=d.str();if(line.startsWith("native_reliable_exchange_failure"))failure=line;}
            PeerTlsProof.check(failure.contains("stage="+(mode==0?"transfer_archive_identity":"transfer_header")));
            PeerTlsProof.check(mode==0?failure.contains("transfer="+id)&&failure.contains("peer="+b.id):!failure.contains("transfer=")&&!failure.contains("peer="));
            PeerTlsProof.check(!failure.contains("private-invalid")&&rows(a).isEmpty());System.out.println("PASS authenticated rejection mode="+mode);a.closeRoute();b.closeRoute();
        }
    }
    static void restored(String directory)throws Exception{
        PeerTlsProof.Identity ia=new PeerTlsProof.Identity(),ib=new PeerTlsProof.Identity();byte[] payload=new byte[1024*1024];new Random(87).nextBytes(payload);String id;Facade oldA,oldB;
        try(Side a=new Side(ia,directory+"/a",PeerTls.peerId(ib.pub),10,20);Side b=new Side(ib,directory+"/b",PeerTls.peerId(ia.pub),20,10)){
            oldA=a.facade;oldB=b.facade;initArchive(a);initArchive(b);a.attach(false);b.attach(true);exchange(a,b);id=offer(a,payload);boolean accepted=false,partial=false;
            for(int i=0;i<2000&&!partial;i++){pump(a,b);for(Row row:rows(b))if(row.id.equals(id)){if(!accepted&&row.state==2){PeerTlsProof.check(TransferBridge.request(b.handle,2,command(id))!=null);accepted=true;}if(row.done>=65536&&row.done<payload.length)partial=true;}Thread.sleep(2);}PeerTlsProof.check(partial);
            // Match production owner teardown: close native routes before core
            // destruction, releasing callbacks and Windows exclusive handles.
            a.closeRoute();b.closeRoute();
        }
        PeerTlsProof.check(oldA.closed&&oldB.closed);
        try(Side a=new Side(ia,directory+"/a",PeerTls.peerId(ib.pub),10,20);Side b=new Side(ib,directory+"/b",PeerTls.peerId(ia.pub),20,10)){
            initArchive(a);initArchive(b);a.attach(false);b.attach(true);exchange(a,b);
            for(int i=0;i<2000;i++){pump(a,b);for(Row row:rows(b))if(row.id.equals(id)&&row.state==4){PeerTlsProof.check(Arrays.equals(payload,Files.readAllBytes(Path.of(row.path))));for(int j=0;j<5;j++)pump(a,b);System.out.println("PASS production JNI protected-journal restart/resume");a.closeRoute();b.closeRoute();return;}Thread.sleep(2);}
            for(Side side:new Side[]{a,b}){for(Row row:rows(side))System.out.println("fixture restore state="+row.state+" error="+row.error);Reader d=new Reader(TransferBridge.request(side.handle,5,command()),30);long n=d.num();for(int i=0;i<n;i++)System.out.println(d.str());}throw new AssertionError("Restored transfer incomplete");
        }
    }
    public static void main(String[] args)throws Exception{
        Class.forName(ReliableProfileProof.class.getName());
        if(args.length==2&&args[1].equals("restore")){restored(args[0]);return;}
        PeerTlsProof.Identity ia=new PeerTlsProof.Identity(),ib=new PeerTlsProof.Identity();
        try(Side a=new Side(ia,args[0]+"/a",PeerTls.peerId(ib.pub),10,20);Side b=new Side(ib,args[0]+"/b",PeerTls.peerId(ia.pub),20,10)){
            initArchive(a);a.attach(false);b.attach(true);exchange(a,b);
            transfer(a,b,16384);transfer(a,b,2*1024*1024);transfer(b,a,2*1024*1024);a.closeRoute();b.closeRoute();
        }
        for(int mode=0;mode<5;mode++)rejected(args[0]+"/reject"+mode,mode);restored(args[0]+"/restored");
    }
}
