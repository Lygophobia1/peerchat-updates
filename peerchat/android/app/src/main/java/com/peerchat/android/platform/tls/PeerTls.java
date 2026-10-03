package com.peerchat.android.platform.tls;

import java.io.*;
import java.math.BigInteger;
import java.security.*;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.interfaces.ECPublicKey;
import java.util.*;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x509.AlgorithmIdentifier;
import org.bouncycastle.asn1.x509.SubjectPublicKeyInfo;
import org.bouncycastle.asn1.x9.X9ObjectIdentifiers;
import org.bouncycastle.cert.X509v3CertificateBuilder;
import org.bouncycastle.operator.ContentSigner;
import org.bouncycastle.tls.*;
import org.bouncycastle.tls.crypto.*;
import org.bouncycastle.tls.crypto.impl.bc.BcTlsCrypto;

/** Nonblocking peer TLS bytes only. No sockets, framing, trust promotion or retries.
 * Native session/delivery owners must separately authorize application dispatch.
 */
public final class PeerTls implements AutoCloseable {
    private static final int LIMIT=256*1024;
    private final TlsProtocol protocol;
    private final byte[] local;
    private byte[] remote;
    private boolean failed;
    private static byte[] fixed(BigInteger value){byte[] b=value.toByteArray(),out=new byte[32];int n=Math.min(32,b.length);System.arraycopy(b,b.length-n,out,32-n,n);return out;}
    public static byte[] publicBytes(ECPublicKey key)throws Exception{
        byte[] bytes=new byte[72];System.arraycopy(new byte[]{0x45,0x43,0x53,0x31,0x20,0,0,0},0,bytes,0,8);
        if(key.getW().getAffineX().bitLength()>256 || key.getW().getAffineY().bitLength()>256)throw new IOException("Unsupported TLS key");
        System.arraycopy(fixed(key.getW().getAffineX()),0,bytes,8,32);System.arraycopy(fixed(key.getW().getAffineY()),0,bytes,40,32);
        PublicKey canonical=OpaqueTlsSigner.publicKey(bytes);
        if(!MessageDigest.isEqual(key.getEncoded(),canonical.getEncoded()))throw new IOException("TLS peer key is not P-256");
        return bytes;
    }
    public static String peerId(byte[] publicKey)throws Exception{
        OpaqueTlsSigner.publicKey(publicKey);byte[] digest=MessageDigest.getInstance("SHA-256").digest(publicKey);StringBuilder text=new StringBuilder();
        for(byte b:digest)text.append(String.format(Locale.ROOT,"%02x",b&255));return text.toString();
    }
    public static byte[] certificate(OpaqueTlsSigner.Identity identity,Date start,Date end)throws Exception{
        if(identity==null || !start.before(end))throw new IOException("Missing TLS identity");
        PublicKey key=OpaqueTlsSigner.publicKey(identity.publicKey());X500Name name=new X500Name("CN="+peerId(identity.publicKey()));
        BigInteger serial=new BigInteger(128,new SecureRandom()).setBit(0);
        X509v3CertificateBuilder builder=new X509v3CertificateBuilder(name,serial,start,end,name,SubjectPublicKeyInfo.getInstance(key.getEncoded()));
        return builder.build(new ContentSigner(){
            private final ByteArrayOutputStream bytes=new ByteArrayOutputStream();
            public AlgorithmIdentifier getAlgorithmIdentifier(){return new AlgorithmIdentifier(X9ObjectIdentifiers.ecdsa_with_SHA256);}
            public OutputStream getOutputStream(){return bytes;}
            public byte[] getSignature(){try{return OpaqueTlsSigner.signOriginal(identity,bytes.toByteArray());}catch(IOException e){throw new IllegalStateException("TLS certificate signing failed",e);}}
        }).getEncoded();
    }
    public static byte[] validateCertificate(byte[] der,String expectedPeer)throws IOException{
        try{
            if(der==null || der.length<1 || der.length>32768 || expectedPeer==null || !expectedPeer.matches("[a-f0-9]{64}"))throw new IOException("Invalid peer certificate/pin");
            ByteArrayInputStream input=new ByteArrayInputStream(der);
            X509Certificate certificate=(X509Certificate)CertificateFactory.getInstance("X.509").generateCertificate(input);
            if(input.available()!=0)throw new IOException("Trailing certificate data");
            certificate.checkValidity();
            if(!certificate.getSubjectX500Principal().equals(certificate.getIssuerX500Principal()) || !certificate.getSigAlgOID().equals("1.2.840.10045.4.3.2"))throw new IOException("Invalid self-signed peer identity");
            certificate.verify(certificate.getPublicKey());
            if(!(certificate.getPublicKey() instanceof ECPublicKey))throw new IOException("Unsupported peer key");
            byte[] publicKey=publicBytes((ECPublicKey)certificate.getPublicKey());
            if(!MessageDigest.isEqual(peerId(publicKey).getBytes(java.nio.charset.StandardCharsets.US_ASCII),expectedPeer.getBytes(java.nio.charset.StandardCharsets.US_ASCII)))throw new IOException("Peer device pin mismatch");
            return publicKey;
        }catch(Exception failure){throw new IOException("Peer TLS certificate validation failed",failure);}
    }
    private void verify(org.bouncycastle.tls.Certificate chain,String expected)throws IOException{
        // Windows peers use one self-signed device certificate, not server CA trust.
        if(chain==null || chain.getLength()!=1)throw new TlsFatalAlert(AlertDescription.bad_certificate);
        remote=validateCertificate(chain.getCertificateAt(0).getEncoded(),expected);
    }
    private static Vector<SignatureAndHashAlgorithm> algorithms(){Vector<SignatureAndHashAlgorithm> values=new Vector<>();values.add(OpaqueTlsSigner.SCHEME);return values;}
    public PeerTls(OpaqueTlsSigner.Identity identity,boolean incoming,String expectedPeer)throws Exception{
        this(identity,incoming,expectedPeer,false);
    }
    // Protocol selection only; production native factory preserves TLS1.2.
    // No certificate override or insecure validation/test bypass exists.
    PeerTls(OpaqueTlsSigner.Identity identity,boolean incoming,String expectedPeer,boolean tls13Only)throws Exception{
        if(identity==null || expectedPeer==null || !expectedPeer.matches("[a-f0-9]{64}"))throw new IOException("Missing peer TLS identity/pin");
        local=identity.publicKey().clone();OpaqueTlsSigner.publicKey(local);
        BcTlsCrypto crypto=new BcTlsCrypto(new SecureRandom());long now=System.currentTimeMillis();
        byte[] der=certificate(identity,new Date(now-86400000L),new Date(now+315360000000L));
        // Validate our own certificate and identity before beginning a handshake.
        validateCertificate(der,peerId(local));
        org.bouncycastle.tls.Certificate chain=tls13Only
            ?new org.bouncycastle.tls.Certificate(new byte[0],new CertificateEntry[]{new CertificateEntry(crypto.createCertificate(der),null)})
            :new org.bouncycastle.tls.Certificate(new TlsCertificate[]{crypto.createCertificate(der)});
        OpaqueTlsSigner signer=new OpaqueTlsSigner(identity,chain);
        ProtocolVersion[] versions=tls13Only?new ProtocolVersion[]{ProtocolVersion.TLSv13}:new ProtocolVersion[]{ProtocolVersion.TLSv12};
        int[] ciphers=tls13Only?new int[]{CipherSuite.TLS_AES_128_GCM_SHA256}:new int[]{CipherSuite.TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256};
        if(incoming){
            TlsServerProtocol server=new TlsServerProtocol();protocol=server;
            server.accept(new DefaultTlsServer(crypto){
                protected ProtocolVersion[] getSupportedVersions(){return versions;}
                protected int[] getSupportedCipherSuites(){return ciphers;}
                protected TlsCredentialedSigner getECDSASignerCredentials(){return signer;}
                public TlsCredentials getCredentials(){return signer;}
                public CertificateRequest getCertificateRequest()throws IOException{
                    return tls13Only?new CertificateRequest(new byte[0],algorithms(),algorithms(),null):new CertificateRequest(new short[]{ClientCertificateType.ecdsa_sign},algorithms(),null);
                }
                public void notifyClientCertificate(org.bouncycastle.tls.Certificate chain)throws IOException{verify(chain,expectedPeer);}
                public boolean requiresCloseNotify(){return true;}
            });
        }else{
            TlsClientProtocol client=new TlsClientProtocol();protocol=client;
            client.connect(new DefaultTlsClient(crypto){
                protected ProtocolVersion[] getSupportedVersions(){return versions;}
                protected int[] getSupportedCipherSuites(){return ciphers;}
                protected Vector<SignatureAndHashAlgorithm> getSupportedSignatureAlgorithms(){return algorithms();}
                public TlsAuthentication getAuthentication(){return new TlsAuthentication(){
                    public void notifyServerCertificate(TlsServerCertificate chain)throws IOException{verify(chain.getCertificate(),expectedPeer);}
                    public TlsCredentials getClientCredentials(CertificateRequest request)throws IOException{
                        if(request.getSupportedSignatureAlgorithms()==null || !request.getSupportedSignatureAlgorithms().contains(OpaqueTlsSigner.SCHEME))throw new TlsFatalAlert(AlertDescription.handshake_failure);
                        return signer;
                    }
                };}
                public boolean requiresCloseNotify(){return true;}
            });
        }
    }
    private void bounds()throws IOException{if(protocol.getAvailableInputBytes()>LIMIT || protocol.getAvailableOutputBytes()>LIMIT)throw new IOException("TLS buffer limit");}
    public synchronized void receive(byte[] encrypted)throws IOException{
        if(failed || encrypted==null || encrypted.length>LIMIT)throw new IOException("Invalid TLS input");
        try{protocol.offerInput(encrypted);bounds();}catch(IOException|RuntimeException error){failed=true;throw error;}
    }
    public synchronized byte[] takeOutput()throws IOException{bounds();byte[] result=new byte[protocol.getAvailableOutputBytes()];if(protocol.readOutput(result,0,result.length)!=result.length)throw new IOException("TLS output accounting failed");return result;}
    public synchronized byte[] takePlaintext()throws IOException{bounds();if(!ready())return new byte[0];byte[] result=new byte[protocol.getAvailableInputBytes()];if(protocol.readInput(result,0,result.length)!=result.length)throw new IOException("TLS input accounting failed");return result;}
    public synchronized boolean ready(){return !failed && remote!=null && !protocol.isHandshaking() && !protocol.isClosed();}
    public synchronized boolean closed(){return failed || protocol.isClosed();}
    public synchronized byte[] remotePublicKey(){return remote==null?new byte[0]:remote.clone();}
    public byte[] localPublicKey(){return local.clone();}
    public synchronized void send(byte[] bytes)throws IOException{if(!ready() || bytes==null || bytes.length>LIMIT)throw new IOException("TLS session not ready/input limit");try{protocol.writeApplicationData(bytes,0,bytes.length);bounds();}catch(IOException|RuntimeException error){failed=true;throw error;}}
    public synchronized void close()throws IOException{protocol.close();}
}
