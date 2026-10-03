package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID
import java.util.concurrent.Executors

@RunWith(AndroidJUnit4::class)
class SecurityProviderTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private fun rejected(block: () -> Unit) {
        try { block(); fail("Invalid operation accepted") } catch (_: ProviderFailure) { }
    }
    private fun clean(profile: String) {
        val state = ProfileSecurity(context, profile)
        check(profile.startsWith("test_"))
        val store = state.store()
        store.deleteEntry(state.identityAlias); store.deleteEntry(state.storageAlias)
        check(!store.containsAlias(state.identityAlias) && !store.containsAlias(state.storageAlias))
        check(!state.directory.exists() || state.directory.deleteRecursively()) // Only this UUID-namespaced test directory.
    }
    @Test fun identityAndAuthenticatedRecords() {
        val profile = "test_${UUID.randomUUID()}"
        val other = "test_${UUID.randomUUID()}"
        try {
            val identity = AndroidIdentityProvider(context, profile)
            val public = identity.publicRepresentation()
            val message = "PeerChat identity compatibility / 日本語 / 😀\u0000".toByteArray()
            assertEquals(72, public.size)
            assertEquals(64, PeerIdentityFormat.peerId(public).length)
            for (iteration in 0 until 250) {
                val reopened = AndroidIdentityProvider(context, profile)
                assertArrayEquals(public, reopened.publicRepresentation())
                val signature = reopened.sign(message)
                assertEquals(64, signature.size)
                assertTrue(PeerIdentityFormat.verify(public, message, signature))
                assertFalse(PeerIdentityFormat.verify(public, message + byteArrayOf(1), signature))
                assertArrayEquals(public + signature, NativeBridge.normalizeIdentity(public, signature))
            }
            rejected { identity.sign(ByteArray(70001)) }
            rejected { PeerIdentityFormat.verify(public, message, ByteArray(63)) }
            // Copied public buffers cannot mutate provider state.
            identity.publicRepresentation().fill(0)
            assertArrayEquals(public, identity.publicRepresentation())
            val binding = "history/record/1".toByteArray()
            val payloads = listOf(byteArrayOf(), "日本語 😀\u0000".toByteArray(), byteArrayOf(0, -1, 1), ByteArray(65536) { (it % 251).toByte() })
            for (payload in payloads) {
                val protector = AndroidRecordProtector(context, profile)
                val blob = protector.protect(payload, binding)
                assertArrayEquals(payload, AndroidRecordProtector(context, profile).unprotect(blob, binding))
                assertFalse(blob.contentEquals(protector.protect(payload, binding)))
                for (position in listOf(0, 4, 12, 24, blob.lastIndex)) {
                    val changed = blob.copyOf(); changed[position] = (changed[position].toInt() xor 1).toByte()
                    rejected { protector.unprotect(changed, binding) }
                }
                rejected { protector.unprotect(blob.copyOf(blob.size - 1), binding) }
                rejected { protector.unprotect(blob, "wrong context".toByteArray()) }
                rejected { AndroidRecordProtector(context, other).unprotect(blob, binding) }
            }
            rejected { AndroidRecordProtector(context, profile).protect(ByteArray(1048537), binding) }
            rejected { AndroidRecordProtector(context, profile).unprotect(byteArrayOf(), binding) }
            for (iteration in 0 until 250) {
                val protector = AndroidRecordProtector(context, profile)
                assertArrayEquals(message, protector.unprotect(protector.protect(message, binding), binding))
            }
            val pool = Executors.newFixedThreadPool(4)
            try {
                val tasks = (0 until 40).map { pool.submit<Boolean> {
                    val provider = AndroidIdentityProvider(context, profile)
                    val record = AndroidRecordProtector(context, profile)
                    PeerIdentityFormat.verify(public, message, provider.sign(message)) &&
                        record.unprotect(record.protect(message, binding), binding).contentEquals(message)
                } }
                assertTrue(tasks.all { it.get() })
            } finally { pool.shutdown() }
            // Missing existing keys must not silently create replacements.
            val state = ProfileSecurity(context, profile)
            assertNull(state.store().getKey(state.identityAlias, null).encoded)
            assertNull(state.store().getKey(state.storageAlias, null).encoded)
            val changedPublic = public.copyOf(); changedPublic[8] = (changedPublic[8].toInt() xor 1).toByte()
            state.write("identity.public", changedPublic)
            rejected { AndroidIdentityProvider(context, profile) }
            state.write("identity.public", public)
            state.store().deleteEntry(state.identityAlias)
            rejected { AndroidIdentityProvider(context, profile) }
            state.store().deleteEntry(state.storageAlias)
            rejected { AndroidRecordProtector(context, profile).protect(message, binding) }
        } finally { clean(profile); clean(other) }
    }
    @Test fun productionProfilePersistence() {
        // One normal app identity; this test never destroys or rotates it.
        val identity = AndroidIdentityProvider(context, "default")
        val protector = AndroidRecordProtector(context, "default")
        val directory = File(context.noBackupFilesDir, "provider-proof").apply { mkdirs() }
        val publicFile = File(directory, "expected.public")
        val blobFile = File(directory, "restart.record")
        val binding = "restart-proof/v1".toByteArray()
        val synthetic = "PeerChat deterministic restart proof / 日本語\u0000😀".toByteArray()
        val public = identity.publicRepresentation()
        val aliases = ProfileSecurity(context, "default").store().aliases().toList()
        assertFalse(aliases.any { it.startsWith("peerchat.test.") })
        assertEquals(1, aliases.count { it.startsWith("peerchat.identity.") })
        assertEquals(1, aliases.count { it.startsWith("peerchat.records.") })
        if (publicFile.exists()) assertArrayEquals(publicFile.readBytes(), public)
        else publicFile.writeBytes(public)
        if (blobFile.exists()) assertArrayEquals(synthetic, protector.unprotect(blobFile.readBytes(), binding))
        else blobFile.writeBytes(protector.protect(synthetic, binding))
        assertTrue(PeerIdentityFormat.verify(public, synthetic, identity.sign(synthetic)))
        assertFalse(blobFile.readBytes().toString(Charsets.ISO_8859_1).contains("PeerChat deterministic"))
        // Public interoperability fixture only; no key or protected data export.
        File(directory, "compatibility.bin").writeBytes(public + identity.sign(synthetic))
        InstrumentationRegistry.getInstrumentation().sendStatus(0, android.os.Bundle().apply {
            putString("identity_security_level", identity.securityLevel())
            putString("peer_id", PeerIdentityFormat.peerId(public))
            putString("restart_proof", "identity and authenticated record stable")
        })
    }
}
