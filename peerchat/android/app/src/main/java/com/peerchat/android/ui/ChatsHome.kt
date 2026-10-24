package com.peerchat.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.tooling.preview.Preview

@OptIn(ExperimentalMaterial3Api::class)
@Composable internal fun Home(state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    var menu by remember { mutableStateOf(false) }
    var deleteConversation by remember(state.account?.accountId,state.attachmentContextGeneration) { mutableStateOf<ConversationUi?>(null) }
    var newGroup by remember {mutableStateOf("")}
    Scaffold(topBar = {
        TopAppBar(title = { Text("PeerChat") }, actions = {
            if (state.tab == Tab.Chats) {
                ActionIcon(Icons.Default.Search, "Search conversations", { dispatch(UiAction.ToggleSearch) })
                if(state.demo) Box {
                    ActionIcon(Icons.Default.MoreVert, "Preview states", { menu = true })
                    DropdownMenu(menu, { menu = false }) {
                        LoadState.entries.forEach { load -> DropdownMenuItem(text = { Text("Preview: ${load.name}") },
                            onClick = { menu = false; dispatch(UiAction.SetLoad(load)) }) }
                    }
                }
            }
        })
    }, floatingActionButton = {
        if(state.tab==Tab.Chats)FloatingActionButton({dispatch(UiAction.Contacts())},shape=CircleShape,
            modifier=Modifier.imePadding().testTag("chats-add")) {
            Icon(Icons.Default.Add,contentDescription="New chat, people and invitations")
        }
    }, bottomBar = {
        NavigationBar {
            Tab.entries.forEach { tab -> NavigationBarItem(selected = state.tab == tab,
                onClick = { dispatch(UiAction.SelectTab(tab)) }, label = { Text(tab.name) }, icon = {
                    val chatsBadgeCount=state.conversations.sumOf{it.unread}+state.incomingRequests.size+state.groupInvitations.size
                    BadgedBox(badge={if(tab==Tab.Chats&&chatsBadgeCount>0)Badge(containerColor=MaterialTheme.colorScheme.primary,contentColor=MaterialTheme.colorScheme.onPrimary){Text(chatsBadgeCount.toString())}}){
                        Icon(when (tab) { Tab.Chats -> Icons.Default.Email; Tab.Calls -> Icons.Default.Call; Tab.You -> Icons.Default.Person }, null)
                    }
                }) }
        }
    }) { padding ->
        Column(Modifier.fillMaxSize().padding(padding)) {
            if(state.demo) DemoNote()
            if (state.connection != ConnectionState.Online) Text(if(state.demo)"${state.connection.name} preview · no active server" else "${state.connection.name} · server presence not confirmed",
                Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.secondary)
            when (state.tab) {
                Tab.Chats -> {
                    if (state.searching) OutlinedTextField(state.query, { dispatch(UiAction.Search(it)) },
                        label = { Text("Search conversations") }, singleLine = true,
                        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).testTag("search"))
                    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth().padding(16.dp)) {
                        ChatKind.entries.forEachIndexed { index, kind -> SegmentedButton(
                            selected = state.kind == kind, onClick = { dispatch(UiAction.SelectKind(kind)) },
                            shape = SegmentedButtonDefaults.itemShape(index, 2)) { Text(if (kind == ChatKind.Direct) "DM" else "Groups") }
                        }
                    }
                    if(!state.demo&&state.kind==ChatKind.Group){
                        Row(Modifier.fillMaxWidth().padding(horizontal=16.dp),verticalAlignment=Alignment.CenterVertically){
                            OutlinedTextField(newGroup,{newGroup=it.take(80)},label={Text("Group name")},modifier=Modifier.weight(1f),singleLine=true)
                            TextButton({dispatch(UiAction.CreateGroup(newGroup));newGroup=""},enabled=newGroup.isNotBlank()&&state.connection==ConnectionState.Online){Text("Create")}
                        }
                        state.groupInvitations.forEach{invite->Row(Modifier.padding(horizontal=16.dp),verticalAlignment=Alignment.CenterVertically){
                            Text("Invitation from ${invite.inviter}",Modifier.weight(1f))
                            TextButton({dispatch(UiAction.RespondGroup(invite.id,true))}){Text("Accept")}
                            TextButton({dispatch(UiAction.RespondGroup(invite.id,false))}){Text("Decline")}
                        }}
                    }
                    when (state.load) {
                        LoadState.Loading -> StatusPanel("Loading conversations…", "Preview loading state") { CircularProgressIndicator() }
                        LoadState.Error -> StatusPanel("Couldn’t load conversations", "Preview error — no server request was sent.") {
                            Button({ dispatch(UiAction.SetLoad(LoadState.Ready)) }) { Text("Retry preview") }
                        }
                        LoadState.Empty -> StatusPanel("No conversations yet", "Your conversations will appear here.") {}
                        LoadState.Ready -> if (state.visible.isEmpty()) StatusPanel("No matches", "Try a different search.") {} else
                            LazyColumn(Modifier.fillMaxSize().testTag("conversation-list"),contentPadding=PaddingValues(bottom=88.dp)) {
                                items(state.visible, key = { it.id }) { c -> ConversationRow(c,{dispatch(UiAction.Select(c.id))},{if(!state.demo)deleteConversation=c}) }
                            }
                    }
                }
                Tab.Calls -> CallsPage(state, dispatch)
                Tab.You -> YouPage(state, dispatch)
            }
        }
    }
    deleteConversation?.let{conversation->
        val capturedAccount=state.account?.accountId?.removePrefix("account:")?.toLongOrNull()
        AlertDialog(onDismissRequest={deleteConversation=null},
        title={Text("Delete conversation history from this device?")},
        text={Text("Messages and files in ${conversation.name} will be removed from this conversation on this device. The chat remains. Saved files are kept, sending items continue, and this does not delete them for anyone else.")},
        confirmButton={TextButton({deleteConversation=null;dispatch(UiAction.DeleteLocalConversation(conversation.id,capturedAccount))},Modifier.testTag("confirm-delete-conversation")){Text("Delete from this device")}},
        dismissButton={TextButton({deleteConversation=null}){Text("Cancel")}})
    }
}

