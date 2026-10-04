package com.peerchat.android

import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.UUID

@RunWith(AndroidJUnit4::class)
class RecipientKeyDeviceTest {
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
