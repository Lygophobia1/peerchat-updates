package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class AttachmentEarlyPhaseTest {
    private fun frame(phase:String):ByteArray {
        val id="a".repeat(32)
        return NativeRecordWriter(30).apply {
            number(1);text(id);text("b".repeat(64));text("photo.png");number(100);number(3);number(0);number(0);number(1);text("");text("")
            text("PeerChat/TransferStatisticsSnapshot/v1");number(1);text(id);repeat(5){number(0)}
            text("PeerChat/DMCachePhases/v1");number(1);text(id);text(phase)
        }.bytes()
    }
    @Test fun earlyOffersAndAcceptanceRemainVisibleWithoutClaimingCompletion(){
        for(phase in listOf("Checking cached upload authorization","Preparing encrypted file","Offer delivered; uploading encrypted file","Accepted; sender is preparing the cached file")){
            val row=attachmentSnapshots(frame(phase)).single()
            assertEquals(phase,row.cachePhase);assertFalse(row.complete);assertEquals(0L,row.done)
        }
    }
    @Test fun pendingUploadDoesNotClaimByteProgress(){
        val row=attachmentSnapshots(frame("Preparing encrypted file")).single().copy(outgoing=true,state=0)
        assertTrue(row.awaitingUploadStart)
        assertFalse(row.statisticsText.contains("0.00%"))
        assertEquals("Preparing upload",com.peerchat.android.ui.attachmentCompactStatus(row))
        assertFalse(com.peerchat.android.ui.attachmentShowsCompactProgress(row))
        val started=row.copy(state=3,done=10,cachePhase="Uploading encrypted file")
        assertFalse(started.awaitingUploadStart)
        assertTrue(started.statisticsText.contains("10.00%"))
        assertFalse(row.copy(outgoing=false).awaitingUploadStart)
        assertFalse(row.copy(state=5).awaitingUploadStart)
    }
    @Test(expected=IllegalArgumentException::class) fun unknownPhaseStillFailsClosed(){attachmentSnapshots(frame("unrecognized"))}
}
