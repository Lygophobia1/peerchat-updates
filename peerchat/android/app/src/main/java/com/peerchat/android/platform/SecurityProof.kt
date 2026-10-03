package com.peerchat.android.platform

import android.content.Context
import android.util.AtomicFile
import com.peerchat.android.NativeBridge
import java.io.File

// Minimal application-adapter proof. No real history/profile database is opened.
object SecurityProof {
    fun run(context: Context): String {
        val identity = AndroidIdentityProvider(context, "default")
        val protector = AndroidRecordProtector(context, "default")
        val directory = File(context.noBackupFilesDir, "provider-proof").apply { check(isDirectory || mkdirs()) }
        val public = identity.publicRepresentation()
        val synthetic = "PeerChat deterministic restart proof / 日本語\u0000😀".toByteArray()
        val binding = "restart-proof/v1".toByteArray()
        fun keep(name: String, first: () -> ByteArray, validate: (ByteArray) -> Boolean) {
            val record = AtomicFile(File(directory, name))
            if (record.baseFile.exists() || File(directory, "$name.bak").exists()) {
                check(validate(record.readFully())) { "Persistence recovery required" }
            } else {
                val stream = record.startWrite()
                try { stream.write(first()); record.finishWrite(stream) }
                catch (failure: Exception) { record.failWrite(stream); throw failure }
            }
        }
        keep("expected.public", { public }, { it.contentEquals(public) })
        keep("restart.record", { protector.protect(synthetic, binding) }, { protector.unprotect(it, binding).contentEquals(synthetic) })
        val signature = identity.sign(synthetic)
        check(PeerIdentityFormat.verify(public, synthetic, signature))
        check(NativeBridge.normalizeIdentity(public, signature)?.contentEquals(public + signature) == true)
        return "Identity: ${PeerIdentityFormat.peerId(public)}\nKey protection: ${identity.securityLevel()}\nIdentity/record restart proof: Passed"
    }
}
