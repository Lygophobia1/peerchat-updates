package com.peerchat.android.platform

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyInfo
import android.security.keystore.KeyProperties
import android.os.Build
import android.util.AtomicFile
import java.io.File
import java.io.RandomAccessFile
import java.math.BigInteger
import java.nio.ByteBuffer
import java.security.*
import java.security.interfaces.ECPublicKey
import java.security.spec.*
import javax.crypto.*
import javax.crypto.spec.GCMParameterSpec

// Platform operations only. No messaging, account, session or transport ownership.
interface IdentityProvider {
    fun publicRepresentation(): ByteArray
    fun sign(data: ByteArray): ByteArray
}
interface RecordProtector {
    fun protect(plain: ByteArray, context: ByteArray): ByteArray
    fun unprotect(blob: ByteArray, context: ByteArray): ByteArray
}
class ProviderFailure(val code: String) : RuntimeException(code)
internal fun invalid(condition: Boolean) { if (!condition) throw ProviderFailure("INVALID_ARGUMENT") }
internal inline fun <T> protectedOperation(block: () -> T): T = try { block() }
    catch (failure: ProviderFailure) { throw failure }
    catch (_: Exception) { throw ProviderFailure("CRYPTO_PROVIDER_FAILURE") }

internal class ProfileSecurity(context: Context, profile: String) {
    val id: ByteArray
    val directory: File
    val identityAlias: String
    val storageAlias: String
    init {
        invalid(profile.matches(Regex("[A-Za-z0-9_-]{1,64}")))
        id = MessageDigest.getInstance("SHA-256").digest(profile.toByteArray(Charsets.UTF_8))
        val label = id.joinToString("") { "%02x".format(it) }
        directory = File(context.noBackupFilesDir, "security/$label")
        val namespace = if (profile.startsWith("test_")) "peerchat.test" else "peerchat"
        identityAlias = "$namespace.identity.v1.$label"
        storageAlias = "$namespace.records.v1.$label"
    }
    fun store(): KeyStore = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    fun <T> locked(block: () -> T): T = synchronized(creationLock) {
        protectedOperation {
            if (!directory.isDirectory && !directory.mkdirs()) throw ProviderFailure("PROFILE_IO_FAILURE")
            RandomAccessFile(File(directory, "provider.lock"), "rw").use { file ->
                file.channel.lock().use { block() }
            }
        }
    }
    fun read(name: String): ByteArray? {
        val file = AtomicFile(File(directory, name))
        return if (file.baseFile.exists() || File(directory, "$name.bak").exists()) file.readFully() else null
    }
    fun write(name: String, data: ByteArray) {
        val file = AtomicFile(File(directory, name)); val stream = file.startWrite()
        try { stream.write(data); file.finishWrite(stream) }
        catch (failure: Exception) { file.failWrite(stream); throw failure }
    }
    companion object { private val creationLock = Any() } // Lock only; no key/JNI state.
}

object PeerIdentityFormat {
    private val header = byteArrayOf(0x45, 0x43, 0x53, 0x31, 0x20, 0, 0, 0)
    private fun fixed(value: BigInteger): ByteArray {
        invalid(value.signum() >= 0 && value.bitLength() <= 256)
        val bytes = value.toByteArray().let { if (it.size == 33 && it[0] == 0.toByte()) it.copyOfRange(1, 33) else it }
        return ByteArray(32 - bytes.size) + bytes
    }
    fun publicBytes(key: ECPublicKey): ByteArray {
        invalid(key.params.curve.field.fieldSize == 256)
        return header + fixed(key.w.affineX) + fixed(key.w.affineY)
    }
    fun peerId(public: ByteArray): String {
        invalid(public.size == 72 && public.copyOfRange(0, 8).contentEquals(header))
        return MessageDigest.getInstance("SHA-256").digest(public).joinToString("") { "%02x".format(it) }
    }
    // Encoding normalization only, not a custom signing algorithm.
    fun rawSignature(der: ByteArray): ByteArray {
        invalid(der.size in 8..72 && der[0] == 0x30.toByte() && (der[1].toInt() and 255) == der.size - 2)
        var position = 2
        fun scalar(): ByteArray {
            invalid(position + 2 <= der.size && der[position++] == 2.toByte())
            val size = der[position++].toInt() and 255
            invalid(size in 1..33 && position + size <= der.size)
            val bytes = der.copyOfRange(position, position + size); position += size
            invalid(bytes[0].toInt() >= 0 && (size == 1 || bytes[0] != 0.toByte() || bytes[1].toInt() < 0))
            return fixed(BigInteger(1, bytes))
        }
        val output = scalar() + scalar(); invalid(position == der.size); return output
    }
    fun verify(public: ByteArray, data: ByteArray, raw: ByteArray): Boolean = protectedOperation {
        invalid(public.size == 72 && public.copyOfRange(0, 8).contentEquals(header) && raw.size == 64 && data.size <= 65536)
        val parameters = AlgorithmParameters.getInstance("EC").apply { init(ECGenParameterSpec("secp256r1")) }
            .getParameterSpec(ECParameterSpec::class.java)
        val key = KeyFactory.getInstance("EC").generatePublic(ECPublicKeySpec(
            ECPoint(BigInteger(1, public.copyOfRange(8, 40)), BigInteger(1, public.copyOfRange(40, 72))), parameters))
        fun integer(bytes: ByteArray): ByteArray {
            val normalized = BigInteger(1, bytes).toByteArray()
            return byteArrayOf(2, normalized.size.toByte()) + normalized
        }
        val integers = integer(raw.copyOfRange(0, 32)) + integer(raw.copyOfRange(32, 64))
        Signature.getInstance("SHA256withECDSA").run {
            initVerify(key); update(data); verify(byteArrayOf(0x30, integers.size.toByte()) + integers)
        }
    }
}

