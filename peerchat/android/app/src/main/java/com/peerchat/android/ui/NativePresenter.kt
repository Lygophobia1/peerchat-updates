package com.peerchat.android.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.peerchat.android.application.*
import com.peerchat.android.platform.ServerEndpoint
import com.peerchat.android.platform.ServerRejected
import com.peerchat.android.platform.ServerConfigurationRepository
import com.peerchat.android.platform.AndroidServerConfigurationStore
import com.peerchat.android.platform.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.withContext
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.ensureActive
import kotlin.coroutines.coroutineContext
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import com.peerchat.android.platform.tls.ServerChannel

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
                status=if(!outgoing)MessageStatus.Received else when(m.delivery){2L->MessageStatus.Delivered;3L->MessageStatus.Stored;1L->MessageStatus.Sent;else->MessageStatus.Queued})
        }
        ConversationUi(id,c.friend.username,ChatKind.Direct,c.friend.online,messages.lastOrNull()?.text ?: "",
            messages.lastOrNull()?.time ?: "",messages=messages,hasOlder=c.messages.size==8,attachments=c.attachments)
    }
    // Diagnostics are safe owner metadata, never messages, credentials or keys.
    val diagnostics=buildString {
        appendLine(snapshot.status)
        appendLine("Authenticated=${snapshot.authenticated} Server presence live=${snapshot.serverPresenceLive}")
        appendLine("Profile identity: ${snapshot.localPeerId}")
        appendLine("History Sync admission:")
        appendLine(snapshot.archiveAuthorizationDiagnostics)
        for(c in snapshot.conversations) {
            appendLine("${c.friend.username}:${c.friend.accountId} · Server Relay")
            appendLine("Ready=${c.connection.ready} Pending=${c.connection.pending} Retrying=${c.connection.retrying}")
            appendLine("Network revision=${c.connection.networkRevision} Retry delay=${c.connection.retryDelayMs} ms")
            appendLine("Failure=${c.connection.failure}")
        }
    }
    return previous.copy(demo=false,conversations=conversations,diagnostics=diagnostics,
        incomingRequests=snapshot.incomingRequests.map {MemberUi("account:${it.accountId}",it.username,false)},
        account=if(snapshot.authenticated||snapshot.localHistoryReady)AccountUi("account:${snapshot.accountId}",snapshot.username) else null,
        connection=when {snapshot.authenticated&&snapshot.serverPresenceLive->ConnectionState.Online
            snapshot.authenticated->ConnectionState.Reconnecting
            else->ConnectionState.Offline},
        load=if(conversations.isEmpty())LoadState.Empty else LoadState.Ready,recentCalls=emptyList())
}

