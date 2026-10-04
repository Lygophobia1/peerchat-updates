package com.peerchat.android.platform

import android.content.Context
import android.os.Build
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyInfo
import android.security.keystore.KeyProperties
import java.security.KeyFactory
import java.security.AlgorithmParameters
import java.security.KeyPairGenerator
import java.security.PrivateKey
import java.security.interfaces.ECPublicKey
import java.security.spec.ECGenParameterSpec
import java.security.spec.ECParameterSpec
import javax.crypto.KeyAgreement

// Agreement-only platform adapter. Native KeyManager/registry owns lifecycle;
// this provider never creates on open, registers with the server, or owns messages.
class AndroidRecipientKeyProvider(context: Context, profile: String) {
    private val state = ProfileSecurity(context.applicationContext, profile)
    private val prefix = (if (profile.startsWith("test_")) "peerchat.test" else "peerchat") +
        ".recipient." + state.id.joinToString("") { "%02x".format(it) } + ".v"

    fun supported(): Boolean = Build.VERSION.SDK_INT >= 31 && try {
        KeyAgreement.getInstance("ECDH", "AndroidKeyStore"); true
    } catch (_: Exception) { false }
    fun alias(generation: Int): String {
        invalid(generation > 0)
        return prefix + generation
    }
    private fun checkAlias(alias: String) {
        invalid(alias.startsWith(prefix) && alias.removePrefix(prefix).toIntOrNull()?.let { it > 0 && alias == this.alias(it) } == true)
        if (!supported()) throw ProviderFailure("RECIPIENT_KEY_UNSUPPORTED")
    }
    fun exists(alias: String): Boolean = protectedOperation {
        checkAlias(alias); state.store().containsAlias(alias)
    }
    fun create(alias: String): ByteArray = state.locked {
        checkAlias(alias)
        if (state.store().containsAlias(alias)) throw ProviderFailure("RECIPIENT_KEY_ALREADY_EXISTS")
        val generator = KeyPairGenerator.getInstance("EC", "AndroidKeyStore")
        generator.initialize(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_AGREE_KEY)
            .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
            .setUserAuthenticationRequired(false).build())
        generator.generateKeyPair()
        publicKey(alias)
    }
    private fun privateHandle(alias: String): PrivateKey {
        checkAlias(alias)
        val key = state.store().getKey(alias, null) as? PrivateKey
            ?: throw ProviderFailure("RECIPIENT_KEY_RECOVERY_REQUIRED")
        val info = KeyFactory.getInstance("EC", "AndroidKeyStore").getKeySpec(key, KeyInfo::class.java)
        if (key.encoded != null || info.purposes != KeyProperties.PURPOSE_AGREE_KEY || info.isUserAuthenticationRequired)
            throw ProviderFailure("RECIPIENT_KEY_POLICY_MISMATCH")
        return key
    }
    fun publicKey(alias: String): ByteArray = protectedOperation {
        privateHandle(alias)
        val key = state.store().getCertificate(alias)?.publicKey as? ECPublicKey
            ?: throw ProviderFailure("RECIPIENT_KEY_RECOVERY_REQUIRED")
        val expected = AlgorithmParameters.getInstance("EC").apply { init(ECGenParameterSpec("secp256r1")) }
            .getParameterSpec(ECParameterSpec::class.java)
        if (key.params.curve != expected.curve || key.params.generator != expected.generator ||
            key.params.order != expected.order || key.params.cofactor != expected.cofactor)
            throw ProviderFailure("RECIPIENT_KEY_POLICY_MISMATCH")
        val p256 = PeerIdentityFormat.publicBytes(key)
        RecipientPublicKey.validate(byteArrayOf(4) + p256.copyOfRange(8, 72))
    }
    fun agreeP256(alias: String, publicPoint: ByteArray): ByteArray = protectedOperation {
        val remote = RecipientPublicKey.jca(publicPoint)
        val agreement = KeyAgreement.getInstance("ECDH", "AndroidKeyStore")
        agreement.init(privateHandle(alias)); agreement.doPhase(remote, true)
        val dh = agreement.generateSecret()
        if (dh.size != 32) { dh.fill(0); throw ProviderFailure("RECIPIENT_AGREEMENT_FAILED") }
        dh // Transient shared secret only. The KEM adapter must clear after use.
    }
}
