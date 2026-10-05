package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.application.NativeRecordWriter
import com.peerchat.android.platform.*
import java.io.File
import java.util.UUID
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeMailboxTest {
    @Test fun opaqueNativeRegistrationReopenAndConsentRotation() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val name = "test_${UUID.randomUUID()}"
        val state = ProfileSecurity(context, name)
        val recipient = AndroidRecipientKeyProvider(context, name)
        org.junit.Assume.assumeTrue(recipient.supported())
        var identity: ByteArray? = null
        var first: ByteArray? = null
        try {
            // Simulated authenticated application snapshot/registration response.
            // This tests the production JNI/provider/SQL path, not server auth.
            repeat(32) {
                NativeProfile(context, name).use { profile ->
                    val public = checkNotNull(ProfileBridge.publicKey(profile.handle))
                    if (identity == null) identity = public else assertArrayEquals(identity, public)
                    val snapshot = NativeRecordWriter(1).apply { number(0) }.bytes()
                    assertEquals(0, ReliableBridge.friends(profile.handle, 8, 1, snapshot))
                    val binding = checkNotNull(MailboxBridge.registration(profile.handle, true))
                    if (first == null) first = binding else assertArrayEquals(first, binding)
                    // Canonical trailing signature is a 4-byte length + raw P1363.
                    assertTrue(PeerIdentityFormat.verify(public, binding.copyOfRange(0, binding.size - 68), binding.takeLast(64).toByteArray()))
                    assertEquals(0, MailboxBridge.registered(profile.handle, binding))
                    assertNull(state.store().getKey(recipient.alias(1), null).encoded)
                    assertNull(state.store().getKey(state.identityAlias, null).encoded)
                    assertNull(MailboxBridge.prepare(profile.handle, ByteArray(33), binding, true))
                    assertNull(MailboxBridge.receive(profile.handle, byteArrayOf(1)))
                }
            }
            NativeProfile(context, name).use { profile ->
                assertEquals(0, ReliableBridge.friends(profile.handle, 8, 1, NativeRecordWriter(1).apply { number(0) }.bytes()))
                val forbidden = checkNotNull(MailboxBridge.registration(profile.handle, false))
                assertFalse(forbidden.contentEquals(first))
                assertEquals(0, MailboxBridge.registered(profile.handle, forbidden))
                assertArrayEquals(forbidden, MailboxBridge.registration(profile.handle, false))
                assertArrayEquals(identity, ProfileBridge.publicKey(profile.handle))
                assertNull(state.store().getKey(recipient.alias(2), null).encoded)
            }
            assertNull(MailboxBridge.registration(0, true))
        } finally {
            check(name.startsWith("test_"))
            val store = state.store()
            store.deleteEntry(recipient.alias(1)); store.deleteEntry(recipient.alias(2))
            store.deleteEntry(state.identityAlias); store.deleteEntry(state.storageAlias)
            val directory = File(context.noBackupFilesDir, "profiles/" + state.id.joinToString("") { "%02x".format(it) })
            check(!directory.exists() || directory.deleteRecursively())
            check(!state.directory.exists() || state.directory.deleteRecursively())
        }
    }
}
