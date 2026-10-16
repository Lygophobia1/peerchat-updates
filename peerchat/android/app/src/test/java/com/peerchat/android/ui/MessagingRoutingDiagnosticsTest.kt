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
    @Test fun transferIdentityContextsAreBoundedFreshAndExcludePrivateAttachmentFields(){
        val local="a".repeat(64);val peer="b".repeat(64)
        val files=(1..70).map{n->AttachmentSnapshot(n.toString().padStart(32,'0'),peer,"PRIVATE_FILENAME",100,3,n%2==0,25,1,"PRIVATE_ERROR","/data/private/attachment")}
        val snapshot=MessagingSnapshot(accountId=5,localPeerId=local,authenticated=true,serverPresenceLive=true,
            conversations=listOf(ConversationSnapshot(FriendIdentity(4,"Peer",peer,true),ConnectionSnapshot(networkRevision=7),emptyList(),files)))
        val first=projectMessaging(snapshot,PeerChatUiState())
        val again=projectMessaging(snapshot,first)
        assertEquals(first.diagnostics,again.diagnostics)
        assertEquals(64,Regex("transfer_identity transfer_id=").findAll(first.diagnostics).count())
        assertTrue(first.diagnostics.contains("sender_device=$peer recipient_device=$local authenticated=true server_presence_live=true network_revision=7 outgoing=false"))
        assertTrue(first.diagnostics.contains("sender_device=$local recipient_device=$peer authenticated=true server_presence_live=true network_revision=7 outgoing=true"))
        listOf("PRIVATE_FILENAME","PRIVATE_ERROR","/data/private/attachment").forEach{assertFalse(first.diagnostics.contains(it))}
        assertFalse(first.diagnostics.contains("transfer_id="+70.toString().padStart(32,'0')))
    }
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
