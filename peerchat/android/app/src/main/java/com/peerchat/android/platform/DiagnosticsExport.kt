package com.peerchat.android.platform

import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import androidx.activity.result.contract.ActivityResultContracts
import java.io.File
import java.io.OutputStream
import java.io.OutputStreamWriter
import java.time.Instant

/** Captures only the existing immutable, safe diagnostic projection. Never reads
 * messages, logcat, private storage, keys or transport/session owners. */
internal class DiagnosticsSnapshot(val build:String,val capturedAt:Instant,private val source:String) {
    init {require(source.length<=MAX_CHARACTERS){"Diagnostic snapshot exceeds export budget"}}
    val suggestedName:String get()="PeerChat-diagnostics-${capturedAt.toString().replace(':','-')}.txt"
    fun write(output:OutputStream){
        // Fixed-size buffering; never build a second full report or put file
        // contents in a Binder Intent/clipboard. Keep the caller's stream open.
        val writer=OutputStreamWriter(output,Charsets.UTF_8).buffered(8192)
        writer.appendLine("PeerChat Android diagnostics")
        writer.appendLine("Build: ${safeDiagnosticLine(build)}")
        writer.appendLine("Snapshot UTC: $capturedAt")
        writer.appendLine("Source: immutable application diagnostic snapshot; private fields and paths redacted")
        var privateBlock=false
        source.lineSequence().forEach{line->
            if(line.contains("-----BEGIN ")&&line.contains("PRIVATE KEY-----")){privateBlock=true;writer.appendLine("[private key redacted]")}
            else if(privateBlock){if(line.contains("-----END ")&&line.contains("PRIVATE KEY-----"))privateBlock=false}
            else writer.appendLine(safeDiagnosticLine(line))
        }
        writer.flush()
    }
    companion object {const val MAX_CHARACTERS=32*1024*1024}
}

// Defense in depth on top of the allowlisted application projection. Retain
// message/transfer IDs, public device fingerprints, stages, timings and reasons.
private val diagnosticSecrets=Regex("(?i)([\"']?(?:password|passwd|session[_-]?token|auth[_-]?token|access[_-]?token|refresh[_-]?token|token|credential|private[_-]?key|secret|plaintext|message|text|body|message[_-]?(?:text|body|plaintext)|payload|authorization)[\"']?\\s*[:=]\\s*)(?:\"[^\"]*\"|'[^']*'|[^\\r\\n]*)")
private val diagnosticPaths=Regex("(?i)(?:[a-z]:[\\\\/][^\\r\\n]*|\\\\\\\\[^\\r\\n]*|(?:file|content)://[^\\s\"']+|(?<![\\w:])/(?:data|storage|sdcard|mnt|home|Users|tmp|var|private)(?:/[^\\r\\n]*)?)")
private val diagnosticPathFields=Regex("(?i)([\"']?(?:(?:source|destination|local|download|cache)[_-]?)?(?:file[_-]?path|path|directory|file[_-]?name)[\"']?\\s*[:=]\\s*)[^\\r\\n]*")
private val diagnosticDeliveryId=Regex("^delivery_targets message=([0-9a-f]{32})(?=\\s|$)")
internal fun safeDiagnosticLine(line:String):String {
    // This native diagnostic's historical `message` field is a validated ID,
    // never body text. Canonicalize only that exact stage/ID before redaction.
    val identified=diagnosticDeliveryId.replace(line){"delivery_targets message_id=${it.groupValues[1]}"}
    return diagnosticPaths.replace(diagnosticPathFields.replace(diagnosticSecrets.replace(identified){it.groupValues[1]+"[redacted]"}){it.groupValues[1]+"[path redacted]"},"[path redacted]")
}

internal class DiagnosticsCreateDocument:ActivityResultContracts.CreateDocument("text/plain") {
    override fun createIntent(context:Context,input:String):Intent=super.createIntent(context,input).addCategory(Intent.CATEGORY_OPENABLE)
}

internal object DiagnosticsExport {
    fun save(context:Context,uri:Uri,snapshot:DiagnosticsSnapshot){
        require(uri.scheme=="content"){"System document URI required"}
        checkNotNull(context.contentResolver.openOutputStream(uri,"wt")){"Document output unavailable"}.use(snapshot::write)
    }
    fun share(context:Context,snapshot:DiagnosticsSnapshot):Intent {
        val file=prepareShareFile(context,snapshot)
        try{
            val uri=FileProvider.getUriForFile(context,"${context.packageName}.diagnostics",file)
            return shareIntent(context,uri,snapshot)
        }catch(error:Exception){file.delete();throw error}
    }
    internal fun prepareShareFile(context:Context,snapshot:DiagnosticsSnapshot):File {
        val directory=File(context.cacheDir,"diagnostics").apply{check(isDirectory||mkdirs())}
        // Only this provider's generated cache files. At most two previous
        // reports survive; expired reports are removed on the next share. Never
        // touch user files or schedule background export/cleanup work.
        val generated=directory.listFiles().orEmpty().filter{it.isFile&&it.name.startsWith("PeerChat-diagnostics-")&&it.name.endsWith(".txt")}.sortedByDescending{it.lastModified()}
        generated.forEachIndexed{index,file->if(index>=2||System.currentTimeMillis()-file.lastModified()>24*60*60*1000L)file.delete()}
        val file=File.createTempFile("PeerChat-diagnostics-",".txt",directory)
        try{file.outputStream().use(snapshot::write)}catch(error:Exception){file.delete();throw error}
        return file
    }
    internal fun shareIntent(context:Context,uri:Uri,snapshot:DiagnosticsSnapshot):Intent {
        require(uri.scheme=="content"&&uri.authority=="${context.packageName}.diagnostics")
        return Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_STREAM,uri)
            .putExtra(Intent.EXTRA_TITLE,snapshot.suggestedName)
            .apply{clipData=ClipData.newRawUri("PeerChat diagnostics",uri)}
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
}
