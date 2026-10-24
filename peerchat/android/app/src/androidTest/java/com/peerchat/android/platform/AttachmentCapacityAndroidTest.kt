package com.peerchat.android.platform

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.core.content.FileProvider
import android.content.Context
import java.io.File
import java.io.RandomAccessFile
import java.util.UUID
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AttachmentCapacityAndroidTest {
    @Test fun declaredLimitReservationsCancellationAndCopy() {
        val context=ApplicationProvider.getApplicationContext<Context>()
        val suffix=UUID.randomUUID().toString()
        val sources=File(context.cacheDir,"diagnostics/capacity-$suffix").also{check(it.mkdirs())}
        val target=File(context.cacheDir,"capacity-$suffix").also{check(it.mkdirs())}
        fun uri(file:File)=FileProvider.getUriForFile(context,"${context.packageName}.diagnostics",file)
        fun retained()=target.listFiles()!!.filter{it.name!=".capacity.lock"}
        try {
            val large=File(sources,"large.bin")
            RandomAccessFile(large,"rw").use{it.setLength(MAX_CACHED_FILE_BYTES+1)}
            val error=assertThrows(IllegalArgumentException::class.java){importAttachment(context,uri(large),target,MAX_CACHED_FILE_BYTES)}
            assertTrue(error.message!!.contains("Direct-only"));assertTrue(retained().isEmpty())
            RandomAccessFile(large,"rw").use{it.setLength(MAX_DIRECT_FILE_BYTES+1)}
            assertThrows(IllegalArgumentException::class.java){importAttachment(context,uri(large),target)}
            assertTrue(retained().isEmpty())
            val source=File(sources,"small.bin").also{it.writeBytes(ByteArray(100003){n->(n*31).toByte()})}
            val reservation=File(target,"1".repeat(32)+".part")
            RandomAccessFile(reservation,"rw").use{it.setLength(10L*1024*1024*1024)}
            assertThrows(IllegalArgumentException::class.java){importAttachment(context,uri(source),target)}
            assertEquals(1,retained().size);assertEquals(10L*1024*1024*1024,reservation.length())
            check(reservation.delete())
            var checks=0
            assertThrows(java.util.concurrent.CancellationException::class.java){importAttachment(context,uri(source),target){if(++checks==3)throw java.util.concurrent.CancellationException()}}
            assertTrue(retained().isEmpty())
            val imported=importAttachment(context,uri(source),target,MAX_CACHED_FILE_BYTES)
            assertEquals("small.bin",imported.second);assertArrayEquals(source.readBytes(),File(target,imported.first).readBytes())
            assertEquals(1,retained().size);assertFalse(retained().single().name.endsWith(".part"))
        } finally {
            // Only the two unique directories created by this test are removed.
            sources.deleteRecursively();target.deleteRecursively()
        }
    }
}
