package com.peerchat.android.platform.tls

import com.peerchat.android.platform.IdentityProvider

/** Operations only: the existing profile provider owns the Keystore identity.
 * No key creation, key replacement, private-key handle or bytes cross this API.
 */
class AndroidTlsIdentity(private val identity: IdentityProvider) : OpaqueTlsSigner.Identity {
    override fun publicKey(): ByteArray = identity.publicRepresentation()
    override fun sign(original: ByteArray): ByteArray = identity.sign(original)
}
