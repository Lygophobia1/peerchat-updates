package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID
import org.bouncycastle.crypto.hpke.HPKE

@RunWith(AndroidJUnit4::class)
class RecipientKeyDeviceTest {
    @Test fun stockSenderToOpaqueKeystoreHpke() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val profile = "test_${UUID.randomUUID()}"
        val provider = AndroidRecipientKeyProvider(context, profile)
        assumeTrue(provider.supported())
        val alias = provider.alias(1)
        val state = ProfileSecurity(context, profile)
        try {
            val publicKey = provider.create(alias)
            val stock = HPKE(HPKE.mode_base, HPKE.kem_P256_SHA256,
                HPKE.kdf_HKDF_SHA256, HPKE.aead_AES_GCM128)
            val info = "PeerChat/opaque-hpke-device-test/v1".toByteArray()
            val aad = "test-account/device/message".toByteArray()
            val plain = "Synthetic device fixture".toByteArray()
            repeat(100) {
                val reopened = AndroidRecipientKeyProvider(context, profile)
                val sender = stock.setupBaseS(stock.deserializePublicKey(publicKey), info)
                val ct = sender.seal(aad, plain)
                assertArrayEquals(plain, AndroidRecipientHpke.open(reopened, alias,
                    publicKey, sender.encapsulation, info, aad, ct))
            }
            assertNull(state.store().getKey(alias, null).encoded)
            assertFalse(state.store().containsAlias(state.identityAlias))
            state.store().deleteEntry(alias)
            val sender = stock.setupBaseS(stock.deserializePublicKey(publicKey), info)
            var rejected = false
            try { AndroidRecipientHpke.open(provider, alias, publicKey,
                sender.encapsulation, info, aad, sender.seal(aad, plain)) }
            catch (_: Exception) { rejected = true }
            assertTrue(rejected)
            assertFalse(provider.exists(alias)) // Never creates on failed open.
        } finally {
            check(profile.startsWith("test_"))
            state.store().deleteEntry(alias)
            check(!state.directory.exists() || state.directory.deleteRecursively())
        }
    }
    @Test fun opaqueAgreementAndReopen() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val profiles = listOf("test_${UUID.randomUUID()}", "test_${UUID.randomUUID()}")
        val providers = profiles.map { AndroidRecipientKeyProvider(context, it) }
        assumeTrue(providers.all { it.supported() })
        fun rejected(f: () -> Unit) { try { f(); fail("Invalid recipient operation accepted") } catch (_: Exception) {} }
        try {
            val a=providers[0]; val b=providers[1]; val aliasA=a.alias(1); val aliasB=b.alias(1)
            assertFalse(a.exists(aliasA)); rejected { a.publicKey(aliasA) }
            val publicA=a.create(aliasA); val publicB=b.create(aliasB)
            val ab=a.agreeP256(aliasA,publicB); val ba=b.agreeP256(aliasB,publicA)
            try { assertArrayEquals(ab,ba) } finally { ab.fill(0); ba.fill(0) }
            for (i in 0 until 100) assertArrayEquals(publicA,AndroidRecipientKeyProvider(context,profiles[0]).publicKey(aliasA))
            rejected { a.create(aliasA) }; rejected { a.publicKey(aliasB) }
            rejected { a.agreeP256(aliasA,ByteArray(65).apply { this[0]=4 }) }
            rejected { a.agreeP256(aliasA,byteArrayOf(0)) }
            val state=ProfileSecurity(context,profiles[0])
            assertNull(state.store().getKey(aliasA,null).encoded)
            assertFalse(state.store().containsAlias(state.identityAlias)) // No second/signing identity created.
        } finally {
            profiles.forEachIndexed { index, profile ->
                check(profile.startsWith("test_"))
                val state=ProfileSecurity(context,profile)
                state.store().deleteEntry(providers[index].alias(1))
                check(!state.directory.exists() || state.directory.deleteRecursively())
            }
        }
    }
}
