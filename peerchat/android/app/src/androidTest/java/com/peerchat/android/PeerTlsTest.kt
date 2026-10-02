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
