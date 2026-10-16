package com.peerchat.android.platform

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.core.content.FileProvider
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.ByteArrayOutputStream
import java.io.File
import java.time.Instant
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Actual Android provider/resolver coverage; the Windows host has different
 * java.io.File separators from Android's FileProvider path strategy. */
@RunWith(AndroidJUnit4::class)
class DiagnosticsExportAndroidTest {
    @Test fun fullReportSaveAndShareUseProtectedProviderAndReadOnlyUri(){
        val context=ApplicationProvider.getApplicationContext<Context>()
        val source="stage=archive_control message_id=${"1".repeat(32)} 日本語 👋\n".repeat(25_000)+"last_complete_diagnostic_record"
        assertTrue(source.length>1_048_576)
        val snapshot=DiagnosticsSnapshot("android-provider-fixture",Instant.EPOCH,source)
        val intent=DiagnosticsExport.share(context,snapshot)
        assertEquals(Intent.ACTION_SEND,intent.action);assertEquals("text/plain",intent.type)
        val uri=checkNotNull(intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM))
        assertEquals("content",uri.scheme);assertEquals("${context.packageName}.diagnostics",uri.authority)
        assertEquals(Intent.FLAG_GRANT_READ_URI_PERMISSION,intent.flags)
        assertEquals(uri,intent.clipData!!.getItemAt(0).uri);assertFalse(intent.hasExtra(Intent.EXTRA_TEXT))
        val expected=ByteArrayOutputStream().also(snapshot::write).toString("UTF-8")
        assertEquals(expected,context.contentResolver.openInputStream(uri)!!.bufferedReader().use{it.readText()})
        val saved=DiagnosticsSnapshot("save-fixture",Instant.EPOCH,"replacement_complete_report 日本語 👋")
        DiagnosticsExport.save(context,uri,saved)
        assertTrue(context.contentResolver.openInputStream(uri)!!.bufferedReader().use{it.readText()}.endsWith("replacement_complete_report 日本語 👋\n"))
        val outside=File(context.cacheDir,"not-diagnostics.txt")
        assertThrows(IllegalArgumentException::class.java){FileProvider.getUriForFile(context,"${context.packageName}.diagnostics",outside)}
        val info=context.packageManager.resolveContentProvider("${context.packageName}.diagnostics",0)!!
        assertFalse(info.exported);assertTrue(info.grantUriPermissions)
    }
}
