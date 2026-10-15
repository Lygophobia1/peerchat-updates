package com.peerchat.android.ui

import com.peerchat.android.application.*
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[28])
class MessagingRoutingDiagnosticsTest {
    @Test fun accountSelectionAndActualDeviceAttributionStaySeparateWithoutPlaintext(){
        val local="a".repeat(64);val phone="b".repeat(64);val desktop="c".repeat(64)
        val rows=(1L..4L).map{n->MessageSnapshot(n.toString().padStart(32,'0'),local,desktop,n,n,n,0,"PRIVATE_MESSAGE_CONTENT")}
        val friend=FriendIdentity(4,"Phone account",phone,true,listOf(phone,desktop))
        val snapshot=MessagingSnapshot(accountId=5,localPeerId=local,authenticated=true,serverPresenceLive=true,
            conversations=listOf(ConversationSnapshot(friend,ConnectionSnapshot(),rows)),
            messagingDiagnostics="stage=native_recipient_registration")
        val result=projectMessaging(snapshot,PeerChatUiState())
        assertTrue(result.diagnostics.contains("Resolved account=4 current_device=$phone"))
        assertTrue(result.diagnostics.contains("recipient_device=$desktop"))
        assertTrue(result.diagnostics.contains("stage=native_recipient_registration"))
        assertFalse(result.diagnostics.contains("PRIVATE_MESSAGE_CONTENT"))
        assertEquals(3,Regex("recent_native_row message_id=").findAll(result.diagnostics).count())
        assertFalse(result.diagnostics.contains("message_id="+1.toString().padStart(32,'0')))
        assertEquals(rows.map{it.recipientDevice},snapshot.conversations.single().messages.map{it.recipientDevice})
        assertEquals(4,result.conversations.single().messages.size)
    }
}
