package com.peerchat.android.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import com.peerchat.android.platform.NotificationPreview
import com.peerchat.android.application.ArchiveEnrollmentSnapshot
import com.peerchat.android.application.ArchiveDeviceAction

enum class Route { Welcome, Login, Register, Server, Home, Conversation, Details, Diagnostics, Contacts, TransferSettings }
enum class ContactPage { Menu, FindPeople, Incoming, Outgoing, Invitations, NewChat }
enum class Tab { Chats, Calls, You }
enum class ChatKind { Direct, Group }
enum class LoadState { Ready, Loading, Empty, Error }
enum class ConnectionState { Online, Offline, Reconnecting }
enum class MessageStatus { Received, Queued, Sent, Stored, Delivered, Read, Failed, Preview, Saving }
enum class ThemeChoice { System, Dark, Light }
data class MemberUi(val accountId: String, val name: String, val online: Boolean, val role: String = "Member")
data class MessageUi(
    val id: String, val senderId: String, val senderName: String, val text: String,
    val outgoing: Boolean, val time: String, val date: String = "Today",
    val status: MessageStatus = MessageStatus.Received,
    val timestamp:Long=0,val logicalOrder:Long=0,val retryAvailable:Boolean=false,val deliveryDetail:String=""
)
data class VoiceRoomUi(val id: String, val name: String, val participants: List<String>, val joined: Boolean = false,
    val muted: Boolean = false, val deafened: Boolean = false)
data class ShareRoomUi(val id: String, val name: String, val sharer: String, val viewers: Int, val watching: Boolean = false)
data class MemberAudioUi(val accountId: String, val volume: Float = 1f, val muted: Boolean = false)
data class CallUi(val id: String, val name: String, val detail: String, val missed: Boolean = false)
data class ConversationUi(
    val id: String, val name: String, val kind: ChatKind, val online: Boolean,
    val preview: String, val time: String, val unread: Int = 0,
    val members: List<MemberUi> = emptyList(), val messages: List<MessageUi> = emptyList(),
    val voiceRooms: List<VoiceRoomUi> = emptyList(), val shareRooms: List<ShareRoomUi> = emptyList(),
    val hasOlder: Boolean = true, val notifications: Boolean = true,
    val memberAudio: List<MemberAudioUi> = emptyList(),
    val attachments:List<com.peerchat.android.application.AttachmentSnapshot> = emptyList()
)
data class ServerUi(val host: String = "", val port: String = "7443", val certificatePin: String = "")
data class ServerTrustUi(val server: ServerUi, val observed: String, val expected: String = "")
fun validServerDraft(server: ServerUi): Boolean = server.host.trim().let { host ->
    host.isNotEmpty() && host.length <= 253 && host.none { it.isWhitespace() || it == '/' || it == '\\' } &&
        server.port.toIntOrNull() in 1..65535 && (server.certificatePin.isEmpty() || server.certificatePin.matches(Regex("[0-9a-fA-F]{64}")))
}
data class AccountUi(val accountId: String, val username: String)
sealed interface LoginResult {
    data object Unavailable : LoginResult
    data object Rejected : LoginResult
    data object ServerUnreachable : LoginResult
    data class Authenticated(val account: AccountUi) : LoginResult
}
data class PeerChatUiState(
    val demo: Boolean = true, val diagnostics: String = "UI demo — no live diagnostics",
    val voiceAvailable:Boolean=false,val voiceDiagnostic:String="Voice calls are unavailable in this build.",
    val sendBusy: Boolean = false, val sendCompletion: Long = 0, val sendAccepted: Boolean = false,val localSendCount:Int=0,
    val incomingRequests:List<MemberUi> = emptyList(),
    val outgoingRequests:List<MemberUi> = emptyList(),
    val contactPage:ContactPage=ContactPage.Menu,
    val peopleQuery:String="",val peopleResults:List<com.peerchat.android.application.DirectoryAccountSnapshot> = emptyList(),
    val peopleLoading:Boolean=false,val peopleError:String?=null,val contactBusy:Boolean=false,
    val transferSettings:com.peerchat.android.application.TransferSettingsSnapshot?=null,
    val transferSettingsBusy:Boolean=false,val transferSettingsError:String?=null,
    val groupInvitations:List<com.peerchat.android.application.GroupInvitationSnapshot> = emptyList(),
    val route: Route = Route.Welcome, val tab: Tab = Tab.Chats, val kind: ChatKind = ChatKind.Direct,
    val conversations: List<ConversationUi> = emptyList(), val selectedId: String? = null,
    val query: String = "", val searching: Boolean = false,
    val load: LoadState = LoadState.Ready, val connection: ConnectionState = ConnectionState.Online,
    val server: ServerUi = ServerUi(), val loginBusy: Boolean = false, val loginError: String? = null,
    val rememberMe:Boolean=false,
    val deviceVerification:com.peerchat.android.application.DeviceVerification?=null,
    val mailboxReenrollment:com.peerchat.android.application.MailboxReenrollment?=null,
    val mailboxRecoveryError:String?=null,val mailboxRecoveryBusy:Boolean=false,
    val serverTrust:ServerTrustUi?=null,
    val serverIdentityChanged:ServerUi?=null,
    val startupStatus:String?=null,
    val drafts:Map<String,String> = emptyMap(),val notificationsEnabled:Boolean=true,
    val notificationPreview:NotificationPreview=NotificationPreview.SenderOnly,
    val mailboxStorageAllowed:Boolean=false,
    val archiveEnrollment:ArchiveEnrollmentSnapshot?=null,val archiveBusy:Boolean=false,val archiveProgress:com.peerchat.android.application.ArchiveSyncProgress=com.peerchat.android.application.ArchiveSyncProgress(),
    val attachmentPreviews:Map<String,String> = emptyMap(),
    val notice: String? = null, val serverReturn: Route = Route.Welcome, val account: AccountUi? = null,
    val diagnosticsReturn: Route = Route.Details,
    val theme: ThemeChoice = ThemeChoice.System, val incomingCallPreview: Boolean = false,
    val olderLoading: Boolean = false,
    val presentationDiagnostics:String="",
    val recentCalls: List<CallUi> = listOf(CallUi("sample-missed", "Alex", "Missed call · Yesterday", true),
        CallUi("sample-call", "Sammy", "Outgoing · Monday, 14:10"))
) {
    val selected: ConversationUi? get() = conversations.find { it.id == selectedId }
    val visible: List<ConversationUi> get() = conversations.filter {
        it.kind == kind && (it.name.contains(query, ignoreCase = true) || it.preview.contains(query, ignoreCase = true))
    }
}

