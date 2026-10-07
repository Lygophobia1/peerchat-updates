package com.peerchat.android.platform

import com.peerchat.android.platform.tls.ServerChannel
import java.math.BigInteger
import java.net.InetAddress
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.MessageDigest
import java.util.Date
import javax.net.ssl.*
import org.bouncycastle.asn1.x500.X500Name
import org.bouncycastle.cert.jcajce.JcaX509CertificateConverter
import org.bouncycastle.cert.jcajce.JcaX509v3CertificateBuilder
import org.bouncycastle.operator.jcajce.JcaContentSignerBuilder
import org.bouncycastle.jce.provider.BouncyCastleProvider
import org.junit.Assert.*
import org.junit.Test

class ServerTrustTest {
    @Test fun endpointPinCannotBeErasedOrReplacedByBlankEdits() {
        val saved=ServerEndpoint("Server.example",7443,"a".repeat(64))
        assertEquals(saved.certificatePin,serverTrustExpected(saved,ServerEndpoint("server.example",7443)))
        assertThrows(IllegalArgumentException::class.java){
            serverTrustExpected(saved,saved.copy(certificatePin="b".repeat(64)))
        }
        assertEquals("",serverTrustExpected(saved,ServerEndpoint("different.example",7443)))
        validateObservedServer(saved.certificatePin,saved.certificatePin)
        assertThrows(IllegalArgumentException::class.java){validateObservedServer(saved.certificatePin,"b".repeat(64))}
    }
    @Test fun realTlsInspectionAbortsBeforeAuthenticationAndPinnedReconnectWorks() {
        val pair=KeyPairGenerator.getInstance("RSA").apply{initialize(2048)}.generateKeyPair()
        val bc=BouncyCastleProvider()
        val name=X500Name("CN=localhost")
        val cert=JcaX509CertificateConverter().setProvider(bc).getCertificate(
            JcaX509v3CertificateBuilder(name,BigInteger.ONE,Date(System.currentTimeMillis()-60000),
                Date(System.currentTimeMillis()+3600000),name,pair.public)
                .build(JcaContentSignerBuilder("SHA256withRSA").setProvider(bc).build(pair.private)))
        val pin=MessageDigest.getInstance("SHA-256").digest(cert.encoded).joinToString(""){"%02x".format(it)}
        val keys=KeyStore.getInstance(KeyStore.getDefaultType()).apply{
            load(null,null);setKeyEntry("test-only",pair.private,"test-only".toCharArray(),arrayOf(cert))
        }
        val managers=KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()).apply{init(keys,"test-only".toCharArray())}
        // Robolectric installs Conscrypt globally for other tests. The host
        // fixture deliberately uses JDK TLS, avoiding its Android-only
        // reflective socket internals under JDK 22's module boundaries.
        val context=SSLContext.getInstance("TLS","SunJSSE").apply{init(managers.keyManagers,null,null)}
        fun round(requireSuccess:Boolean=false,action:(Int,java.util.concurrent.CountDownLatch)->Unit):Boolean {
            val listener=context.serverSocketFactory.createServerSocket(0,1,InetAddress.getLoopbackAddress()) as SSLServerSocket
            listener.enabledProtocols=arrayOf("TLSv1.2")
            var completed=false
            var serverFailure:Exception?=null
            val ready=java.util.concurrent.CountDownLatch(1)
            val worker=Thread {
                try{(listener.accept() as SSLSocket).use{s->s.soTimeout=10000;s.startHandshake();completed=true;ready.countDown();s.inputStream.read()}}
                catch(error:Exception){serverFailure=error;ready.countDown()}finally{listener.close()}
            }
            worker.start()
            try{action(listener.localPort,ready)}finally{worker.join(12000);listener.close()}
            assertFalse("TLS fixture worker did not drain",worker.isAlive)
            if(requireSuccess&&serverFailure!=null)throw AssertionError("Pinned fixture handshake",serverFailure)
            if(completed)assertNull("Pinned TLS fixture failed after handshake",serverFailure)
            return completed
        }
        assertFalse(round{port,_->assertEquals(pin,ServerChannel.inspectCertificate("localhost",port))})
        assertTrue(round(true){port,ready->ServerChannel("localhost",port,pin,false).use{
            assertTrue(ready.await(10,java.util.concurrent.TimeUnit.SECONDS))
        }})
        assertFalse(round{port,_->
            try{ServerChannel("localhost",port,"a".repeat(64),false).close();fail("Wrong pin accepted")}
            catch(error:Exception){
                val change=ServerChannel.identityChange(error)
                assertNotNull(change);assertEquals(pin,change!!.observed);assertEquals("a".repeat(64),change.expected)
            }
        })
    }
}
