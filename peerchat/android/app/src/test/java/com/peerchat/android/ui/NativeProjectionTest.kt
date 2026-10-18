package com.peerchat.android.ui

import com.peerchat.android.application.*
import org.junit.Assert.*
import org.junit.Test

class NativeProjectionTest {
    private val local="a".repeat(64)
    private val remote="b".repeat(64)
    @Test fun voiceCapabilityAndDiagnosticComeFromNativeSnapshot(){
        val reason="Pinned Android receive component unavailable"
        val ui=projectMessaging(MessagingSnapshot(voice=VoiceSnapshot(available=false,diagnostic=reason)),PeerChatUiState())
        assertFalse(ui.voiceAvailable);assertEquals(reason,ui.voiceDiagnostic);assertTrue(ui.diagnostics.contains(reason))
        assertFalse(PeerChatUiState().voiceAvailable)
    }
    @Test fun liveCallProjectionUsesExactNativeTargetAndPeerName(){
        val call=VoiceSnapshot(available=true,diagnostic="Ready",active=true,id="c".repeat(32),
            peer=remote,lifetime=23,state=2,direction=1,owner="owner-1")
        val projected=projectMessaging(snapshot().copy(voice=call),PeerChatUiState())
        assertEquals(call.target,projected.voice.target)
        assertEquals("Sammy",projected.voicePeerName)
        assertEquals("Incoming call",voiceStatus(projected.voice.state))
        val ended=projectMessaging(snapshot().copy(voice=VoiceSnapshot(available=true,diagnostic="Call ended")),projected)
        assertNull(ended.voice.target)
        assertEquals("",ended.voicePeerName)
    }
    @Test fun permissionResultCannotAnswerReplacedIncomingCall(){
        val call=VoiceSnapshot(available=true,active=true,id="c".repeat(32),peer=remote,
            lifetime=23,state=2,direction=1,owner="owner-1")
        val target=call.target!!
        val consent=VoiceConsent("consent",remote,call.id,true)
        assertTrue(voiceConsentMatchesCurrent(consent,target,call))
        assertFalse(voiceConsentMatchesCurrent(consent,target,call.copy(lifetime=24)))
        assertFalse(voiceConsentMatchesCurrent(consent,target,call.copy(state=5)))
        assertFalse(voiceConsentMatchesCurrent(consent,target,call.copy(direction=0)))
        assertFalse(voiceConsentMatchesCurrent(consent,target,VoiceSnapshot(available=true)))
    }
    @Test fun groupRoomsProjectOnlyAuthenticatedMembersAndCurrentVoiceJoin(){
        val group="d".repeat(32);val room="e".repeat(32);val epoch="f".repeat(32)
        val groups=listOf(GroupConversationSnapshot(group,"1".repeat(32),2,"Planning",true,2))
        val rooms=listOf(GroupVoiceRoomsSnapshot(group,epoch,3,2,listOf(GroupVoiceRoom(room,"General",listOf(
            GroupVoiceMember(19,local,"2".repeat(32),"Jonah"),GroupVoiceMember(8,remote,"3".repeat(32),"Sammy"))))))
        val base=snapshot().copy(groups=groups,voiceRooms=rooms)
        val idle=projectMessaging(base,PeerChatUiState()).conversations.last().voiceRooms.single()
        assertEquals("General",idle.name);assertEquals(listOf("Jonah","Sammy"),idle.participants);assertFalse(idle.joined)
        val voice=VoiceSnapshot(available=true,active=true,owner="owner",peer=local,id=room,lifetime=1,
            state=5,group=group,room=room,epoch=epoch,participation="2".repeat(32),muted=true)
        val joined=projectMessaging(base.copy(voice=voice),PeerChatUiState()).conversations.last().voiceRooms.single()
        assertTrue(joined.joined);assertTrue(joined.muted)
        assertFalse(projectMessaging(base.copy(voice=voice.copy(epoch="0".repeat(32))),PeerChatUiState())
            .conversations.last().voiceRooms.single().joined)
        assertTrue(projectMessaging(base.copy(voiceRooms=emptyList()),PeerChatUiState()).conversations.last().voiceRooms.isEmpty())
    }
    @Test fun groupPermissionResultNeedsSameRoomEpochAndIdleVoice(){
        val group="d".repeat(32);val room="e".repeat(32);val epoch="f".repeat(32)
        val consent=VoiceConsent("token",local,room,false,group)
        val rooms=listOf(GroupVoiceRoomsSnapshot(group,epoch,1,2,listOf(GroupVoiceRoom(room,"General",emptyList()))))
        val idle=VoiceSnapshot(available=true)
        assertTrue(voiceConsentMatchesCurrent(consent,null,idle,rooms,epoch,local))
        assertFalse(voiceConsentMatchesCurrent(consent,null,idle,rooms,"0".repeat(32),local))
        assertFalse(voiceConsentMatchesCurrent(consent,null,idle,emptyList(),epoch,local))
        assertFalse(voiceConsentMatchesCurrent(consent,null,idle,rooms,epoch,remote))
        assertFalse(voiceConsentMatchesCurrent(consent,null,idle.copy(active=true),rooms,epoch,local))
    }
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
