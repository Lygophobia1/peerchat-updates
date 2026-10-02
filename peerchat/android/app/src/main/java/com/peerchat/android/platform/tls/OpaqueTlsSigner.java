package com.peerchat.android.platform.tls;

import java.io.*;
import java.math.BigInteger;
import java.security.*;
import java.security.spec.*;
import java.util.Arrays;
import org.bouncycastle.asn1.*;
import org.bouncycastle.tls.*;
import org.bouncycastle.tls.crypto.TlsStreamSigner;

/** Signing operations only; no private-key object/bytes, session or trust owner. */
public final class OpaqueTlsSigner implements TlsCredentialedSigner {
    public interface Identity {
        byte[] publicKey(); // Unchanged CNG72 P-256.
        byte[] sign(byte[] original) throws Exception; // Unchanged raw64 SHA256/ECDSA.
    }
    private final Identity identity;
    private final org.bouncycastle.tls.Certificate certificate;
    public static final SignatureAndHashAlgorithm SCHEME=new SignatureAndHashAlgorithm(HashAlgorithm.sha256,SignatureAlgorithm.ecdsa);
    public OpaqueTlsSigner(Identity identity,org.bouncycastle.tls.Certificate certificate){
        if(identity==null || certificate==null || certificate.isEmpty())throw new IllegalArgumentException("Missing TLS identity");
        this.identity=identity;this.certificate=certificate;
    }
    public org.bouncycastle.tls.Certificate getCertificate(){return certificate;}
    public SignatureAndHashAlgorithm getSignatureAndHashAlgorithm(){return SCHEME;}
    // Never feed an already-computed digest into SHA256withECDSA.
    public byte[] generateRawSignature(byte[] hash)throws IOException{throw new TlsFatalAlert(AlertDescription.internal_error);}
    public TlsStreamSigner getStreamSigner(){return new TlsStreamSigner(){
        private boolean used;
        private final ByteArrayOutputStream input=new ByteArrayOutputStream();
        private final OutputStream bounded=new OutputStream(){
            public void write(int value)throws IOException{if(used || input.size()>=70000)throw new IOException("TLS signing input limit");input.write(value);}
            public void write(byte[] bytes,int offset,int length)throws IOException{
                if(bytes==null || offset<0 || length<0 || offset>bytes.length-length || used || length>70000-input.size())throw new IOException("TLS signing input limit");
                input.write(bytes,offset,length);
            }
        };
        public OutputStream getOutputStream(){return bounded;}
        public byte[] getSignature()throws IOException{
            if(used || input.size()==0)throw new IOException("Invalid TLS signing operation");used=true;
            byte[] original=input.toByteArray();try{return signOriginal(identity,original);}finally{Arrays.fill(original,(byte)0);input.reset();}
        }
    };}
    public static PublicKey publicKey(byte[] bytes)throws Exception{
        byte[] header={0x45,0x43,0x53,0x31,0x20,0,0,0};
        if(bytes==null || bytes.length!=72 || !Arrays.equals(Arrays.copyOf(bytes,8),header))throw new IOException("Invalid TLS public identity");
        AlgorithmParameters params=AlgorithmParameters.getInstance("EC");params.init(new ECGenParameterSpec("secp256r1"));
        return KeyFactory.getInstance("EC").generatePublic(new ECPublicKeySpec(new ECPoint(new BigInteger(1,Arrays.copyOfRange(bytes,8,40)),new BigInteger(1,Arrays.copyOfRange(bytes,40,72))),params.getParameterSpec(ECParameterSpec.class)));
    }
    public static byte[] signOriginal(Identity identity,byte[] original)throws IOException{
        if(identity==null || original==null || original.length==0 || original.length>70000)throw new IOException("Invalid TLS signing input");
        try{
            PublicKey key=publicKey(identity.publicKey());byte[] raw=identity.sign(original.clone());
            if(raw==null || raw.length!=64)throw new IOException("Invalid TLS signature length");
            BigInteger r=new BigInteger(1,Arrays.copyOfRange(raw,0,32)),s=new BigInteger(1,Arrays.copyOfRange(raw,32,64)),order=((java.security.interfaces.ECPublicKey)key).getParams().getOrder();
            if(r.signum()<=0 || s.signum()<=0 || r.compareTo(order)>=0 || s.compareTo(order)>=0)throw new IOException("Invalid TLS signature scalar");
            byte[] der=new DERSequence(new ASN1Encodable[]{new ASN1Integer(r),new ASN1Integer(s)}).getEncoded();
            Signature verify=Signature.getInstance("SHA256withECDSA");verify.initVerify(key);verify.update(original);
            if(!verify.verify(der))throw new IOException("TLS provider signature mismatch");return der;
        }catch(Exception failure){throw new IOException("Opaque TLS signing failed",failure);}
    }
}
