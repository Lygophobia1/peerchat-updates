package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview
import kotlinx.coroutines.delay

@OptIn(ExperimentalMaterial3Api::class)
@Composable internal fun ConversationScreen(c: ConversationUi, state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    var draft by rememberSaveable(c.id) { mutableStateOf("") }
    var sending by remember(c.id) { mutableStateOf(false) }
    val list = rememberLazyListState()
    LaunchedEffect(c.id, c.messages.lastOrNull()?.id) {
        if (c.messages.isNotEmpty()) {
            val dateSeparators = c.messages.indices.count { it == 0 || c.messages[it - 1].date != c.messages[it].date }
            list.scrollToItem(c.messages.size + dateSeparators) // + older item, then -1 for last index
        }
    }
    LaunchedEffect(sending) {
        if (sending) {
            if(state.demo){delay(150);dispatch(UiAction.PreviewMessage(draft));draft="";sending=false}
            else dispatch(UiAction.PreviewMessage(draft))
        }
    }
    LaunchedEffect(state.sendCompletion) {
        if(!state.demo&&sending){if(state.sendAccepted)draft="";sending=false}
    }
    Scaffold(topBar = {
        TopAppBar(title = {
            Column {
                Text(c.name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.titleMedium)
                Text(if (c.kind == ChatKind.Group) "Group · ${c.members.size} members · ${c.members.count { it.online }} online" else
                    if (c.online) "● Online" else "Offline", style = MaterialTheme.typography.labelSmall,
                    color = presenceColor(c.online))
            }
        }, navigationIcon = { ActionIcon(Icons.AutoMirrored.Filled.ArrowBack, "Back", { dispatch(UiAction.Back) }) }, actions = {
            ActionIcon(Icons.Default.Call, if (c.kind == ChatKind.Group) "Voice rooms" else "Voice call", {
                dispatch(if (c.kind == ChatKind.Group) UiAction.Navigate(Route.Details) else UiAction.Notice("Calls are not connected in this preview."))
            })
            ActionIcon(Icons.Default.Info, "Conversation details", { dispatch(UiAction.Navigate(Route.Details)) })
        })
    }, bottomBar = {
        // The bottom bar owns its insets; Scaffold reserves this measured height.
        // Union uses the larger nav/IME inset, never adding both paddings.
        Column(Modifier.windowInsetsPadding(WindowInsets.navigationBars.union(WindowInsets.ime))) {
            c.voiceRooms.find { it.joined }?.let { CallBar(it, dispatch) }
            c.shareRooms.find { it.watching }?.let { share -> Surface(color = MaterialTheme.colorScheme.secondaryContainer) {
                Row(Modifier.padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("Watching ${share.sharer} · UI only", Modifier.weight(1f), style = MaterialTheme.typography.labelSmall)
                    TextButton({ dispatch(UiAction.StopWatching(share.id)) }) { Text("Stop Watching") }
                }
            } }
            HorizontalDivider()
            Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 8.dp), verticalAlignment = Alignment.Bottom) {
                ActionIcon(Icons.Default.Add, "Attachments", { dispatch(UiAction.Notice("File sharing is not available in this preview.")) })
                OutlinedTextField(draft, { if (it.length <= 4096) draft = it }, placeholder = { Text("Message ${c.name}", maxLines = 1, overflow = TextOverflow.Ellipsis) },
                    enabled = state.connection == ConnectionState.Online && !sending, minLines = 1, maxLines = 5,
                    modifier = Modifier.weight(1f).testTag("composer"), shape = MaterialTheme.shapes.large)
                IconButton({ sending = true }, enabled = draft.isNotBlank() && !sending && state.connection == ConnectionState.Online) {
                    if (sending) CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 2.dp)
                    else Icon(Icons.AutoMirrored.Filled.Send, if(state.demo) "Send local preview" else "Send message", tint = if (draft.isNotBlank()) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
    }) { padding ->
        Column(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)) {
            if(state.demo) DemoNote()
            if (state.connection != ConnectionState.Online) Surface(color = MaterialTheme.colorScheme.secondaryContainer) {
                Text(if (state.connection == ConnectionState.Offline) "Offline preview · sending disabled" else "Reconnecting preview · sending disabled",
                    Modifier.fillMaxWidth().padding(12.dp), style = MaterialTheme.typography.bodySmall)
            }
            state.notice?.let { notice -> Row(Modifier.padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(notice, Modifier.weight(1f), style = MaterialTheme.typography.bodySmall)
                ActionIcon(Icons.Default.Close, "Dismiss", { dispatch(UiAction.Notice(null)) })
            } }
            if (c.kind == ChatKind.Group && c.voiceRooms.isNotEmpty()) TextButton({ dispatch(UiAction.Navigate(Route.Details)) }, Modifier.fillMaxWidth()) {
                Text("Voice rooms · ${c.voiceRooms.sumOf { it.participants.size }} in voice (sample)")
            }
            c.shareRooms.firstOrNull()?.let { share -> TextButton({ dispatch(UiAction.Navigate(Route.Details)) }, Modifier.fillMaxWidth()) {
                Text("${share.sharer} is sharing · ${if (share.watching) "Watching" else "Watch"} (preview)")
            } }
            LazyColumn(Modifier.fillMaxSize().testTag("messages"), state = list, contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)) {
                item(key = "older") { if (c.hasOlder) TextButton({ dispatch(UiAction.BeginOlder) }, enabled = !state.olderLoading, modifier = Modifier.fillMaxWidth()) {
                    Text(if(state.demo) {if (state.olderLoading) "Loading older preview…" else "Load older preview messages"} else if (state.olderLoading) "Loading older messages…" else "Load older messages")
                } }
                c.messages.forEachIndexed { index, message ->
                    if (index == 0 || c.messages[index - 1].date != message.date) item(key = "date-${message.id}") {
                        Box(Modifier.fillMaxWidth().padding(8.dp), contentAlignment = Alignment.Center) {
                            Text(message.date, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    item(key = message.id) { MessageBubble(message, c.kind == ChatKind.Group &&
                        (index == 0 || c.messages[index - 1].senderId != message.senderId), dispatch) }
                }
            }
        }
    }
}

@Composable private fun linkedText(text: String): AnnotatedString {
    val color = MaterialTheme.colorScheme.primary
    return buildAnnotatedString {
        append(text)
        Regex("https?://[^\\s]+").findAll(text).forEach { match ->
            addLink(LinkAnnotation.Url(match.value, TextLinkStyles(style = SpanStyle(color = color, textDecoration = TextDecoration.Underline))),
                match.range.first, match.range.last + 1)
        }
    }
}
@Composable private fun MessageBubble(m: MessageUi, sender: Boolean, dispatch: (UiAction) -> Unit) {
    Row(Modifier.fillMaxWidth(), horizontalArrangement = if (m.outgoing) Arrangement.End else Arrangement.Start) {
        Surface(color = if (m.outgoing) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant,
            shape = MaterialTheme.shapes.large, modifier = Modifier.widthIn(max = 330.dp).fillMaxWidth(0.9f)) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (sender && !m.outgoing) Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Avatar(m.senderName, size = 28)
                    Text(m.senderName, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                }
                SelectionContainer { Text(linkedText(m.text)) }
                Text("${m.time}" + if (m.outgoing) " · ${when (m.status) {
                    MessageStatus.Read -> "Read ✓✓"; MessageStatus.Delivered -> "Delivered ✓"; MessageStatus.Failed -> "Failed";
                    MessageStatus.Queued -> "Queued";
                    MessageStatus.Preview -> "Local preview · not sent"; else -> "Sent"
                }}" else "", style = MaterialTheme.typography.labelSmall,
                    color = if (m.status == MessageStatus.Failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
                if (m.status == MessageStatus.Failed) TextButton({ dispatch(UiAction.RetryPreview(m.id)) }) { Text("Retry locally") }
            }
        }
    }
}
@Composable private fun CallBar(room: VoiceRoomUi, dispatch: (UiAction) -> Unit) {
    Surface(color = MaterialTheme.colorScheme.secondaryContainer) {
        Column(Modifier.fillMaxWidth().padding(12.dp)) {
            Text("In Voice · ${room.name}", style = MaterialTheme.typography.labelLarge)
            Text("Joined-state preview only · no audio", style = MaterialTheme.typography.labelSmall)
            Row {
                TextButton({ dispatch(UiAction.ToggleMute(room.id)) }, Modifier.weight(1f)) { Text(if (room.muted) "Unmute" else "Mute") }
                TextButton({ dispatch(UiAction.ToggleDeafen(room.id)) }, Modifier.weight(1f)) { Text(if (room.deafened) "Hear" else "Deafen") }
                TextButton({ dispatch(UiAction.Notice("Screen capture is unavailable in this preview.")) }, Modifier.weight(1f)) { Text("Share") }
                TextButton({ dispatch(UiAction.LeaveVoice(room.id)) }, Modifier.weight(1f)) { Text("Leave") }
            }
        }
    }
}
@Preview(name = "DM dark", widthDp = 360, heightDp = 800)
@Composable private fun DmPreview() { val s = PeerChatUiState(route = Route.Conversation, selectedId = "demo-dm-sammy", conversations = DemoConversations().initialSnapshot())
    PeerChatTheme(true) { ConversationScreen(s.selected!!, s, {}) } }
