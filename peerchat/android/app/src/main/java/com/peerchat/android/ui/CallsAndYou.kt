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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.platform.LocalContext
import android.app.Activity
import android.os.Build
import android.Manifest
import com.peerchat.android.platform.NotificationPreview

@Composable internal fun CallsPage(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(20.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Text("Calls", style = MaterialTheme.typography.headlineSmall)
        Text(if(state.demo) "Local media UI previews · microphone and speakers are never opened." else "Voice and screen sharing are not implemented in Android messaging.", style = MaterialTheme.typography.bodySmall)
        Text("Voice rooms", style = MaterialTheme.typography.titleMedium)
        state.conversations.filter { it.voiceRooms.isNotEmpty() }.forEach { c ->
            OutlinedCard(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(c.name, style = MaterialTheme.typography.titleMedium)
                    c.voiceRooms.forEach { room -> Text("${room.name} · ${room.participants.size} in voice${if (room.joined) " · In Voice" else ""}") }
                    TextButton({ dispatch(UiAction.Select(c.id)); dispatch(UiAction.Navigate(Route.Details)) }) { Text("Open room previews") }
                }
            }
        }
        Text("Recent calls · samples", style = MaterialTheme.typography.titleMedium)
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
        OutlinedButton({ dispatch(UiAction.Navigate(Route.Diagnostics)) }) { Text("Application diagnostics") }
        Text("PeerChat · ${com.peerchat.android.BuildConfig.VERSION_NAME}", color = MaterialTheme.colorScheme.onSurfaceVariant)
        state.notice?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
        TextButton({ dispatch(UiAction.Navigate(Route.Welcome)) }) { Text("Return to welcome") }
        if(!state.demo && state.account!=null) TextButton({dispatch(UiAction.SignOut)}) {Text("Sign out")}
    }
}