/** Presentation snapshots only: a real implementation will project native/application state. */
interface ConversationSource { fun initialSnapshot(): List<ConversationUi> }
interface AccountAccess { suspend fun signIn(username: String, password: String, server: ServerUi): LoginResult }
class UnconnectedAccountAccess : AccountAccess {
    override suspend fun signIn(username: String, password: String, server: ServerUi): LoginResult {
        // No credentials retained, written, sent or accepted. Never simulates authentication success.
        delay(150)
        return LoginResult.Unavailable
    }
}

sealed interface UiAction {
    data object OpenTransferSettings:UiAction
    data class SaveTransferSettings(val value:com.peerchat.android.application.TransferSettingsSnapshot):UiAction
    data class Contacts(val page:ContactPage=ContactPage.Menu):UiAction
    data class PeopleQuery(val text:String):UiAction
    data object RefreshContacts:UiAction
    data class CancelFriend(val accountId:Long):UiAction
    data class CreateGroup(val name:String):UiAction
    data class InviteGroup(val group:String,val account:Long):UiAction
    data class RespondGroup(val id:String,val accept:Boolean):UiAction
    data object Explore : UiAction
    data object SignOut : UiAction
    data object ResetNetworkSession : UiAction
    data object DisconnectChangeServer : UiAction
    data class RequestFriend(val username:String):UiAction
    data class RespondFriend(val accountId:Long,val accept:Boolean):UiAction
    data class Navigate(val route: Route) : UiAction
    data object Back : UiAction
    data class Select(val id: String) : UiAction
    data class SelectTab(val tab: Tab) : UiAction
    data class SelectKind(val kind: ChatKind) : UiAction
    data class Search(val text: String) : UiAction
    data object ToggleSearch : UiAction
    data class SetLoad(val load: LoadState) : UiAction
    data class SetConnection(val connection: ConnectionState) : UiAction
    data class SaveServer(val server: ServerUi) : UiAction
    data object TrustServer : UiAction
    data object CancelServerTrust : UiAction
    data class RememberMe(val enabled:Boolean):UiAction
    data class Draft(val conversationId:String,val text:String):UiAction
    data class NotificationSettings(val enabled:Boolean,val preview:NotificationPreview):UiAction
    data class MailboxStorage(val allowed:Boolean):UiAction
    data object ArchiveRefresh:UiAction
    data class ArchiveSynchronize(val context:ArchiveEnrollmentSnapshot,val source:String?=null,val pause:Boolean=false):UiAction
    data class HistorySync(val context:ArchiveEnrollmentSnapshot,val enabled:Boolean):UiAction
    data class ArchiveConsent(val context:ArchiveEnrollmentSnapshot,val action:ArchiveDeviceAction,val device:String,val read:Boolean=false,val serve:Boolean=false):UiAction
    data class PreviewMessage(val text: String) : UiAction
    data class SendAttachment(val uri:android.net.Uri):UiAction
    data class AttachmentAction(val id:String,val accept:Boolean):UiAction
    data class AttachmentPreview(val id:String):UiAction
    data object Older : UiAction
    data object ToggleNotifications : UiAction
    data class Notice(val text: String?) : UiAction
    data class SetTheme(val theme: ThemeChoice) : UiAction
    data class PreviewLoginFailure(val failure: LoginResult) : UiAction
    data class VerifyDevice(val value:com.peerchat.android.application.DeviceVerification,val accept:Boolean):UiAction
    data class ReenrollMailbox(val value:com.peerchat.android.application.MailboxReenrollment,val accept:Boolean):UiAction
    data object PreviewIncomingCall : UiAction
    data class JoinVoice(val roomId: String) : UiAction
    data class LeaveVoice(val roomId: String) : UiAction
    data class ToggleMute(val roomId: String) : UiAction
    data class ToggleDeafen(val roomId: String) : UiAction
    data class WatchShare(val roomId: String) : UiAction
    data class StopWatching(val roomId: String) : UiAction
    data class MemberAudio(val config: MemberAudioUi) : UiAction
    data class RetryPreview(val messageId: String) : UiAction
    data object BeginOlder : UiAction
}

