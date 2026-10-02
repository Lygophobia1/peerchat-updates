package com.peerchat.android.ui

import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class UiStateTest {
    private fun initial() = PeerChatUiState(conversations = DemoConversations().initialSnapshot())
    @Test fun explicitDemoNavigationAndBack() {
        val welcome = initial()
        val home = reduceUi(welcome, UiAction.Explore)
        val chat = reduceUi(home, UiAction.Select("demo-dm-sammy"))
        assertEquals(Route.Welcome, welcome.route)
        assertEquals(Route.Conversation, chat.route)
        assertEquals(0, chat.selected!!.unread)
        assertEquals(Route.Conversation, reduceUi(reduceUi(chat, UiAction.Navigate(Route.Details)), UiAction.Back).route)
        assertEquals(Route.Home, reduceUi(chat, UiAction.Back).route)
        assertEquals(home, reduceUi(home, UiAction.Select("unknown")))
        assertEquals(2, welcome.conversations.first().unread)
    }
    @Test fun independentDmGroupSearchAndLoadStates() {
        var s = initial()
        assertTrue(s.visible.all { it.kind == ChatKind.Direct })
        s = reduceUi(s, UiAction.SelectKind(ChatKind.Group))
        assertEquals(2, s.visible.size)
        s = reduceUi(s, UiAction.Search("GAMING"))
        assertEquals("demo-group-gaming", s.visible.single().id)
        s = reduceUi(s, UiAction.Search("not found"))
        assertTrue(s.visible.isEmpty())
        LoadState.entries.forEach { assertEquals(it, reduceUi(s, UiAction.SetLoad(it)).load) }
        assertEquals(256, reduceUi(s, UiAction.Search("x".repeat(400))).query.length)
    }
    @Test fun localComposerIsBoundedOfflineAndDoesNotFakeDelivery() {
        val s = reduceUi(initial(), UiAction.Select("demo-dm-sammy"))
        val sent = reduceUi(s, UiAction.PreviewMessage("日本語 👋\nTwo lines"))
        assertEquals(MessageStatus.Preview, sent.selected!!.messages.last().status)
        assertEquals(s.selected!!.messages.size + 1, sent.selected!!.messages.size)
        assertEquals(s, reduceUi(s, UiAction.PreviewMessage(" ")))
        assertEquals(s, reduceUi(s, UiAction.PreviewMessage("x".repeat(4097))))
        ConnectionState.entries.filter { it != ConnectionState.Online }.forEach {
            val offline = reduceUi(s, UiAction.SetConnection(it))
            assertEquals(offline, reduceUi(offline, UiAction.PreviewMessage("Must not send")))
        }
    }
    @Test fun historyPrependAndNotificationsStayConversationScoped() {
        val s = reduceUi(initial(), UiAction.Select("demo-group-gaming"))
        val older = reduceUi(s, UiAction.Older)
        assertEquals("Yesterday", older.selected!!.messages.first().date)
        assertFalse(older.selected!!.hasOlder)
        assertEquals(older, reduceUi(older, UiAction.Older))
        val toggled = reduceUi(older, UiAction.ToggleNotifications)
        assertFalse(toggled.selected!!.notifications)
        assertTrue(toggled.conversations.filter { it.id != s.selectedId }.all { it.notifications })
        assertEquals("demo-sammy", toggled.selected!!.messages[1].senderId)
    }
    @Test fun noFakeAuthenticationSuccess() = runBlocking {
        val result = UnconnectedAccountAccess().signIn("user", "not-a-real-password", ServerUi())
        assertEquals(LoginResult.Unavailable, result)
    }
    @Test fun singleParticipantRoomIsValidAndViewingIsIndependent() {
        val group = initial().conversations.first { it.id == "demo-group-work" }
        assertTrue(group.voiceRooms.single().joined)
        assertEquals(listOf("You"), group.voiceRooms.single().participants)
        assertTrue(group.shareRooms.isEmpty())
    }
    @Test fun serverDraftReturnsToCallerAndRestDoesNotCreateIdentity() {
        val s = reduceUi(initial(), UiAction.Navigate(Route.Login))
        val server = reduceUi(s, UiAction.Navigate(Route.Server))
        val saved = reduceUi(server, UiAction.SaveServer(ServerUi("example.invalid", "7443")))
        assertEquals(Route.Login, reduceUi(saved, UiAction.Back).route)
        assertNull(saved.account)
    }
    @Test fun voiceParticipationAndScreenViewingAreIndependentLocalPreviews() {
        var s = reduceUi(initial(), UiAction.Select("demo-group-gaming"))
        s = reduceUi(s, UiAction.WatchShare("build"))
        assertTrue(s.selected!!.shareRooms.single().watching)
        assertFalse(s.selected!!.voiceRooms.any { it.joined })
        s = reduceUi(s, UiAction.JoinVoice("quiet"))
        assertEquals(listOf("You"), s.selected!!.voiceRooms.last().participants)
        s = reduceUi(s, UiAction.ToggleMute("quiet"))
        s = reduceUi(s, UiAction.ToggleDeafen("quiet"))
        assertTrue(s.selected!!.voiceRooms.last().muted)
        assertTrue(s.selected!!.voiceRooms.last().deafened)
        s = reduceUi(s, UiAction.LeaveVoice("quiet"))
        assertTrue(s.selected!!.shareRooms.single().watching)
        assertEquals(2, s.selected!!.shareRooms.single().viewers)
        s = reduceUi(s, UiAction.StopWatching("build"))
        assertEquals(1, s.selected!!.shareRooms.single().viewers)
    }
    @Test fun memberAudioPreviewIsScopedAndRejectsInvalidUiValues() {
        val s = reduceUi(initial(), UiAction.Select("demo-group-gaming"))
        assertEquals(s, reduceUi(s, UiAction.MemberAudio(MemberAudioUi("not-a-member"))))
        assertEquals(s, reduceUi(s, UiAction.MemberAudio(MemberAudioUi("demo-sammy", Float.NaN))))
        val updated = reduceUi(s, UiAction.MemberAudio(MemberAudioUi("demo-sammy", 9f, true)))
        assertEquals(2f, updated.selected!!.memberAudio.single().volume)
        assertTrue(updated.selected!!.memberAudio.single().muted)
    }
    @Test fun manyConversationsAndDuplicateDisplayNamesKeepStableUiIds() {
        val fixture = initial().conversations.first()
        val many = initial().copy(conversations = (0 until 500).map { fixture.copy(id = "demo-$it", name = "Sammy") })
        assertEquals(500, many.visible.size)
        val selected = reduceUi(many, UiAction.Select("demo-499"))
        assertEquals("demo-499", selected.selected!!.id)
        assertEquals(499, selected.conversations.count { it.unread == 2 })
    }
    @Test fun validationAndBackNavigationFromGlobalTools() {
        assertTrue(validServerDraft(ServerUi("example.invalid", "7443")))
        assertFalse(validServerDraft(ServerUi("https://example.invalid", "7443")))
        assertFalse(validServerDraft(ServerUi("server", "65536")))
        assertFalse(validServerDraft(ServerUi(" ", "7443")))
        val you = initial().copy(route = Route.Home, tab = Tab.You)
        val diagnostics = reduceUi(you, UiAction.Navigate(Route.Diagnostics))
        assertEquals(Route.Home, reduceUi(diagnostics, UiAction.Back).route)
        assertEquals(Tab.You, reduceUi(diagnostics, UiAction.Back).tab)
        assertEquals(Tab.Chats, reduceUi(you, UiAction.Back).tab)
        val theme = reduceUi(you, UiAction.SetTheme(ThemeChoice.Light))
        assertEquals(ThemeChoice.Light, theme.theme)
    }
}
