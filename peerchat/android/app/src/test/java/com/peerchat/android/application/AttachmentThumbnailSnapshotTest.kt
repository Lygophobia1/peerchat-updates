package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class AttachmentThumbnailSnapshotTest {
    private val id="a".repeat(32)
    private fun snapshot(bytes:ByteArray,duplicate:Boolean=false):ByteArray=NativeRecordWriter(30).also{w->
        w.number(1);w.text(id);w.text("b".repeat(64));w.text("photo.jpg");w.number(5000000);w.number(2);w.number(0);w.number(0);w.number(1);w.text("");w.text("")
        w.text("PeerChat/TransferStatisticsSnapshot/v1");w.number(1);w.text(id);repeat(5){w.number(0)}
        w.text("PeerChat/DMCachePhases/v1");w.number(0)
        w.text("PeerChat/AttachmentThumbnails/v1");w.number(if(duplicate)2 else 1);w.text(id);w.blob(bytes)
        if(duplicate){w.text(id);w.blob(bytes)}
    }.bytes()
    @Test fun earlyPreviewKeepsOriginalAwaitingAcceptance(){
        val hint=byteArrayOf(1,2,3)
        val row=attachmentSnapshots(snapshot(hint)).single()
        assertEquals(java.util.Base64.getEncoder().encodeToString(hint),row.thumbnail);assertEquals(row,attachmentSnapshots(snapshot(hint)).single());assertTrue(row.awaitingAcceptance)
        assertFalse(row.complete);assertEquals(0L,row.done);assertEquals("",row.localPath)
    }
    @Test fun rejectsUnboundedAndDuplicateMetadata(){
        for(bytes in listOf(snapshot(ByteArray(2049)),snapshot(byteArrayOf(1),true))){
            try{attachmentSnapshots(bytes);fail("Unbounded thumbnail metadata accepted")}catch(_:IllegalArgumentException){}
        }
    }
}
