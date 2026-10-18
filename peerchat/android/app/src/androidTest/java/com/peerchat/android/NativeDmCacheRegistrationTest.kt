package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.NativeRecord
import com.peerchat.android.application.NativeRecordWriter
import com.peerchat.android.platform.AndroidRecipientKeyProvider
import com.peerchat.android.platform.PeerIdentityFormat
import com.peerchat.android.platform.ProfileSecurity
import java.io.File
import java.util.UUID
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Real Android JNI, POSIX cache and Keystore. Server acceptance is simulated;
 * this is a registration-boundary test, not an end-to-end transfer claim. */
@RunWith(AndroidJUnit4::class)
class NativeDmCacheRegistrationTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private val server = "localhost:7443".toByteArray(Charsets.US_ASCII)
    private val pin = "f".repeat(64).toByteArray(Charsets.US_ASCII)
    private val account = 71L

    private data class Command(val generation: Long, val id: String, val operation: String, val data: ByteArray)

    private fun command(handle: Long): Command? {
        val wire = checkNotNull(TransferBridge.request(handle, 12, ByteArray(0)))
        if (wire.isEmpty()) return null
        val r = NativeRecord(wire, 30)
        assertEquals("PeerChat/DMCacheCommand/v1", r.text())
        val generation = r.number()
        val id = r.text()
        val operation = r.text()
        r.number(); r.text(); r.text(); r.number(); r.number(); r.number(); r.bytes(); r.bytes()
        val data = r.bytes()
        r.end()
        return Command(generation, id, operation, data)
    }

    private fun reply(command: Command): ByteArray = NativeRecordWriter(30).apply {
        text("PeerChat/DMCacheReply/v1")
        number(command.generation); text(command.id); text("accepted")
        number(2); number(1)
        repeat(4) { number(0) }
        text(""); blob(ByteArray(0)); blob(ByteArray(0)); blob(ByteArray(0))
        number(0); number(0); text(""); number(0)
    }.bytes()

    private fun scope(handle: Long, generation: Long) {
        val friends = NativeRecordWriter(1).apply { number(0) }.bytes()
        assertEquals(0, ReliableBridge.friends(handle, account, generation, friends))
        assertEquals(0, MailboxBridge.configureEnrollment(handle, account, generation, server, pin))
        val session = NativeRecordWriter(30).apply { number(account); number(generation) }.bytes()
        val result = NativeRecord(checkNotNull(TransferBridge.request(handle, 11, session)), 30)
        assertEquals(0L, result.number()); result.end()
    }

    private fun capability(handle: Long): Command {
        val deadline = System.nanoTime() + 15_000_000_000L
        while (System.nanoTime() < deadline) {
            val next = command(handle)
            if (next != null) return next.also { assertEquals("cache_dm_capability", it.operation) }
            Thread.sleep(100)
        }
        error("Native capability retry never became available")
    }

    @Test(timeout = 55_000)
    fun acceptedMailboxBindingIsRequiredForKeystoreCacheCapabilityProof() {
        val name = "test_dm_cache_registration_${UUID.randomUUID()}"
        val security = ProfileSecurity(context, name)
        val recipient = AndroidRecipientKeyProvider(context, name)
        assumeTrue(recipient.supported())
        var binding = ByteArray(0)
        var identity = ByteArray(0)
        try {
            NativeProfile(context, name).use { profile ->
                val handle = profile.handle
                identity = checkNotNull(ProfileBridge.publicKey(handle))
                scope(handle, 1)
                binding = checkNotNull(MailboxBridge.registration(handle, true))
                val premature = capability(handle)
                assertNull("Server capability must not bypass unaccepted mailbox key",
                    TransferBridge.request(handle, 13, reply(premature)))
                assertNull("No cache device proof without accepted mailbox registration", command(handle))

                // Same authenticated generation recovers after an accepted registration.
                assertEquals(0, MailboxBridge.registered(handle, binding))
                val retry = capability(handle)
                assertNotNull(TransferBridge.request(handle, 13, reply(retry)))
                val register = checkNotNull(command(handle))
                assertEquals("cache_dm_register", register.operation)
                val proof = NativeRecord(register.data, 30)
                assertEquals("PeerChat/dm-cache-device/v1", proof.text())
                assertEquals(account, proof.number())
                assertEquals(PeerIdentityFormat.peerId(identity), proof.text())
                proof.text(); proof.number(); assertEquals(1L, proof.number()); assertEquals(1L, proof.number())
                assertArrayEquals(identity, proof.bytes())
                val signature = proof.bytes(); proof.end()
                assertEquals(64, signature.size)
                assertTrue("Real Android Keystore signed the exact capability proof",
                    PeerIdentityFormat.verify(identity, register.data.copyOfRange(0, register.data.size - 72), signature))
                assertNotNull(TransferBridge.request(handle, 13, reply(register)))
                val active = NativeRecord(checkNotNull(TransferBridge.request(handle, 11,
                    NativeRecordWriter(30).apply { number(account); number(1) }.bytes())), 30)
                assertEquals(1L, active.number()); active.end()
            }

            // Process-local acceptance is not inherited by a new JNI owner.
            NativeProfile(context, name).use { reopened ->
                assertArrayEquals(identity, ProfileBridge.publicKey(reopened.handle))
                scope(reopened.handle, 1)
                assertEquals(0, MailboxBridge.registered(reopened.handle, binding))
                assertEquals("cache_dm_capability", capability(reopened.handle).operation)
                assertEquals(0, ReliableBridge.friends(reopened.handle, account, 2,
                    NativeRecordWriter(1).apply { number(0) }.bytes()))
                assertEquals(0, MailboxBridge.configureEnrollment(reopened.handle, account, 2, server, pin))
                val changed = NativeRecordWriter(30).apply { number(account); number(2) }.bytes()
                assertNotNull(TransferBridge.request(reopened.handle, 11, changed))
                val fresh = capability(reopened.handle)
                assertNull("Old-generation acceptance must not authorize a new cache proof",
                    TransferBridge.request(reopened.handle, 13, reply(fresh)))
                assertNull(command(reopened.handle))
            }
        } finally {
            check(name.startsWith("test_dm_cache_registration_"))
            val store = security.store()
            store.deleteEntry(recipient.alias(1)); store.deleteEntry(recipient.alias(2))
            store.deleteEntry(security.identityAlias); store.deleteEntry(security.storageAlias)
            val profile = File(context.noBackupFilesDir, "profiles/" + security.id.joinToString("") { "%02x".format(it) })
            check(!profile.exists() || profile.deleteRecursively())
            check(!security.directory.exists() || security.directory.deleteRecursively())
        }
    }
}
