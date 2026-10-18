package com.peerchat.android.ui

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.runtime.mutableStateOf
import com.peerchat.android.application.AttachmentSnapshot
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

class CompactMessagePresentationTest {
    private fun message(status: MessageStatus, targets: DeliveryTargetsUi? = null) = MessageUi(
        "message", "a".repeat(64), "Me", "hello", true, "10:30", status=status,
        deliveryDetail="server accepted", deliveryTargets=targets)

    @Test fun compactMessageStatesDoNotExposePerDeviceDetailsOrTurnAcceptanceIntoDelivery() {
        val pending=DeliveryTargetsUi(false,listOf(DeliveryTargetUi("b".repeat(64),DeliveryTargetStatusUi.Pending)))
        val received=DeliveryTargetsUi(false,listOf(DeliveryTargetUi("b".repeat(64),DeliveryTargetStatusUi.Received)))
        assertEquals(" · Pending",compactMessageDelivery(message(MessageStatus.Sent,pending)))
        assertEquals(" · Received",compactMessageDelivery(message(MessageStatus.Delivered,received)))
        assertEquals(" · Some devices received · selection unresolved",compactMessageDelivery(message(MessageStatus.Delivered,
            DeliveryTargetsUi(true,listOf(DeliveryTargetUi("b".repeat(64),DeliveryTargetStatusUi.Received))))))
        assertEquals(" · Some devices received · selection unresolved",compactMessageDelivery(message(MessageStatus.Read,
            DeliveryTargetsUi(true,listOf(DeliveryTargetUi("b".repeat(64),DeliveryTargetStatusUi.Received))))))
        assertEquals(" · Pending · selection unresolved",compactMessageDelivery(message(MessageStatus.Delivered,
            DeliveryTargetsUi(true,listOf(DeliveryTargetUi("b".repeat(64),DeliveryTargetStatusUi.Pending))))))
        assertEquals(" · Status unavailable",compactMessageDelivery(message(MessageStatus.Delivered,
            DeliveryTargetsUi(false,emptyList()))))
        assertEquals(" · Failed",compactMessageDelivery(message(MessageStatus.Failed)))
        assertEquals(" · Sent",compactMessageDelivery(message(MessageStatus.Sent)))
        assertEquals(" · Device status unavailable",compactMessageDelivery(message(MessageStatus.Queued,
            DeliveryTargetsUi(true,emptyList()))))
    }

    @Test fun attachmentCompactPresentationKeepsCancelledAndFailedDistinctFromTransfers() {
        fun row(state:Int,done:Long=0)=AttachmentSnapshot("c".repeat(32),"b".repeat(64),"file.bin",100,state,true,done,1,"","")
        assertEquals("100 B · 50% · Transferring",attachmentCompactSummary(row(3,50)))
        assertEquals("100 B · Complete",attachmentCompactSummary(row(4,100)))
        assertEquals("100 B · Cancelled",attachmentCompactSummary(row(5,20)))
        assertEquals("100 B · Failed",attachmentCompactSummary(row(6,20)))
        assertEquals("Cancelled",attachmentCompactStatus(row(5)))
        assertEquals(true,attachmentShowsCompactProgress(row(3,50)))
        assertEquals(false,attachmentShowsCompactProgress(row(5,20)))
        val cancelled=row(5,20).copy(cachePhase="Uploading encrypted file")
        assertEquals(false,attachmentHasCurrentTransferPhase(cancelled))
        assertEquals("Last recorded phase (inactive): Uploading encrypted file",attachmentTransferPhaseText(cancelled))
    }
}

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class CompactBubbleInteractionTest {
    @get:Rule val compose=createComposeRule()

    @Test fun messageDetailsAreHiddenUntilTheWholeBubbleIsExpanded() {
        val device="b".repeat(64)
        val message=MessageUi("message","a".repeat(64),"Me","hello",true,"10:30",status=MessageStatus.Sent,
            deliveryDetail="Server accepted",deliveryTargets=DeliveryTargetsUi(false,
                listOf(DeliveryTargetUi(device,DeliveryTargetStatusUi.Pending))))
        val conversation=ConversationUi("account:2","Peer",ChatKind.Direct,false,"","",messages=listOf(message),hasOlder=false)
        compose.setContent{PeerChatTheme(true){ConversationScreen(conversation,
            PeerChatUiState(demo=false,route=Route.Conversation,selectedId=conversation.id,conversations=listOf(conversation)),{})}}
        compose.onNodeWithText("10:30 · Pending").assertExists()
        compose.onNodeWithText("Server accepted").assertDoesNotExist()
        compose.onNodeWithText("${device}").assertDoesNotExist()
        compose.onNodeWithTag("message-bubble-message").performClick()
        compose.onNodeWithText("Server accepted").assertExists()
        compose.onNodeWithText("Pending\n$device").assertExists()
        compose.onNodeWithTag("message-bubble-message").performClick()
        compose.onNodeWithText("Server accepted").assertDoesNotExist()
    }

    @Test fun cancelledAttachmentRemainsCompactAndDoesNotShowTransferProgress() {
        val row=AttachmentSnapshot("c".repeat(32),"b".repeat(64),"cancelled.bin",100,5,true,25,1,"","")
        compose.setContent{PeerChatTheme(true){AttachmentCard(row,{})}}
        compose.onNodeWithText("cancelled.bin").assertExists()
        compose.onNodeWithText("100 B · Cancelled").assertExists()
        compose.onNodeWithText("Tap for details").assertExists()
        compose.onNodeWithText("25 / 100 bytes · 25.00%",substring=true).assertDoesNotExist()
        compose.onNodeWithTag("attachment-${row.id}").performClick()
        compose.onNodeWithText("25 / 100 bytes · 25.00%",substring=true).assertExists()
    }

    @Test fun cancelledAndFailedAttachmentsKeepTheirTransferPhaseClearlyHistorical() {
        val cancelled=AttachmentSnapshot("d".repeat(32),"b".repeat(64),"cancelled.bin",100,5,true,25,1,"","",
            cachePhase="Uploading encrypted file")
        val attachment=mutableStateOf(cancelled)
        compose.setContent{PeerChatTheme(true){AttachmentCard(attachment.value,{})}}
        compose.onNodeWithTag("attachment-${cancelled.id}").performClick()
        compose.onNodeWithText("Transfer state: Cancelled").assertExists()
        compose.onNodeWithText("Last recorded phase (inactive): Uploading encrypted file").assertExists()
        compose.onNodeWithText("Uploading encrypted file").assertDoesNotExist()

        val failed=cancelled.copy(id="e".repeat(32),state=6,error="Transfer failed",cachePhase="Uploading encrypted file")
        compose.runOnIdle{attachment.value=failed}
        compose.onNodeWithText("100 B · Failed").assertExists()
        compose.onNodeWithText("Transfer failed").assertDoesNotExist()
        compose.onNodeWithTag("attachment-${failed.id}").performClick()
        compose.onNodeWithText("Transfer failed").assertExists()
        compose.onNodeWithText("Last recorded phase (inactive): Uploading encrypted file").assertExists()
    }
}
