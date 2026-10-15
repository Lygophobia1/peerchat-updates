package com.peerchat.android.application
import com.peerchat.android.platform.*
import org.junit.Assert.*
import org.junit.Test
class GroupNotificationTest {
    private val group=GroupConversationSnapshot("a".repeat(32),"b".repeat(32),1,"Group",true,2,
        listOf(GroupMessageSnapshot("m","remote",7,1,1,5,"Accepted","secret")))
    private val snapshot=MessagingSnapshot(accountId=1,localPeerId="local",authenticated=true)
    @Test fun requiresBaselineAndAuthenticatedNativeSnapshot(){
        assertNull(planGroupMessageNotification(snapshot,group,null,NotificationPreview.SenderOnly))
        assertNull(planGroupMessageNotification(snapshot.copy(authenticated=false),group,ConversationPreference(),NotificationPreview.SenderOnly))
    }
    @Test fun honorsPrivacyAndReadOrNotifiedCursor(){
        val hidden=planGroupMessageNotification(snapshot,group,ConversationPreference(),NotificationPreview.HideContent)!!
        assertEquals("PeerChat",hidden.title);assertEquals("New PeerChat message",hidden.body)
        assertEquals("secret",planGroupMessageNotification(snapshot,group,ConversationPreference(),NotificationPreview.SenderAndMessage)!!.body)
        assertNull(planGroupMessageNotification(snapshot,group,ConversationPreference(readSequence=5),NotificationPreview.SenderAndMessage))
        assertNull(planGroupMessageNotification(snapshot,group,ConversationPreference(notifiedSequence=5),NotificationPreview.SenderAndMessage))
        assertNull(planGroupMessageNotification(snapshot.copy(localPeerId="remote"),group,ConversationPreference(),NotificationPreview.SenderAndMessage))
    }
}
