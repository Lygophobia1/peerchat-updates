package com.peerchat.android.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.tooling.preview.Preview

private fun fixture(route: Route = Route.Home, selected: String? = null, tab: Tab = Tab.Chats) =
    PeerChatUiState(route = route, selectedId = selected, tab = tab, conversations = DemoConversations().initialSnapshot())

@Preview(name = "Login", widthDp = 360, heightDp = 800)
@Composable private fun LoginPreview() { PeerChatTheme(true) { PeerChatShell(fixture(Route.Login), {}) } }
@Preview(name = "Login error light", widthDp = 360, heightDp = 800)
@Composable private fun LoginErrorPreview() { PeerChatTheme(false) { PeerChatShell(fixture(Route.Login).copy(loginError = "Preview: Server unreachable"), {}) } }
@Preview(name = "Group chat", widthDp = 360, heightDp = 800)
@Composable private fun GroupPreview() { PeerChatTheme(true) { PeerChatShell(fixture(Route.Conversation, "demo-group-gaming"), {}) } }
@Preview(name = "DM details light", widthDp = 360, heightDp = 800)
@Composable private fun DirectDetailsPreview() { PeerChatTheme(false) { PeerChatShell(fixture(Route.Details, "demo-dm-sammy"), {}) } }
@Preview(name = "In voice", widthDp = 360, heightDp = 800)
@Composable private fun InVoicePreview() { PeerChatTheme(true) { PeerChatShell(fixture(Route.Conversation, "demo-group-work"), {}) } }
@Preview(name = "Share rooms", widthDp = 360, heightDp = 800)
@Composable private fun ShareRoomsPreview() { PeerChatTheme(true) { PeerChatShell(fixture(Route.Details, "demo-group-gaming"), {}) } }
@Preview(name = "Calls", widthDp = 360, heightDp = 800)
@Composable private fun CallsPreview() { PeerChatTheme(true) { PeerChatShell(fixture(tab = Tab.Calls), {}) } }
@Preview(name = "You light", widthDp = 360, heightDp = 800)
@Composable private fun YouPreview() { PeerChatTheme(false) { PeerChatShell(fixture(tab = Tab.You), {}) } }
@Preview(name = "Offline compact", widthDp = 320, heightDp = 640)
@Composable private fun OfflinePreview() { PeerChatTheme(true) { PeerChatShell(fixture(Route.Conversation, "demo-dm-long").copy(connection = ConnectionState.Offline), {}) } }
