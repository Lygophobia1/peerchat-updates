import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import org.bouncycastle.crypto.agreement.ECDHBasicAgreement;
import org.bouncycastle.crypto.hpke.HPKE;
import com.peerchat.android.platform.OpaqueRecipientHpke;

/** Actual Android HPKE adapter, with a synthetic host ECDH capability only.
 * Not a substitute for physical Keystore acceptance. */
public final class BcGoInterop {
    static void check(boolean ok){if(!ok)throw new AssertionError("Go/BC interoperability failed");}
    public static void main(String[] args)throws Exception {
        HPKE hpke=new HPKE((byte)0,(short)16,(short)1,(short)1);
        var key=hpke.generatePrivateKey();byte[] pub=hpke.serializePublicKey(key.getPublic());
        OpaqueRecipientHpke.Recipient recipient=new OpaqueRecipientHpke.Recipient(){
            public byte[] publicKey(){return pub.clone();}
            public byte[] agree(byte[] point){var dh=new ECDHBasicAgreement();dh.init(key.getPrivate());
                return org.bouncycastle.util.BigIntegers.asUnsignedByteArray(32,dh.calculateAgreement(hpke.deserializePublicKey(point)));}
        };
        Process child=new ProcessBuilder(args[0],"--interop").redirectError(ProcessBuilder.Redirect.INHERIT).start();
        var watchdog=Executors.newSingleThreadScheduledExecutor();watchdog.schedule(()->child.destroyForcibly(),90,TimeUnit.SECONDS);
        var hex=HexFormat.of();
        try(var read=new BufferedReader(new InputStreamReader(child.getInputStream()));var write=new PrintWriter(child.getOutputStream(),true)){
            byte[] remote=hex.parseHex(read.readLine()),info=hex.parseHex(read.readLine()),aad=hex.parseHex(read.readLine()),plain=hex.parseHex(read.readLine());
            write.println(hex.formatHex(pub));
            for(int i=0;i<100;++i){byte[] wire=OpaqueRecipientHpke.seal(remote,info,aad,plain);
                write.println(hex.formatHex(Arrays.copyOfRange(wire,0,65)));write.println(hex.formatHex(Arrays.copyOfRange(wire,65,wire.length)));check("PASS".equals(read.readLine()));}
            for(int i=0;i<100;++i){byte[] enc=hex.parseHex(read.readLine()),ct=hex.parseHex(read.readLine());
                check(Arrays.equals(plain,OpaqueRecipientHpke.open(recipient,pub,enc,info,aad,ct)));write.println("01");}
            String result=read.readLine();check(result!=null&&result.startsWith("PASS:"));System.out.println(result);
            check(child.waitFor(15,TimeUnit.SECONDS)&&child.exitValue()==0);
        }finally{watchdog.shutdownNow();if(child.isAlive())child.destroyForcibly();}
    }
}
