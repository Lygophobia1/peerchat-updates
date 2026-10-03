package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.platform.AndroidIdentityProvider
import com.peerchat.android.platform.PeerIdentityFormat
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class PeerTlsTest {
    @Test fun certificateAndProviderFailuresFailClosed() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val provider = AndroidIdentityProvider(context, "default")
        val public = provider.publicRepresentation()
        val identity = com.peerchat.android.platform.tls.AndroidTlsIdentity(provider)
        val pin = PeerIdentityFormat.peerId(public)
        val now = System.currentTimeMillis()
        fun rejected(block: () -> Unit) {
            var failed = false; try { block() } catch (_: Exception) { failed = true }; assertTrue(failed)
        }
        val cert = com.peerchat.android.platform.tls.PeerTls.certificate(identity, java.util.Date(now - 86400000), java.util.Date(now + 86400000))
        assertArrayEquals(public, com.peerchat.android.platform.tls.PeerTls.validateCertificate(cert, pin))
        rejected { com.peerchat.android.platform.tls.PeerTls.validateCertificate(cert, "0".repeat(64)) }
        rejected { com.peerchat.android.platform.tls.PeerTls.validateCertificate(byteArrayOf(1, 2, 3), pin) }
        val altered = cert.copyOf(); altered[altered.lastIndex] = (altered.last().toInt() xor 1).toByte()
        rejected { com.peerchat.android.platform.tls.PeerTls.validateCertificate(altered, pin) }
        val expired = com.peerchat.android.platform.tls.PeerTls.certificate(identity, java.util.Date(now - 172800000), java.util.Date(now - 86400000))
        rejected { com.peerchat.android.platform.tls.PeerTls.validateCertificate(expired, pin) }
        val future = com.peerchat.android.platform.tls.PeerTls.certificate(identity, java.util.Date(now + 86400000), java.util.Date(now + 172800000))
        rejected { com.peerchat.android.platform.tls.PeerTls.validateCertificate(future, pin) }
        rejected { com.peerchat.android.platform.tls.PeerTls(null, false, pin) }
        val broken = object : com.peerchat.android.platform.tls.OpaqueTlsSigner.Identity {
            override fun publicKey() = public.copyOf()
            override fun sign(original: ByteArray): ByteArray = throw java.io.IOException("Synthetic unavailable signer")
        }
        rejected { com.peerchat.android.platform.tls.PeerTls(broken, false, pin) }
        NativeProfile(context).use { profile ->
            val client = PeerTlsBridge.open(profile.handle, false, "0".repeat(64).toByteArray())
            val server = PeerTlsBridge.open(profile.handle, true, pin.toByteArray())
            assertTrue(client > 0 && server > 0)
            try {
                val hello = checkNotNull(PeerTlsBridge.exchange(profile.handle, client, PeerTlsBridge.OUTPUT, byteArrayOf()))
                assertNotNull(PeerTlsBridge.exchange(profile.handle, server, PeerTlsBridge.RECEIVE, hello))
                val response = checkNotNull(PeerTlsBridge.exchange(profile.handle, server, PeerTlsBridge.OUTPUT, byteArrayOf()))
                assertNull(PeerTlsBridge.exchange(profile.handle, client, PeerTlsBridge.RECEIVE, response))
                assertNull(PeerTlsBridge.exchange(profile.handle, client, PeerTlsBridge.STATUS, byteArrayOf()))
            } finally { PeerTlsBridge.close(profile.handle, client); PeerTlsBridge.close(profile.handle, server) }
        }
        assertArrayEquals(public, AndroidIdentityProvider(context, "default").publicRepresentation())
    }
    @Test fun existingIdentityNativeMutualTls() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        // Reuse the ONE normal identity. No new alias, uninstall or second app.
        val public = AndroidIdentityProvider(context, "default").publicRepresentation()
        val pin = PeerIdentityFormat.peerId(public).toByteArray(Charsets.US_ASCII)
        NativeProfile(context).use { profile ->
            fun bytes(session: Long, op: Int, input: ByteArray = byteArrayOf()): ByteArray =
                checkNotNull(PeerTlsBridge.exchange(profile.handle, session, op, input))
            repeat(100) {
                val client = PeerTlsBridge.open(profile.handle, false, pin)
                val server = PeerTlsBridge.open(profile.handle, true, pin)
                assertTrue(client > 0 && server > 0 && client != server)
                try {
                    repeat(20) {
                        bytes(server, PeerTlsBridge.RECEIVE, bytes(client, PeerTlsBridge.OUTPUT))
                        bytes(client, PeerTlsBridge.RECEIVE, bytes(server, PeerTlsBridge.OUTPUT))
                    }
                    assertEquals(1, bytes(client, PeerTlsBridge.STATUS)[0].toInt())
                    assertEquals(1, bytes(server, PeerTlsBridge.STATUS)[0].toInt())
                    assertArrayEquals(public, bytes(client, PeerTlsBridge.REMOTE_PUBLIC_KEY))
                    val payload = "synthetic TLS / 日本語\u0000😀".toByteArray()
                    bytes(client, PeerTlsBridge.SEND, payload)
                    val encrypted = bytes(client, PeerTlsBridge.OUTPUT)
                    assertFalse(encrypted.contentEquals(payload))
                    encrypted.forEach { bytes(server, PeerTlsBridge.RECEIVE, byteArrayOf(it)) }
                    assertArrayEquals(payload, bytes(server, PeerTlsBridge.PLAINTEXT))
                    assertNull(PeerTlsBridge.exchange(profile.handle, client, -1, byteArrayOf()))
                } finally {
                    assertEquals(0, PeerTlsBridge.close(profile.handle, client))
                    assertEquals(0, PeerTlsBridge.close(profile.handle, server))
                    assertEquals(2, PeerTlsBridge.close(profile.handle, client))
                    assertNull(PeerTlsBridge.exchange(profile.handle, client, PeerTlsBridge.STATUS, byteArrayOf()))
                }
            }
            assertEquals(0, PeerTlsBridge.open(profile.handle, false, byteArrayOf()))
            assertEquals(0, PeerTlsBridge.open(0, false, pin))
            assertArrayEquals(public, ProfileBridge.publicKey(profile.handle))
        }
        assertArrayEquals(public, AndroidIdentityProvider(context, "default").publicRepresentation())
    }
}