/** Pure UI reducer, not a protocol, queue, database, identity or delivery engine. */
fun reduceUi(state: PeerChatUiState, action: UiAction): PeerChatUiState {
    fun updateSelected(transform: (ConversationUi) -> ConversationUi) = state.copy(conversations =
        state.conversations.map { if (it.id == state.selectedId) transform(it) else it })
    return when (action) {
        UiAction.OpenTransferSettings -> state.copy(route=Route.TransferSettings,notice=null)
        is UiAction.SaveTransferSettings -> state
        is UiAction.Contacts -> state.copy(route=Route.Contacts,contactPage=action.page,notice=null)
        is UiAction.PeopleQuery -> state.copy(peopleQuery=action.text.take(32))
        UiAction.RefreshContacts,is UiAction.CancelFriend -> state
        is UiAction.CreateGroup,is UiAction.InviteGroup,is UiAction.RespondGroup -> state
        UiAction.Explore -> state.copy(route = Route.Home, notice = null)
        UiAction.ResetNetworkSession -> state.copy(route=Route.Login,startupStatus=null,loginBusy=false,connection=ConnectionState.Offline)
        UiAction.DisconnectChangeServer -> state.copy(route=Route.Server,startupStatus=null,loginBusy=false,connection=ConnectionState.Offline)
        UiAction.SignOut -> state.copy(account=null,route=Route.Welcome)
        is UiAction.RequestFriend,is UiAction.RespondFriend -> state.copy(notice="Friend management is not connected in demo mode.")
        is UiAction.Navigate -> state.copy(route = action.route, notice = null, loginError = null,
            serverReturn = if (action.route == Route.Server) state.route else state.serverReturn,
            diagnosticsReturn = if (action.route == Route.Diagnostics) state.route else state.diagnosticsReturn)
        UiAction.Back -> if(state.route==Route.Contacts&&state.contactPage!=ContactPage.Menu)state.copy(contactPage=ContactPage.Menu) else if (state.route == Route.Home && state.tab != Tab.Chats) state.copy(tab = Tab.Chats) else state.copy(route = when (state.route) {
            Route.Contacts -> Route.Home
            Route.TransferSettings -> Route.Home
            Route.Conversation -> Route.Home
            Route.Details -> Route.Conversation
            Route.Diagnostics -> state.diagnosticsReturn
            Route.Login -> Route.Welcome
            Route.Register -> Route.Login
            Route.Server -> state.serverReturn
            else -> Route.Welcome
        }, notice = null)
        is UiAction.Select -> if (state.conversations.none { it.id == action.id }) state else
            state.copy(selectedId = action.id, route = Route.Conversation, notice = null, olderLoading = false,
                conversations = state.conversations.map { if (it.id == action.id) it.copy(unread = 0) else it })
        is UiAction.SelectTab -> state.copy(tab = action.tab, notice = null)
        is UiAction.SelectKind -> state.copy(kind = action.kind)
        is UiAction.Search -> state.copy(query = action.text.take(256))
        UiAction.ToggleSearch -> state.copy(searching = !state.searching, query = "")
        is UiAction.SetLoad -> state.copy(load = action.load)
        is UiAction.SetConnection -> state.copy(connection = action.connection)
        is UiAction.SaveServer -> state.copy(server = action.server)
        UiAction.TrustServer -> state // Only the real presenter may persist trust.
        UiAction.CancelServerTrust -> state.copy(serverTrust=null)
        is UiAction.RememberMe -> state.copy(rememberMe=action.enabled)
        is UiAction.Draft -> if(action.text.toByteArray().size<=512*1024)state.copy(drafts=state.drafts+(action.conversationId to action.text)) else state
        is UiAction.NotificationSettings -> state.copy(notificationsEnabled=action.enabled,notificationPreview=action.preview)
        is UiAction.MailboxStorage -> state.copy(mailboxStorageAllowed=action.allowed)
        UiAction.ArchiveRefresh, is UiAction.ArchiveConsent, is UiAction.HistorySync, is UiAction.ArchiveSynchronize,
        is UiAction.SendAttachment,is UiAction.AttachmentAction,is UiAction.AttachmentPreview -> state // Real application commands only, never demo trust.
        is UiAction.PreviewMessage -> if (state.selected == null || state.connection != ConnectionState.Online ||
            action.text.isBlank() || action.text.length > 4096) state else updateSelected { c ->
                c.copy(preview = action.text, messages = c.messages + MessageUi(
                    "preview-${c.messages.size}", "demo-local", "You", action.text, true, "Now", status = MessageStatus.Preview))
            }
        UiAction.Older -> updateSelected { c -> if (!c.hasOlder) c else c.copy(hasOlder = false,
            messages = listOf(MessageUi("older-${c.id}", "demo-peer", c.name,
                "An older local preview message. No history was downloaded.", false, "09:20", "Yesterday")) + c.messages) }
        UiAction.ToggleNotifications -> updateSelected { it.copy(notifications = !it.notifications) }
        is UiAction.Notice -> state.copy(notice = action.text)
        is UiAction.SetTheme -> state.copy(theme = action.theme)
        is UiAction.PreviewLoginFailure -> state.copy(loginError = "Preview only: " + loginFailureText(action.failure))
        is UiAction.VerifyDevice,is UiAction.ReenrollMailbox -> state
        UiAction.PreviewIncomingCall -> state.copy(incomingCallPreview = !state.incomingCallPreview)
        // These commands exercise local UI fixtures ONLY. A real presenter delegates to application owners.
        is UiAction.JoinVoice -> updateSelected { c -> c.copy(voiceRooms = c.voiceRooms.map { room ->
            if (room.id == action.roomId) room.copy(joined = true, participants = (room.participants + "You").distinct())
            else if (room.joined) room.copy(joined = false, participants = room.participants.filter { it != "You" }) else room
        }) }
        is UiAction.LeaveVoice -> updateSelected { c -> c.copy(voiceRooms = c.voiceRooms.map {
            if (it.id == action.roomId) it.copy(joined = false, participants = it.participants.filter { name -> name != "You" }) else it }) }
        is UiAction.ToggleMute -> updateSelected { c -> c.copy(voiceRooms = c.voiceRooms.map {
            if (it.id == action.roomId && it.joined) it.copy(muted = !it.muted) else it }) }
        is UiAction.ToggleDeafen -> updateSelected { c -> c.copy(voiceRooms = c.voiceRooms.map {
            if (it.id == action.roomId && it.joined) it.copy(deafened = !it.deafened) else it }) }
        is UiAction.WatchShare -> updateSelected { c -> c.copy(shareRooms = c.shareRooms.map {
            if (it.id == action.roomId && !it.watching) it.copy(watching = true, viewers = it.viewers + 1) else it }) }
        is UiAction.StopWatching -> updateSelected { c -> c.copy(shareRooms = c.shareRooms.map {
            if (it.id == action.roomId && it.watching) it.copy(watching = false, viewers = (it.viewers - 1).coerceAtLeast(0)) else it }) }
        is UiAction.MemberAudio -> updateSelected { c -> if (!action.config.volume.isFinite() || c.members.none { it.accountId == action.config.accountId }) c else
            c.copy(memberAudio = c.memberAudio.filter { it.accountId != action.config.accountId } + action.config.copy(volume = action.config.volume.coerceIn(0f, 2f))) }
        is UiAction.RetryPreview -> updateSelected { c -> c.copy(messages = c.messages.map {
            if (it.id == action.messageId && it.status == MessageStatus.Failed) it.copy(status = MessageStatus.Preview) else it }) }
        UiAction.BeginOlder -> state.copy(olderLoading = state.selected?.hasOlder == true)
    }
}

