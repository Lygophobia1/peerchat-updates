import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.Executors;
import org.bouncycastle.crypto.hpke.HPKE;
import org.bouncycastle.crypto.AsymmetricCipherKeyPair;
import org.bouncycastle.crypto.agreement.ECDHBasicAgreement;
import org.bouncycastle.crypto.params.ECPublicKeyParameters;
import com.peerchat.crypto.bc186.hpke.ExternalP256PrivateKey;

/** Evaluation oracle only: stock BC, not a Windows runtime dependency. */
public final class WolfCryptCngEvaluation {
    private static byte[] bytes(String s){return s.getBytes(StandardCharsets.US_ASCII);}
    private static void check(boolean ok){if(!ok)throw new AssertionError("native HPKE evaluation failed");}
    public static void main(String[] args)throws Exception{
        HPKE hpke=new HPKE(HPKE.mode_base,HPKE.kem_P256_SHA256,HPKE.kdf_HKDF_SHA256,HPKE.aead_AES_GCM128);
        var recipient=hpke.generatePrivateKey();
        var patched=new com.peerchat.crypto.bc186.hpke.HPKE((byte)0,(short)16,(short)1,(short)1);
        var publicR=(ECPublicKeyParameters)recipient.getPublic();
        // HOST synthetic software provider exercises the same upstream seam.
        // It is not Android Keystore or physical Android interoperability.
        var opaque=new AsymmetricCipherKeyPair(publicR,new ExternalP256PrivateKey(publicR,point->{
            var agreement=new ECDHBasicAgreement();agreement.init(recipient.getPrivate());
            return org.bouncycastle.util.BigIntegers.asUnsignedByteArray(32,agreement.calculateAgreement(hpke.deserializePublicKey(point)));
        }));
        byte[] info=bytes("PeerChat evaluation RFC9180 Base"),aad=bytes("synthetic-account/device/message-v1"),plain=bytes("Synthetic HPKE test fixture only");
        var hex=HexFormat.of();
        Process process=new ProcessBuilder(args[0]).redirectError(ProcessBuilder.Redirect.INHERIT).start();
        var watchdog=Executors.newSingleThreadScheduledExecutor(r->{var t=new Thread(r,"evaluation-timeout");t.setDaemon(true);return t;});
        watchdog.schedule(()->{if(process.isAlive())process.destroyForcibly();},90,TimeUnit.SECONDS);
        try(var read=new BufferedReader(new InputStreamReader(process.getInputStream()));var write=new PrintWriter(process.getOutputStream(),true)){
            var cng=hpke.deserializePublicKey(hex.parseHex(read.readLine()));
            write.println(hex.formatHex(hpke.serializePublicKey(recipient.getPublic())));
            for(int i=0;i<100;++i){var sender=hpke.setupBaseS(cng,info);write.println(hex.formatHex(sender.getEncapsulation()));write.println(hex.formatHex(sender.seal(aad,plain)));check("PASS".equals(read.readLine()));}
            for(int i=0;i<100;++i){var sender=patched.setupBaseS(cng,info);write.println(hex.formatHex(sender.getEncapsulation()));write.println(hex.formatHex(sender.seal(aad,plain)));check("PASS".equals(read.readLine()));}
            for(int i=0;i<100;++i){byte[] enc=hex.parseHex(read.readLine()),ct=hex.parseHex(read.readLine());check(Arrays.equals(plain,hpke.setupBaseR(enc,recipient,info).open(aad,ct)));write.println("01");}
            for(int i=0;i<100;++i){byte[] enc=hex.parseHex(read.readLine()),ct=hex.parseHex(read.readLine());check(Arrays.equals(plain,patched.setupBaseR(enc,opaque,info).open(aad,ct)));write.println("01");}
            String result=read.readLine();check(result!=null&&result.startsWith("PASS:"));System.out.println(result);
            check(process.waitFor(15,TimeUnit.SECONDS)&&process.exitValue()==0);
        }finally{watchdog.shutdownNow();if(process.isAlive())process.destroyForcibly();}
    }
}
