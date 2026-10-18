package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Call
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import android.app.Activity
import android.os.Build
import android.Manifest
import com.peerchat.android.platform.NotificationPreview
import com.peerchat.android.application.VoiceControl

internal fun voiceStatus(state:Long):String=when(state){
    1L->"Calling";2L->"Incoming call";3L->"Negotiating";4L->"Connecting audio"
    5L->"Connected";6L->"Ending";7L->"Ended";8L->"Failed";else->"Call status unavailable"
}

@OptIn(ExperimentalLayoutApi::class)
@Composable internal fun VoiceCallCard(state:PeerChatUiState,dispatch:(UiAction)->Unit) {
    val voice=state.voice
    val target=voice.target
    val group=voice.group.isNotEmpty()
    val groupName=state.conversations.firstOrNull{it.id=="group:${voice.group}"}?.name.orEmpty()
    val roomName=state.conversations.firstOrNull{it.id=="group:${voice.group}"}?.voiceRooms?.firstOrNull{it.id==voice.room}?.name.orEmpty()
    Surface(color=MaterialTheme.colorScheme.primaryContainer,modifier=Modifier.fillMaxWidth().testTag(if(group)"group-voice-call" else "dm-voice-call")) {
        Column(Modifier.padding(horizontal=16.dp,vertical=10.dp),verticalArrangement=Arrangement.spacedBy(4.dp)) {
            Text(if(group)"${groupName.ifEmpty{"Group voice"}} · ${roomName.ifEmpty{"Voice room"}} · ${voice.diagnostic.take(160)}"
                else "${state.voicePeerName.ifEmpty { "Direct voice call" }} · ${voiceStatus(voice.state)}",style=MaterialTheme.typography.titleSmall)
            if(voice.platformDiagnostic.isNotBlank())Text(voice.platformDiagnostic.take(160),style=MaterialTheme.typography.bodySmall)
            if(voice.diagnostic.isNotBlank()&&voice.state==8L)Text(voice.diagnostic.take(160),style=MaterialTheme.typography.bodySmall)
            state.voiceError?.let{Text(it,style=MaterialTheme.typography.bodySmall,color=MaterialTheme.colorScheme.error)}
            if(target!=null)FlowRow(horizontalArrangement=Arrangement.spacedBy(6.dp)) {
                if(!group&&voice.direction==1L&&voice.state==2L){
                    Button({dispatch(UiAction.AnswerDmVoice(target))},enabled=!state.voiceBusy,modifier=Modifier.testTag("dm-voice-answer")){Text("Answer")}
                    TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.Decline))},enabled=!state.voiceBusy,modifier=Modifier.testTag("dm-voice-decline")){Text("Decline")}
                } else {
                    if(voice.state in 3L..5L){
                        TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.Mute,!voice.muted))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-mute" else "dm-voice-mute")){Text(if(voice.muted)"Unmute" else "Mute")}
                        TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.Deafen,!voice.deafened))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-deafen" else "dm-voice-deafen")){Text(if(voice.deafened)"Undeafen" else "Deafen")}
                        TextButton({dispatch(UiAction.SpeakerDmVoice(target,!voice.speaker))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-speaker" else "dm-voice-speaker")){Text(if(voice.speaker)"Speaker off" else "Speaker on")}
                    }
                    if(voice.state in 1L..5L)TextButton({dispatch(UiAction.ControlDmVoice(target,VoiceControl.End))},enabled=!state.voiceBusy,modifier=Modifier.testTag(if(group)"group-voice-leave" else "dm-voice-hangup")){Text(if(group)"Leave voice" else "Hang up")}
                }
            }
            if(state.voiceBusy)LinearProgressIndicator(Modifier.fillMaxWidth())
        }
    }
}