@Composable internal fun presenceColor(online: Boolean): Color =
    if (online) Color(0xFF4ADE80) else MaterialTheme.colorScheme.onSurfaceVariant

@Composable internal fun Avatar(name: String, group: Boolean = false, size: Int = 48) {
    Surface(shape = CircleShape, color = if (group) MaterialTheme.colorScheme.secondaryContainer else MaterialTheme.colorScheme.primaryContainer,
        modifier = Modifier.size(size.dp)) {
        Box(contentAlignment = Alignment.Center) {
            Text(name.trim().split(" ").filter { it.isNotEmpty() }.take(2).joinToString("") { it.take(1) },
                color = MaterialTheme.colorScheme.onPrimaryContainer, style = MaterialTheme.typography.titleMedium)
        }
    }
}
@OptIn(ExperimentalFoundationApi::class)
@Composable private fun ConversationRow(c: ConversationUi, open: () -> Unit,delete:()->Unit) {
    Row(Modifier.fillMaxWidth().combinedClickable(onClick = open,onLongClick=delete).testTag("conversation-row-${c.id}").padding(horizontal = 16.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
        Avatar(c.name, c.kind == ChatKind.Group)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
            Text(c.name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.titleMedium)
            Text(if (c.kind == ChatKind.Group) "Group · ${c.members.size} members · ${c.members.count { it.online }} online" else
                if (c.online) "● Online" else "Offline", style = MaterialTheme.typography.labelSmall, color = presenceColor(c.online))
            Text(c.preview, maxLines = 1, overflow = TextOverflow.Ellipsis, style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (!c.notifications) Text("Muted · preview", style = MaterialTheme.typography.labelSmall)
        }
        Column(horizontalAlignment = Alignment.End, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(c.time, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            if (c.unread > 0) Badge(containerColor=MaterialTheme.colorScheme.primary,contentColor=MaterialTheme.colorScheme.onPrimary) { Text(c.unread.toString()) }
            else if (c.messages.lastOrNull()?.outgoing == true) Text(when (c.messages.last().status) {
                MessageStatus.Failed -> "Failed"; MessageStatus.Read -> "✓✓"; MessageStatus.Delivered -> "✓";
                MessageStatus.Stored -> "Stored"; MessageStatus.Queued -> "Queued";
                MessageStatus.Preview -> "Local"; else -> "Sent"
            }, style = MaterialTheme.typography.labelSmall)
        }
    }
    HorizontalDivider(Modifier.padding(start = 76.dp, end = 16.dp), color = MaterialTheme.colorScheme.outlineVariant)
}
@Composable private fun StatusPanel(title: String, detail: String, extra: @Composable () -> Unit) {
    Column(Modifier.fillMaxSize().padding(24.dp), horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(16.dp, Alignment.CenterVertically)) {
        Text(title, style = MaterialTheme.typography.titleLarge)
        Text(detail, color = MaterialTheme.colorScheme.onSurfaceVariant)
        extra()
    }
}

@Preview(name = "Chats compact dark", widthDp = 320, heightDp = 640)
@Composable private fun ChatsPreview() { PeerChatTheme(true) { Home(PeerChatUiState(route = Route.Home, conversations = DemoConversations().initialSnapshot()), {}) } }
@Preview(name = "Groups light", widthDp = 411, heightDp = 891)
@Composable private fun GroupsPreview() { PeerChatTheme(false) { Home(PeerChatUiState(route = Route.Home, kind = ChatKind.Group, conversations = DemoConversations().initialSnapshot()), {}) } }