class NativePresenter(application: Application,private val backend:MessagingApplication,
    private val serverConfiguration:ServerConfigurationRepository=AndroidServerConfigurationStore(application),
    private val configurationDispatcher:CoroutineDispatcher=Dispatchers.IO,
    private val qol:AndroidQolRepository=AndroidQolStore(application),
    private val inspectServer:(ServerEndpoint)->String={ServerChannel.inspectCertificate(it.host,it.port)}
):AndroidViewModel(application),PeerChatPresenter {
    constructor(application:Application):this(application,AndroidMessagingRuntime.acquire(application))
    private val mutable=MutableStateFlow(PeerChatUiState(demo=false,connection=ConnectionState.Offline,recentCalls=emptyList(),startupStatus="Checking saved sign-in…"))
    override val state:StateFlow<PeerChatUiState> = mutable.asStateFlow()
    private var current=MessagingSnapshot()
    private var configurationReady=false
    private var configurationInvalid=false
    private var configurationSaving=false
    private var startupJob:Job?=null
    private var loginJob:Job?=null
    private var startupWatchdog:Job?=null
    private var recovering=false
    private var automaticResumeSuppressed=false
    private var uiGeneration=0L
    private val retainedDiagnostics=ArrayDeque<String>()
    private fun retainDiagnostic(category:String,reason:String) {
        if(retainedDiagnostics.size>=16)retainedDiagnostics.removeFirst()
        retainedDiagnostics.addLast("generation=$uiGeneration category=$category $reason")
    }
    private fun watchStartup() {
        if(startupWatchdog?.isActive==true)return
        startupWatchdog=viewModelScope.launch {
            delay(30000)
            if(mutable.value.loginBusy||mutable.value.startupStatus!=null||(current.authenticated&&!current.serverPresenceLive)) {
                retainDiagnostic("Messaging startup","Authenticated presence did not become live within startup budget")
                recoverNetwork(Route.Login,"Connection startup timed out. Sign in to retry; saved authorization is retained.")
            }
        }
    }
    private fun recoverNetwork(destination:Route,message:String="Network session reset. Your identity, keys, history and settings are unchanged.",after:(()->Unit)?=null) {
        if(recovering)return
        recovering=true;automaticResumeSuppressed=true;uiGeneration++
        startupJob?.cancel();loginJob?.cancel();startupWatchdog?.cancel();loginAfterTrust=null
        mutable.value=mutable.value.copy(route=destination,startupStatus=null,loginBusy=false,loginError=null,notice=null,serverTrust=null,archiveEnrollment=null,archiveBusy=false,connection=ConnectionState.Offline)
        viewModelScope.launch {
            var completed=false
            try {backend.resetNetworkSession();current=backend.state.value;configurationReady=true;completed=true
                mutable.value=mutable.value.copy(route=destination,startupStatus=null,loginBusy=false,notice=message)
            }catch(failure:Exception){
                if(failure is CancellationException)throw failure
                retainDiagnostic("Network recovery","Teardown failed closed")
                mutable.value=mutable.value.copy(notice="Network teardown could not complete. Retry Reset Network Session; no profile data was removed.")
            }finally {recovering=false;project()}
            if(completed)after?.invoke()
        }
    }
    // In-memory continuation only; never persists or logs a password.
    private var loginAfterTrust:(()->Unit)?=null
    private var foreground=true
    private var qolState=AndroidQolState()
    private var qolReady=false
    private val draftSaves=mutableMapOf<String,Job>()
    private var pendingOpen:Pair<String,String>?=null
    private fun preferenceAccountKey():String? {
        val endpoint=mutable.value.server
        if(current.accountId<=0||!validServerDraft(endpoint))return null
        return accountPreferenceKey(ServerEndpoint(endpoint.host,endpoint.port.toInt(),endpoint.certificatePin),current.accountId)
    }
    fun openNotification(accountKey:String,conversationId:String) {
        if(!accountKey.matches(Regex("[0-9a-f]{64}"))||!conversationId.matches(Regex("account:[1-9][0-9]{0,18}")))return
        pendingOpen=accountKey to conversationId;project()
    }
    private fun savePreference(key:String,update:(ConversationPreference)->ConversationPreference) {
        if(!qolReady)return
        viewModelScope.launch {
            try{qolState=withContext(configurationDispatcher){qol.update{state->
                if(!state.conversations.containsKey(key)&&state.conversations.size>=128)error("Preference bound reached")
                state.copy(conversations=state.conversations+(key to update(state.conversations[key] ?: ConversationPreference())))
            };qol.load()}}
            catch(_:Exception){mutable.value=mutable.value.copy(notice="Local preferences could not be saved securely.")}
        }
    }
    // View-only older pages, always fetched from native Storage. Never an outbox.
    private val older=mutableMapOf<Long,List<MessageSnapshot>>()
    private val exhausted=mutableSetOf<Long>()
    private fun project() {
        val combined=current.copy(conversations=current.conversations.map {c ->
            c.copy(messages=chronologicalMessages((older[c.friend.accountId] ?: emptyList())+c.messages))
        })
        // projectMessaging's paging hint is overridden by the actual native page result.
        mutable.value=projectMessaging(combined,mutable.value).let {s->s.copy(conversations=s.conversations.map {c->
            c.copy(hasOlder=c.id.removePrefix("account:").toLongOrNull() !in exhausted)
        })}
        if(retainedDiagnostics.isNotEmpty())mutable.value=mutable.value.copy(diagnostics=mutable.value.diagnostics+"\nRetained application diagnostics:\n"+retainedDiagnostics.joinToString("\n"))
        val accountKey=preferenceAccountKey() ?: return
        pendingOpen?.let{(owner,id)->if(current.authenticated){
            if(owner!=accountKey)pendingOpen=null
            else if(mutable.value.conversations.any{it.id==id}){pendingOpen=null;mutable.value=reduceUi(mutable.value,UiAction.Select(id))}
        }}
        if(qolReady){
            val unseen=mutable.value.conversations.map {c->
                val key=conversationPreferenceKey(accountKey,c.id.removePrefix("account:").toLong())
                val preference=qolState.conversations[key]
                val source=current.conversations.find{it.friend.accountId.toString()==c.id.removePrefix("account:")}
                val incoming=source?.messages?.filter{it.senderDevice!=current.localPeerId} ?: emptyList()
                val newest=incoming.maxOfOrNull{it.sequence} ?: 0L
                if(preference==null){
                    // Baseline historical messages, not fabricated new arrivals.
                    qolState=qolState.copy(conversations=qolState.conversations+(key to ConversationPreference(readSequence=newest,notifiedSequence=newest)))
                    savePreference(key){row->row.copy(readSequence=maxOf(row.readSequence,newest),notifiedSequence=maxOf(row.notifiedSequence,newest))}
                }else if(foreground&&mutable.value.route==Route.Conversation&&mutable.value.selectedId==c.id&&newest>preference.readSequence){
                    qolState=qolState.copy(conversations=qolState.conversations+(key to preference.copy(readSequence=newest)))
                    savePreference(key){row->row.copy(readSequence=maxOf(row.readSequence,newest))}
                }
                val row=qolState.conversations[key] ?: ConversationPreference()
                c.copy(unread=incoming.count{it.sequence>row.readSequence})
            }
            mutable.value=mutable.value.copy(conversations=unseen,drafts=unseen.associate{c->c.id to (qolState.conversations[conversationPreferenceKey(accountKey,c.id.removePrefix("account:").toLong())]?.draft ?: "")},notificationsEnabled=qolState.notifications,notificationPreview=qolState.preview)
        }
    }
    init {
    // Collect during authentication, rather than only after restore completes.
    viewModelScope.launch {backend.state.collect {snapshot ->
        if(recovering)return@collect
        if(snapshot.accountId!=current.accountId){older.clear();exhausted.clear()}
        current=snapshot
        if(!snapshot.authenticated||!snapshot.serverPresenceLive||mutable.value.archiveEnrollment?.accountId!=snapshot.accountId)mutable.value=mutable.value.copy(archiveEnrollment=null)
        mutable.value=mutable.value.copy(mailboxStorageAllowed=snapshot.mailboxStorageAllowed,archiveProgress=snapshot.archiveProgress)
        if(!automaticResumeSuppressed&&snapshot.localHistoryReady&&mutable.value.route in setOf(Route.Welcome,Route.Login)) {
            mutable.value=mutable.value.copy(startupStatus=null,route=Route.Home)
        }
        if(mutable.value.startupStatus!=null) {
            mutable.value=when {
                snapshot.authenticated&&snapshot.serverPresenceLive -> mutable.value.copy(startupStatus=null,route=Route.Home,rememberMe=true)
                snapshot.authenticated -> mutable.value.copy(startupStatus="Connecting…")
                snapshot.status.startsWith("Reconnecting") -> mutable.value.copy(startupStatus="Reconnecting…")
                else -> mutable.value
            }
        }
        if(snapshot.authenticated&&!snapshot.serverPresenceLive)watchStartup()
        if(snapshot.serverPresenceLive)startupWatchdog?.cancel()
        project()
    }}
    startupJob=viewModelScope.launch {
        watchStartup()
        try{qolState=withContext(configurationDispatcher){qol.load()};qolReady=true}
        catch(failure:Exception){if(failure is CancellationException)throw failure;mutable.value=mutable.value.copy(notificationsEnabled=false,notice="Local preferences could not be verified; notifications remain unavailable.")}
        var settingsVerified=false
        try {
            val saved=withContext(configurationDispatcher){serverConfiguration.load()}
            settingsVerified=true
            if(saved!=null){
                mutable.value=mutable.value.copy(server=ServerUi(saved.host,saved.port.toString(),saved.certificatePin),loginBusy=true,startupStatus="Signing in…")
                if(backend.restoreLocalHistory(saved))mutable.value=mutable.value.copy(startupStatus=null,route=Route.Home)
                try{if(backend.restoreRemembered(saved)) {
                    mutable.value=mutable.value.copy(rememberMe=true,startupStatus=if(backend.state.value.serverPresenceLive||backend.state.value.localHistoryReady)null else "Connecting…",route=if(backend.state.value.serverPresenceLive||backend.state.value.localHistoryReady)Route.Home else mutable.value.route)
                } else mutable.value=mutable.value.copy(startupStatus=null)}
                catch(failure:Exception){
                    if(failure is CancellationException)throw failure
                    retainDiagnostic(if(failure is ServerRejected)"Authentication" else "Server connection","Remembered startup failed; see current sign-in status")
                    val changed=ServerChannel.identityChange(failure)
                    if(changed!=null)mutable.value=mutable.value.copy(serverTrust=ServerTrustUi(mutable.value.server,changed.observed,changed.expected),serverIdentityChanged=mutable.value.server)
                    mutable.value=mutable.value.copy(
                    startupStatus=if(changed==null&&isRememberedNetworkFailure(failure)&&!backend.state.value.localHistoryReady)"Reconnecting…" else null,
                    route=if(changed==null&&isRememberedNetworkFailure(failure))mutable.value.route else Route.Login,
                    loginError=if(failure is ServerRejected)"Saved sign-in was rejected. Please sign in again." else "Unable to connect. Verify the server and trusted fingerprint.")}
                finally{mutable.value=mutable.value.copy(loginBusy=false)}
            } else mutable.value=mutable.value.copy(startupStatus=null)
        }catch(failure:Exception){
            if(failure is CancellationException)throw failure
            android.util.Log.w("PeerChatStartup","stage=local_startup_failed category="+failure.javaClass.simpleName)
            configurationInvalid=!settingsVerified
            retainDiagnostic(if(settingsVerified)"Messaging startup" else "Server configuration",if(settingsVerified)"Local startup failed before remembered authentication" else "Protected server configuration could not be verified")
            mutable.value=mutable.value.copy(route=Route.Login,startupStatus=null,loginBusy=false,loginError=if(settingsVerified)"Local startup failed. Reset Network Session to retry; existing data is retained." else "Saved server configuration could not be verified. Verify trusted Server Settings before signing in.")
        }finally{configurationReady=true;if(!recovering){mutable.value=mutable.value.copy(loginBusy=false);project()}}
    }}
    override fun login(username:String,password:String) {
        authenticate(username,password,mutable.value.route==Route.Register)
    }
    private fun authenticate(username:String,password:String,creating:Boolean) {
        val s=mutable.value
        if(s.loginBusy)return
        if(recovering)return
        if(!configurationReady||configurationInvalid||configurationSaving){mutable.value=s.copy(loginError="Verify and save trusted server settings before signing in.");return}
        if(username.isBlank()||password.isEmpty()||username.length>64||password.length>1024) {
            mutable.value=s.copy(loginError="Enter your username and password.");return
        }
        if(!validServerDraft(s.server)) {mutable.value=s.copy(loginError="Configure a valid server first.");return}
        if(s.server.certificatePin.isEmpty()){
            loginAfterTrust={authenticate(username,password,creating)}
            mutable.value=s.copy(loginError="Confirm the server fingerprint before signing in.")
            dispatch(UiAction.SaveServer(s.server));return
        }
        if(s.rememberMe&&!s.server.certificatePin.matches(Regex("[0-9a-f]{64}"))){
            mutable.value=s.copy(loginError="Remember Me requires a verified server fingerprint. Save trusted server settings first.");return
        }
        automaticResumeSuppressed=false
        mutable.value=s.copy(loginBusy=true,loginError=null,notice=null)
        watchStartup()
        loginJob=viewModelScope.launch {
            try {
                val endpoint=ServerEndpoint(s.server.host.trim(),s.server.port.toInt(),s.server.certificatePin.lowercase())
                if(creating)backend.createAccountRemembered(endpoint,username,password,s.rememberMe)
                else backend.signInRemembered(endpoint,username,password,s.rememberMe)
                coroutineContext.ensureActive()
                mutable.value=mutable.value.copy(loginBusy=false,loginError=null,route=Route.Home)
            } catch(failure:Exception) {
                if(failure is CancellationException)throw failure
                retainDiagnostic(if(failure is MessagingStartupFailure)"Messaging startup" else if(ServerChannel.identityChange(failure)!=null)"Server trust" else "Authentication","Sign-in attempt failed")
                ServerChannel.identityChange(failure)?.let { changed ->
                    mutable.value=mutable.value.copy(serverTrust=ServerTrustUi(s.server,changed.observed,changed.expected),serverIdentityChanged=s.server)
                }
                mutable.value=mutable.value.copy(loginBusy=false,loginError=accountFailureText(failure,creating))
            }
        }
    }
    fun setForeground(active:Boolean) {
        foreground=active
        backend.setForeground(active)
        if(active&&!automaticResumeSuppressed&&!recovering&&configurationReady&&!configurationInvalid&&!mutable.value.loginBusy){
            val s=mutable.value
            if(!validServerDraft(s.server))return
            val epoch=uiGeneration
            loginJob?.cancel()
            loginJob=viewModelScope.launch {
                try{val restored=backend.restoreRemembered(ServerEndpoint(s.server.host,s.server.port.toInt(),s.server.certificatePin))
                    coroutineContext.ensureActive()
                    if(epoch==uiGeneration&&!restored&&!current.localHistoryReady)mutable.value=mutable.value.copy(route=Route.Welcome)
                }catch(failure:Exception){
                    if(failure is CancellationException)throw failure
                    retainDiagnostic("Server connection","Foreground resume failed")
                    if(epoch==uiGeneration)mutable.value=mutable.value.copy(notice="Reconnect unavailable; retry when the server is reachable.")
                }
            }
        }
    }
    override fun dispatch(action:UiAction) {
        when(action) {
            is UiAction.Draft -> {
                val c=current.conversations.find{"account:${it.friend.accountId}"==action.conversationId} ?: return
                if(action.text.length>4096)return
                val accountKey=preferenceAccountKey() ?: return
                val key=conversationPreferenceKey(accountKey,c.friend.accountId)
                mutable.value=reduceUi(mutable.value,action)
                qolState=qolState.copy(conversations=qolState.conversations+(key to (qolState.conversations[key] ?: ConversationPreference()).copy(draft=action.text)))
                draftSaves[key]?.cancel();draftSaves[key]=viewModelScope.launch{delay(250);savePreference(key){it.copy(draft=action.text)}}
            }
            is UiAction.NotificationSettings -> {
                mutable.value=reduceUi(mutable.value,action)
                viewModelScope.launch{try{qolState=withContext(configurationDispatcher){qol.update{it.copy(notifications=action.enabled,preview=action.preview)};qol.load()}}
                    catch(_:Exception){mutable.value=mutable.value.copy(notice="Notification settings could not be saved securely.")}}
            }
            is UiAction.MailboxStorage -> viewModelScope.launch {
                try{backend.setMailboxStorageAllowed(action.allowed);qolState=qol.load()}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Mailbox privacy preference could not be saved; existing consent is unchanged.")}
            }
            UiAction.ArchiveRefresh, is UiAction.ArchiveConsent, is UiAction.HistorySync, is UiAction.ArchiveSynchronize -> {
                if(mutable.value.archiveBusy)return
                val displayed=mutable.value.archiveEnrollment
                if(action is UiAction.ArchiveSynchronize&&(displayed==null||displayed!=action.context)){mutable.value=mutable.value.copy(notice="Archive account context changed. Refresh before synchronizing.");return}
                if(action is UiAction.ArchiveConsent&&(displayed==null||displayed!=action.context)){mutable.value=mutable.value.copy(notice="Archive account context changed. Refresh and confirm again.");return}
                if(action is UiAction.HistorySync&&(displayed==null||displayed!=action.context)){mutable.value=mutable.value.copy(notice="Archive account context changed. Refresh before changing history policy.");return}
                mutable.value=mutable.value.copy(archiveBusy=true)
                val epoch=uiGeneration
                viewModelScope.launch {
                    try{
                        val result=when(action){
                            is UiAction.ArchiveConsent->backend.archiveConsent(checkNotNull(displayed),action.action,action.device,action.read,action.serve)
                            is UiAction.HistorySync->backend.setHistorySync(checkNotNull(displayed),action.enabled)
                            is UiAction.ArchiveSynchronize->backend.synchronizeArchive(checkNotNull(displayed),action.source,action.pause)
                            else->backend.archiveDevices()
                        }
                        if(epoch==uiGeneration&&current.authenticated&&current.serverPresenceLive&&current.accountId==result.accountId&&current.archiveProgress.authenticationGeneration==result.authenticationGeneration)mutable.value=mutable.value.copy(archiveEnrollment=result,notice="Signed history metadata verified; existing permissions are preserved.")
                    }catch(failure:Exception){
                        if(failure is CancellationException)throw failure
                        retainDiagnostic("Archive","Archive operation failed closed")
                        if(epoch==uiGeneration)mutable.value=mutable.value.copy(archiveEnrollment=null,notice="Archive operation failed closed. Refresh to retry pending public grants; login alone never grants history access.")
                    }finally{if(epoch==uiGeneration)mutable.value=mutable.value.copy(archiveBusy=false)}
                }
            }
            is UiAction.Select -> {mutable.value=reduceUi(mutable.value,action);project()}
            is UiAction.RequestFriend -> viewModelScope.launch {
                try {backend.requestFriend(action.username);mutable.value=mutable.value.copy(notice="Friend request sent through authenticated server.")}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Friend request was not accepted by server.")}
            }
            is UiAction.RespondFriend -> viewModelScope.launch {
                try {backend.respondFriend(action.accountId,action.accept)}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Friend response failed; authorization unchanged.")}
            }
            is UiAction.SendAttachment -> {
                val c=current.conversations.find{"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                viewModelScope.launch{try{backend.sendAttachment(c.friend.peerId,action.uri)}
                    catch(_:Exception){mutable.value=mutable.value.copy(notice="File could not be offered. Check connection, file access and available storage.")}}
            }
            is UiAction.AttachmentAction -> viewModelScope.launch{try{backend.attachmentAction(action.id,action.accept)}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="File action could not complete. The protected transfer state is retained.")}}
            is UiAction.PreviewMessage -> {
                if(mutable.value.sendBusy)return
                val c=current.conversations.find {"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                if(action.text.isBlank()||action.text.length>4096)return
                mutable.value=mutable.value.copy(sendBusy=true,sendAccepted=false)
                viewModelScope.launch {
                    var accepted=false
                    try {backend.send(c.friend.peerId,action.text);accepted=true
                        val accountKey=preferenceAccountKey();if(accountKey!=null)savePreference(conversationPreferenceKey(accountKey,c.friend.accountId)){it.copy(draft="")}
                        mutable.value=mutable.value.copy(drafts=mutable.value.drafts+("account:${c.friend.accountId}" to ""))
                    } catch(_:Exception) {
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
            UiAction.SignOut -> viewModelScope.launch {try {backend.signOut();mutable.value=mutable.value.copy(route=Route.Welcome,startupStatus=null,rememberMe=false)}
                catch(_:Exception){mutable.value=mutable.value.copy(route=Route.Login,startupStatus=null,notice="Sign-out revocation is pending; saved sign-in will not be reused.")}}
            UiAction.ResetNetworkSession -> recoverNetwork(Route.Login)
            UiAction.DisconnectChangeServer -> recoverNetwork(Route.Server)
            UiAction.Explore -> mutable.value=mutable.value.copy(notice="Demo is available only through the explicit debug launch mode.")
            is UiAction.JoinVoice,is UiAction.LeaveVoice,is UiAction.ToggleMute,is UiAction.ToggleDeafen,
            is UiAction.WatchShare,is UiAction.StopWatching,is UiAction.MemberAudio,UiAction.PreviewIncomingCall,
            is UiAction.PreviewLoginFailure,is UiAction.RetryPreview -> mutable.value=mutable.value.copy(notice="This capability is unavailable in Android messaging.")
            UiAction.CancelServerTrust -> {
                loginAfterTrust=null
                mutable.value=mutable.value.copy(serverTrust=null,notice="Server trust unchanged.")
            }
            UiAction.TrustServer -> {
                val prompt=mutable.value.serverTrust
                if(prompt!=null&&prompt.expected.isEmpty()){
                    mutable.value=mutable.value.copy(serverTrust=null)
                    dispatch(UiAction.SaveServer(prompt.server.copy(certificatePin=prompt.observed)))
                }
            }
            is UiAction.SaveServer -> if(current.authenticated||mutable.value.loginBusy) recoverNetwork(Route.Server,after={dispatch(action)})
                else if(configurationReady&&!configurationSaving&&validServerDraft(action.server)) {
                    configurationSaving=true
                    viewModelScope.launch {
                    var savedSuccessfully=false
                    var written=false
                    try {
                        val draft=ServerEndpoint(action.server.host.trim(),action.server.port.toInt(),action.server.certificatePin.lowercase())
                        val saved=withContext(configurationDispatcher){serverConfiguration.load()}
                        val expected=serverTrustExpected(saved,draft)
                        val observed=withContext(configurationDispatcher){inspectServer(draft)}
                        if(expected.isEmpty()||expected!=observed){
                            if(expected.isNotEmpty())loginAfterTrust=null
                            mutable.value=mutable.value.copy(serverTrust=ServerTrustUi(action.server,observed,expected),
                                serverIdentityChanged=if(expected.isNotEmpty())action.server else mutable.value.serverIdentityChanged,
                                notice=if(expected.isEmpty())"Confirm the server identity before signing in." else "Server identity changed; connection rejected.")
                            return@launch
                        }
                        validateObservedServer(expected,observed)
                        // Preserve the existing endpoint spelling used by account/history
                        // indexes when only host casing or surrounding space was edited.
                        val endpoint=draft.copy(host=if(saved!=null&&sameServer(saved,draft))saved.host else draft.host,certificatePin=expected)
                        withContext(configurationDispatcher){
                            serverConfiguration.save(endpoint);written=true
                            check(serverConfiguration.load()==endpoint)
                        }
                        configurationInvalid=false
                        mutable.value=mutable.value.copy(server=ServerUi(endpoint.host,endpoint.port.toString(),endpoint.certificatePin),
                            serverIdentityChanged=null,serverTrust=null,notice="Server trusted and saved securely for future sign-ins.",loginError=null)
                        savedSuccessfully=true
                    }catch(_:Exception){
                        loginAfterTrust=null
                        if(written)configurationInvalid=true
                        mutable.value=mutable.value.copy(notice=if(written)
                            "Saved server configuration could not be verified. Verify trusted settings before signing in."
                            else "Server configuration was not saved. Existing trusted settings are unchanged.")
                    }
                    finally{configurationSaving=false}
                    if(savedSuccessfully){val resume=loginAfterTrust;loginAfterTrust=null;resume?.invoke()}
                    }
                }
            else -> mutable.value=reduceUi(mutable.value,action)
        }
    }
    override fun onCleared() {loginAfterTrust=null;backend.close();super.onCleared()}
}
