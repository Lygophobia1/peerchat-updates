package com.peerchat.android.platform

import org.bouncycastle.crypto.hpke.HPKE
import org.bouncycastle.crypto.agreement.ECDHBasicAgreement
import org.bouncycastle.util.BigIntegers
import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.Callable
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class OpaqueMailboxHpkeTest {
    private fun standard() = HPKE(HPKE.mode_base, HPKE.kem_P256_SHA256, HPKE.kdf_HKDF_SHA256, HPKE.aead_AES_GCM128)
    @Test fun concurrentAdapterInteroperability() {
        // Host-only TEST keys. Production Recipient supplies Keystore operations,
        // never the private scalar used by this independent test oracle.
        val keys = List(2) { standard().generatePrivateKey() }
        val executor = Executors.newFixedThreadPool(8)
        try {
            val tasks = List(8) { worker -> Callable {
                val pair = keys[worker % 2]
                val publicKey = standard().serializePublicKey(pair.public)
                val recipient = object : OpaqueRecipientHpke.Recipient {
                    override fun publicKey() = publicKey.clone()
                    override fun agree(point: ByteArray): ByteArray {
                        val operation = ECDHBasicAgreement()
                        operation.init(pair.private)
                        return BigIntegers.asUnsignedByteArray(32, operation.calculateAgreement(standard().deserializePublicKey(point)))
                    }
                }
                repeat(125) {
                    val info = "PeerChat/adapter/host-test".toByteArray()
                    val aad = byteArrayOf(worker.toByte(), it.toByte())
                    val plaintext = "Synthetic mailbox fixture".toByteArray()
                    val wire = OpaqueRecipientHpke.seal(publicKey, info, aad, plaintext)
                    val enc = wire.copyOfRange(0,65); val ciphertext = wire.copyOfRange(65,wire.size)
                    assertArrayEquals(plaintext, standard().setupBaseR(enc,pair,info).open(aad,ciphertext))
                    assertArrayEquals(plaintext, OpaqueRecipientHpke.open(recipient,publicKey,enc,info,aad,ciphertext))
                    val changed = ciphertext.clone().apply { this[lastIndex] = (this[lastIndex].toInt() xor 1).toByte() }
                    assertThrows(Exception::class.java) { OpaqueRecipientHpke.open(recipient,publicKey,enc,info,aad,changed) }
                }
                true
            } }
            executor.invokeAll(tasks).forEach { assertTrue(it.get(30, TimeUnit.SECONDS)) }
        } finally { executor.shutdownNow(); assertTrue(executor.awaitTermination(10,TimeUnit.SECONDS)) }
    }
}
