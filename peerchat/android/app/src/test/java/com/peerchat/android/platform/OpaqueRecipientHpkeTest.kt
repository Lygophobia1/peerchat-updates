package com.peerchat.android.platform

import org.bouncycastle.crypto.agreement.ECDHBasicAgreement
import org.bouncycastle.crypto.hpke.HPKE
import org.bouncycastle.util.BigIntegers
import org.junit.Assert.*
import org.junit.Test

class OpaqueRecipientHpkeTest {
    private fun stock() = HPKE(HPKE.mode_base, HPKE.kem_P256_SHA256,
        HPKE.kdf_HKDF_SHA256, HPKE.aead_AES_GCM128)
    private fun rejected(f: () -> Unit) {
        var failed = false
        try { f() } catch (_: Exception) { failed = true }
        assertTrue("Operation must fail closed", failed)
    }
    @Test fun stockSenderOpaqueReceiverAndCapabilityFailures() {
        val hpke = stock()
        val pair = hpke.generatePrivateKey() // Exportable synthetic fixture ONLY.
        val publicKey = hpke.serializePublicKey(pair.public)
        var authorized = true
        var calls = 0
        var lastDh = ByteArray(0)
        val recipient = object : OpaqueRecipientHpke.Recipient {
            override fun publicKey(): ByteArray {
                check(authorized) { "Revoked, wrong role/version or unavailable" }
                return publicKey.clone()
            }
            override fun agree(point: ByteArray): ByteArray {
                check(authorized)
                calls++
                val agreement = ECDHBasicAgreement().apply { init(pair.private) }
                return BigIntegers.asUnsignedByteArray(32,
                    agreement.calculateAgreement(hpke.deserializePublicKey(point))).also { lastDh = it }
            }
        }
        val info = "PeerChat/opaque-hpke-test/v1".toByteArray()
        val aad = "test-account/device/message".toByteArray()
        val plain = "Synthetic interoperability fixture".toByteArray()
        repeat(100) {
            val sender = hpke.setupBaseS(pair.public, info)
            val ciphertext = sender.seal(aad, plain)
            assertArrayEquals(plain, OpaqueRecipientHpke.open(recipient, publicKey,
                sender.encapsulation, info, aad, ciphertext))
            assertArrayEquals(ByteArray(32), lastDh)
        }
        val sender = hpke.setupBaseS(pair.public, info)
        val enc = sender.encapsulation
        val ct = sender.seal(aad, plain)
        val before = calls
        rejected { OpaqueRecipientHpke.open(recipient, publicKey, ByteArray(65), info, aad, ct) }
        rejected { OpaqueRecipientHpke.open(recipient, publicKey, enc, ByteArray(65537), aad, ct) }
        rejected { OpaqueRecipientHpke.open(recipient, publicKey, enc, info, aad, ByteArray(15)) }
        val otherPublic = hpke.serializePublicKey(hpke.generatePrivateKey().public)
        rejected { OpaqueRecipientHpke.open(recipient, otherPublic, enc, info, aad, ct) }
        assertEquals(before, calls)
        authorized = false
        rejected { OpaqueRecipientHpke.open(recipient, publicKey, enc, info, aad, ct) }
        assertEquals(before, calls)
        authorized = true
        rejected { OpaqueRecipientHpke.open(recipient, publicKey, enc, info, byteArrayOf(1), ct) }
        rejected { OpaqueRecipientHpke.open(recipient, publicKey, enc, info, aad,
            ct.clone().apply { this[0] = (this[0].toInt() xor 1).toByte() }) }
        assertArrayEquals(ByteArray(32), lastDh)
    }
}
