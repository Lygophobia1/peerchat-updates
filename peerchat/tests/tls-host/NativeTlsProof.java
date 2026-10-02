package com.peerchat.android.platform.tls;
import java.util.concurrent.atomic.AtomicInteger;

/** Real Java TLS through the production JNI provider and native TlsSession.
 * Test keys only; neither Android Keystore nor physical execution is claimed.
 */
public final class NativeTlsProof {
    static {System.loadLibrary("tls-provider-proof");}
    private static native boolean run(Object left,Object right,byte[] leftPin,byte[] rightPin,byte[] input);
    public static final class Facade {
        final PeerTlsProof.Identity identity;
        static final AtomicInteger closes=new AtomicInteger();
        boolean closed;
        Facade(PeerTlsProof.Identity value){identity=value;}
        public byte[] publicKey(){return identity.publicKey();}
        public PeerTls createPeerTls(boolean incoming,byte[] expected)throws Exception{
            if(closed)throw new IllegalStateException("Closed fixture");
            return new PeerTls(identity,incoming,new String(expected,java.nio.charset.StandardCharsets.US_ASCII));
        }
        public void close(){if(closed)throw new AssertionError("Double-close");closed=true;closes.incrementAndGet();}
    }
    public static void main(String[] ignored)throws Exception{
        PeerTlsProof.Identity a=new PeerTlsProof.Identity(),b=new PeerTlsProof.Identity();
        byte[] aid=PeerTls.peerId(a.pub).getBytes(java.nio.charset.StandardCharsets.US_ASCII),bid=PeerTls.peerId(b.pub).getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        byte[] bytes="JNI/native TLS 日本語\0😀".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        for(int i=0;i<100;i++)PeerTlsProof.check(run(new Facade(a),new Facade(b),aid,bid,bytes));
        PeerTlsProof.check(!run(new Facade(a),new Facade(b),aid,aid,bytes));
        a.broken=true;try{PeerTlsProof.check(!run(new Facade(a),new Facade(b),aid,bid,bytes));}finally{a.broken=false;}
        PeerTlsProof.check(Facade.closes.get()==204);
        System.out.println("PASS: actual native TlsSession/JNI/original-signing Java TLS, 100 mutual lifecycles, Unicode/NUL byte echo, wrong pin and signer failure, exact context cleanup. HOST ONLY");
    }
}
