package com.peerchat.android.ui

import com.peerchat.android.application.*
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

class DeliveryTargetsPresentationTest {
    private val local="a".repeat(64)
    private val remote="b".repeat(64)
    private fun project(targets:DeliveryTargetsSnapshot?=null,outgoing:Boolean=true,delivery:Long=2):MessageUi {
        val message=MessageSnapshot("c".repeat(32),if(outgoing)local else remote,if(outgoing)remote else local,
            100,5,7,delivery,"fixture",targets)
        val snapshot=MessagingSnapshot(localPeerId=local,conversations=listOf(
            ConversationSnapshot(FriendIdentity(2,"Contact",remote,false),ConnectionSnapshot(),listOf(message))))
        return projectMessaging(snapshot,PeerChatUiState()).conversations.single().messages.single()
    }
    @Test fun legacyDeliveryDoesNotInventADeviceManifest() {
        val message=project()
        assertEquals(MessageStatus.Delivered,message.status)
        assertNull(message.deliveryTargets)
    }
    @Test fun partialReceiptCountsDoNotUpgradeOtherTargetsOrChangeAggregateStatus() {
        val manifest=DeliveryTargetsSnapshot(false,listOf(
            DeliveryTargetSnapshot(remote,DeliveryTargetStatus.Acknowledged),
            DeliveryTargetSnapshot("d".repeat(64),DeliveryTargetStatus.Pending),
            DeliveryTargetSnapshot("e".repeat(64),DeliveryTargetStatus.Unavailable)))
        val message=project(manifest)
        assertEquals(MessageStatus.Delivered,message.status)
        assertEquals("Devices: 1 received · 1 pending · 1 unavailable",message.deliveryTargets!!.summary)
        assertEquals(manifest.targets.map{it.deviceId},message.deliveryTargets!!.targets.map{it.deviceId})
        assertEquals(100L,message.timestamp);assertEquals(5L,message.logicalOrder)
        assertEquals("fixture",message.text)
        assertEquals(MessageStatus.Stored,project(manifest,delivery=3).status)
    }
    @Test fun fullReceiptAndUnresolvedSelectionRemainDistinct() {
        val full=project(DeliveryTargetsSnapshot(false,listOf(DeliveryTargetSnapshot(remote,DeliveryTargetStatus.Acknowledged))))
        assertEquals("Devices: 1 received · 0 pending · 0 unavailable",full.deliveryTargets!!.summary)
        assertFalse(full.deliveryTargets!!.selectionUnresolved)
        val unresolved=project(DeliveryTargetsSnapshot(true,emptyList()),delivery=0)
        assertEquals(MessageStatus.Queued,unresolved.status)
        assertTrue(unresolved.deliveryTargets!!.selectionUnresolved)
        assertTrue(unresolved.deliveryTargets!!.targets.isEmpty())
    }
    @Test fun inboundMessagesDoNotDisplayOutgoingDeliveryTargets() {
        val inbound=project(DeliveryTargetsSnapshot(false,listOf(DeliveryTargetSnapshot(remote,DeliveryTargetStatus.Acknowledged))),outgoing=false)
        assertEquals(MessageStatus.Received,inbound.status)
        assertNull(inbound.deliveryTargets)
    }
}

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34],qualifiers="w600dp-h1200dp-port")
class DeliveryTargetsInteractionTest {
    @get:Rule val compose=createComposeRule()
    @Test fun partialReceiptReplacesLegacyAggregateTextButPreservesLocalFailures() {
        val message=MessageUi("message","a".repeat(64),"Local","Fixture",true,"00:00",status=MessageStatus.Stored,
            deliveryTargets=DeliveryTargetsUi(false,listOf(
                DeliveryTargetUi("b".repeat(64),DeliveryTargetStatusUi.Received),
                DeliveryTargetUi("c".repeat(64),DeliveryTargetStatusUi.Pending))))
        val conversation=mutableStateOf(ConversationUi("account:2","Peer",ChatKind.Direct,false,"","",messages=listOf(message),hasOlder=false))
        compose.setContent{PeerChatTheme(true){ConversationScreen(conversation.value,PeerChatUiState(demo=false),{})}}
        compose.onNodeWithText("Devices: 1 received · 1 pending · 0 unavailable").assertExists()
        compose.onNodeWithText("Stored · awaiting recipient",substring=true).assertDoesNotExist()
        compose.runOnIdle{conversation.value=conversation.value.copy(messages=listOf(message.copy(status=MessageStatus.Delivered)))}
        compose.onNodeWithText("Delivered ✓",substring=true).assertDoesNotExist()
        compose.runOnIdle{conversation.value=conversation.value.copy(messages=listOf(message.copy(status=MessageStatus.Saving)))}
        compose.onNodeWithText("Saving locally…",substring=true).assertExists()
        compose.runOnIdle{conversation.value=conversation.value.copy(messages=listOf(message.copy(status=MessageStatus.Failed)))}
        compose.onNodeWithText("00:00 · Failed").assertExists()
        compose.runOnIdle{conversation.value=conversation.value.copy(messages=listOf(message.copy(deliveryTargets=null)))}
        compose.onNodeWithText("00:00 · Stored · awaiting recipient").assertExists()
        compose.onNodeWithText("Devices: 1 received · 1 pending · 0 unavailable").assertDoesNotExist()
    }
    @Test fun expandsExactDeviceIdsAndUpdatesReceiptsWithoutHidingDetails() {
        val device="1234567890abcdef".repeat(4)
        val message=MessageUi("message","a".repeat(64),"Local","Fixture",true,"00:00",status=MessageStatus.Delivered,
            deliveryTargets=DeliveryTargetsUi(true,listOf(DeliveryTargetUi(device,DeliveryTargetStatusUi.Pending))))
        val conversation=mutableStateOf(ConversationUi("account:2","Peer",ChatKind.Direct,false,"","",messages=listOf(message),hasOlder=false))
        compose.setContent{PeerChatTheme(true){ConversationScreen(conversation.value,PeerChatUiState(demo=false),{})}}
        compose.onNodeWithText("Devices: 0 received · 1 pending · 0 unavailable").assertExists()
        compose.onNodeWithText("Device selection unresolved").assertExists()
        compose.onNodeWithText("Pending\n$device").assertDoesNotExist()
        compose.onNodeWithText("Show device details").performClick()
        compose.onNodeWithText("Pending\n$device").assertExists()
        compose.runOnIdle{conversation.value=conversation.value.copy(messages=listOf(message.copy(
            deliveryTargets=DeliveryTargetsUi(false,listOf(DeliveryTargetUi(device,DeliveryTargetStatusUi.Received))))))}
        compose.onNodeWithText("Received\n$device").assertExists()
        compose.onNodeWithText("Device selection unresolved").assertDoesNotExist()
        compose.onNodeWithText("Devices: 1 received · 0 pending · 0 unavailable").assertExists()
        compose.onNodeWithText("Hide device details").performClick()
        compose.onNodeWithText("Received\n$device").assertDoesNotExist()
    }
}