@Composable internal fun CallsPage(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Text("Calls", style = MaterialTheme.typography.headlineSmall)
        Text(if(state.demo) "Local media UI previews · microphone and speakers are never opened." else if(state.voiceAvailable)
            "Direct calls and available group voice rooms can be joined explicitly. Screen video is unavailable."
            else "Voice: ${state.voiceDiagnostic.take(160)} Screen video is unavailable.", style = MaterialTheme.typography.bodySmall)
        if(!state.demo&&state.voiceBusy)Text("Preparing call or waiting for microphone permission…",modifier=Modifier.testTag("dm-voice-progress"))
        if(!state.demo)state.voiceError?.let{Text(it,color=MaterialTheme.colorScheme.error,modifier=Modifier.testTag("dm-voice-error"))}
        Text("Voice rooms", style = MaterialTheme.typography.titleMedium)
        state.conversations.filter { it.voiceRooms.isNotEmpty() }.forEach { c ->
            OutlinedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(c.name, style = MaterialTheme.typography.titleMedium)
                    c.voiceRooms.forEach { room -> Text("${room.name} · ${room.participants.size} in voice${if (room.joined) " · In Voice" else ""}") }
                    TextButton({ dispatch(UiAction.Select(c.id)); dispatch(UiAction.Navigate(Route.Details)) }) { Text(if(state.demo)"Open room previews" else "Open voice rooms") }
                }
            }
        }
        Text(if(state.demo)"Recent calls · samples" else "Recent calls", style = MaterialTheme.typography.titleMedium)
        state.recentCalls.forEach { call -> Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.Call, null)
            Column { Text(call.name); Text(call.detail, color = if (call.missed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant) }
        } }
        if(state.demo) TextButton({ dispatch(UiAction.PreviewIncomingCall) }) { Text("Preview incoming DM call") }
        if (state.incomingCallPreview) OutlinedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Sammy · incoming call preview", style = MaterialTheme.typography.titleMedium)
                Text("No ringing, media or remote invite is active.")
                Row { Button(onClick = {}, enabled = false) { Text("Answer") }; TextButton({ dispatch(UiAction.PreviewIncomingCall) }) { Text("Dismiss preview") } }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable internal fun YouPage(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    var friendName by remember {mutableStateOf("")}
    val context=LocalContext.current
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Avatar("You", size = 64)
        Text("Your space", style = MaterialTheme.typography.headlineSmall)
        Text(state.account?.let {"${it.username} · ${it.accountId}"} ?: "No signed-in account")
        OutlinedCard(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Device & identity", style = MaterialTheme.typography.titleMedium)
                Text(if(state.demo) "Device identity: Not connected" else state.diagnostics.substringBefore("\n","") + "\n" + state.diagnostics.lineSequence().firstOrNull {it.startsWith("Profile identity:")}.orEmpty())
                Text("Connected devices: Unavailable")
                Text(if(state.demo) "Demo only: no identity created." else "Profile and identity are owned by the native engine and Android Keystore.", style = MaterialTheme.typography.bodySmall)
            }
        }
        Text("Settings", style = MaterialTheme.typography.titleLarge)
        if(!state.demo){
            OutlinedButton({dispatch(UiAction.OpenTransferSettings)},Modifier.fillMaxWidth().testTag("open-transfer-settings")){Text("File Transfer Settings")}
            ArchiveDevicesSection(state,dispatch)
            Text("Offline encrypted mailbox storage",style=MaterialTheme.typography.titleMedium)
            Row(verticalAlignment=Alignment.CenterVertically){
                Switch(state.mailboxStorageAllowed,{dispatch(UiAction.MailboxStorage(it))})
                Text(if(state.mailboxStorageAllowed) "ON" else "OFF")
            }
            Text("On allows temporary encrypted server storage when a live connection is unavailable. Off keeps messages on the sender until a live route is available. Stored is not Delivered; only recipient acknowledgement confirms delivery.",style=MaterialTheme.typography.bodySmall)
            Text("Message notifications",style=MaterialTheme.typography.titleMedium)
            Row(verticalAlignment=Alignment.CenterVertically){
                Switch(state.notificationsEnabled,{dispatch(UiAction.NotificationSettings(it,state.notificationPreview))})
                Text("Enable notifications")
            }
            if(Build.VERSION.SDK_INT>=33)TextButton({(context as? Activity)?.requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS),7341)}){Text("Allow Android notifications")}
            FlowRow(horizontalArrangement=Arrangement.spacedBy(8.dp)){
                NotificationPreview.entries.forEach{choice->FilterChip(state.notificationPreview==choice,
                    {dispatch(UiAction.NotificationSettings(state.notificationsEnabled,choice))},label={Text(when(choice){NotificationPreview.SenderAndMessage->"Sender and message";NotificationPreview.SenderOnly->"Sender only";NotificationPreview.HideContent->"Hide content"})})}
            }
            Text("Push carries no content. Background delivery requires Remember Me and a configured push provider.",style=MaterialTheme.typography.bodySmall)
        }
        if(!state.demo && state.account!=null) {
            OutlinedTextField(friendName,{friendName=it.take(64)},label={Text("Add friend by username")},modifier=Modifier.fillMaxWidth())
            TextButton({dispatch(UiAction.RequestFriend(friendName))},enabled=friendName.isNotBlank()){Text("Send friend request")}
            state.incomingRequests.forEach {request->
                Text("Friend request: ${request.name}")
                Row {
                    TextButton({dispatch(UiAction.RespondFriend(request.accountId.removePrefix("account:").toLong(),true))}){Text("Accept")}
                    TextButton({dispatch(UiAction.RespondFriend(request.accountId.removePrefix("account:").toLong(),false))}){Text("Reject")}
                }
            }
        }
        Text("Appearance", style = MaterialTheme.typography.titleMedium)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            ThemeChoice.entries.forEach { theme -> FilterChip(state.theme == theme, { dispatch(UiAction.SetTheme(theme)) }, label = { Text(theme.name) }) }
        }
        Text("Appearance choice is session-local in this preview.", style = MaterialTheme.typography.bodySmall)
        OutlinedButton({ dispatch(UiAction.Navigate(Route.Server)) }) { Text("Advanced / server & trust") }
        if(!state.demo) {
            OutlinedButton({dispatch(UiAction.ResetNetworkSession)},Modifier.testTag("reset-network-session")) {Text("Reset Network Session")}
            Text("Disconnects PeerChat from the server and peers and clears temporary connection state. Your account, messages, identity, keys and settings are not deleted.",style=MaterialTheme.typography.bodySmall)
        }
        OutlinedButton({ dispatch(UiAction.Navigate(Route.Diagnostics)) }) { Text("Application diagnostics") }
        Text("PeerChat · ${com.peerchat.android.BuildConfig.VERSION_NAME}", color = MaterialTheme.colorScheme.onSurfaceVariant)
        state.notice?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
        TextButton({ dispatch(UiAction.Navigate(Route.Welcome)) }) { Text("Return to welcome") }
        if(!state.demo && state.account!=null) TextButton({dispatch(UiAction.SignOut)}) {Text("Sign out")}
    }
}
