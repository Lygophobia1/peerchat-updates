import com.peerchat.android.platform.tls.OpaqueTlsSigner;
import java.io.*;
import java.math.BigInteger;
import java.nio.file.*;
import java.security.*;
import java.security.interfaces.ECPublicKey;
import java.security.spec.ECGenParameterSpec;
import java.util.*;
import java.util.concurrent.*;
import org.bouncycastle.asn1.*;
import org.bouncycastle.tls.*;

public final class SigningProof {
    static void check(boolean value){if(!value)throw new AssertionError("Opaque TLS signing proof failed");}
    static byte[] fixed(BigInteger value){byte[] b=value.toByteArray(),out=new byte[32];int size=Math.min(32,b.length);System.arraycopy(b,b.length-size,out,32-size,size);return out;}
    static byte[] raw(byte[] der)throws Exception{ASN1Sequence seq=ASN1Sequence.getInstance(der);byte[] out=new byte[64];System.arraycopy(fixed(ASN1Integer.getInstance(seq.getObjectAt(0)).getValue()),0,out,0,32);System.arraycopy(fixed(ASN1Integer.getInstance(seq.getObjectAt(1)).getValue()),0,out,32,32);return out;}
    static byte[] publicBytes(ECPublicKey key){byte[] b=new byte[72];System.arraycopy(new byte[]{0x45,0x43,0x53,0x31,0x20,0,0,0},0,b,0,8);System.arraycopy(fixed(key.getW().getAffineX()),0,b,8,32);System.arraycopy(fixed(key.getW().getAffineY()),0,b,40,32);return b;}
    interface Action{void run()throws Exception;}
    static void rejected(Action call)throws Exception{boolean failed=false;try{call.run();}catch(Exception expected){failed=true;}check(failed);}
    public static void main(String[] args)throws Exception{
        KeyPairGenerator generator=KeyPairGenerator.getInstance("EC");generator.initialize(new ECGenParameterSpec("secp256r1"));KeyPair key=generator.generateKeyPair();byte[] pub=publicBytes((ECPublicKey)key.getPublic());
        OpaqueTlsSigner.Identity identity=new OpaqueTlsSigner.Identity(){
            public byte[] publicKey(){return pub.clone();}
            public byte[] sign(byte[] original)throws Exception{Signature s=Signature.getInstance("SHA256withECDSA");s.initSign(key.getPrivate());s.update(original);return raw(s.sign());}
        };
        byte[] original="PeerChat deterministic restart proof / 日本語\0😀".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        byte[] signature=OpaqueTlsSigner.signOriginal(identity,original);
        Signature verifier=Signature.getInstance("SHA256withECDSA");verifier.initVerify(key.getPublic());verifier.update(original);check(verifier.verify(signature));
        verifier.initVerify(key.getPublic());verifier.update(MessageDigest.getInstance("SHA-256").digest(original));check(!verifier.verify(signature));
        rejected(()->OpaqueTlsSigner.signOriginal(null,original));
        // Feeding the precomputed digest into SHA256withECDSA must fail the
        // adapter's verification, rather than quietly producing a double hash.
        rejected(()->OpaqueTlsSigner.signOriginal(new OpaqueTlsSigner.Identity(){public byte[] publicKey(){return pub;}public byte[] sign(byte[] b)throws Exception{return identity.sign(MessageDigest.getInstance("SHA-256").digest(b));}},original));
        KeyPair other=generator.generateKeyPair();
        rejected(()->OpaqueTlsSigner.signOriginal(new OpaqueTlsSigner.Identity(){public byte[] publicKey(){return pub;}public byte[] sign(byte[] b)throws Exception{Signature s=Signature.getInstance("SHA256withECDSA");s.initSign(other.getPrivate());s.update(b);return raw(s.sign());}},original));
        for(int size:new int[]{0,63,64,65})rejected(()->OpaqueTlsSigner.signOriginal(new OpaqueTlsSigner.Identity(){public byte[] publicKey(){return pub;}public byte[] sign(byte[] b){return new byte[size];}},original));
        rejected(()->OpaqueTlsSigner.signOriginal(new OpaqueTlsSigner.Identity(){public byte[] publicKey(){return pub;}public byte[] sign(byte[] b)throws Exception{throw new IOException("Synthetic provider failure");}},original));
        for(int i=0;i<1000;i++)OpaqueTlsSigner.signOriginal(identity,original);
        ExecutorService pool=Executors.newFixedThreadPool(4);try{List<Future<?>> tasks=new ArrayList<>();for(int i=0;i<100;i++)tasks.add(pool.submit(()->{try{OpaqueTlsSigner.signOriginal(identity,original);}catch(Exception e){throw new RuntimeException(e);}}));for(Future<?> f:tasks)f.get();}finally{pool.shutdown();}
        byte[] proof=Arrays.copyOf(pub,136);System.arraycopy(raw(signature),0,proof,72,64);Files.write(Paths.get(args[0]),proof);
        check(OpaqueTlsSigner.SCHEME.getHash()==HashAlgorithm.sha256 && OpaqueTlsSigner.SCHEME.getSignature()==SignatureAlgorithm.ecdsa);
        System.out.println("PASS: original-message SHA256/ECDSA, no double hash, P-256/raw64, malformed/provider failure, 1000 repeats and 100 concurrent signatures (host test key only)");
    }
}
