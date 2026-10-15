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
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlin.coroutines.coroutineContext
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import com.peerchat.android.platform.tls.ServerChannel

/** Immutable presentation only. Native Profile/Storage and session/delivery owners
 * remain authoritative. This adapter never constructs peer frames or delivery ACKs. */
internal fun projectMessaging(snapshot: MessagingSnapshot, previous: PeerChatUiState): PeerChatUiState {
    fun accountName(account:Long)=if(account==snapshot.accountId)snapshot.username else snapshot.conversations.firstOrNull{it.friend.accountId==account}?.friend?.username?:"Account $account"
    val conversations=snapshot.conversations.map { c ->
        val id="account:${c.friend.accountId}"
        val messages=chronologicalMessages(c.messages).map {m ->
            val outgoing=m.senderDevice==snapshot.localPeerId
            MessageUi(m.id,m.senderDevice,if(outgoing)snapshot.username else c.friend.username,m.text,outgoing,
                SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(m.timestamp)),
                date=SimpleDateFormat("yyyy-MM-dd",Locale.getDefault()).format(Date(m.timestamp)),
                status=if(!outgoing)MessageStatus.Received else when(m.delivery){2L->MessageStatus.Delivered;3L->MessageStatus.Stored;1L->MessageStatus.Sent;else->MessageStatus.Queued},
                timestamp=m.timestamp,logicalOrder=m.logicalOrder)
        }
        ConversationUi(id,c.friend.username,ChatKind.Direct,c.friend.online,messages.lastOrNull()?.text ?: "",
            messages.lastOrNull()?.time ?: "",messages=messages,hasOlder=c.messages.size==8,attachments=c.attachments)
    }+snapshot.groups.map {g->
        val messages=g.messages.map{m->val outgoing=m.device==snapshot.localPeerId
            MessageUi(m.id,m.device,if(outgoing)snapshot.username else accountName(m.account),m.text,outgoing,
                SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(m.timestamp)),
                SimpleDateFormat("yyyy-MM-dd",Locale.getDefault()).format(Date(m.timestamp)),
                if(!outgoing)MessageStatus.Received else if(m.status.startsWith("Recipients:")||m.status.startsWith("Server accepted"))MessageStatus.Sent else if(m.status.startsWith("Not submitted")||m.status.contains("unknown")||m.status.contains("rejected"))MessageStatus.Failed else MessageStatus.Queued,
                m.timestamp,m.order,retryAvailable=outgoing&&!m.status.startsWith("Recipients:"),deliveryDetail=if(outgoing)m.status else "")
        }
        ConversationUi("group:${g.id}",g.name,ChatKind.Group,g.ready,messages.lastOrNull()?.text?:if(g.ready)"Encrypted group text" else "Waiting for signed membership and current group key",
            messages.lastOrNull()?.time?:"",unread=g.unread,messages=messages,hasOlder=g.hasOlder,
            members=g.memberRows.map{MemberUi(it.device,accountName(it.account),false,if(it.invited)"Invited" else when(it.role){3L->"Owner";2L->"Admin";else->"Member"})})
    }
    // Diagnostics are safe owner metadata, never messages, credentials or keys.
    val diagnostics=buildString {
        appendLine(snapshot.status)
        appendLine("Authenticated=${snapshot.authenticated} Server presence live=${snapshot.serverPresenceLive}")
        appendLine("Profile identity: ${snapshot.localPeerId}")
        appendLine("Messaging execution:")
        appendLine(snapshot.messagingDiagnostics)
        appendLine("Group execution:")
        appendLine(snapshot.groupDiagnostics)
        appendLine("Voice execution:")
        appendLine(snapshot.voice.diagnostic)
        appendLine("History Sync admission:")
        appendLine(snapshot.archiveAuthorizationDiagnostics)
        appendLine("File transfer measurements (native, confirmed/durable bytes):")
        appendLine(snapshot.transferDiagnostics)
        for(c in snapshot.conversations) {
            appendLine("${c.friend.username}:${c.friend.accountId} · Server Relay")
            appendLine("Resolved account=${c.friend.accountId} current_device=${c.friend.peerId} known_devices=${c.friend.knownPeerIds.joinToString(",")}")
            appendLine("Ready=${c.connection.ready} Pending=${c.connection.pending} Retrying=${c.connection.retrying}")
            appendLine("Network revision=${c.connection.networkRevision} Retry delay=${c.connection.retryDelayMs} ms")
            appendLine("Failure=${c.connection.failure}")
            // Already-retained native rows only, not a new history/outbox scan.
            // IDs/attribution/state are enough to correlate a controlled send.
            for(message in c.messages.takeLast(3))if(message.id.matches(Regex("[0-9a-f]{32}"))&&
                message.senderDevice.matches(Regex("[0-9a-f]{64}"))&&message.recipientDevice.matches(Regex("[0-9a-f]{64}")))
                appendLine("recent_native_row message_id=${message.id} sender_device=${message.senderDevice} recipient_device=${message.recipientDevice} delivery=${message.delivery} recipient_relation="+
                    if(message.recipientDevice==c.friend.peerId)"current_resolved_device" else if(message.recipientDevice in c.friend.knownPeerIds)"historical_account_device" else "not_in_account_display_index")
        }
    }
    return previous.copy(demo=false,conversations=conversations,deviceVerification=snapshot.deviceVerifications.firstOrNull(),
        mailboxReenrollment=if(snapshot.authenticated)snapshot.mailboxReenrollment else null,
        mailboxRecoveryError=if(snapshot.authenticated)snapshot.mailboxRecoveryError else null,
        diagnostics=diagnostics,voiceAvailable=snapshot.voice.available,voiceDiagnostic=snapshot.voice.diagnostic,
        incomingRequests=snapshot.incomingRequests.map {MemberUi("account:${it.accountId}",it.username,false)},
        account=if(snapshot.authenticated||snapshot.localHistoryReady)AccountUi("account:${snapshot.accountId}",snapshot.username) else null,
        connection=when {snapshot.authenticated&&snapshot.serverPresenceLive->ConnectionState.Online
            snapshot.authenticated->ConnectionState.Reconnecting
            else->ConnectionState.Offline},
        load=if(conversations.isEmpty())LoadState.Empty else LoadState.Ready,recentCalls=emptyList(),groupInvitations=snapshot.groupInvitations)
}

