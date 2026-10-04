package com.peerchat.android.platform

// Adapter for an already authorized KeyManager capability. Never creates keys
// or grants authorization; caller must hold the registry's current operation
// lease while using it, including revocation/version checks.
object AndroidRecipientHpke {
    fun open(provider: AndroidRecipientKeyProvider, alias: String, expectedPublicKey: ByteArray,
             enc: ByteArray, info: ByteArray, aad: ByteArray, ciphertext: ByteArray): ByteArray =
        OpaqueRecipientHpke.open(object : OpaqueRecipientHpke.Recipient {
            override fun publicKey() = provider.publicKey(alias)
            override fun agree(validatedPoint: ByteArray) = provider.agreeP256(alias, validatedPoint)
        }, expectedPublicKey, enc, info, aad, ciphertext)
}
