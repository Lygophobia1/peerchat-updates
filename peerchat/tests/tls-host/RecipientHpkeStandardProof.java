import com.peerchat.android.platform.RecipientPublicKey;
import java.io.*;
import java.util.*;
import java.util.concurrent.TimeUnit;
import org.bouncycastle.crypto.AsymmetricCipherKeyPair;
import org.bouncycastle.crypto.agreement.ECDHBasicAgreement;
import org.bouncycastle.crypto.hpke.HPKE;
import org.bouncycastle.crypto.hpke.HPKEContext;
import org.bouncycastle.crypto.hpke.HPKEContextWithEncapsulation;

// Independent upstream implementation: RFC 9180 Appendix A.3.1. Published
// deterministic test scalars are TEST ONLY, never platform production keys.
public final class RecipientHpkeStandardProof {
    private static byte[] hex(String value) { return HexFormat.of().parseHex(value); }
    private static void check(boolean value) { if(!value) throw new AssertionError("HPKE standards proof failed"); }
    private static void rejects(Checked operation) throws Exception {
        boolean rejected=false; try { operation.run(); } catch(Exception expected) { rejected=true; } check(rejected);
    }
    interface Checked { void run() throws Exception; }
    public static void main(String[] args) throws Exception {
        HPKE hpke=new HPKE(HPKE.mode_base,HPKE.kem_P256_SHA256,HPKE.kdf_HKDF_SHA256,HPKE.aead_AES_GCM128);
        byte[] info=hex("4f6465206f6e2061204772656369616e2055726e");
        byte[] ikmE=hex("4270e54ffd08d79d5928020af4686d8f6b7d35dbe470265f1f5aa22816ce860e");
        byte[] ikmR=hex("668b37171f1072f3cf12ea8a236a45df23fc13b82af3609ad1e354f6ef817550");
        byte[] publicE=hex("04a92719c6195d5085104f469a8b9814d5838ff72b60501e2c4466e5e67b325ac98536d7b61a1af4b78e5b7f951c0900be863c403ce65c9bfcb9382657222d18c4");
        byte[] publicR=hex("04fe8c19ce0905191ebc298a9245792531f26f0cece2460639e8bc39cb7f706a826a779b4cf969b8a0e539c7f62fb3d30ad6aa8f80e30f1d128aafd68a2ce72ea0");
        AsymmetricCipherKeyPair ephemeral=hpke.deriveKeyPair(ikmE),recipient=hpke.deriveKeyPair(ikmR);
        check(Arrays.equals(publicE,hpke.serializePublicKey(ephemeral.getPublic())));
        check(Arrays.equals(publicR,hpke.serializePublicKey(recipient.getPublic())));
        check(Arrays.equals(hex("4995788ef4b9d6132b249ce59a77281493eb39af373d236a1fe415cb0c2d7beb"),hpke.serializePrivateKey(ephemeral.getPrivate())));
        check(Arrays.equals(hex("f3ce7fdae57e1a310d87f1ebbde6f328be0a99cdbcadf4d6589cf29de4b8ffd2"),hpke.serializePrivateKey(recipient.getPrivate())));
        HPKEContextWithEncapsulation sender=hpke.setupBaseS(recipient.getPublic(),info,ephemeral);
        HPKEContext receiver=hpke.setupBaseR(sender.getEncapsulation(),recipient,info);
        check(Arrays.equals(publicE,sender.getEncapsulation()));
        byte[] plaintext=hex("4265617574792069732074727574682c20747275746820626561757479");
        String[] expected={
            "5ad590bb8baa577f8619db35a36311226a896e7342a6d836d8b7bcd2f20b6c7f9076ac232e3ab2523f39513434",
            "fa6f037b47fc21826b610172ca9637e82d6e5801eb31cbd3748271affd4ecb06646e0329cbdf3c3cd655b28e82",
            "895cabfac50ce6c6eb02ffe6c048bf53b7f7be9a91fc559402cbc5b8dcaeb52b2ccc93e466c28fb55fed7a7fec"};
        for(int i=0;i<expected.length;++i) {
            byte[] aad=("Count-"+i).getBytes(java.nio.charset.StandardCharsets.US_ASCII);
            byte[] ciphertext=sender.seal(aad,plaintext); check(Arrays.equals(hex(expected[i]),ciphertext));
            check(Arrays.equals(plaintext,receiver.open(aad,ciphertext)));
        }
        check(Arrays.equals(hex("5e9bc3d236e1911d95e65b576a8a86d478fb827e8bdfe77b741b289890490d4d"),sender.export(new byte[0],32)));
        check(Arrays.equals(hex("6cff87658931bda83dc857e6353efe4987a201b849658d9b047aab4cf216e796"),sender.export(new byte[]{0},32)));
        check(Arrays.equals(hex("d8f1ea7942adbba7412c6d431c62d01371ea476b823eb697e1f6e6cae1dab85a"),sender.export(hex("54657374436f6e74657874"),32)));
        RecipientPublicKey.validate(publicR);
        rejects(()->RecipientPublicKey.validate(new byte[]{0}));
        rejects(()->RecipientPublicKey.validate(new byte[65]));
        byte[] offCurve=new byte[65];offCurve[0]=4;rejects(()->RecipientPublicKey.validate(offCurve));
        byte[] outOfField=new byte[65];Arrays.fill(outOfField,(byte)255);outOfField[0]=4;rejects(()->RecipientPublicKey.validate(outOfField));
        byte[] wrongFormat=publicR.clone();wrongFormat[0]=2;rejects(()->RecipientPublicKey.validate(wrongFormat));
        byte[] aad="Count-0".getBytes(java.nio.charset.StandardCharsets.US_ASCII),ct=hex(expected[0]);
        ct[0]^=1; rejects(()->hpke.setupBaseR(publicE,recipient,info).open(aad,ct));
        rejects(()->hpke.setupBaseR(publicE,recipient,info).open(new byte[]{0},hex(expected[0])));
        if(args.length==1) {
            Process process=new ProcessBuilder(args[0],"--standard-dh").redirectError(ProcessBuilder.Redirect.INHERIT).start();
            try(BufferedReader read=new BufferedReader(new InputStreamReader(process.getInputStream()));
                PrintWriter write=new PrintWriter(process.getOutputStream(),true)) {
                byte[] cngPublic=hex(read.readLine()); RecipientPublicKey.validate(cngPublic);
                ECDHBasicAgreement agreement=new ECDHBasicAgreement();agreement.init(ephemeral.getPrivate());
                byte[] signed=agreement.calculateAgreement(hpke.deserializePublicKey(cngPublic)).toByteArray();
                byte[] dh=new byte[32];int offset=signed.length==33?1:0;
                System.arraycopy(signed,offset,dh,32-(signed.length-offset),signed.length-offset);
                write.println(HexFormat.of().formatHex(publicE));write.println(HexFormat.of().formatHex(dh));
                Arrays.fill(dh,(byte)0);Arrays.fill(signed,(byte)0);
                check(read.readLine().startsWith("PASS:"));
                check(process.waitFor(15,TimeUnit.SECONDS) && process.exitValue()==0);
            } finally { if(process.isAlive()) process.destroyForcibly(); }
        }
        System.out.println("PASS: unmodified BC 1.86 RFC9180 A.3.1 key derivation, three ciphertext vectors, three exporter vectors, malformed points and ciphertext/AAD rejection");
        System.out.println("NOT YET PROVEN: opaque HPKE receiver, PeerChat HPKE sender, Android/Windows HPKE interoperability");
    }
}
