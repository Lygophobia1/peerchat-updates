package com.peerchat.android.ui

import com.peerchat.android.application.*
import org.junit.Assert.*
import org.junit.Test

class NativeProjectionTest {
    private val local="a".repeat(64)
    private val remote="b".repeat(64)
    private fun snapshot(delivery:Long=0,outgoing:Boolean=true)=MessagingSnapshot(19,"Jonah",local,true,"Authenticated",listOf(
        ConversationSnapshot(FriendIdentity(8,"Sammy",remote,true),ConnectionSnapshot(ready=true),listOf(
            MessageSnapshot("c".repeat(32),if(outgoing)local else remote,if(outgoing)remote else local,1700000000000,1,1,delivery,"private message")))))
    @Test fun projectsCanonicalIdentityAndRealDelivery() {
        val s=projectMessaging(snapshot(),PeerChatUiState())
        assertFalse(s.demo);assertEquals("account:19",s.account!!.accountId)
        assertEquals("account:8",s.conversations.single().id)
        assertEquals("Sammy",s.conversations.single().name)
        assertEquals(MessageStatus.Queued,s.conversations.single().messages.single().status)
        assertEquals(MessageStatus.Delivered,projectMessaging(snapshot(2),s).conversations.single().messages.single().status)
        assertEquals(MessageStatus.Received,projectMessaging(snapshot(2,false),s).conversations.single().messages.single().status)
    }
    @Test fun noDemoMessagesOrSecretDiagnostics() {
        val demo=PeerChatUiState(conversations=DemoConversations().initialSnapshot())
        val s=projectMessaging(snapshot(),demo)
        assertEquals(1,s.conversations.size);assertTrue(s.recentCalls.isEmpty())
        assertFalse(s.diagnostics.contains("private message"));assertTrue(s.diagnostics.contains("Ready=true"))
        val loggedOut=projectMessaging(MessagingSnapshot(),s)
        assertNull(loggedOut.account);assertTrue(loggedOut.conversations.isEmpty())
        assertEquals(ConnectionState.Offline,loggedOut.connection)
    }
    @Test fun pinCannotDisableValidation() {
        assertTrue(validServerDraft(ServerUi("localhost","7443")))
        assertTrue(validServerDraft(ServerUi("localhost","7443","f".repeat(64))))
        assertFalse(validServerDraft(ServerUi("localhost","7443","trust-all")))
    }
    @Test fun selectionCannotInventOnlinePresence() {
        val online=projectMessaging(snapshot(),PeerChatUiState(selectedId="account:8"))
        assertTrue(online.selected!!.online)
        val offlineSnapshot=snapshot().let {s->s.copy(conversations=s.conversations.map{c->c.copy(friend=c.friend.copy(online=false))})}
        val offline=projectMessaging(offlineSnapshot,online)
        assertFalse(offline.selected!!.online)
    }
    @Test fun loginAloneCannotReportActiveServerPresence() {
        val authenticated=snapshot()
        val pending=projectMessaging(authenticated,PeerChatUiState())
        assertNotNull(pending.account)
        assertEquals(ConnectionState.Reconnecting,pending.connection)
        val live=projectMessaging(authenticated.copy(serverPresenceLive=true),pending)
        assertEquals(ConnectionState.Online,live.connection)
        assertEquals(ConnectionState.Reconnecting,projectMessaging(authenticated,live).connection)
        assertEquals(ConnectionState.Offline,projectMessaging(MessagingSnapshot(),live).connection)
    }
    @Test fun supersededPresenceFailsClosedWithoutSecretDiagnostics() {
        val superseded=com.peerchat.android.platform.ServerRejected("session_superseded")
        assertTrue(expiredServerSession(superseded))
        assertTrue(expiredServerSession(com.peerchat.android.platform.ServerRejected("event_session_superseded")))
        assertFalse(expiredServerSession(java.io.IOException("secret request and token")))
        assertFalse(serverRefreshFailure(java.io.IOException("secret request and token")).contains("secret"))
        assertTrue(serverRefreshFailure(superseded).contains("session_superseded"))
    }
}
