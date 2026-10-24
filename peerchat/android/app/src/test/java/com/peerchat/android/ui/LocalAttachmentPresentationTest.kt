package com.peerchat.android.ui

import android.net.Uri
import com.peerchat.android.platform.AttachmentSendMode
import com.peerchat.android.application.AttachmentSnapshot
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class LocalAttachmentPresentationTest {
    private val chat=ConversationUi("account:8","contact",ChatKind.Direct,false,"","")
    private val uri=Uri.parse("content://picker/private-photo")
    private val pending=LocalAttachmentPresentation("local-attachment-test",chat.id,uri,2,emptySet(),name="photo.jpg",size=123)
    private val native=AttachmentSnapshot("c".repeat(32),"b".repeat(64),"photo.jpg",123,0,true,0,2,"","")

    @Test fun pendingRowAppearsImmediatelyWithoutNativeDeliveryClaim(){
        val row=presentLocalAttachments(listOf(chat),listOf(pending)).single().attachments.single()
        assertEquals(pending.token,row.id)
        assertEquals(0,row.state)
        assertEquals(0,row.done)
        assertEquals("",row.localPath)
        assertEquals("Preparing encrypted file",row.cachePhase)
    }

    @Test fun nativeSnapshotRaceAndExactIdReconcileWithoutDuplicates(){
        val raced=chat.copy(attachments=listOf(native))
        val saving=presentLocalAttachments(listOf(raced),listOf(pending)).single().attachments
        // Until the command returns an exact ID, a second outgoing native row
        // cannot safely be assumed to belong to this pending send.
        assertEquals(setOf(pending.token,native.id),saving.map{it.id}.toSet())
        val bound=presentLocalAttachments(listOf(raced),listOf(pending.copy(nativeId=native.id))).single().attachments
        assertEquals(listOf(native.id),bound.map{it.id})
        assertEquals(native, bound.single())
    }

    @Test fun failureAndCancellationRemainHonestLocalRows(){
        val failed=presentLocalAttachments(listOf(chat),listOf(pending.copy(phase=LocalAttachmentPhase.Failed))).single().attachments.single()
        val cancelled=presentLocalAttachments(listOf(chat),listOf(pending.copy(phase=LocalAttachmentPhase.Cancelled))).single().attachments.single()
        assertEquals(6,failed.state)
        assertEquals(5,cancelled.state)
        assertEquals(0,failed.done)
        assertEquals(0,cancelled.done)
    }

    @Test fun preparationFailureRetainsSafeReasonInsideTheSameBubble(){
        val text=attachmentSendFailureText(AttachmentSendMode.Cached,java.io.EOFException("private/picture.jpg"))
        val failed=presentLocalAttachments(listOf(chat),listOf(pending.copy(phase=LocalAttachmentPhase.Failed,failureText=text))).single().attachments.single()
        assertEquals(pending.token,failed.id)
        assertEquals(text,failed.error)
        assertTrue(text.contains("before the file was queued"))
        assertFalse(text.contains("private"))
        assertEquals("connection_closed",attachmentPreparationFailureReason(java.io.EOFException("secret")))
        assertEquals("tls_failure",attachmentPreparationFailureReason(javax.net.ssl.SSLException("secret")))
    }

    @Test fun groupAndDirectRowsStayInTheirConversation(){
        val group=chat.copy(id="group:room",kind=ChatKind.Group)
        val rows=presentLocalAttachments(listOf(chat,group),listOf(pending.copy(conversation=group.id)))
        assertTrue(rows.first().attachments.isEmpty())
        assertEquals(pending.token,rows.last().attachments.single().id)
    }
}
