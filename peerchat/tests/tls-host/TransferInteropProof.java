package com.peerchat.android.platform.tls;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Production native endpoint/transfer owner and Android Java TLS versus actual
 * Schannel. Shipping Windows bounded file source, memory sink/pipe adapters;
 * no Android POSIX, phone or server claim. */
public final class TransferInteropProof {
    static {System.loadLibrary("transfer-android-proof");}
    private static native byte[] step(Object facade,byte[] path,byte[] peer,byte[] encrypted,boolean verify);
    public static final class Facade {
        final PeerTlsProof.Identity identity;Facade(PeerTlsProof.Identity value){identity=value;}
        public byte[] publicKey(){return identity.publicKey();}
        public byte[] sign(byte[] value)throws Exception{return identity.sign(value);}
        public PeerTls createPeerTls(boolean incoming,byte[] pin)throws Exception{return new PeerTls(identity,incoming,new String(pin,StandardCharsets.US_ASCII));}
        public void close(){}
    }
    public static void main(String[] args)throws Exception{
        PeerTlsProof.Identity identity=new PeerTlsProof.Identity();Facade facade=new Facade(identity);
        Process child=new ProcessBuilder(args[0],args[1]+"/windows",PeerTls.peerId(identity.pub)).redirectError(ProcessBuilder.Redirect.INHERIT).start();
        java.util.concurrent.ScheduledExecutorService watchdog=java.util.concurrent.Executors.newSingleThreadScheduledExecutor();
        watchdog.schedule(()->{if(child.isAlive())child.destroyForcibly();},55,java.util.concurrent.TimeUnit.SECONDS);
        try(BufferedReader input=new BufferedReader(new InputStreamReader(child.getInputStream(),StandardCharsets.US_ASCII));PrintWriter output=new PrintWriter(child.getOutputStream(),true)){
            byte[] remote=input.readLine().getBytes(StandardCharsets.US_ASCII),path=(args[1]+"/android").getBytes(StandardCharsets.UTF_8),wire=new byte[0];boolean done=false;
            for(int i=0;i<4000;i++){
                byte[] nativeOutput=step(facade,path,remote,wire,false);if(nativeOutput==null)throw new AssertionError("Android native transfer failed at round "+i);
                byte[] payload=Arrays.copyOfRange(nativeOutput,1,nativeOutput.length);PeerTlsProof.check(payload.length<=RelayFrames.MAXIMUM_PAYLOAD);
                output.println("R:"+PeerTlsProof.hex(payload));String received=input.readLine(),state=input.readLine();
                if(received==null||!received.startsWith("O:")||state==null)throw new AssertionError("Windows native transfer failed at round "+i);
                wire=PeerTlsProof.unhex(received.substring(2));PeerTlsProof.check(wire.length<=RelayFrames.MAXIMUM_PAYLOAD);
                if(nativeOutput[0]==1&&state.equals("S:1")&&wire.length==0){done=true;break;}
            }
            PeerTlsProof.check(done&&step(facade,path,remote,new byte[0],true)!=null);output.println("V");PeerTlsProof.check("VERIFIED".equals(input.readLine()));
            PeerTlsProof.check(child.waitFor(5,java.util.concurrent.TimeUnit.SECONDS)&&child.exitValue()==0);
        }finally{watchdog.shutdownNow();if(child.isAlive())child.destroyForcibly();}
        System.out.println("PASS: actual Android JNI/Java TLS versus Windows Schannel, mutual pinned Hello/trust/winner and negotiated v2, bidirectional 2 MiB+123 files from shipping 32 KiB-bounded FileSource, 64 KiB chunk/window16/10Mbps unchanged, opaque output <=64 KiB, exact committed bytes/completion ACKs. HOST ONLY");
    }
}
