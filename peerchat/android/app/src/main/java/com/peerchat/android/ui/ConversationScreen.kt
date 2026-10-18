package com.peerchat.android.ui

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.LazyListState
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
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview
import kotlinx.coroutines.delay
import com.peerchat.android.platform.AttachmentSendMode

internal data class AttachmentPickerBinding(val accountId:String,val conversationId:String,val contextGeneration:Long,
    val mode:AttachmentSendMode)

internal fun attachmentPickerAction(uri:android.net.Uri,binding:AttachmentPickerBinding?,state:PeerChatUiState):UiAction.SendAttachment? {
    if(binding==null||state.demo||state.route!=Route.Conversation||state.account?.accountId!=binding.accountId||
        state.selectedId!=binding.conversationId||state.attachmentContextGeneration!=binding.contextGeneration)return null
    return UiAction.SendAttachment(uri,binding.mode,binding.conversationId,binding.accountId,binding.contextGeneration)
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable internal fun ConversationScreen(c: ConversationUi, state: PeerChatUiState, dispatch: (UiAction) -> Unit) {
    var draft by rememberSaveable(state.account?.accountId,c.id) { mutableStateOf(state.drafts[c.id] ?: "") }
    val list = rememberSaveable(c.id,saver=LazyListState.Saver){LazyListState()}
    val timeline=remember(c.messages,c.attachments){conversationTimeline(c)}
    var followingLatest by remember(c.id){mutableStateOf(true)}
    var initialScroll by remember(c.id){mutableStateOf(true)}
    var positioning by remember(c.id){mutableStateOf(false)}
    var scrollRequest by remember(c.id){mutableIntStateOf(0)}
    val viewportHeight by remember(list){derivedStateOf{list.layoutInfo.viewportSize.height}}
    var attachmentMenu by remember(c.id){mutableStateOf(false)}
    // Save primitive launch fields so an Activity recreation during the system
    // picker cannot reinterpret the result using a later conversation closure.
    var pickerAccount by rememberSaveable{mutableStateOf<String?>(null)}
    var pickerConversation by rememberSaveable{mutableStateOf<String?>(null)}
    var pickerGeneration by rememberSaveable{mutableLongStateOf(-1L)}
    var pickerMode by rememberSaveable{mutableStateOf<String?>(null)}
    val picker=androidx.activity.compose.rememberLauncherForActivityResult(androidx.activity.result.contract.ActivityResultContracts.OpenDocument()){uri->
        val binding=if(pickerAccount!=null&&pickerConversation!=null&&pickerMode!=null)
            AttachmentPickerBinding(pickerAccount!!,pickerConversation!!,pickerGeneration,AttachmentSendMode.valueOf(pickerMode!!)) else null
        pickerAccount=null;pickerConversation=null;pickerMode=null;pickerGeneration=-1L
        if(uri!=null)attachmentPickerAction(uri,binding,state)?.let(dispatch)
    }
    LaunchedEffect(c.id,state.drafts[c.id]){if(!state.demo)draft=state.drafts[c.id] ?: ""}
    LaunchedEffect(c.id) {
        // Observe the anchored row, not total row count: appending/importing
        // messages must not reinterpret an unchanged viewport as user scrolling.
        snapshotFlow{list.layoutInfo.visibleItemsInfo.firstOrNull()?.key to list.firstVisibleItemScrollOffset}.collect {
            if(!initialScroll&&!positioning)followingLatest=conversationNearLatest(
                list.layoutInfo.visibleItemsInfo.lastOrNull()?.index,list.layoutInfo.totalItemsCount)
        }
    }
    LaunchedEffect(c.id,timeline.lastOrNull()?.key,timeline.size,viewportHeight,scrollRequest) {
        if(timeline.isNotEmpty()&&(initialScroll||followingLatest)) {
            val dateSeparators = timeline.indices.count { it == 0 || timeline[it - 1].date != timeline[it].date }
            positioning=true
            try{list.scrollToItem(timeline.size+dateSeparators);followingLatest=true;initialScroll=false}
            finally{positioning=false}
        }
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
                dispatch(if(c.kind==ChatKind.Group)UiAction.Navigate(Route.Details) else if(state.demo)UiAction.Notice("Calls are not connected in this preview.")
                    else if(!state.voiceAvailable)UiAction.Notice(state.voiceDiagnostic.take(160)) else UiAction.StartDmVoice(c.id))
            },enabled=c.kind==ChatKind.Group||state.demo||!state.voiceBusy)
            ActionIcon(Icons.Default.Info, "Conversation details", { dispatch(UiAction.Navigate(Route.Details)) })
        })
    }, bottomBar = {
        // The bottom bar owns its insets; Scaffold reserves this measured height.
        // Union uses the larger nav/IME inset, never adding both paddings.
        Column(Modifier.windowInsetsPadding(WindowInsets.navigationBars.union(WindowInsets.ime))) {
            if(state.demo)c.voiceRooms.find { it.joined }?.let { CallBar(it, dispatch) }
            c.shareRooms.find { it.watching }?.let { share -> Surface(color = MaterialTheme.colorScheme.secondaryContainer) {
                Row(Modifier.padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                    Text("Watching ${share.sharer} · UI only", Modifier.weight(1f), style = MaterialTheme.typography.labelSmall)
                    TextButton({ dispatch(UiAction.StopWatching(share.id)) }) { Text("Stop Watching") }
                }
            } }
            HorizontalDivider()
            Row(Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 8.dp), verticalAlignment = Alignment.Bottom) {
                if(c.kind==ChatKind.Direct)Box {
                    ActionIcon(Icons.Default.Add, "Send picture or file", { if(!state.demo)attachmentMenu=true })
                    DropdownMenu(attachmentMenu,{attachmentMenu=false}) {
                        listOf(AttachmentSendMode.Direct,AttachmentSendMode.Cached).forEach{mode->
                            DropdownMenuItem(text={Text((if(mode==AttachmentSendMode.Direct)"Direct transfer" else "Cached transfer")+
                                (if(mode==state.attachmentSendMode)" · default" else ""))},onClick={
                                attachmentMenu=false
                                val account=state.account?.accountId
                                if(account!=null&&state.route==Route.Conversation&&state.selectedId==c.id){
                                    pickerAccount=account;pickerConversation=c.id;pickerGeneration=state.attachmentContextGeneration;pickerMode=mode.name
                                    picker.launch(arrayOf("*/*"))
                                }
                            },modifier=Modifier.testTag("send-${mode.name.lowercase()}"))
                        }
                    }
                }
                OutlinedTextField(draft, { if (it.toByteArray(Charsets.UTF_8).size <= if(c.kind==ChatKind.Group)4096 else 512*1024){draft=it;if(!state.demo)dispatch(UiAction.Draft(c.id,it))} }, placeholder = { Text("Message ${c.name}", maxLines = 1, overflow = TextOverflow.Ellipsis) },
                    enabled = (state.demo&&state.connection==ConnectionState.Online||!state.demo&&state.account!=null), minLines = 1, maxLines = 5,
                    modifier = Modifier.weight(1f).testTag("composer"), shape = MaterialTheme.shapes.large)
                IconButton({
                    val captured=draft
                    if(captured.isNotBlank()){
                        followingLatest=true;scrollRequest++;draft=""
                        dispatch(UiAction.PreviewMessage(captured))
                    }
                }, enabled = draft.isNotBlank() && state.localSendCount<16 && (state.demo&&state.connection==ConnectionState.Online||!state.demo&&state.account!=null)) {
                    Icon(Icons.AutoMirrored.Filled.Send, if(state.demo) "Send local preview" else "Send message", tint = if (draft.isNotBlank()) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
        }
    }) { padding ->
        Column(Modifier.fillMaxSize().padding(padding).consumeWindowInsets(padding)) {
            if(state.demo) DemoNote()
            if(!state.demo&&state.voiceBusy)Text("Preparing call or waiting for microphone permission…",Modifier.fillMaxWidth().padding(12.dp).testTag("dm-voice-progress"),style=MaterialTheme.typography.bodySmall)
            if(!state.demo)state.voiceError?.let{Text(it,Modifier.fillMaxWidth().padding(12.dp).testTag("dm-voice-error"),color=MaterialTheme.colorScheme.error,style=MaterialTheme.typography.bodySmall)}
            if (state.connection != ConnectionState.Online) Surface(color = MaterialTheme.colorScheme.secondaryContainer) {
                Text(if(state.demo) "Offline preview · sending disabled" else "${if(state.connection==ConnectionState.Offline) "Offline" else "Reconnecting"} · messages queue locally until delivery is available",
                    Modifier.fillMaxWidth().padding(12.dp), style = MaterialTheme.typography.bodySmall)
            }
            state.notice?.let { notice -> Row(Modifier.padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(notice, Modifier.weight(1f), style = MaterialTheme.typography.bodySmall)
                ActionIcon(Icons.Default.Close, "Dismiss", { dispatch(UiAction.Notice(null)) })
            } }
            if (c.kind == ChatKind.Group && c.voiceRooms.isNotEmpty()) TextButton({ dispatch(UiAction.Navigate(Route.Details)) }, Modifier.fillMaxWidth()) {
                Text("Voice rooms · ${c.voiceRooms.sumOf { it.participants.size }} in voice${if(state.demo)" (sample)" else ""}")
            }
            c.shareRooms.firstOrNull()?.let { share -> TextButton({ dispatch(UiAction.Navigate(Route.Details)) }, Modifier.fillMaxWidth()) {
                Text("${share.sharer} is sharing · ${if (share.watching) "Watching" else "Watch"} (preview)")
            } }
            if(!followingLatest)TextButton({followingLatest=true;scrollRequest++},Modifier.align(Alignment.CenterHorizontally).testTag("jump-to-latest")){
                Icon(Icons.Default.KeyboardArrowDown,contentDescription=null)
                Text("Jump to latest")
            }
            LazyColumn(Modifier.weight(1f).fillMaxWidth().testTag("messages"), state = list, contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)) {
                item(key = "older") { if (c.hasOlder) TextButton({ dispatch(UiAction.BeginOlder) }, enabled = !state.olderLoading, modifier = Modifier.fillMaxWidth()) {
                    Text(if(state.demo) {if (state.olderLoading) "Loading older preview…" else "Load older preview messages"} else if (state.olderLoading) "Loading older messages…" else "Load older messages")
                } }
                timeline.forEachIndexed { index, row ->
                    if (index == 0 || timeline[index - 1].date != row.date) item(key = "date-${row.key}") {
                        Box(Modifier.fillMaxWidth().padding(8.dp), contentAlignment = Alignment.Center) {
                            Text(row.date, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                    item(key = row.key) { when(row){
                        is ConversationTimelineEntry.Text -> MessageBubble(row.message,c.kind==ChatKind.Group&&
                            (index==0||(timeline[index-1] as? ConversationTimelineEntry.Text)?.message?.senderId!=row.message.senderId),dispatch)
                        is ConversationTimelineEntry.File -> AttachmentCard(row.attachment,dispatch,state.attachmentPreviews[row.attachment.id])
                    } }
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
    var expanded by rememberSaveable(m.id){mutableStateOf(false)}
    Row(Modifier.fillMaxWidth(), horizontalArrangement = if (m.outgoing) Arrangement.End else Arrangement.Start) {
        Surface(color = if (m.outgoing) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceVariant,
            shape = MaterialTheme.shapes.large, modifier = Modifier.widthIn(max = 330.dp)
                .toggleable(value=expanded,role=Role.Button,onValueChange={expanded=it})
                .semantics { contentDescription=if(expanded)"Collapse message details" else "Expand message details";stateDescription=if(expanded)"Expanded" else "Collapsed" }
                .testTag("message-bubble-${m.id}")) {
            Column(Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                if (sender && !m.outgoing) Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Avatar(m.senderName, size = 28)
                    Text(m.senderName, style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                }
                if(needsCompactMessage(m.text))CompactLongMessage(m.text)
                else SelectionContainer { Text(linkedText(m.text)) }
                Row(verticalAlignment=Alignment.CenterVertically,horizontalArrangement=Arrangement.spacedBy(2.dp)) {
                    Text("${m.time}" + compactMessageDelivery(m), style = MaterialTheme.typography.labelSmall,
                        color = if (m.status == MessageStatus.Failed) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
                    Icon(if(expanded)Icons.Default.KeyboardArrowUp else Icons.Default.KeyboardArrowDown,
                        contentDescription=null,modifier=Modifier.size(18.dp),tint=MaterialTheme.colorScheme.onSurfaceVariant)
                }
                if(expanded){
                    Text("Collapse details",style=MaterialTheme.typography.labelSmall,color=MaterialTheme.colorScheme.primary,
                        modifier=Modifier.testTag("message-details-toggle-${m.id}"))
                    Text("Aggregate status: ${messageStatusDetail(m.status)}",style=MaterialTheme.typography.labelSmall)
                    if(m.deliveryDetail.isNotEmpty())Text(m.deliveryDetail,style=MaterialTheme.typography.labelSmall)
                    if(m.outgoing)m.deliveryTargets?.let{DeliveryTargets(it)}
                }
                if (m.status == MessageStatus.Failed||m.retryAvailable) TextButton({ dispatch(UiAction.RetryPreview(m.id)) }) { Text(if(m.retryAvailable)"Retry encrypted message" else "Retry locally") }
            }
        }
    }
}
@Composable private fun DeliveryTargets(delivery:DeliveryTargetsUi) {
    Text(delivery.summary,style=MaterialTheme.typography.labelSmall)
    if(delivery.selectionUnresolved)Text("Device selection unresolved",style=MaterialTheme.typography.labelSmall)
    if(delivery.targets.isNotEmpty()) {
        SelectionContainer {
            Column(verticalArrangement=Arrangement.spacedBy(8.dp)) {
                delivery.targets.forEach{target->
                    Text((target.displayName.takeIf{it.isNotEmpty()}?.let{"$it · "}?:"")+"${target.status.name}\n${target.deviceId}",style=MaterialTheme.typography.bodySmall)
                }
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
