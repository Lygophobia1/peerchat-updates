package com.peerchat.android.ui

import com.peerchat.android.application.*
import org.junit.Assert.*
import org.junit.Test

class GroupUiProjectionCacheTest {
    private val message=GroupMessageSnapshot("message","me",7,1000,1,1,"Awaiting server acceptance","hello")
    private val group=GroupConversationSnapshot("group","legacy",1,"Name",true,1,messages=listOf(message),memberRows=listOf(GroupMemberSnapshot(8,"peer",1,false)))
    private val friend=ConversationSnapshot(FriendIdentity(8,"Friend","peer",true),ConnectionSnapshot(),emptyList())
    private val snapshot=MessagingSnapshot(accountId=7,username="Me",localPeerId="me",authenticated=true,groups=listOf(group),conversations=listOf(friend))
    private fun project(cache:GroupUiProjectionCache,s:MessagingSnapshot=snapshot,generation:Long=1)=
        projectMessaging(s,PeerChatUiState(),cache,generation).conversations.single{it.kind==ChatKind.Group}

    @Test fun dmOnlyAndCallDiagnosticsChangesReuseIdenticalGroupProjection() {
        val cache=GroupUiProjectionCache();val first=project(cache)
        val dm=MessageSnapshot("dm","peer","me",1000,1,1,2,"new DM")
        val update=snapshot.copy(conversations=listOf(friend.copy(messages=listOf(dm))),voice=VoiceSnapshot(diagnostic="new diagnostics",mediaDiagnostics="packets changed"))
        assertSame(first,project(cache,update))
        assertSame(first.messages,project(cache,update).messages)
    }
    @Test fun unreadRosterMessagesDeliveryAndNamesInvalidateImmediately() {
        val changes=listOf(
            group.copy(unread=2),group.copy(ready=false,memberRows=emptyList()),
            group.copy(memberRows=listOf(GroupMemberSnapshot(8,"peer",2,false))),
            group.copy(messages=listOf(message.copy(status="Recipients: accepted"))),
            group.copy(messages=listOf(message,message.copy(id="new",order=2,text="new message"))),
            group.copy(name="Renamed"),group.copy(hasOlder=false))
        changes.forEach{changed->val cache=GroupUiProjectionCache();val before=project(cache);assertNotSame(before,project(cache,snapshot.copy(groups=listOf(changed))))}
        val cache=GroupUiProjectionCache();val before=project(cache)
        val renamed=project(cache,snapshot.copy(conversations=listOf(friend.copy(friend=friend.friend.copy(username="Renamed friend")))))
        assertNotSame(before,renamed);assertEquals("Renamed friend",renamed.members.single().name)
    }
    @Test fun roomMembershipJoinMuteAndEpochAreNeverStale() {
        val rooms=GroupVoiceRoomsSnapshot("group","epoch",1,1,listOf(GroupVoiceRoom("room","Voice",emptyList())))
        val source=snapshot.copy(voiceRooms=listOf(rooms));val cache=GroupUiProjectionCache()
        val idle=project(cache,source);assertFalse(idle.voiceRooms.single().joined)
        val voice=VoiceSnapshot(active=true,group="group",room="room",epoch="epoch")
        val joined=project(cache,source.copy(voice=voice));assertTrue(joined.voiceRooms.single().joined)
        val muted=project(cache,source.copy(voice=voice.copy(muted=true)));assertTrue(muted.voiceRooms.single().muted)
        val departed=project(cache,source.copy(voice=voice.copy(epoch="other")));assertFalse(departed.voiceRooms.single().joined)
        val member=GroupVoiceMember(8,"peer","participation","")
        val updated=project(cache,source.copy(voiceRooms=listOf(rooms.copy(rooms=listOf(rooms.rooms.single().copy(members=listOf(member)))))))
        assertEquals(listOf("Friend"),updated.voiceRooms.single().participants)
    }
    @Test fun accountSessionAndRemovedGroupsDropCachedProjection() {
        val cache=GroupUiProjectionCache();val first=project(cache)
        assertNotSame(first,project(cache,generation=2))
        val current=project(cache,generation=2)
        assertNotSame(current,project(cache,snapshot.copy(accountId=9),2))
        projectMessaging(snapshot.copy(groups=emptyList()),PeerChatUiState(),cache,2)
        assertNotSame(current,project(cache,generation=2))
    }
}
