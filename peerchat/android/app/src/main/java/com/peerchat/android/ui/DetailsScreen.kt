package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable internal fun DetailsScreen(c: ConversationUi, state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    if(!state.demo){
        Scaffold(topBar={PageBar(if(c.kind==ChatKind.Group)"Group details" else "Conversation details",dispatch)}){padding->
            Column(Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(20.dp),verticalArrangement=Arrangement.spacedBy(12.dp)){
                Avatar(c.name,c.kind==ChatKind.Group,64);Text(c.name,style=MaterialTheme.typography.headlineSmall)
                if(c.kind==ChatKind.Group){
                    Text(if(c.online)"Signed membership and current group key ready" else "Waiting for signed membership and current group key")
                    Text("${c.members.size} authenticated roster devices")
                    c.members.forEach{Text("${it.name} · ${it.role}\n${it.accountId}")}
                    Text("Invite a friend",style=MaterialTheme.typography.titleMedium)
                    state.conversations.filter{it.kind==ChatKind.Direct}.forEach{friend->
                        TextButton({friend.id.removePrefix("account:").toLongOrNull()?.let{dispatch(UiAction.InviteGroup(c.id.removePrefix("group:"),it))}}){Text(friend.name)}
                    }
                    Text("Voice rooms",style=MaterialTheme.typography.titleMedium)
                    Text("Joining is voluntary. Your microphone starts only after permission and a successful room join.",style=MaterialTheme.typography.bodySmall)
                    if(c.voiceRooms.isEmpty())Text("Voice rooms and occupancy are not currently verified. Waiting for an authenticated update.")
                    c.voiceRooms.forEach{room->
                        Text(room.name,style=MaterialTheme.typography.titleSmall)
                        Text("${room.participants.size} in voice · ${room.participants.joinToString().ifEmpty{"Empty room"}}")
                        Text(if(room.joined)"In voice" else "Not joined",style=MaterialTheme.typography.labelSmall)
                        OutlinedButton(onClick={dispatch(if(room.joined)UiAction.LeaveVoice(room.id) else UiAction.JoinVoice(room.id))},
                            enabled=room.joined&&!state.voiceBusy||!room.joined&&c.online&&state.voiceAvailable&&!state.voice.active&&!state.voiceBusy,
                            modifier=Modifier.testTag("voice-room-${room.id}")){
                            Text(if(room.joined)"Leave voice" else "Join voice")
                        }
                    }
                    if(state.voiceBusy)LinearProgressIndicator(Modifier.fillMaxWidth())
                    state.voiceError?.let{Text(it,color=MaterialTheme.colorScheme.error)}
                    Text("Messages use native group encryption. Delivery reports describe online recipient sessions. Group attachments and screen video are unavailable.",style=MaterialTheme.typography.bodySmall)
                }else Text("Device verification, delivery and local history are owned by the native messaging engine.")
                TextButton({dispatch(UiAction.Navigate(Route.Diagnostics))}){Text("Application diagnostics")}
            }
        }
        return
    }
    var confirm by remember { mutableStateOf(false) }
    var audioMember by remember { mutableStateOf<MemberUi?>(null) }
    Scaffold(topBar = { PageBar(if (c.kind == ChatKind.Group) "Group details" else "Conversation details", dispatch) }) { padding ->
        Column(Modifier.fillMaxSize().padding(padding).verticalScroll(rememberScrollState()).padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Avatar(c.name, c.kind == ChatKind.Group, 64)
            Text(c.name, style = MaterialTheme.typography.headlineSmall)
            Text(if (c.kind == ChatKind.Group) "Group · ${c.members.size} members" else "Direct conversation", color = MaterialTheme.colorScheme.onSurfaceVariant)
            DemoNote()
            DetailSection("Identity & verification") {
                Text(if (c.kind == ChatKind.Group) "Authoritative roster: not connected" else "Contact identity: demo fixture")
                Text("Trust: Not verified in this UI preview", color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text("Real device fingerprints and verification actions will come from the native/application identity boundary.", style = MaterialTheme.typography.bodySmall)
            }
            if (c.kind == ChatKind.Group) {
                DetailSection("Members") {
                    c.members.forEach { member -> Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                        Avatar(member.name, size = 36)
                        Column(Modifier.weight(1f)) { Text(member.name); Text("${member.role} · ${if (member.online) "Online" else "Offline"}", style = MaterialTheme.typography.labelSmall) }
                        // Per-member settings are prepared, not fake working media controls.
                        TextButton({ audioMember = member }) { Text("Audio") }
                    } }
                }
                DetailSection("Voice rooms") {
                    Text("Voluntary rooms · no ringing or automatic joins", style = MaterialTheme.typography.bodySmall)
                    if (c.voiceRooms.isEmpty()) Text("No voice rooms")
                    c.voiceRooms.forEach { room ->
                        Text(room.name, style = MaterialTheme.typography.titleSmall)
                        Text("${room.participants.size} in voice (sample) · ${room.participants.joinToString().ifEmpty { "Empty room" }}")
                        Text(if (room.joined) "In Voice · joined-state preview" else "Not joined", style = MaterialTheme.typography.labelSmall)
                        OutlinedButton(onClick = { dispatch(if (room.joined) UiAction.LeaveVoice(room.id) else UiAction.JoinVoice(room.id)) }) {
                            Text(if (room.joined) "Leave Voice (preview)" else "Join Voice (preview)")
                        }
                    }
                    Text("Voice is not implemented in this UI build.", style = MaterialTheme.typography.bodySmall)
                }
                DetailSection("Screen share rooms") {
                    if (c.shareRooms.isEmpty()) Text("No active share previews")
                    c.shareRooms.forEach { share ->
                        Text(share.name, style = MaterialTheme.typography.titleSmall)
                        Text("${share.sharer} is sharing · ${share.viewers} viewers (sample)")
                        OutlinedButton(onClick = { dispatch(if (share.watching) UiAction.StopWatching(share.id) else UiAction.WatchShare(share.id)) }) {
                            Text(if (share.watching) "Stop Watching (preview)" else "Watch (preview)")
                        }
                    }
                    Text("Screen video is unavailable. Voice participation and screen viewing will remain independent.", style = MaterialTheme.typography.bodySmall)
                }
            }
            DetailSection("Notifications") {
                Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                    Text("Preview notifications", Modifier.weight(1f))
                    Switch(c.notifications, { dispatch(UiAction.ToggleNotifications) })
                }
                Text("Local UI state only; no system notification settings are changed.", style = MaterialTheme.typography.bodySmall)
            }
            DetailSection("Shared files") {
                Text("No shared files in this demo.")
                Text("File transfer and real media history are not implemented.", style = MaterialTheme.typography.bodySmall)
            }
            DetailSection("Connection") {
                Text("${state.connection.name} · preview state only")
                Text("No authenticated session is active.", style = MaterialTheme.typography.bodySmall)
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    ConnectionState.entries.forEach { connection -> FilterChip(state.connection == connection,
                        { dispatch(UiAction.SetConnection(connection)) }, label = { Text(connection.name) }) }
                }
                TextButton({ dispatch(UiAction.Navigate(Route.Diagnostics)) }) { Text("Diagnostics") }
            }
            state.notice?.let { Text(it, color = MaterialTheme.colorScheme.primary) }
            TextButton({ confirm = true }) { Text(if (c.kind == ChatKind.Group) "Leave group…" else "Remove contact…", color = MaterialTheme.colorScheme.error) }
        }
    }
    if (confirm) AlertDialog(onDismissRequest = { confirm = false }, title = { Text("Preview action") },
        text = { Text("This UI build does not remove contacts or change group membership. No backend action will be performed.") },
        confirmButton = { TextButton({ confirm = false }) { Text("Got it") } })
    audioMember?.let { member ->
        val audio = c.memberAudio.find { it.accountId == member.accountId } ?: MemberAudioUi(member.accountId)
        AlertDialog(onDismissRequest = { audioMember = null }, title = { Text("${member.name} · audio preview") },
            text = { Column {
                Text("Local visual controls only · no audio is changed.")
                Text("Volume: ${(audio.volume * 100).toInt()}%")
                Slider(audio.volume, { dispatch(UiAction.MemberAudio(audio.copy(volume = it))) }, valueRange = 0f..2f)
                Row(verticalAlignment = Alignment.CenterVertically) { Text("Mute", Modifier.weight(1f)); Switch(audio.muted, { dispatch(UiAction.MemberAudio(audio.copy(muted = it))) }) }
            } }, confirmButton = { TextButton({ audioMember = null }) { Text("Done") } })
    }
}
@Composable private fun DetailSection(title: String, content: @Composable ColumnScope.() -> Unit) {
    OutlinedCard(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}
@Preview(name = "Group details dark", widthDp = 360, heightDp = 800)
@Composable private fun DetailsPreview() { val s = PeerChatUiState(route = Route.Details, selectedId = "demo-group-gaming", conversations = DemoConversations().initialSnapshot())
    PeerChatTheme(true) { DetailsScreen(s.selected!!, s, {}) } }
