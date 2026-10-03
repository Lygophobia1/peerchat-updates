package com.peerchat.android.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.peerchat.android.application.*
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.ServerRejected
import com.peerchat.android.platform.ServerConfigurationRepository
import com.peerchat.android.platform.AndroidServerConfigurationStore
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Immutable presentation only. Native Profile/Storage and session/delivery owners
 * remain authoritative. This adapter never constructs peer frames or delivery ACKs. */
internal fun projectMessaging(snapshot: MessagingSnapshot, previous: PeerChatUiState): PeerChatUiState {
    val conversations=snapshot.conversations.map { c ->
        val id="account:${c.friend.accountId}"
        val messages=c.messages.map {m ->
            val outgoing=m.senderDevice==snapshot.localPeerId
            MessageUi(m.id,m.senderDevice,if(outgoing)snapshot.username else c.friend.username,m.text,outgoing,
                SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(m.timestamp)),
                date=SimpleDateFormat("yyyy-MM-dd",Locale.getDefault()).format(Date(m.timestamp)),
                status=if(!outgoing)MessageStatus.Received else when(m.delivery){2L->MessageStatus.Delivered;1L->MessageStatus.Sent;else->MessageStatus.Queued})
        }
        ConversationUi(id,c.friend.username,ChatKind.Direct,c.friend.online,messages.lastOrNull()?.text ?: "",
            messages.lastOrNull()?.time ?: "",messages=messages,hasOlder=c.messages.size==8)
    }
    // Diagnostics are safe owner metadata, never messages, credentials or keys.
    val diagnostics=buildString {
        appendLine(snapshot.status)
        appendLine("Authenticated=${snapshot.authenticated} Server presence live=${snapshot.serverPresenceLive}")
        appendLine("Profile identity: ${snapshot.localPeerId}")
        for(c in snapshot.conversations) {
            appendLine("${c.friend.username}:${c.friend.accountId} · Server Relay")
            appendLine("Ready=${c.connection.ready} Pending=${c.connection.pending} Retrying=${c.connection.retrying}")
            appendLine("Network revision=${c.connection.networkRevision} Retry delay=${c.connection.retryDelayMs} ms")
            appendLine("Failure=${c.connection.failure}")
        }
    }
    return previous.copy(demo=false,conversations=conversations,diagnostics=diagnostics,
        incomingRequests=snapshot.incomingRequests.map {MemberUi("account:${it.accountId}",it.username,false)},
        account=if(snapshot.authenticated)AccountUi("account:${snapshot.accountId}",snapshot.username) else null,
        connection=when {snapshot.authenticated&&snapshot.serverPresenceLive->ConnectionState.Online
            snapshot.authenticated->ConnectionState.Reconnecting
            else->ConnectionState.Offline},
        load=if(conversations.isEmpty())LoadState.Empty else LoadState.Ready,recentCalls=emptyList())
}

