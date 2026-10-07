package com.peerchat.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.platform.*
import java.io.File
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Three-phase public/ciphertext-only exchange. Never touches the normal profile. */
@RunWith(AndroidJUnit4::class)
class HpkeInteropDeviceTest {
    @Test fun windowsHpkeAndNonExportableKeystore() {
        val arguments = InstrumentationRegistry.getArguments()
        val token = checkNotNull(arguments.getString("interopToken"))
        require(token.matches(Regex("[0-9a-f]{32}")))
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val profile = "test_hpke_interop_$token"
        val state = ProfileSecurity(context, profile)
        val keys = AndroidRecipientKeyProvider(context, profile)
        val alias = keys.alias(1)
        val directory = File(checkNotNull(context.getExternalFilesDir(null)), "mailbox-interop/$token")
        val phase = arguments.getString("interopPhase")
        if (phase == "cleanup") {
            state.store().deleteEntry(alias)
            check(!state.directory.exists() || state.directory.deleteRecursively())
            check(!directory.exists() || directory.deleteRecursively())
            return
        }
        check(keys.supported())
        check(directory.exists() || directory.mkdirs())
        if (phase == "prepare") {
            File(directory, "android-public.bin").writeBytes(keys.create(alias))
            assertFalse(state.store().containsAlias(state.identityAlias))
        } else {
            require(phase == "exchange")
            val public = keys.publicKey(alias)
            assertArrayEquals(File(directory, "android-public.bin").readBytes(), public)
            val info = "PeerChat/physical-HPKE-test/v1".toByteArray()
            val aad = "test-only/account/device/message".toByteArray()
            fun plain(i: Int) = "Synthetic physical HPKE interoperability".toByteArray() + byteArrayOf((i shr 8).toByte(), i.toByte())
            val size = 65 + plain(0).size + 16
            val input = File(directory, "windows-cipher.bin").readBytes()
            assertEquals(100 * size, input.size)
            val windows = File(directory, "windows-public.bin").readBytes()
            val output = java.io.ByteArrayOutputStream()
            repeat(100) { i ->
                val frame = input.copyOfRange(i * size, (i + 1) * size)
                val enc = frame.copyOfRange(0, 65)
                val ct = frame.copyOfRange(65, frame.size)
                assertArrayEquals(plain(i), AndroidRecipientHpke.open(keys, alias, public, enc, info, aad, ct))
                ct[0] = (ct[0].toInt() xor 1).toByte()
                var rejected = false
                try { AndroidRecipientHpke.open(keys, alias, public, enc, info, aad, ct) } catch (_: Exception) { rejected = true }
                assertTrue(rejected)
                output.write(OpaqueRecipientHpke.seal(windows, info, aad, plain(i)))
            }
            File(directory, "android-cipher.bin").writeBytes(output.toByteArray())
        }
        assertNull(state.store().getKey(alias, null).encoded)
        assertFalse(state.store().containsAlias(state.identityAlias))
    }
}
