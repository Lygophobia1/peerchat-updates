import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import org.bouncycastle.crypto.AsymmetricCipherKeyPair;
import org.bouncycastle.crypto.agreement.ECDHBasicAgreement;
import org.bouncycastle.crypto.params.ECPublicKeyParameters;
import com.peerchat.crypto.bc186.hpke.ExternalP256PrivateKey;
import com.peerchat.crypto.bc186.hpke.HPKE;

public final class OpaqueHpkeHookProof {
    private static byte[] hex(String s) { return HexFormat.of().parseHex(s); }
    private static void check(boolean b) { if(!b) throw new AssertionError("Opaque HPKE proof failed"); }
    interface Checked { void run() throws Exception; }
    private static void rejects(Checked f) throws Exception { boolean rejected=false; try { f.run(); } catch(Exception expected) { rejected=true; } check(rejected); }
    private static HPKE patched() { return new HPKE((byte)0,(short)16,(short)1,(short)1); }
    private static org.bouncycastle.crypto.hpke.HPKE stock() { return new org.bouncycastle.crypto.hpke.HPKE((byte)0,(short)16,(short)1,(short)1); }
    private static byte[] dh(AsymmetricCipherKeyPair key,byte[] remote) {
        ECDHBasicAgreement operation=new ECDHBasicAgreement();operation.init(key.getPrivate());
        return org.bouncycastle.util.BigIntegers.asUnsignedByteArray(32,operation.calculateAgreement(stock().deserializePublicKey(remote)));
    }
    public static void main(String[] args) throws Exception {
        var original=stock();var hook=patched();
        byte[] info=hex("4f6465206f6e2061204772656369616e2055726e");
        byte[] aad=hex("436f756e742d30"),plain=hex("4265617574792069732074727574682c20747275746820626561757479");
        var pair=original.deriveKeyPair(hex("668b37171f1072f3cf12ea8a236a45df23fc13b82af3609ad1e354f6ef817550"));
        var ephemeral=original.deriveKeyPair(hex("4270e54ffd08d79d5928020af4686d8f6b7d35dbe470265f1f5aa22816ce860e"));
        var publicR=(ECPublicKeyParameters)pair.getPublic();
        AtomicInteger calls=new AtomicInteger();byte[][] lastSecret={null};
        var opaque=new AsymmetricCipherKeyPair(publicR,new ExternalP256PrivateKey(publicR,point->{calls.incrementAndGet();return lastSecret[0]=dh(pair,point);}));
        var sender=original.setupBaseS(pair.getPublic(),info,ephemeral);
        byte[] enc=sender.getEncapsulation(),ct=sender.seal(aad,plain);
        check(Arrays.equals(ct,hex("5ad590bb8baa577f8619db35a36311226a896e7342a6d836d8b7bcd2f20b6c7f9076ac232e3ab2523f39513434")));
        var normal=hook.setupBaseR(enc,pair,info);var external=hook.setupBaseR(enc,opaque,info);
        check(calls.get()==1 && Arrays.equals(lastSecret[0],new byte[32]));
        check(Arrays.equals(normal.export(new byte[0],32),external.export(new byte[0],32)));
        check(Arrays.equals(plain,external.open(aad,ct)) && Arrays.equals(plain,normal.open(aad,ct)));
        for(int i=0;i<100;++i) {
            var fresh=original.setupBaseS(pair.getPublic(),info);byte[] ciphertext=fresh.seal(aad,plain);
            check(Arrays.equals(plain,hook.setupBaseR(fresh.getEncapsulation(),opaque,info).open(aad,ciphertext)));
            var reverse=hook.setupBaseS(pair.getPublic(),info);byte[] outbound=reverse.seal(aad,plain);
            check(Arrays.equals(plain,original.setupBaseR(reverse.getEncapsulation(),pair,info).open(aad,outbound)));
        }
        int before=calls.get();
        byte[] offCurve=new byte[65];offCurve[0]=4;
        for(byte[] bad:new byte[][]{new byte[0],new byte[]{0},new byte[65],offCurve,Arrays.copyOf(enc,64)}) rejects(()->hook.setupBaseR(bad,opaque,info));
        check(calls.get()==before); // Validation precedes private operation.
        var p384=new org.bouncycastle.crypto.hpke.HPKE((byte)0,(short)17,(short)1,(short)1).generatePrivateKey();
        rejects(()->new ExternalP256PrivateKey((ECPublicKeyParameters)p384.getPublic(),p->new byte[32]));
        rejects(()->new HPKE((byte)0,(short)17,(short)1,(short)1).setupBaseR(enc,opaque,info));
        rejects(()->new HPKE((byte)1,(short)16,(short)1,(short)1).setupBaseR(enc,opaque,info));
        check(calls.get()==before);
        rejects(()->hook.setupPSKR(enc,opaque,info,new byte[32],new byte[]{1}));
        rejects(()->hook.setupAuthR(enc,opaque,info,pair.getPublic()));
        rejects(()->hook.setupAuthPSKR(enc,opaque,info,new byte[32],new byte[]{1},pair.getPublic()));
        check(calls.get()==before);
        var failing=new AsymmetricCipherKeyPair(publicR,new ExternalP256PrivateKey(publicR,p->{throw new IllegalStateException("Provider unavailable");}));
        rejects(()->hook.setupBaseR(enc,failing,info));
        var shortResult=new AsymmetricCipherKeyPair(publicR,new ExternalP256PrivateKey(publicR,p->new byte[31]));
        rejects(()->hook.setupBaseR(enc,shortResult,info));
        var nullResult=new AsymmetricCipherKeyPair(publicR,new ExternalP256PrivateKey(publicR,p->null));
        rejects(()->hook.setupBaseR(enc,nullResult,info));
        byte[] changed=ct.clone();changed[0]^=1;rejects(()->hook.setupBaseR(enc,opaque,info).open(aad,changed));
        rejects(()->hook.setupBaseR(enc,opaque,info).open(new byte[]{0},ct));
        var wrong=original.generatePrivateKey();var mismatched=new AsymmetricCipherKeyPair(wrong.getPublic(),opaque.getPrivate());
        rejects(()->hook.setupBaseR(enc,mismatched,info));
        final long handle=NativeCngHpkeFixture.create("PeerChat.Recipient.test.hpke."+UUID.randomUUID());
        boolean destroyed=false;
        try {
            var cngPublic=(ECPublicKeyParameters)original.deserializePublicKey(NativeCngHpkeFixture.publicKey(handle));
            var cngOpaque=new AsymmetricCipherKeyPair(cngPublic,new ExternalP256PrivateKey(cngPublic,p->NativeCngHpkeFixture.agree(handle,p)));
            for(int i=0;i<100;++i) {
                var standardSender=original.setupBaseS(cngPublic,info);var ciphertext=standardSender.seal(aad,plain);
                check(Arrays.equals(plain,hook.setupBaseR(standardSender.getEncapsulation(),cngOpaque,info).open(aad,ciphertext)));
            }
            NativeCngHpkeFixture.destroy(handle);
            destroyed=true;
            rejects(()->hook.setupBaseR(enc,cngOpaque,info)); // Missing/stale key fails, never recreated.
        } finally { if(!destroyed) NativeCngHpkeFixture.destroy(handle); }
        System.out.println("PASS: RFC A.3.1 external-DH differential, 100 stock-to-hook + 100 hook-to-stock, 100 stock-to-opaque-CNG HPKE envelopes; validated-before-callback, zeroization and provider/mode/tamper failures");
        System.out.println("PENDING: physical Android Keystore HPKE and Windows/Android cross-device interoperability");
    }
}