class NativePresenter(application: Application,private val backend:MessagingApplication,
    private val serverConfiguration:ServerConfigurationRepository=AndroidServerConfigurationStore(application),
    private val configurationDispatcher:CoroutineDispatcher=Dispatchers.IO):AndroidViewModel(application),PeerChatPresenter {
    constructor(application:Application):this(application,MessagingBackend(application))
    private val mutable=MutableStateFlow(PeerChatUiState(demo=false,connection=ConnectionState.Offline,recentCalls=emptyList()))
    override val state:StateFlow<PeerChatUiState> = mutable.asStateFlow()
    private var current=MessagingSnapshot()
    private var configurationReady=false
    private var configurationInvalid=false
    private var configurationSaving=false
    // View-only older pages, always fetched from native Storage. Never an outbox.
    private val older=mutableMapOf<Long,List<MessageSnapshot>>()
    private val exhausted=mutableSetOf<Long>()
    private fun project() {
        val combined=current.copy(conversations=current.conversations.map {c ->
            c.copy(messages=((older[c.friend.accountId] ?: emptyList())+c.messages)
                .associateBy {it.id}.values.sortedBy {it.sequence})
        })
        // projectMessaging's paging hint is overridden by the actual native page result.
        mutable.value=projectMessaging(combined,mutable.value).let {s->s.copy(conversations=s.conversations.map {c->
            c.copy(hasOlder=c.id.removePrefix("account:").toLongOrNull() !in exhausted)
        })}
    }
    init {viewModelScope.launch {
        try {
            val saved=withContext(configurationDispatcher){serverConfiguration.load()}
            if(saved!=null)mutable.value=mutable.value.copy(server=ServerUi(saved.host,saved.port.toString(),saved.certificatePin))
        }catch(_:Exception){
            configurationInvalid=true
            mutable.value=mutable.value.copy(notice="Saved server configuration could not be verified. Re-enter trusted server settings before signing in.")
        }finally{configurationReady=true}
        backend.state.collect {snapshot ->
        if(snapshot.accountId!=current.accountId){older.clear();exhausted.clear()}
        current=snapshot;project()
    }}}
    override fun login(username:String,password:String) {
        val s=mutable.value
        if(s.loginBusy)return
        if(!configurationReady||configurationInvalid||configurationSaving){mutable.value=s.copy(loginError="Verify and save trusted server settings before signing in.");return}
        if(username.isBlank()||password.isEmpty()||username.length>64||password.length>1024) {
            mutable.value=s.copy(loginError="Enter your username and password.");return
        }
        if(!validServerDraft(s.server)) {mutable.value=s.copy(loginError="Configure a valid server first.");return}
        mutable.value=s.copy(loginBusy=true,loginError=null)
        viewModelScope.launch {
            try {
                backend.signIn(ServerEndpoint(s.server.host.trim(),s.server.port.toInt(),s.server.certificatePin.lowercase()),username,password)
                mutable.value=mutable.value.copy(loginBusy=false,loginError=null,route=Route.Home)
            } catch(failure:Exception) {
                mutable.value=mutable.value.copy(loginBusy=false,loginError=if(failure is ServerRejected)
                    "Sign-in rejected by server." else "Sign-in failed; check server and certificate configuration.")
            }
        }
    }
    override fun dispatch(action:UiAction) {
        when(action) {
            is UiAction.RequestFriend -> viewModelScope.launch {
                try {backend.requestFriend(action.username);mutable.value=mutable.value.copy(notice="Friend request sent through authenticated server.")}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Friend request was not accepted by server.")}
            }
            is UiAction.RespondFriend -> viewModelScope.launch {
                try {backend.respondFriend(action.accountId,action.accept)}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Friend response failed; authorization unchanged.")}
            }
            is UiAction.PreviewMessage -> {
                if(mutable.value.sendBusy)return
                val c=current.conversations.find {"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                if(action.text.isBlank()||action.text.length>4096)return
                mutable.value=mutable.value.copy(sendBusy=true,sendAccepted=false)
                viewModelScope.launch {
                    var accepted=false
                    try {backend.send(c.friend.peerId,action.text);accepted=true} catch(_:Exception) {
                        mutable.value=mutable.value.copy(notice="Message was not accepted. Your draft is retained.")
                    }finally {
                        mutable.value=mutable.value.copy(sendBusy=false,sendAccepted=accepted,sendCompletion=mutable.value.sendCompletion+1)
                    }
                }
            }
            UiAction.BeginOlder,UiAction.Older -> {
                val c=current.conversations.find {"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                val account=current.accountId
                if(mutable.value.olderLoading)return
                mutable.value=mutable.value.copy(olderLoading=true)
                viewModelScope.launch {
                    try {
                        val existing=(older[c.friend.accountId] ?: emptyList())+c.messages
                        val before=existing.minOfOrNull {it.sequence} ?: Long.MAX_VALUE
                        val page=backend.older(c.friend.peerId,before)
                        if(current.accountId==account&&current.conversations.any {it.friend.accountId==c.friend.accountId}) {
                            older[c.friend.accountId]=((older[c.friend.accountId] ?: emptyList())+page).distinctBy {it.id}
                            if(page.size<8)exhausted.add(c.friend.accountId)
                            project()
                        }
                    }catch(_:Exception){mutable.value=mutable.value.copy(notice="Native history could not be read.")}
                    finally {mutable.value=mutable.value.copy(olderLoading=false)}
                }
            }
            UiAction.SignOut -> viewModelScope.launch {try {backend.signOut();mutable.value=mutable.value.copy(route=Route.Welcome)}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Sign-out cleanup failed; restart the application.")}}
            UiAction.Explore -> mutable.value=mutable.value.copy(notice="Demo is available only through the explicit debug launch mode.")
            is UiAction.JoinVoice,is UiAction.LeaveVoice,is UiAction.ToggleMute,is UiAction.ToggleDeafen,
            is UiAction.WatchShare,is UiAction.StopWatching,is UiAction.MemberAudio,UiAction.PreviewIncomingCall,
            is UiAction.PreviewLoginFailure,is UiAction.RetryPreview -> mutable.value=mutable.value.copy(notice="This capability is unavailable in Android messaging.")
            is UiAction.SaveServer -> if(current.authenticated||mutable.value.loginBusy) mutable.value=mutable.value.copy(notice="Sign out before changing server.")
                else if(configurationReady&&!configurationSaving&&validServerDraft(action.server)) {
                    configurationSaving=true
                    viewModelScope.launch {
                    try {
                        val endpoint=ServerEndpoint(action.server.host.trim(),action.server.port.toInt(),action.server.certificatePin.lowercase())
                        withContext(configurationDispatcher){serverConfiguration.save(endpoint)}
                        configurationInvalid=false
                        mutable.value=mutable.value.copy(server=ServerUi(endpoint.host,endpoint.port.toString(),endpoint.certificatePin),
                            notice="Server configuration saved securely for future sign-ins.",loginError=null)
                    }catch(_:Exception){mutable.value=mutable.value.copy(notice="Server configuration was not saved. Existing trusted settings are unchanged.")}
                    finally{configurationSaving=false}
                    }
                }
            else -> mutable.value=reduceUi(mutable.value,action)
        }
    }
    override fun onCleared() {backend.close();super.onCleared()}
}
