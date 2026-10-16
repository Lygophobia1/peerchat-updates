package com.peerchat.android.platform.tls;

import java.nio.*;
import java.nio.charset.StandardCharsets;
import java.io.ByteArrayOutputStream;
import java.util.Arrays;

/** Positive production Profile/JNI/reliable-runtime proof. Host-only test keys,
 * real SQLite and DPAPI primitives, production pinned Java peer TLS. No phone,
 * Android Keystore, UI, live server relay, or process-cold identity claim. */
public final class ReliableProfileProof {
    static {System.loadLibrary("peerchat_messaging");System.loadLibrary("peerchat_jni");}
    private static native byte[] database(byte[] directory);
    private static native byte[] sql(long db,long stmt,int op,int index,byte[] input,long number,boolean blob);
    private static native byte[] protection(boolean encrypt,byte[] input,byte[] label);
    private static native byte[] closeDatabase(long db);
    static byte[] utf8(String text){return text.getBytes(StandardCharsets.UTF_8);}
    static byte[] empty=new byte[0];
    static long number(byte[] bytes){return ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN).getLong();}
    public static final class Facade {
        final PeerTlsProof.Identity identity;final long db;final java.io.File attachmentPath;boolean closed,failAttachments;PeerTls lastTls;
        Facade(PeerTlsProof.Identity identity,String directory){this.identity=identity;db=ReliableProfileProof.number(database(utf8(directory)));attachmentPath=new java.io.File(directory,"attachments");}
        public byte[] publicKey(){return identity.publicKey();}
        // Required by the current native owner even for text-only sessions.
        // The selected host adapter decides whether file I/O is available.
        public byte[] attachmentDirectory(){if(failAttachments)throw new IllegalStateException("synthetic-private-path-never-log");PeerTlsProof.check(attachmentPath.isDirectory()||attachmentPath.mkdirs());return utf8(attachmentPath.getAbsolutePath());}
        public byte[] sign(byte[] original)throws Exception{return identity.sign(original);}
        public PeerTls createPeerTls(boolean incoming,byte[] pin)throws Exception{
            if(closed)throw new IllegalStateException("Closed fixture");
            lastTls=new PeerTls(identity,incoming,new String(pin,StandardCharsets.US_ASCII));return lastTls;
        }
        public byte[] protect(byte[] bytes,byte[] label){return protection(true,bytes,label);}
        public byte[] unprotect(byte[] bytes,byte[] label){return protection(false,bytes,label);}
        public void execute(byte[] query){sql(db,0,0,0,query,0,false);}
        public long prepare(byte[] query){return ReliableProfileProof.number(sql(db,0,1,0,query,0,false));}
        public void bindBytes(long stmt,int index,byte[] value,boolean blob){sql(db,stmt,2,index,value,0,blob);}
        public void bindNumber(long stmt,int index,long value){sql(db,stmt,3,index,empty,value,false);}
        public boolean row(long stmt){return sql(db,stmt,4,0,empty,0,false)[0]!=0;}
        public byte[] bytes(long stmt,int index){return sql(db,stmt,5,index,empty,0,false);}
        public byte[] text(long stmt,int index){return sql(db,stmt,6,index,empty,0,false);}
        public long number(long stmt,int index){return ReliableProfileProof.number(sql(db,stmt,7,index,empty,0,false));}
        public void finish(long stmt){sql(db,stmt,8,0,empty,0,false);}
        public void close(){PeerTlsProof.check(!closed);closed=true;closeDatabase(db);}
    }
    static final class Record {
        ByteArrayOutputStream out=new ByteArrayOutputStream();
        Record(int type){out.write('P');out.write('T');out.write(1);out.write(type);}
        void num(long value){for(int i=0;i<8;i++)out.write((byte)(value>>(8*i)));}
        void str(String text){byte[] value=utf8(text);num(value.length);out.writeBytes(value);}
        byte[] bytes(){return out.toByteArray();}
    }
    static final class Reader {
        final ByteBuffer bytes;
        Reader(byte[] value,int type){PeerTlsProof.check(value!=null&&value.length>=4&&value[0]=='P'&&value[1]=='T'&&value[2]==1&&value[3]==type);bytes=ByteBuffer.wrap(value).order(ByteOrder.LITTLE_ENDIAN);bytes.position(4);}
        long num(){return bytes.getLong();}
        String str(){long count=num();PeerTlsProof.check(count>=0&&count<=65536&&count<=bytes.remaining());byte[] result=new byte[(int)count];bytes.get(result);return new String(result,StandardCharsets.UTF_8);}
        void end(){PeerTlsProof.check(!bytes.hasRemaining());}
    }
    static final class Side implements AutoCloseable {
        final com.peerchat.android.NativeBridge core=new com.peerchat.android.NativeBridge();
        final long handle;final String id,other;final Facade facade;long peer;
        Side(PeerTlsProof.Identity identity,String directory,String other,long account,long remote)throws Exception{
            id=PeerTls.peerId(identity.pub);this.other=other;handle=core.create();PeerTlsProof.check(handle!=0);
            facade=new Facade(identity,directory);PeerTlsProof.check(com.peerchat.android.ProfileBridge.bind(handle,facade)==0);
            // Read failure diagnostics while the original initialization still
            // fails; diagnostics must not retry cache/journal construction.
            facade.failAttachments=true;
            Record diagnostic=new Record(30);
            PeerTlsProof.check(com.peerchat.android.TransferBridge.request(handle,0,diagnostic.bytes())==null);
            Reader failure=new Reader(com.peerchat.android.TransferBridge.request(handle,5,diagnostic.bytes()),30);
            PeerTlsProof.check(failure.num()==2);String row=failure.str();failure.str();failure.end();
            PeerTlsProof.check(row.contains("stage=attachment_prepare reason=native_operation_rejected")&&!row.contains("synthetic-private"));
            facade.failAttachments=false;
            PeerTlsProof.check(Arrays.equals(com.peerchat.android.ProfileBridge.publicKey(handle),identity.pub));
            Record record=new Record(1);record.num(1);record.num(remote);record.str(other);record.num(1);record.str(other);
            PeerTlsProof.check(com.peerchat.android.ReliableBridge.friends(handle,account,1,record.bytes())==0);
            PeerTlsProof.check(com.peerchat.android.ReliableBridge.presence(handle,utf8(other),true,1,1)==0);
            // Explicit verification/control operation; presence alone is not demand.
            PeerTlsProof.check(com.peerchat.android.ReliableBridge.demand(handle,utf8(other))==0);
            Reader request=new Reader(com.peerchat.android.ReliableBridge.requests(handle),1);
            PeerTlsProof.check(request.num()==1);peer=request.num();PeerTlsProof.check(request.str().equals(other)&&request.num()==1);
            // Production relay requests include an explicit scope blob. This
            // direct-message fixture must receive an empty (not group/archive) scope.
            PeerTlsProof.check(request.num()==0);request.end();
        }
        void attach(boolean incoming){PeerTlsProof.check(com.peerchat.android.ReliableBridge.open(handle,peer,incoming,utf8(other))==peer);receive(empty);}
        byte[] operation(int op){byte[] result=com.peerchat.android.ReliableBridge.exchange(handle,peer,op,empty);PeerTlsProof.check(result!=null);return result;}
        void receive(byte[] bytes){PeerTlsProof.check(com.peerchat.android.ReliableBridge.exchange(handle,peer,0,bytes)!=null);}
        boolean ready(){
            boolean active=operation(2)[0]!=0;
            Reader state=new Reader(com.peerchat.android.ReliableBridge.state(handle,utf8(other)),4);
            PeerTlsProof.check((state.num()!=0)==active);state.num();state.num();state.num();state.num();state.str();state.end();
            return active;
        }
        void events(){while(true){byte[] event=operation(4);if(event.length==0)return;Reader read=new Reader(event,3);long kind=read.num();read.str();read.str();long message=read.num();if(kind==0)PeerTlsProof.check(operation(3)[0]!=0);PeerTlsProof.check(message==0||kind==2);}}
        void closeRoute(){PeerTlsProof.check(com.peerchat.android.ReliableBridge.close(handle,peer,false)==0);}
        @Override public void close(){PeerTlsProof.check(core.destroy(handle)==0);}
    }
    static void exchange(Side left,Side right){
        for(int i=0;i<100;i++){
            left.events();right.events();byte[] a=left.operation(1),b=right.operation(1);
            if(a.length==0&&b.length==0)break;
            // Exercise copied encrypted buffers and fragmented TLS records.
            for(int offset=0;offset<a.length;offset+=113)right.receive(Arrays.copyOfRange(a,offset,Math.min(a.length,offset+113)));
            for(int offset=0;offset<b.length;offset+=113)left.receive(Arrays.copyOfRange(b,offset,Math.min(b.length,offset+113)));
        }
        left.events();right.events();PeerTlsProof.check(left.ready()&&right.ready());
    }
    static long history(Side side,String expectedId,long expectedCount){
        Reader read=new Reader(com.peerchat.android.ReliableBridge.history(side.handle,utf8(side.other),Long.MAX_VALUE),2);
        long count=read.num();PeerTlsProof.check(count==expectedCount);
        for(long i=0;i<count;i++){String id=read.str(),sender=read.str(),recipient=read.str();read.num();read.num();read.num();long delivered=read.num();String text=read.str();
            PeerTlsProof.check(sender.equals(side.id)||sender.equals(side.other));PeerTlsProof.check(recipient.equals(side.id)||recipient.equals(side.other));
            PeerTlsProof.check(text.equals("Native JNI message 日本語 😀"));if(id.equals(expectedId))return delivered;
        }throw new AssertionError("Message missing from native history");
    }
    static void diagnosticBounds(Side side){
        byte[] oversized=new byte[65537];Arrays.fill(oversized,(byte)'x');
        PeerTlsProof.check(com.peerchat.android.ReliableBridge.exchange(side.handle,side.peer,0,oversized)==null);
        PeerTlsProof.check(com.peerchat.android.TransferBridge.request(side.handle,1,new byte[4097])==null);
        Record command=new Record(30);Reader read=new Reader(com.peerchat.android.TransferBridge.request(side.handle,5,command.bytes()),30);
        long count=read.num();PeerTlsProof.check(count==2);
        String request=read.str(),exchange=read.str();read.end();
        PeerTlsProof.check(request.contains("operation=1 stage=jni_input reason=native_operation_rejected input_bytes=4097"));
        PeerTlsProof.check(exchange.contains("operation=0 stage=jni_input reason=input_bound_rejected input_bytes=65537"));
        PeerTlsProof.check(request.length()<512&&exchange.length()<512&&!request.contains(side.id)&&!exchange.contains(side.other));
        PeerTlsProof.check(side.operation(2).length==2&&side.ready());
    }
    public static void main(String[] args)throws Exception{
        PeerTlsProof.Identity a=new PeerTlsProof.Identity(),b=new PeerTlsProof.Identity();
        String aid=PeerTls.peerId(a.pub),bid=PeerTls.peerId(b.pub),pending=null;
        for(int cycle=0;cycle<8;cycle++){
            try(Side left=new Side(a,args[0]+"/a",bid,10,20);Side right=new Side(b,args[0]+"/b",aid,20,10)){
                left.attach(false);right.attach(true);exchange(left,right);
                diagnosticBounds(left);
                if(cycle==0)System.out.println("PASS: bounded native op5 failure diagnostics preserve operation/stage/reason/input length, omit identities/payloads and keep STATUS exactly2 bytes/winner live");
                if(pending!=null){PeerTlsProof.check(history(left,pending,cycle)==2);PeerTlsProof.check(history(right,pending,cycle)==2);}
                byte[] result=com.peerchat.android.ReliableBridge.send(left.handle,utf8(bid),utf8("Native JNI message 日本語 😀"));PeerTlsProof.check(result!=null);
                pending=new String(result,StandardCharsets.US_ASCII);
                if(cycle==3){
                    // Deliver and discard the ACK, then destroy both native
                    // profiles. The next reopen must retry this exact durable ID.
                    right.receive(left.operation(1));right.events();PeerTlsProof.check(right.operation(1).length>0);
                    PeerTlsProof.check(history(left,pending,cycle+1)==0);
                }else{exchange(left,right);PeerTlsProof.check(history(left,pending,cycle+1)==2);}
                PeerTlsProof.check(history(right,pending,cycle+1)==2);
                left.closeRoute();right.closeRoute();
            }
        }
        System.out.println("PASS: production JNI/Profile + real SQLite/DPAPI + pinned original-signing TLS; 8 profile reopen/Hello/trust/message/ACK cycles; lost ACK restart retry preserves ID, dedupe and durable history. HOST ONLY, test identities; no Android runtime claim.");
    }
}
