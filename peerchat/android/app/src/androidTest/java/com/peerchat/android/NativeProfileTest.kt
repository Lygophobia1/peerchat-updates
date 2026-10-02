package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID

@RunWith(AndroidJUnit4::class)
class NativeProfileTest {
    private val context get() = InstrumentationRegistry.getInstrumentation().targetContext
    private fun clean(profile: String) {
        check(profile.startsWith("test_"))
        val state = ProfileSecurity(context, profile)
        val store = state.store(); store.deleteEntry(state.identityAlias); store.deleteEntry(state.storageAlias)
        val directory = File(context.noBackupFilesDir, "profiles/" + state.id.joinToString("") { "%02x".format(it) })
        check(!directory.exists() || directory.deleteRecursively())
        check(!state.directory.exists() || state.directory.deleteRecursively())
    }
    @Test fun nativeProfileAndProductionStorage() {
        val profile = "test_${UUID.randomUUID()}"
        try {
            val public = AndroidIdentityProvider(context, profile).publicRepresentation()
            repeat(32) {
                val handle: Long
                NativeProfile(context, profile).use { native ->
                    handle = native.handle
                    assertArrayEquals(public, ProfileBridge.publicKey(handle))
                    assertArrayEquals(intArrayOf(1, 1, 1, 1, 1), NativeBridge.info(handle))
                    val bytes = "日本語 😀\u0000native signing".toByteArray()
                    assertTrue(PeerIdentityFormat.verify(public, bytes, checkNotNull(ProfileBridge.sign(handle, bytes))))
                    assertEquals(0, ProfileBridge.storageTest(handle, 0))
                    assertNull(ProfileBridge.sign(handle, ByteArray(70001)))
                }
                assertNull(ProfileBridge.publicKey(handle)); assertNull(ProfileBridge.sign(handle, byteArrayOf(1)))
            }
            NativeProfile(context, profile).use { assertEquals(0, ProfileBridge.storageTest(it.handle, 1)) }
            NativeProfile(context, profile).use { assertEquals(0, ProfileBridge.storageTest(it.handle, 2)); assertEquals(0, ProfileBridge.storageTest(it.handle, 3)) }
            // Corrupted message must not prevent reading an independent valid profile.
            NativeProfile(context, profile).use { assertArrayEquals(public, ProfileBridge.publicKey(it.handle)) }
            val state = ProfileSecurity(context, profile)
            state.store().deleteEntry(state.identityAlias)
            try { NativeProfile(context, profile).close(); fail("Missing identity was regenerated") } catch (_: Exception) { }
            assertFalse(state.store().containsAlias(state.identityAlias))
        } finally { clean(profile) }
    }
    @Test fun existingNormalIdentityIsNotReplaced() {
        val expected = AndroidIdentityProvider(context, "default").publicRepresentation()
        NativeProfile(context).use {
            assertArrayEquals(expected, ProfileBridge.publicKey(it.handle))
            assertEquals(1, NativeBridge.info(it.handle)?.get(2))
            assertEquals(1, NativeBridge.info(it.handle)?.get(3))
            assertEquals(1, ProfileBridge.storageTest(it.handle, 0)) // test-only hook rejects production profile
            val payload = "PeerChat deterministic restart proof / 日本語\u0000😀".toByteArray()
            val signature = checkNotNull(ProfileBridge.sign(it.handle, payload))
            assertTrue(PeerIdentityFormat.verify(expected, payload, signature))
            // Public/signature only, for the unchanged Windows compatibility verifier.
            val proof = File(context.filesDir, "provider-proof").also { check(it.isDirectory || it.mkdirs()) }
            File(proof, "native-compatibility.bin").writeBytes(expected + signature)
        }
        assertArrayEquals(expected, AndroidIdentityProvider(context, "default").publicRepresentation())
    }
}