class NativePresenter(application: Application,private val backend:MessagingApplication,
    private val serverConfiguration:ServerConfigurationRepository=AndroidServerConfigurationStore(application),
    private val configurationDispatcher:CoroutineDispatcher=Dispatchers.IO,
    private val qol:AndroidQolRepository=AndroidQolStore(application),
    private val groupQol:AndroidQolRepository=AndroidGroupQolStore(application),
    private val inspectServer:(ServerEndpoint)->String={ServerChannel.inspectCertificate(it.host,it.port)}
):AndroidViewModel(application),PeerChatPresenter {
    // Preserve the original explicit six-argument constructor as well as
    // trailing-lambda call sites when injecting the separate group repository.
    constructor(application:Application,backend:MessagingApplication,serverConfiguration:ServerConfigurationRepository,
        configurationDispatcher:CoroutineDispatcher,qol:AndroidQolRepository,inspectServer:(ServerEndpoint)->String):
        this(application,backend,serverConfiguration,configurationDispatcher,qol,AndroidGroupQolStore(application),inspectServer)
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
    private val previewLoading=mutableSetOf<String>()
    private val largeDrafts=mutableMapOf<String,String>()
    private fun retainDiagnostic(category:String,reason:String) {
        if(retainedDiagnostics.size>=16)retainedDiagnostics.removeFirst()
        retainedDiagnostics.addLast("generation=$uiGeneration category=$category $reason")
    }
    private fun watchStartup() {
        if(startupWatchdog?.isActive==true)return
        startupWatchdog=viewModelScope.launch {
            delay(30000)
            if(mutable.value.loginBusy||mutable.value.startupStatus!=null) {
                retainDiagnostic("Messaging startup","Authenticated presence did not become live within startup budget")
                recoverNetwork(Route.Login,"Connection startup timed out. Sign in to retry; saved authorization is retained.",reason="startup_presence_timeout")
            }
        }
    }
    private fun recoverNetwork(destination:Route,message:String="Network session reset. Your identity, keys, history and settings are unchanged.",after:(()->Unit)?=null,reason:String="manual_network_reset") {
        if(recovering)return
        retainDiagnostic("Network recovery","reason=$reason destination=$destination authenticated=${current.authenticated}")
        android.util.Log.i("PeerChatNetwork","event=login_navigation reason=$reason ui_generation=$uiGeneration authenticated=${current.authenticated}")
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
    private var verificationBusy=false
    private var foreground=true
    private var qolState=AndroidQolState()
    private var qolReady=false
    private var groupQolState=AndroidQolState()
    private var groupQolReady=false
    private val groupPreferenceWrites=Mutex()
    private val draftSaves=mutableMapOf<String,Job>()
    private var pendingOpen:Pair<String,String>?=null
    private fun preferenceAccountKey():String? {
        val endpoint=mutable.value.server
        if(current.accountId<=0||!validServerDraft(endpoint))return null
        return accountPreferenceKey(ServerEndpoint(endpoint.host,endpoint.port.toInt(),endpoint.certificatePin),current.accountId)
    }
    fun openNotification(accountKey:String,conversationId:String) {
        if(!accountKey.matches(Regex("[0-9a-f]{64}"))||!conversationId.matches(Regex("(?:account:[1-9][0-9]{0,18}|group:[0-9a-f]{32})")))return
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
    private fun saveGroupPreference(key:String,update:(ConversationPreference)->ConversationPreference) {
        if(!groupQolReady)return
        viewModelScope.launch {
            try{groupPreferenceWrites.withLock{withContext(configurationDispatcher){groupQol.update{state->
                check(state.conversations.containsKey(key)||state.conversations.size<128)
                state.copy(conversations=state.conversations+(key to update(state.conversations[key] ?: ConversationPreference(baselinePending=true))))
            }}}}
            catch(failure:Exception){if(failure is CancellationException)throw failure;mutable.value=mutable.value.copy(notice="Group drafts and read position could not be saved securely.")}
        }
    }
    private fun updateGroupPreference(key:String,update:(ConversationPreference)->ConversationPreference):Boolean {
        if(!groupQolReady)return false
        if(key !in groupQolState.conversations&&groupQolState.conversations.size>=128){
            mutable.value=mutable.value.copy(notice="The saved group preference limit has been reached.");return false
        }
        val row=update(groupQolState.conversations[key] ?: ConversationPreference(baselinePending=true))
        groupQolState=groupQolState.copy(conversations=groupQolState.conversations+(key to row))
        return true
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
            if(c.kind==ChatKind.Group)c else c.copy(hasOlder=c.id.removePrefix("account:").toLongOrNull() !in exhausted)
        })}
        if(retainedDiagnostics.isNotEmpty())mutable.value=mutable.value.copy(diagnostics=mutable.value.diagnostics+"\nRetained application diagnostics:\n"+retainedDiagnostics.joinToString("\n"))
        val accountKey=preferenceAccountKey() ?: return
        pendingOpen?.let{(owner,id)->if(current.authenticated){
            if(owner!=accountKey)pendingOpen=null
            else if(mutable.value.conversations.any{it.id==id}){pendingOpen=null;mutable.value=reduceUi(mutable.value,UiAction.Select(id))}
        }}
        if(qolReady){
            val unseen=mutable.value.conversations.map {c->
                if(c.kind==ChatKind.Group)return@map c
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
            mutable.value=mutable.value.copy(conversations=unseen,drafts=unseen.associate{c->c.id to (if(c.kind==ChatKind.Group)mutable.value.drafts[c.id]?:"" else largeDrafts[c.id] ?: qolState.conversations[conversationPreferenceKey(accountKey,c.id.removePrefix("account:").toLong())]?.draft ?: "")},notificationsEnabled=qolState.notifications,notificationPreview=qolState.preview)
        }
        if(groupQolReady){
            val groups=current.groups.associateBy{it.id}
            val rows=mutable.value.conversations.map {c->
                if(c.kind!=ChatKind.Group)return@map c
                val group=groups[c.id.removePrefix("group:")] ?: return@map c
                val key=groupPreferenceKey(accountKey,group.id)
                val incoming=group.messages.filter{it.device!=current.localPeerId}.sortedBy{it.sequence}
                val newest=incoming.maxOfOrNull{it.sequence} ?: 0L
                val preference=groupQolState.conversations[key]
                if((preference==null||preference.baselinePending)&&(group.messages.isNotEmpty()||!group.hasOlder)){
                    // Only current execution's admitted new arrivals are unread
                    // during first history initialization; older pages are baseline.
                    val baseline=incoming.dropLast(group.unread.coerceIn(0,incoming.size)).maxOfOrNull{it.sequence} ?: 0L
                    val update:(ConversationPreference)->ConversationPreference={it.copy(readSequence=maxOf(it.readSequence,baseline),notifiedSequence=maxOf(it.notifiedSequence,baseline),baselinePending=false)}
                    if(updateGroupPreference(key,update))saveGroupPreference(key,update)
                }
                val initialized=groupQolState.conversations[key]
                if(foreground&&mutable.value.route==Route.Conversation&&mutable.value.selectedId==c.id&&initialized!=null&&!initialized.baselinePending&&newest>initialized.readSequence){
                    val update:(ConversationPreference)->ConversationPreference={it.copy(readSequence=maxOf(it.readSequence,newest))}
                    if(updateGroupPreference(key,update))saveGroupPreference(key,update)
                }
                val row=groupQolState.conversations[key]
                c.copy(unread=if(row==null||row.baselinePending)group.unread else incoming.count{it.sequence>row.readSequence})
            }
            mutable.value=mutable.value.copy(conversations=rows,drafts=mutable.value.drafts+rows.filter{it.kind==ChatKind.Group}.associate{c->
                c.id to (groupQolState.conversations[groupPreferenceKey(accountKey,c.id.removePrefix("group:"))]?.draft ?: "")
            })
        }
    }
    init {
    // Collect during authentication, rather than only after restore completes.
    viewModelScope.launch {backend.state.collect {snapshot ->
        if(recovering)return@collect
        if(snapshot.accountId!=current.accountId){older.clear();exhausted.clear();largeDrafts.clear();mutable.value=mutable.value.copy(attachmentPreviews=emptyMap(),drafts=emptyMap())}
        current=snapshot
        if(foreground&&mutable.value.route==Route.Conversation){
            val group=snapshot.groups.find{"group:${it.id}"==mutable.value.selectedId&&it.unread>0}
            if(group!=null)viewModelScope.launch{try{backend.groupHistory(group.id)}catch(_:Exception){}}
        }
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
        // An established login may reconnect without reauthentication. Only a
        // pending explicit startup gets a startup watchdog; subsystem errors
        // cannot navigate a signed-in user back to Login on a timer.
        if(snapshot.authenticated&&!snapshot.serverPresenceLive&&mutable.value.startupStatus!=null)watchStartup()
        if(snapshot.serverPresenceLive)startupWatchdog?.cancel()
        project()
    }}
    startupJob=viewModelScope.launch {
        watchStartup()
        try{qolState=withContext(configurationDispatcher){qol.load()};qolReady=true}
        catch(failure:Exception){if(failure is CancellationException)throw failure;mutable.value=mutable.value.copy(notificationsEnabled=false,notice="Local preferences could not be verified; notifications remain unavailable.")}
        try{groupQolState=withContext(configurationDispatcher){groupQol.load()};groupQolReady=true}
        catch(failure:Exception){if(failure is CancellationException)throw failure;mutable.value=mutable.value.copy(notice="Saved group drafts and read position could not be verified.")}
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
        if(active)project()
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
                if(action.conversationId.startsWith("group:")){
                    val group=current.groups.find{"group:${it.id}"==action.conversationId} ?: return
                    if(action.text.toByteArray(Charsets.UTF_8).size>4096)return
                    val owner=preferenceAccountKey() ?: return
                    val key=groupPreferenceKey(owner,group.id)
                    val update:(ConversationPreference)->ConversationPreference={it.copy(draft=action.text)}
                    if(!updateGroupPreference(key,update))return
                    mutable.value=reduceUi(mutable.value,action)
                    draftSaves[key]?.cancel();draftSaves[key]=viewModelScope.launch{delay(250);saveGroupPreference(key,update)}
                    return
                }
                val c=current.conversations.find{"account:${it.friend.accountId}"==action.conversationId} ?: return
                if(action.text.toByteArray().size>512*1024)return
                if(action.text.length>4096){largeDrafts[action.conversationId]=action.text;mutable.value=reduceUi(mutable.value,action);return}
                largeDrafts.remove(action.conversationId)
                val accountKey=preferenceAccountKey() ?: return
                val key=conversationPreferenceKey(accountKey,c.friend.accountId)
                mutable.value=reduceUi(mutable.value,action)
                qolState=qolState.copy(conversations=qolState.conversations+(key to (qolState.conversations[key] ?: ConversationPreference()).copy(draft=action.text)))
                draftSaves[key]?.cancel();draftSaves[key]=viewModelScope.launch{delay(250);savePreference(key){it.copy(draft=action.text)}}
            }
            is UiAction.VerifyDevice -> {
                if(verificationBusy||mutable.value.deviceVerification!=action.value)return
                verificationBusy=true
                viewModelScope.launch{
                    try{backend.verifyDevice(action.value,action.accept);mutable.value=mutable.value.copy(deviceVerification=null,notice=if(action.accept)"Device explicitly verified. Existing messages retain their original recipients." else "Device remains untrusted. Previous identities and queued messages are unchanged.")}
                    catch(_:Exception){mutable.value=mutable.value.copy(deviceVerification=null,notice="Verification could not complete on this connection. Reconnect and review the current fingerprint; no identity or queued recipient was replaced.")}
                    finally{verificationBusy=false}
                }
            }
            is UiAction.ReenrollMailbox -> {
                if(mutable.value.mailboxRecoveryBusy||mutable.value.mailboxReenrollment!=action.value||!current.authenticated||
                    current.accountId!=action.value.account||current.localPeerId!=action.value.device)return
                mutable.value=mutable.value.copy(mailboxRecoveryBusy=true,mailboxRecoveryError=null)
                viewModelScope.launch{
                    try{
                        backend.reenrollMailbox(action.value,action.accept)
                        if(!current.authenticated||current.accountId!=action.value.account||current.localPeerId!=action.value.device)return@launch
                        mutable.value=mutable.value.copy(mailboxReenrollment=null,mailboxRecoveryError=null,
                            notice=if(action.accept)"Enrollment requested; offline receiving waits for authenticated registration."
                                else "Existing mailbox binding, keys and history are unchanged.")
                    }catch(_:Exception){
                        if(current.authenticated&&current.accountId==action.value.account&&current.localPeerId==action.value.device)
                            mutable.value=mutable.value.copy(mailboxRecoveryError="Mailbox enrollment could not be requested. Reconnect to the trusted server and review the current offer. Existing keys and history are preserved.")
                    }finally{mutable.value=mutable.value.copy(mailboxRecoveryBusy=false)}
                }
            }
            is UiAction.NotificationSettings -> {
                mutable.value=reduceUi(mutable.value,action)
                viewModelScope.launch{try{qolState=withContext(configurationDispatcher){qol.update{it.copy(notifications=action.enabled,preview=action.preview)};qol.load()}}
                    catch(_:Exception){mutable.value=mutable.value.copy(notice="Notification settings could not be saved securely.")}}
            }
            is UiAction.MailboxStorage -> viewModelScope.launch {
                try{backend.setMailboxStorageAllowed(action.allowed);qolState=qol.load()}
                catch(failure:Exception){mutable.value=mutable.value.copy(notice=if(failure is com.peerchat.android.application.MailboxConsentChangeUnavailable)failure.message else "Mailbox privacy preference could not be saved; existing consent is unchanged.")}
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
            is UiAction.Select -> {mutable.value=reduceUi(mutable.value,action);project()
                if(action.id.startsWith("group:"))viewModelScope.launch{try{backend.groupHistory(action.id.removePrefix("group:"))}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group history requires current verified authorization.")}}
            }
            is UiAction.CreateGroup -> viewModelScope.launch{try{backend.createGroup(action.name)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group creation failed; no server membership is claimed.")}}
            is UiAction.InviteGroup -> viewModelScope.launch{try{backend.inviteGroup(action.group,action.account)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group invitation was not accepted by the server.")}}
            is UiAction.RespondGroup -> viewModelScope.launch{try{backend.respondGroup(action.id,action.accept)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group invitation response failed.")}}
            is UiAction.RequestFriend -> viewModelScope.launch {
                try {backend.requestFriend(action.username);mutable.value=mutable.value.copy(notice="Friend request sent through authenticated server.")}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Friend request was not accepted by server.")}
            }
            is UiAction.RespondFriend -> viewModelScope.launch {
                try {backend.respondFriend(action.accountId,action.accept)}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="Friend response failed; authorization unchanged.")}
            }
            is UiAction.AttachmentPreview -> {
                if(action.id in mutable.value.attachmentPreviews||!previewLoading.add(action.id))return
                val account=current.accountId;val requestGeneration=uiGeneration
                if(current.conversations.none{c->c.attachments.any{it.id==action.id}}){previewLoading.remove(action.id);return}
                viewModelScope.launch{try{
                    val text=backend.attachmentText(action.id)
                    if(account==current.accountId&&requestGeneration==uiGeneration){
                        val cache=mutable.value.attachmentPreviews.toMutableMap();if(cache.size>=4)cache.remove(cache.keys.first());cache[action.id]=text
                        mutable.value=mutable.value.copy(attachmentPreviews=cache)
                    }
                }catch(_:Exception){if(account==current.accountId&&requestGeneration==uiGeneration)mutable.value=mutable.value.copy(notice="Text preview unavailable. The original attachment is retained.")}
                finally{previewLoading.remove(action.id)}}
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
                val group=mutable.value.selectedId?.removePrefix("group:")?.let{id->current.groups.find{it.id==id}}
                if(group!=null){
                    if(action.text.isBlank()||action.text.toByteArray(Charsets.UTF_8).size>4096)return
                    val key=preferenceAccountKey()?.let{groupPreferenceKey(it,group.id)}
                    val epoch=uiGeneration;val account=current.accountId
                    mutable.value=mutable.value.copy(sendBusy=true,sendAccepted=false)
                    viewModelScope.launch{var accepted=false;try{backend.sendGroup(group.id,action.text);accepted=true
                            if(key!=null){draftSaves.remove(key)?.cancel();val update:(ConversationPreference)->ConversationPreference={it.copy(draft="")};if(updateGroupPreference(key,update))saveGroupPreference(key,update)}
                            if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(drafts=mutable.value.drafts+("group:${group.id}" to ""))}
                        catch(_:Exception){mutable.value=mutable.value.copy(notice="Native group send could not complete. Check current membership and key synchronization; your draft is retained.")}
                        finally{mutable.value=mutable.value.copy(sendBusy=false,sendAccepted=accepted,sendCompletion=mutable.value.sendCompletion+1)}}
                    return
                }
                val c=current.conversations.find {"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                if(action.text.isBlank()||action.text.toByteArray().size>512*1024)return
                mutable.value=mutable.value.copy(sendBusy=true,sendAccepted=false)
                viewModelScope.launch {
                    var accepted=false
                    try {backend.send(c.friend.peerId,action.text);accepted=true
                        largeDrafts.remove("account:${c.friend.accountId}")
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
                val group=mutable.value.selectedId?.removePrefix("group:")?.let{id->current.groups.find{it.id==id}}
                if(group!=null){viewModelScope.launch{try{backend.groupHistory(group.id,group.messages.minOfOrNull{it.sequence}?:Long.MAX_VALUE)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group history page unavailable.")}};return}
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
            is UiAction.RetryPreview -> {val selected=mutable.value.selectedId
                if(selected?.startsWith("group:")==true)viewModelScope.launch{try{backend.retryGroup(selected.removePrefix("group:"),action.messageId)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Exact group retry requires the current epoch and a live event channel. The saved message is retained.")}}
                else mutable.value=mutable.value.copy(notice="This retry capability is unavailable.")
            }
            is UiAction.JoinVoice,is UiAction.LeaveVoice,is UiAction.ToggleMute,is UiAction.ToggleDeafen -> mutable.value=mutable.value.copy(notice=mutable.value.voiceDiagnostic)
            is UiAction.WatchShare,is UiAction.StopWatching,is UiAction.MemberAudio,UiAction.PreviewIncomingCall,
            is UiAction.PreviewLoginFailure -> mutable.value=mutable.value.copy(notice="This capability is unavailable in Android messaging.")
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