class AndroidIdentityProvider(context: Context, profile: String) : IdentityProvider {
    private val state = ProfileSecurity(context.applicationContext, profile)
    private val public: ByteArray
    init {
        public = state.locked {
            val store = state.store(); val existing = state.read("identity.public")
            val hasKey = store.containsAlias(state.identityAlias)
            if (existing == null && !hasKey) {
                val generator = KeyPairGenerator.getInstance("EC", "AndroidKeyStore")
                generator.initialize(KeyGenParameterSpec.Builder(state.identityAlias, KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
                    .setAlgorithmParameterSpec(ECGenParameterSpec("secp256r1"))
                    .setDigests(KeyProperties.DIGEST_SHA256).build())
                val bytes = PeerIdentityFormat.publicBytes(generator.generateKeyPair().public as ECPublicKey)
                state.write("identity.public", bytes); bytes
            } else {
                if (existing == null || !hasKey) throw ProviderFailure("IDENTITY_RECOVERY_REQUIRED")
                val bytes = PeerIdentityFormat.publicBytes(store.getCertificate(state.identityAlias).publicKey as ECPublicKey)
                if (!MessageDigest.isEqual(existing, bytes)) throw ProviderFailure("IDENTITY_RECOVERY_REQUIRED")
                bytes
            }
        }
    }
    override fun publicRepresentation(): ByteArray = public.copyOf()
    override fun sign(data: ByteArray): ByteArray = protectedOperation {
        invalid(data.size <= 65536)
        val store = state.store()
        val key = store.getKey(state.identityAlias, null) as? PrivateKey ?: throw ProviderFailure("IDENTITY_RECOVERY_REQUIRED")
        val current = PeerIdentityFormat.publicBytes(store.getCertificate(state.identityAlias).publicKey as ECPublicKey)
        if (!MessageDigest.isEqual(current, public)) throw ProviderFailure("IDENTITY_RECOVERY_REQUIRED")
        val signer = Signature.getInstance("SHA256withECDSA"); signer.initSign(key); signer.update(data)
        PeerIdentityFormat.rawSignature(signer.sign())
    }
    fun securityLevel(): String = protectedOperation {
        val key = state.store().getKey(state.identityAlias, null) as PrivateKey
        val info = KeyFactory.getInstance("EC", "AndroidKeyStore").getKeySpec(key, KeyInfo::class.java)
        if (Build.VERSION.SDK_INT >= 31) when (info.securityLevel) {
            KeyProperties.SECURITY_LEVEL_TRUSTED_ENVIRONMENT -> "TEE"
            KeyProperties.SECURITY_LEVEL_STRONGBOX -> "StrongBox"
            KeyProperties.SECURITY_LEVEL_SOFTWARE -> "Software Keystore"
            else -> "Keystore (unknown security level)"
        } else if (info.isInsideSecureHardware) "Hardware-backed (type unavailable)" else "Software Keystore"
    }
}

class AndroidRecordProtector(context: Context, profile: String) : RecordProtector {
    private val state = ProfileSecurity(context.applicationContext, profile)
    private fun key(create: Boolean): SecretKey = state.locked {
        val store = state.store(); val marker = state.read("records.version")
        val exists = store.containsAlias(state.storageAlias)
        if (marker == null && !exists && create) {
            val generator = KeyGenerator.getInstance("AES", "AndroidKeyStore")
            generator.init(KeyGenParameterSpec.Builder(state.storageAlias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setKeySize(256).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).setRandomizedEncryptionRequired(true).build())
            val key = generator.generateKey(); state.write("records.version", byteArrayOf(1)); key
        } else {
            if (!exists || marker == null || !marker.contentEquals(byteArrayOf(1))) throw ProviderFailure("STORAGE_RECOVERY_REQUIRED")
            store.getKey(state.storageAlias, null) as? SecretKey ?: throw ProviderFailure("STORAGE_RECOVERY_REQUIRED")
        }
    }
    private fun aad(header: ByteArray, context: ByteArray): ByteArray = header + state.id +
        ByteBuffer.allocate(4).putInt(context.size).array() + context
    override fun protect(plain: ByteArray, context: ByteArray): ByteArray = protectedOperation {
        invalid(plain.size <= 65536 && context.size <= 4096)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding"); cipher.init(Cipher.ENCRYPT_MODE, key(true))
        invalid(cipher.iv.size == 12)
        val header = byteArrayOf(0x50, 0x43, 0x52, 0x31, 1, 1, 12, 16) + ByteBuffer.allocate(4).putInt(plain.size + 16).array()
        cipher.updateAAD(aad(header, context))
        header + cipher.iv + cipher.doFinal(plain)
    }
    override fun unprotect(blob: ByteArray, context: ByteArray): ByteArray = protectedOperation {
        invalid(blob.size in 40..65576 && context.size <= 4096)
        val header = blob.copyOfRange(0, 12)
        invalid(header.copyOfRange(0, 8).contentEquals(byteArrayOf(0x50, 0x43, 0x52, 0x31, 1, 1, 12, 16)))
        invalid(ByteBuffer.wrap(header, 8, 4).int == blob.size - 24)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key(false), GCMParameterSpec(128, blob.copyOfRange(12, 24)))
        cipher.updateAAD(aad(header, context)); cipher.doFinal(blob, 24, blob.size - 24)
    }
}
