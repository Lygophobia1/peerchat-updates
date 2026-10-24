package com.peerchat.android.platform.tls

import org.junit.Assert.*
import org.junit.Test
import java.math.BigInteger
import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.cert.X509Certificate
import java.util.Date
import org.bouncycastle.asn1.ASN1ObjectIdentifier
import org.bouncycastle.asn1.x500.X500Name
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder

class ServerCertificateRenewalTest {
    // Other Robolectric fixtures install Android Conscrypt in this shared host
    // JVM; use the same explicit BC certificate implementation as production TLS.
    private val bc=org.bouncycastle.jce.provider.BouncyCastleProvider()
    private fun key()=KeyPairGenerator.getInstance("RSA").apply{initialize(2048)}.generateKeyPair()
    private fun cert(key:KeyPair, proof:ByteArray?=null, expired:Boolean=false, signer:KeyPair=key):X509Certificate {
        val now=System.currentTimeMillis()
        val name=X500Name("CN=renewal-test")
        val builder=JcaX509v3CertificateBuilder(name,BigInteger.valueOf(now),Date(now-86400000),Date(now+if(expired)-1000 else 86400000),name,key.public)
        proof?.let{builder.addExtension(ASN1ObjectIdentifier(ServerCertificateRenewal.ANCHOR_OID),false,it)}
        return JcaX509CertificateConverter().setProvider(bc).getCertificate(builder.build(JcaContentSignerBuilder("SHA256withRSA").setProvider(bc).build(signer.private)))
    }
    private fun pin(cert:X509Certificate)=MessageDigest.getInstance("SHA-256").digest(cert.encoded)
    @Test fun renewedValidLeafCanProveContinuityToAnExpiredPreviouslyPinnedCertificate(){
        val key=key();val anchor=cert(key,expired=true)
        val renewed=cert(key,anchor.encoded)
        renewed.checkValidity()
        val encoded=org.bouncycastle.asn1.ASN1OctetString.getInstance(renewed.getExtensionValue(ServerCertificateRenewal.ANCHOR_OID)).octets
        assertArrayEquals(anchor.encoded,encoded)
        val input=java.io.ByteArrayInputStream(encoded)
        val parsed=java.security.cert.CertificateFactory.getInstance("X.509",bc).generateCertificate(input) as X509Certificate
        assertEquals(0,input.available())
        assertArrayEquals(parsed.publicKey.encoded,renewed.publicKey.encoded)
        renewed.verify(parsed.publicKey,bc)
        assertEquals("1.2.840.113549.1.1.11",renewed.sigAlgOID)
        assertTrue(ServerCertificateRenewal.matches(renewed,pin(anchor)))
        // Later renewals use the same original anchor, without growing a chain.
        assertTrue(ServerCertificateRenewal.matches(cert(key,anchor.encoded),pin(anchor)))
        assertFalse(ServerCertificateRenewal.matches(cert(key),pin(anchor)))
    }
    @Test fun publicAnchorCannotAuthorizeAnotherKeyOrAnInvalidSignature(){
        val key=key();val other=key();val anchor=cert(key,expired=true)
        assertFalse(ServerCertificateRenewal.matches(cert(other,anchor.encoded),pin(anchor)))
        assertFalse(ServerCertificateRenewal.matches(cert(key,anchor.encoded,signer=other),pin(anchor)))
        assertFalse(ServerCertificateRenewal.matches(cert(key,anchor.encoded),pin(cert(other))))
    }
    @Test fun expiryMalformedProofAndOversizedProofFailClosed(){
        val key=key();val anchor=cert(key,expired=true)
        assertFalse(ServerCertificateRenewal.matches(cert(key,anchor.encoded,expired=true),pin(anchor)))
        assertFalse(ServerCertificateRenewal.matches(cert(key,byteArrayOf(1,2,3)),pin(anchor)))
        assertFalse(ServerCertificateRenewal.matches(cert(key,ByteArray(17000)),pin(anchor)))
        assertFalse(ServerCertificateRenewal.matches(cert(key,anchor.encoded),ByteArray(0)))
    }
}
