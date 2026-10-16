package com.peerchat.android.platform

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import java.io.ByteArrayOutputStream
import java.io.OutputStream
import java.time.Instant
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class DiagnosticsExportTest {
    private val at=Instant.parse("2026-10-16T12:34:56Z")
    private fun snapshot(source:String)=DiagnosticsSnapshot("4.0.2-dev.test",at,source)
    private fun text(value:DiagnosticsSnapshot)=ByteArrayOutputStream().also(value::write).toString("UTF-8")
    @Test fun completeUtf8SnapshotFarBeyondClipboardLimitUsesBoundedWrites(){
        var source=buildString{repeat(50_000){appendLine("stage=archive_control attempt=$it message_id=${it.toString().padStart(32,'0')} sender_device=${"a".repeat(64)} 日本語 👋")};append("last_complete_record")}
        assertTrue(source.length>1_048_576)
        val captured=snapshot(source);val original=source;source="different later snapshot"
        val bytes=ByteArrayOutputStream();var maximumWrite=0
        captured.write(object:OutputStream(){
            override fun write(value:Int){bytes.write(value)}
            override fun write(data:ByteArray,offset:Int,length:Int){maximumWrite=maxOf(maximumWrite,length);bytes.write(data,offset,length)}
        })
        val exported=bytes.toString("UTF-8")
        assertTrue(exported.contains("Build: 4.0.2-dev.test\nSnapshot UTC: $at"))
        assertTrue(exported.endsWith(original+"\n"));assertFalse(exported.contains(source))
        assertTrue("UTF-8 writer must not hand the whole report to an output",maximumWrite<=8192)
        assertTrue(bytes.size()>1_048_576)
    }
    @Test fun privateFieldsAndPathsAreRedactedButCorrelationRemains(){
        val exported=text(snapshot("""
            message_id=${"1".repeat(32)} transfer_id=${"2".repeat(32)} sender_device=${"a".repeat(64)} reason=retry_scheduled
            password=hunter-secret
            sessionToken="session-secret"
            private_key=key-secret
            message_text=PRIVATE_MESSAGE_CONTENT
            body='PRIVATE_BODY'
            path=C:\Users\Private\Documents\sensitive.txt
            path=/data/user/0/com.peerchat.android/files/private.db
            uri=content://private.documents/id/secret
            local_path=relative/private/history.db
            -----BEGIN PRIVATE KEY-----
            BASE64_PRIVATE_KEY_MATERIAL
            -----END PRIVATE KEY-----
            final_stage=complete
        """.trimIndent()))
        listOf("hunter-secret","session-secret","key-secret","PRIVATE_MESSAGE_CONTENT","PRIVATE_BODY","sensitive.txt","private.db","private.documents","history.db","BASE64_PRIVATE_KEY_MATERIAL").forEach{assertFalse(it,exported.contains(it))}
        assertTrue(exported.contains("message_id=${"1".repeat(32)}"));assertTrue(exported.contains("sender_device=${"a".repeat(64)}"))
        assertTrue(exported.contains("final_stage=complete"))
    }
    @Test fun systemSaveAndShareUseContentUrisAndOnlyReadGrants(){
        val context=ApplicationProvider.getApplicationContext<Context>()
        val captured=snapshot("first_complete_snapshot 日本語 👋")
        val picker=DiagnosticsCreateDocument().createIntent(context,captured.suggestedName)
        assertEquals(Intent.ACTION_CREATE_DOCUMENT,picker.action);assertEquals("text/plain",picker.type)
        assertTrue(picker.hasCategory(Intent.CATEGORY_OPENABLE))
        val file=DiagnosticsExport.prepareShareFile(context,captured)
        assertEquals(text(captured),file.readText(Charsets.UTF_8))
        val uri=Uri.parse("content://${context.packageName}.diagnostics/diagnostics/fixture.txt")
        val intent=DiagnosticsExport.shareIntent(context,uri,captured)
        assertEquals(Intent.ACTION_SEND,intent.action);assertEquals("text/plain",intent.type)
        assertEquals(uri,checkNotNull(intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM)))
        assertEquals("content",uri.scheme);assertEquals("${context.packageName}.diagnostics",uri.authority)
        assertEquals(uri,intent.clipData!!.getItemAt(0).uri)
        assertEquals(Intent.FLAG_GRANT_READ_URI_PERMISSION,intent.flags)
        assertFalse(intent.hasExtra(Intent.EXTRA_TEXT))
        val replacement=snapshot("complete_saved_document")
        val saved=ByteArrayOutputStream()
        org.robolectric.Shadows.shadowOf(context.contentResolver).registerOutputStream(uri,saved)
        DiagnosticsExport.save(context,uri,replacement)
        assertEquals(text(replacement),saved.toString("UTF-8"))
        assertThrows(IllegalArgumentException::class.java){DiagnosticsExport.save(context,Uri.parse("file:///data/private"),replacement)}
    }
    @Test fun shareProviderDeclaresOnlyPrivateDiagnosticCachePath(){
        val context=ApplicationProvider.getApplicationContext<Context>()
        val info=context.packageManager.resolveContentProvider("${context.packageName}.diagnostics",0)!!
        assertFalse(info.exported);assertTrue(info.grantUriPermissions)
        val tags=mutableListOf<String>()
        context.resources.getXml(com.peerchat.android.R.xml.diagnostics_paths).use{xml->while(xml.next()!=org.xmlpull.v1.XmlPullParser.END_DOCUMENT){if(xml.eventType==org.xmlpull.v1.XmlPullParser.START_TAG){tags.add(xml.name);if(xml.name=="cache-path")assertEquals("diagnostics/",xml.getAttributeValue(null,"path"))}}}
        assertEquals(listOf("paths","cache-path"),tags)
    }
}