fun loginFailureText(result: LoginResult): String = when (result) {
    LoginResult.Unavailable -> "Sign-in is unavailable: the secure account backend is not connected. Use Explore UI demo."
    LoginResult.Rejected -> "Sign-in was not accepted. Check your username and password."
    LoginResult.ServerUnreachable -> "The server couldn’t be reached. Check your connection and server settings."
    is LoginResult.Authenticated -> "No authentication success is simulated."
}

interface PeerChatPresenter {
    val state: StateFlow<PeerChatUiState>
    fun dispatch(action: UiAction)
    fun login(username: String, password: String)
    fun rendered(conversation:String?,messageIds:Set<String>) {}
}
class PeerChatUiModel(
    source: ConversationSource = DemoConversations(), private val accounts: AccountAccess = UnconnectedAccountAccess()
) : ViewModel(), PeerChatPresenter {
    private val mutable = MutableStateFlow(PeerChatUiState(conversations = source.initialSnapshot()))
    override val state: StateFlow<PeerChatUiState> = mutable.asStateFlow()
    override fun dispatch(action: UiAction) {
        mutable.value = reduceUi(mutable.value, action)
        if (action == UiAction.BeginOlder) {
            val conversationId = mutable.value.selectedId
            viewModelScope.launch {
                delay(150)
                if (conversationId == mutable.value.selectedId) mutable.value = reduceUi(mutable.value, UiAction.Older).copy(olderLoading = false)
            }
        }
    }
    override fun login(username: String, password: String) {
        if(mutable.value.route==Route.Register){mutable.value=mutable.value.copy(loginError="Account creation is unavailable in UI demo mode.");return}
        if (mutable.value.loginBusy) return
        if (username.isBlank() || password.isEmpty()) {
            mutable.value = mutable.value.copy(loginError = "Enter your username and password.")
            return
        }
        if (username.length > 64 || password.length > 1024) {
            mutable.value = mutable.value.copy(loginError = "One of the fields is too long.")
            return
        }
        mutable.value = mutable.value.copy(loginBusy = true, loginError = null)
        val connectionDraft = mutable.value.server
        viewModelScope.launch {
            val result = try { accounts.signIn(username, password, connectionDraft) }
                catch (cancelled: CancellationException) { throw cancelled }
                catch (_: Exception) { LoginResult.Unavailable }
            mutable.value = when (result) {
                is LoginResult.Authenticated -> mutable.value.copy(loginBusy = false, loginError = null,
                    account = result.account, route = Route.Home)
                else -> mutable.value.copy(loginBusy = false, loginError = loginFailureText(result))
            }
        }
    }
}
