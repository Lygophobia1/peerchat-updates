package com.peerchat.android.ui

import android.app.Application
import android.content.Context
import android.provider.OpenableColumns
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.SavedStateHandle
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
import java.util.UUID
import com.peerchat.android.platform.tls.ServerChannel

/** Immutable presentation only. Native Profile/Storage and session/delivery owners
 * remain authoritative. This adapter never constructs peer frames or delivery ACKs. */
internal fun voiceConsentMatchesCurrent(consent:VoiceConsent,preparedTarget:VoiceCallTarget?,voice:VoiceSnapshot,
    rooms:List<GroupVoiceRoomsSnapshot> = emptyList(),preparedEpoch:String="",localDevice:String=""):Boolean =
    if(consent.group.isNotEmpty())!consent.answer&&preparedTarget==null&&!voice.active&&consent.peer==localDevice&&
        rooms.singleOrNull{it.group==consent.group&&it.epoch==preparedEpoch}?.rooms?.singleOrNull{it.id==consent.id}!=null
    else if(consent.answer)preparedTarget!=null&&preparedTarget==voice.target&&voice.state==2L&&voice.direction==1L&&
        consent.peer==preparedTarget.peer&&consent.id==preparedTarget.id
    else preparedTarget==null&&!voice.active

// Incoming ringing already has an active native target, before Answer starts
// the service. It must not release the UI's pending-accept guard.
internal fun voiceActivationPublished(voice:VoiceSnapshot):Boolean =
    voice.platformDiagnostic.isNotEmpty()||(voice.active&&!(voice.direction==1L&&voice.state==2L))

// Allowlisted failure metadata only: never retain exception messages or picker paths.
internal fun attachmentPreparationFailureReason(failure:Exception):String=when(failure){
    is AttachmentRecipientVerificationRequired->"recipient_verification"
    is AttachmentCachePreflightRejected->"cache_rejected"
    is AttachmentModeUnavailable->"mode_unavailable"
    is com.peerchat.android.application.AttachmentReadinessUnavailable->"readiness_timeout"
    is ServerRejected->"server_rejected"
    is javax.net.ssl.SSLException->"tls_failure"
    is java.net.SocketTimeoutException->"transport_timeout"
    is java.io.EOFException,is java.net.SocketException->"connection_closed"
    is java.io.FileNotFoundException->"source_unavailable"
    is SecurityException->"source_permission"
    is java.io.IOException->"io_failure"
    else->"preparation_failed"
}

internal fun attachmentSendFailureText(mode:AttachmentSendMode,failure:Exception):String {
    if(failure is AttachmentRecipientVerificationRequired)return "Verify the recipient's devices before sending a cached file. Open device verification for this contact and compare fingerprints. No file was uploaded."

    if(failure is java.io.EOFException||failure is java.net.SocketException||failure is java.net.SocketTimeoutException)
        return "The connection closed or timed out before the file was queued. Reconnect and retry this photo."
    if(failure is java.io.FileNotFoundException||failure is SecurityException)
        return "The selected photo could not be read. Choose it again from your gallery. No attachment was queued."
    val retained=" No attachment was sent; the private source is retained."
    if(failure is AttachmentModeUnavailable)return when(failure.reason){
        "direct_device_offline; cached_recipient_upgrade_required"->"Direct needs the recipient's verified device online. Cached delivery is unavailable because no recipient device supports cached delivery."+retained
        "direct_device_offline; cached_preflight_unavailable"->"Direct needs the recipient's verified device online. Cached delivery could not be confirmed with the server; retry when connected."+retained
        "direct_device_offline; cached_unsupported"->"Direct needs the recipient's verified device online. Cached delivery is not supported by this server or device."+retained
        "direct_device_offline; cached_readiness_timeout"->"Direct needs the recipient's verified device online. Cached delivery is not ready; reconnect and retry."+retained
        "cached_recipient_upgrade_required"->"Cached delivery is no longer available because no recipient device supports cached delivery."+retained
        "cached_unsupported"->"Cached delivery is not supported by this server or device."+retained
        "cached_readiness_lost"->"Cached delivery is no longer ready for this authenticated session. Reconnect and retry."+retained
        else->"The selected delivery mode is unavailable."+retained
    }
    val code=when(failure){
        is AttachmentCachePreflightRejected->failure.state
        is ServerRejected->failure.code
        else->null
    }
    val reason=when(code){
        "cache_access_denied"->"the server denied cached access"
        "cache_storage_unavailable"->"the server's encrypted cache storage is unavailable"
        "cache_dm_source_upgrade_required"->"this app needs an update for cached delivery"
        "cache_dm_recipient_upgrade_required"->"no recipient device currently supports cached delivery"
        "cache_expired"->"the cached offer expired"
        "cache_malformed_request"->"the server rejected the cached request"
        else->if(code!=null)"the server rejected it (${code.take(80).filter{it.isLetterOrDigit()||it=='_'}})" else null
    }
    if(reason!=null)return (if(mode==AttachmentSendMode.Direct)"Direct needs the recipient's verified device online; cached delivery failed because " else "Cached delivery failed because ")+reason+"."+retained
    return if(mode==AttachmentSendMode.Direct)"Direct transfer could not reach the recipient's verified online device."+retained
        else "Cached transfer could not be offered. Check connection, file access and available storage."+retained
}

internal fun projectMessaging(snapshot: MessagingSnapshot, previous: PeerChatUiState, groupCache:GroupUiProjectionCache?=null, generation:Long=0): PeerChatUiState {
    groupCache?.prepare(snapshot,generation)
    fun accountName(account:Long)=if(account==snapshot.accountId)snapshot.username else snapshot.conversations.firstOrNull{it.friend.accountId==account}?.friend?.username?:"Account $account"
    val conversations=snapshot.conversations.map { c ->
        val id="account:${c.friend.accountId}"
        val messages=chronologicalMessages(c.messages).map {m ->
            val outgoing=m.senderDevice==snapshot.localPeerId
            MessageUi(m.id,m.senderDevice,if(outgoing)snapshot.username else c.friend.username,m.text,outgoing,
                SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(m.timestamp)),
                date=SimpleDateFormat("yyyy-MM-dd",Locale.getDefault()).format(Date(m.timestamp)),
                status=if(!outgoing)MessageStatus.Received else when(m.delivery){2L->MessageStatus.Delivered;3L->MessageStatus.Stored;1L->MessageStatus.Sent;else->MessageStatus.Queued},
                timestamp=m.timestamp,logicalOrder=m.logicalOrder,
                deliveryTargets=if(outgoing)m.deliveryTargets?.let{manifest->DeliveryTargetsUi(manifest.selectionUnresolved,
                    manifest.targets.map{target->DeliveryTargetUi(target.deviceId,when(target.status){
                        DeliveryTargetStatus.Pending->DeliveryTargetStatusUi.Pending
                        DeliveryTargetStatus.Acknowledged->DeliveryTargetStatusUi.Received
                        DeliveryTargetStatus.Unavailable->DeliveryTargetStatusUi.Unavailable
                    },c.friend.deviceNames?.get(target.deviceId)?.takeIf(::validDeviceDisplayName).orEmpty())})}else null)
        }
        ConversationUi(id,c.friend.username,ChatKind.Direct,c.friend.online,messages.lastOrNull()?.text ?: "",
            messages.lastOrNull()?.time ?: "",messages=messages,hasOlder=c.messages.size==8||c.attachmentsHaveOlder,attachments=c.attachments)
    }+snapshot.groups.map {g->
        val roomState=snapshot.voiceRooms.singleOrNull{it.group==g.id&&g.ready&&it.roster==g.epoch}
        val build={
        val messages=g.messages.sortedWith(groupMessageOrder).map{m->val outgoing=m.device==snapshot.localPeerId
            MessageUi(m.id,m.device,if(outgoing)snapshot.username else accountName(m.account),m.text,outgoing,
                if(m.status=="attachment_reference")"Time unavailable" else SimpleDateFormat("HH:mm",Locale.getDefault()).format(Date(m.timestamp)),
                if(m.status=="attachment_reference")"Attachment references · original order unavailable" else SimpleDateFormat("yyyy-MM-dd",Locale.getDefault()).format(Date(m.timestamp)),
                if(!outgoing||m.status=="attachment_reference")MessageStatus.Received else if(m.status.startsWith("Recipients:")||m.status.startsWith("Server accepted"))MessageStatus.Sent else if(m.status.startsWith("Not submitted")||m.status.contains("unknown")||m.status.contains("rejected"))MessageStatus.Failed else MessageStatus.Queued,
                m.timestamp,m.order,retryAvailable=outgoing&&m.status!="attachment_reference"&&!m.status.startsWith("Recipients:"),deliveryDetail=if(m.status=="attachment_reference")"Authenticated public reference; no download authorization" else if(outgoing)m.status else "")
        }
        ConversationUi("group:${g.id}",g.name,ChatKind.Group,g.ready,messages.lastOrNull()?.text?:if(g.ready)"Encrypted group text" else "Waiting for signed membership and current group key",
            messages.lastOrNull()?.time?:"",unread=g.unread,messages=messages,hasOlder=g.hasOlder,attachments=g.attachments,
            members=g.memberRows.map{MemberUi(it.device,accountName(it.account),false,if(it.invited)"Invited" else when(it.role){3L->"Owner";2L->"Admin";else->"Member"})},
            voiceRooms=roomState?.let{live->live.rooms.map{room->
                val joined=snapshot.voice.active&&snapshot.voice.group==g.id&&snapshot.voice.room==room.id&&snapshot.voice.epoch==live.epoch
                VoiceRoomUi(room.id,room.name,room.members.map{it.name.ifBlank{accountName(it.account)}},joined,
                    muted=joined&&snapshot.voice.muted,deafened=joined&&snapshot.voice.deafened)
            }}.orEmpty())
        }
        groupCache?.project(g,roomState,snapshot.voice,build) ?: build()
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
        snapshot.voice.let { voice ->
            appendLine("available=${voice.available} active=${voice.active} call=${voice.id} peer=${voice.peer} lifetime=${voice.lifetime} state=${voice.state} direction=${voice.direction}")
            appendLine("media_generation=${voice.mediaGeneration} media_state=${voice.mediaState} media_route=${voice.mediaRoute} control_available=${voice.controlAvailable}")
            appendLine("muted=${voice.muted} deafened=${voice.deafened} speaker_requested=${voice.speaker}")
            if(voice.group.isNotEmpty())appendLine("group=${voice.group} room=${voice.room} room_epoch=${voice.epoch} participation=${voice.participation}")
            appendLine(voice.platformDiagnostic.take(160))
            appendLine(voice.mediaDiagnostics.take(17000))
            appendLine("media_route: 0=unavailable 1=LAN ICE 2=NAT ICE 3=TURN; group route counts report DTLS connections")
            appendLine("Decoded Opus FPS and end-to-end audio latency are not separately measured; NetEq playout includes concealment.")
        }
        appendLine("History Sync admission:")
        appendLine(snapshot.archiveAuthorizationDiagnostics)
        appendLine("File transfer measurements (native, confirmed/durable bytes):")
        appendLine(snapshot.transferDiagnostics)
        // Fresh, bounded projection from typed native transfer rows. Never
        // append the previous report or copy filenames, paths or payload data.
        appendLine("Transfer identity context (maximum 64 current transfers):")
        snapshot.conversations.asSequence().flatMap{conversation->conversation.attachments.asSequence().map{conversation to it}}
            .filter{(conversation,file)->file.id.matches(Regex("[0-9a-f]{32}"))&&file.peer.matches(Regex("[0-9a-f]{64}"))&&
                snapshot.localPeerId.matches(Regex("[0-9a-f]{64}"))&&file.peer in conversation.friend.knownPeerIds}
            .distinctBy{it.second.id}.take(64).forEach{(conversation,file)->
                appendLine("transfer_identity transfer_id=${file.id} local_account=${snapshot.accountId} peer_account=${conversation.friend.accountId}"+
                    " sender_device=${if(file.outgoing)snapshot.localPeerId else file.peer} recipient_device=${if(file.outgoing)file.peer else snapshot.localPeerId}"+
                    " authenticated=${snapshot.authenticated} server_presence_live=${snapshot.serverPresenceLive} network_revision=${conversation.connection.networkRevision}"+
                    " outgoing=${file.outgoing} native_state=${file.state} durable_or_acknowledged_bytes=${file.done} total_bytes=${file.size}")
            }
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
        voice=snapshot.voice,voicePeerName=if(snapshot.voice.group.isNotEmpty())conversations.firstOrNull{it.id=="group:${snapshot.voice.group}"}?.name.orEmpty() else snapshot.conversations.firstOrNull{it.friend.peerId==snapshot.voice.peer||snapshot.voice.peer in it.friend.knownPeerIds}?.friend?.username
            ?: if(snapshot.voice.peer.isEmpty()&&(previous.voiceBusy||previous.voiceError!=null))previous.voicePeerName else "",
        voiceConversationId=if(snapshot.voice.group.isNotEmpty())"group:${snapshot.voice.group}" else snapshot.conversations.firstOrNull{it.friend.peerId==snapshot.voice.peer||snapshot.voice.peer in it.friend.knownPeerIds}
            ?.let{"account:${it.friend.accountId}"} ?: if(snapshot.voice.peer.isEmpty()&&(previous.voiceBusy||previous.voiceError!=null))previous.voicePendingConversationId else "",
        voiceUiIdentity=if(snapshot.voice.active)previous.voiceUiIdentity.ifBlank{"${snapshot.voice.owner}|${snapshot.voice.peer}|${snapshot.voice.id}|${snapshot.voice.lifetime}"}
            else if(previous.voiceBusy||previous.voiceError!=null)previous.voiceUiIdentity else "",
        incomingRequests=snapshot.incomingRequests.map {MemberUi("account:${it.accountId}",it.username,false)},
        outgoingRequests=snapshot.outgoingRequests.map {MemberUi("account:${it.accountId}",it.username,false)},
        account=if(snapshot.authenticated||snapshot.localHistoryReady)AccountUi("account:${snapshot.accountId}",snapshot.username) else null,
        connection=when {snapshot.authenticated&&snapshot.serverPresenceLive->ConnectionState.Online
            snapshot.authenticated->ConnectionState.Reconnecting
            else->ConnectionState.Offline},
        load=if(conversations.isEmpty())LoadState.Empty else LoadState.Ready,recentCalls=emptyList(),groupInvitations=snapshot.groupInvitations,
        localDeviceFingerprint=if(snapshot.authenticated)snapshot.localPeerId else "",identityDirectoryStatus=snapshot.identityDirectoryStatus)
}

class NativePresenter(application: Application,private val backend:MessagingApplication,
    private val serverConfiguration:ServerConfigurationRepository=AndroidServerConfigurationStore(application),
    private val configurationDispatcher:CoroutineDispatcher=Dispatchers.IO,
    private val qol:AndroidQolRepository=AndroidQolStore(application),
    private val groupQol:AndroidQolRepository=AndroidGroupQolStore(application),
    private val navigationState:SavedStateHandle=SavedStateHandle(),
    private val inspectServer:(ServerEndpoint)->String={ServerChannel.inspectCertificate(it.host,it.port,it.certificatePin)}
):AndroidViewModel(application),PeerChatPresenter {
    // Preserve the original explicit six-argument constructor as well as
    // trailing-lambda call sites when injecting the separate group repository.
    constructor(application:Application,backend:MessagingApplication,serverConfiguration:ServerConfigurationRepository,
        configurationDispatcher:CoroutineDispatcher,qol:AndroidQolRepository,inspectServer:(ServerEndpoint)->String):
        this(application,backend,serverConfiguration,configurationDispatcher,qol,AndroidGroupQolStore(application),inspectServer=inspectServer)
    constructor(application:Application):this(application,AndroidMessagingRuntime.acquire(application))
    constructor(application:Application,savedState:SavedStateHandle):this(application,AndroidMessagingRuntime.acquire(application),navigationState=savedState)
    private val mutable=MutableStateFlow(PeerChatUiState(demo=false,connection=ConnectionState.Offline,recentCalls=emptyList(),startupStatus="Checking saved sign-in…"))
    override val state:StateFlow<PeerChatUiState> = mutable.asStateFlow()
    private var current=MessagingSnapshot()
    private var voicePermissionInFlight:VoiceConsent?=null
    private var voicePermissionTarget:VoiceCallTarget?=null
    private var voicePermissionRoomEpoch:String=""
    private var voiceStartPendingToken:String?=null
    private var voiceStartExpected:Triple<String,String,String>?=null
    private var voiceControlRevision=0L
    private var voicePreparationRevision=0L
    private fun releasePreparedVoice(includeService:Boolean=true) {
        val serviceToken=voiceStartPendingToken
        val keepService=!includeService&&serviceToken!=null
        val tokens=listOfNotNull(mutable.value.voiceConsent?.token,voicePermissionInFlight?.token,
            serviceToken.takeIf{includeService}).distinct()
        voicePermissionInFlight=null;voicePermissionTarget=null;voicePermissionRoomEpoch=""
        if(includeService){voiceStartPendingToken=null;voiceStartExpected=null}
        voicePreparationRevision++
        if(tokens.isNotEmpty())mutable.value=mutable.value.copy(voiceConsent=null,
            voicePreparationToken=if(keepService)serviceToken else null,
            voiceBusy=if(keepService)mutable.value.voiceBusy else false,
            voicePendingConversationId=if(keepService)mutable.value.voicePendingConversationId else "",
            voiceConversationId=if(keepService)mutable.value.voiceConversationId else "",
            voicePeerName=if(keepService)mutable.value.voicePeerName else "",
            voiceUiIdentity=if(keepService)mutable.value.voiceUiIdentity else "")
        tokens.forEach{token->viewModelScope.launch{
            try{com.peerchat.android.platform.PeerChatVoiceService.cancel(getApplication(),token)}catch(_:Exception){}
            try{backend.releaseVoice(token)}catch(_:Exception){}
        }}
    }
    override fun claimVoicePermission(token:String):Boolean {
        val consent=mutable.value.voiceConsent ?: return false
        if(consent.token!=token||voicePermissionInFlight!=null)return false
        voicePermissionInFlight=consent
        mutable.value=mutable.value.copy(voiceConsent=null,voicePreparationToken=token)
        return true
    }
    override fun resolveVoicePermission(context:Context,token:String,granted:Boolean,resumed:Boolean) {
        val consent=voicePermissionInFlight ?: return
        if(consent.token!=token)return
        val preparedTarget=voicePermissionTarget
        val preparedEpoch=voicePermissionRoomEpoch
        voicePermissionInFlight=null;voicePermissionTarget=null;voicePermissionRoomEpoch=""
        val stillCurrent=voiceConsentMatchesCurrent(consent,preparedTarget,current.voice,current.voiceRooms,preparedEpoch,current.localPeerId)
        if(!granted||!resumed||!foreground||!current.authenticated||!current.voice.available||!stillCurrent){
            mutable.value=mutable.value.copy(voiceBusy=false,voicePreparationToken=null,voiceError=if(!granted)"Microphone permission was denied. No voice started." else "Voice request expired. Try again.")
            viewModelScope.launch{try{backend.releaseVoice(consent.token)}catch(_:Exception){}}
            return
        }
        val started=try{com.peerchat.android.platform.PeerChatVoiceService.start(context,consent.token)}catch(_:Exception){false}
        voiceStartPendingToken=if(started)token else null
        voiceStartExpected=if(started)Triple(consent.peer,consent.id,consent.group) else null
        mutable.value=mutable.value.copy(voiceBusy=started,voicePreparationToken=if(started)token else null,voiceError=if(started)null else "Voice could not start. Try again.")
        if(!started)viewModelScope.launch{try{backend.releaseVoice(consent.token)}catch(_:Exception){}}
        else viewModelScope.launch{delay(35_000);if(voiceStartPendingToken==token){voiceStartPendingToken=null;voiceStartExpected=null;mutable.value=mutable.value.copy(voiceBusy=false,voicePreparationToken=null,voiceError="Call setup timed out. Review the current call before retrying.")}}
    }
    private val localSends=linkedMapOf<String,LocalSendPresentation>()
    private val localAttachments=linkedMapOf<String,LocalAttachmentPresentation>()
    private val localAttachmentJobs=mutableMapOf<String,Job>()
    private val localAttachmentStableIds=linkedMapOf<String,String>()
    private val observedAttachmentIds=java.util.concurrent.ConcurrentHashMap<String,Triple<Long,Long,String>>()
    private val locallyHiddenIds=mutableMapOf<String,MutableSet<String>>()
    private val pendingLocalDeletion=mutableMapOf<String,Pair<String,Boolean>>()
    private val locallyClearingConversations=mutableSetOf<String>()
    private var attachmentCompatibilityLocalToken:String?=null
    private fun hideLocally(conversation:String,id:String){locallyHiddenIds.getOrPut(conversation){mutableSetOf()}.add(id)}
    private fun unhideLocally(conversation:String,id:String){locallyHiddenIds[conversation]?.remove(id)}
    private fun evictDeletedHistory(conversation:String){conversation.removePrefix("account:").toLongOrNull()?.takeIf{conversation.startsWith("account:")}?.let{
        older.evict(it);exhausted.remove(it)
    }}
    private fun persistLocalMessageDeletion(conversation:String,id:String,attachment:Boolean,token:String?=null){
        val epoch=uiGeneration;val account=current.accountId
        viewModelScope.launch{
            try{backend.deleteLocalMessage(account,conversation,id,if(attachment)id else null)}
            catch(cancelled:CancellationException){throw cancelled}
            catch(_:Exception){if(epoch==uiGeneration&&account==current.accountId){
                unhideLocally(conversation,id);token?.let{unhideLocally(conversation,it)}
                project();mutable.value=mutable.value.copy(notice="Could not delete this local copy. It is visible again; retry when local storage is available.")
            }}
        }
    }
    private fun admittedLocalDeletion(token:String,id:String){
        val pending=pendingLocalDeletion.remove(token)?:return
        val (conversation,attachment)=pending
        hideLocally(conversation,id)
        persistLocalMessageDeletion(conversation,id,attachment,token)
    }
    private var navigationRestored=false
    private val uiTimings=OwnerStageTimings()
    private val uiCreatedAt=System.nanoTime()
    private var firstLocalFrame=false
    private var chatOpenedAt=0L
    private val localSendStarted=linkedMapOf<String,Pair<Long,String?>>()
    override fun rendered(conversation:String?,messageIds:Set<String>){
        val now=System.nanoTime();var changed=false
        if(!firstLocalFrame&&mutable.value.conversations.isNotEmpty()){
            firstLocalFrame=true;changed=true;uiTimings.record(OwnerStage.UiLocalListFrame,now-uiCreatedAt)
        }
        if(conversation!=null&&conversation==mutable.value.selectedId&&messageIds.isNotEmpty()&&chatOpenedAt>0){
            uiTimings.record(OwnerStage.UiChatFrame,now-chatOpenedAt);chatOpenedAt=0;changed=true
        }
        val iterator=localSendStarted.iterator()
        while(iterator.hasNext()){
            val (token,observation)=iterator.next();val pending=localSends[token]
            if(token in messageIds||observation.second in messageIds){uiTimings.record(OwnerStage.UiSendFrame,now-observation.first);iterator.remove();changed=true}
            else if(pending==null&&observation.second==null)iterator.remove()
        }
        if(changed)mutable.value=mutable.value.copy(presentationDiagnostics=uiTimings.summary())
    }
    private var configurationReady=false
    private var configurationInvalid=false
    private var configurationSaving=false
    private var startupJob:Job?=null
    private var loginJob:Job?=null
    private var startupWatchdog:Job?=null
    private var recovering=false
    private var automaticResumeSuppressed=false
    private var uiGeneration=0L
    private var peopleSearch:Job?=null
    private var peopleWatchdog:Job?=null
    private val peopleSearchMutex=Mutex()
    private var peopleRevision=0L
    private fun searchPeople(text:String){
        val revision=++peopleRevision;peopleWatchdog?.cancel();peopleSearch?.cancel()
        val query=text.trim().take(32);val epoch=uiGeneration;val account=current.accountId
        fun currentSearch()=revision==peopleRevision&&epoch==uiGeneration&&account==current.accountId&&current.authenticated&&current.serverPresenceLive
        mutable.value=mutable.value.copy(peopleQuery=query,peopleResults=emptyList(),peopleLoading=false,peopleError=null)
        if(!validPeopleQuery(query))return
        if(!current.authenticated||!current.serverPresenceLive){mutable.value=mutable.value.copy(peopleError="Search unavailable while offline. Reconnect and retry.");return}
        mutable.value=mutable.value.copy(peopleLoading=true)
        // Independent from the worker: cancellation/withTimeout alone waits for
        // blocking platform I/O to unwind. Include debounce and mutex waiting in
        // this UI budget without starting another concurrent network lookup.
        peopleWatchdog=viewModelScope.launch{
            delay(12_000)
            if(currentSearch()&&mutable.value.peopleLoading){
                peopleRevision++
                retainDiagnostic("People search","stage=deadline_exceeded budget_ms=12000")
                mutable.value=mutable.value.copy(peopleLoading=false,peopleError="People search timed out. Check your connection, then tap Refresh or edit the query to retry.")
                peopleSearch?.cancel()
            }
        }
        peopleSearch=viewModelScope.launch{
            try{
                delay(350)
                val result=peopleSearchMutex.withLock {coroutineContext.ensureActive();backend.searchPeople(query)}
                if(currentSearch())
                    mutable.value=mutable.value.copy(peopleResults=result,peopleLoading=false)
            }catch(error:Exception){
                if(currentSearch())
                    mutable.value=mutable.value.copy(peopleLoading=false,peopleError=when{
                        error is CancellationException->"People search was interrupted. Tap Refresh or edit the query to retry."
                        error is ServerRejected && error.code=="search_rate_limited"->"Search is temporarily limited. Wait a moment and try again."
                        error is ServerRejected && error.code=="unsupported_request"->"This server does not support authorized partial search. Ask the operator to update it."
                        else->"People search unavailable. Check your connection and try again."})
                if(error is CancellationException)throw error
            }finally{
                if(currentSearch()){
                    peopleWatchdog?.cancel()
                    mutable.value=mutable.value.copy(peopleLoading=false)
                }
            }
        }
    }
    private fun contactOperation(success:String,operation:suspend ()->Unit){
        if(mutable.value.contactBusy||!current.authenticated||!current.serverPresenceLive)return
        val epoch=uiGeneration;val account=current.accountId
        mutable.value=mutable.value.copy(contactBusy=true,notice=null)
        viewModelScope.launch{
            try{operation();if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(notice=success)}
            catch(error:Exception){if(error is CancellationException)throw error
                if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(notice="Request could not complete. Refresh to check its current server state; authorization is unchanged.")}
            finally{if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(contactBusy=false)}
        }
    }
    private val retainedDiagnostics=ArrayDeque<String>()
    private val previewLoading=mutableSetOf<String>()
    private val largeDrafts=mutableMapOf<String,String>()
    private val earlyDrafts=mutableMapOf<String,String>()
    private val earlyGroupDrafts=mutableMapOf<String,String>()
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
        recovering=true;automaticResumeSuppressed=true;uiGeneration++;localSends.clear();localSendStarted.clear()
        localAttachments.clear();localAttachmentJobs.values.forEach{it.cancel()};localAttachmentJobs.clear();localAttachmentStableIds.clear();observedAttachmentIds.clear();attachmentCompatibilityLocalToken=null
        voicePreparationRevision++;releasePreparedVoice();voiceStartPendingToken=null;voiceStartExpected=null
        peopleRevision++;peopleSearch?.cancel();peopleWatchdog?.cancel()
        startupJob?.cancel();loginJob?.cancel();startupWatchdog?.cancel();loginAfterTrust=null
        mutable.value=mutable.value.copy(route=destination,startupStatus=null,loginBusy=false,loginError=null,notice=null,serverTrust=null,archiveEnrollment=null,archiveBusy=false,connection=ConnectionState.Offline,
            peopleLoading=false,peopleResults=emptyList(),peopleError=null,sendBusy=false,sendAccepted=false,localSendCount=0,voiceBusy=false,
            attachmentContextGeneration=uiGeneration)
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
    private val groupUiProjection=GroupUiProjectionCache()
    private val older=ConversationHistoryCache<Long,MessageSnapshot>(512,8L*1024*1024,::messageHistoryBytes)
    private val exhausted=mutableSetOf<Long>()
    private var attachmentCompatibilityScope:Pair<Long,Long>?=null
    private var attachmentCompatibilityConversation:String?=null
    private fun project() {
        observedAttachmentIds.entries.forEach{(token,observed)->
            if(observed.first==uiGeneration&&observed.second==current.accountId){
                localAttachments[token]?.let{localAttachments[token]=it.copy(nativeId=observed.third);localAttachmentStableIds[observed.third]=token}
                admittedLocalDeletion(token,observed.third)
            }
            observedAttachmentIds.remove(token,observed)
        }
        val visible=mutable.value.selectedId?.takeIf{mutable.value.route in setOf(Route.Conversation,Route.Details)&&it.startsWith("account:")}?.removePrefix("account:")?.toLongOrNull()
        exhausted.removeAll(older.select(visible))
        if(attachmentCompatibilityScope?.let{it.first!=current.accountId||it.second!=uiGeneration||!current.authenticated||mutable.value.route!=Route.Conversation||attachmentCompatibilityConversation!=mutable.value.selectedId}==true){
            val choice=mutable.value.attachmentCompatibility
            attachmentCompatibilityLocalToken?.let{localAttachments.remove(it)}
            attachmentCompatibilityLocalToken=null
            attachmentCompatibilityScope=null;attachmentCompatibilityConversation=null;mutable.value=mutable.value.copy(attachmentCompatibility=null,attachmentCompatibilityBusy=false)
            if(choice!=null)viewModelScope.launch{try{backend.resolveAttachmentCompatibility(choice.token,null)}catch(_:Exception){}}
        }
        val combined=current.copy(conversations=current.conversations.map {c ->
            c.copy(messages=chronologicalMessages((older[c.friend.accountId] ?: emptyList())+c.messages))
        })
        // projectMessaging's paging hint is overridden by the actual native page result.
        mutable.value=projectMessaging(combined,mutable.value,groupUiProjection,uiGeneration).let {s->s.copy(attachmentContextGeneration=uiGeneration,conversations=s.conversations.map {c->
            if(c.kind==ChatKind.Group)c else c.copy(hasOlder=c.id.removePrefix("account:").toLongOrNull() !in exhausted||current.conversations.any{original->"account:${original.friend.accountId}"==c.id&&original.attachmentsHaveOlder})
        })}
        val nativeIds=mutable.value.conversations.flatMap{c->c.messages.map{it.id}+c.attachments.map{it.id}}.toSet()
        localSends.entries.removeAll{it.value.nativeId in nativeIds}
        mutable.value=mutable.value.copy(localSendCount=localSends.size,conversations=presentLocalSends(mutable.value.conversations,localSends.values.toList(),current.localPeerId,current.username))
        val nativeAttachments=mutable.value.conversations.flatMap{it.attachments}.associateBy{it.id}
        localAttachments.entries.removeAll{entry->entry.value.nativeId?.let{nativeAttachments[it]?.let{row->row.complete&&row.localPath.isNotEmpty()}}==true}
        while(localAttachmentStableIds.size>64)localAttachmentStableIds.remove(localAttachmentStableIds.keys.first())
        mutable.value=mutable.value.copy(
            conversations=presentLocalAttachments(mutable.value.conversations,localAttachments.values.toList()),
            localAttachmentPreviews=localAttachments.values.associate{(it.nativeId?:it.token) to it.uri},
            localAttachmentIdentityKeys=localAttachmentStableIds.mapValues{"attachment-${it.value}"})
        mutable.value=mutable.value.copy(conversations=mutable.value.conversations.map{c->
            val hidden=locallyHiddenIds[c.id].orEmpty()
            if(hidden.isEmpty()&&c.id !in locallyClearingConversations)c else {
                val messages=c.messages.filter{it.id !in hidden}
                val visibleMessages=if(c.id in locallyClearingConversations)emptyList() else messages
                c.copy(messages=visibleMessages,attachments=if(c.id in locallyClearingConversations)emptyList() else c.attachments.filter{it.id !in hidden},
                    preview=visibleMessages.lastOrNull()?.text.orEmpty(),time=visibleMessages.lastOrNull()?.time.orEmpty())
            }
        })
        if(retainedDiagnostics.isNotEmpty())mutable.value=mutable.value.copy(diagnostics=mutable.value.diagnostics+"\nRetained application diagnostics:\n"+retainedDiagnostics.joinToString("\n"))
        val accountKey=preferenceAccountKey() ?: return
        if(!navigationRestored&&(current.localHistoryReady||current.authenticated)){
            val selected=navigationState.get<String>("conversation")
            if(navigationState.get<String>("owner")!=accountKey||selected==null)navigationRestored=true
            if(navigationState.get<String>("owner")==accountKey&&selected!=null&&mutable.value.route in setOf(Route.Home,Route.Welcome)&&mutable.value.conversations.any{it.id==selected}){
                navigationRestored=true
                dispatch(UiAction.Select(selected))
            }
        }
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
            mutable.value=mutable.value.copy(conversations=unseen,drafts=unseen.associate{c->c.id to (if(c.kind==ChatKind.Group)mutable.value.drafts[c.id]?:"" else largeDrafts[c.id] ?: qolState.conversations[conversationPreferenceKey(accountKey,c.id.removePrefix("account:").toLong())]?.draft ?: "")},notificationsEnabled=qolState.notifications,notificationPreview=qolState.preview,attachmentSendMode=qolState.attachmentSendMode)
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
                c.id to (largeDrafts[c.id] ?: groupQolState.conversations[groupPreferenceKey(accountKey,c.id.removePrefix("group:"))]?.draft ?: "")
            })
        }
    }
    init {
    // Collect during authentication, rather than only after restore completes.
    viewModelScope.launch {backend.state.collect {snapshot ->
        if(recovering)return@collect
        if(snapshot.accountId!=current.accountId||!snapshot.authenticated){
            voicePreparationRevision++;releasePreparedVoice();voiceStartPendingToken=null;voiceStartExpected=null
            mutable.value=mutable.value.copy(voiceBusy=false)
        }
        if(voiceStartPendingToken!=null&&voiceActivationPublished(snapshot.voice)){
            val expected=voiceStartExpected
            voiceStartPendingToken=null;voiceStartExpected=null
            val matched=snapshot.voice.active&&expected==Triple(snapshot.voice.peer,snapshot.voice.id,snapshot.voice.group)
            mutable.value=mutable.value.copy(voiceBusy=false,voicePreparationToken=null,voiceError=if(matched)null else snapshot.voice.platformDiagnostic.take(160).ifEmpty{"Call request changed. Review the current call before retrying."})
        }
        if(snapshot.accountId!=current.accountId||!snapshot.authenticated||!snapshot.serverPresenceLive){
            peopleRevision++;peopleSearch?.cancel();peopleWatchdog?.cancel()
            mutable.value=mutable.value.copy(peopleResults=emptyList(),peopleLoading=false,contactBusy=false,
                peopleQuery=if(snapshot.accountId!=current.accountId)"" else mutable.value.peopleQuery,
                peopleError=if(snapshot.accountId!=current.accountId)null else if(mutable.value.peopleLoading)"Search interrupted by a connection change. Reconnect and retry." else mutable.value.peopleError)
        }
        if(snapshot.accountId!=current.accountId)mutable.value=mutable.value.copy(transferSettings=null,transferSettingsBusy=false,transferSettingsError=null)
        if(snapshot.accountId!=current.accountId){older.clear();exhausted.clear();largeDrafts.clear();localSends.clear();localSendStarted.clear();chatOpenedAt=0
            localAttachments.clear();localAttachmentJobs.values.forEach{it.cancel()};localAttachmentJobs.clear();localAttachmentStableIds.clear();observedAttachmentIds.clear();attachmentCompatibilityLocalToken=null
            locallyHiddenIds.clear();pendingLocalDeletion.clear();locallyClearingConversations.clear()
            mutable.value=mutable.value.copy(sendBusy=false,sendAccepted=false,localSendCount=0)
            if(current.accountId>0){navigationState.remove<String>("conversation");navigationState.remove<String>("owner");mutable.value=mutable.value.copy(selectedId=null,route=if(mutable.value.route in setOf(Route.Conversation,Route.Details))Route.Home else mutable.value.route)}
            mutable.value=mutable.value.copy(attachmentPreviews=emptyMap(),localAttachmentPreviews=emptyMap(),localAttachmentIdentityKeys=emptyMap(),drafts=emptyMap())}
        current=snapshot
        if(foreground&&mutable.value.route==Route.Conversation){
            val group=snapshot.groups.find{"group:${it.id}"==mutable.value.selectedId&&it.unread>0}
            if(group!=null)viewModelScope.launch{try{backend.groupHistory(group.id)}catch(_:Exception){}}
        }
        if(!snapshot.authenticated||!snapshot.serverPresenceLive||mutable.value.archiveEnrollment?.accountId!=snapshot.accountId)mutable.value=mutable.value.copy(archiveEnrollment=null)
        mutable.value=mutable.value.copy(mailboxStorageAllowed=snapshot.mailboxStorageAllowed,archiveProgress=snapshot.archiveProgress,
            ownDevices=snapshot.ownDevices?.takeIf{snapshot.authenticated&&it.account==snapshot.accountId})
        if(!automaticResumeSuppressed&&snapshot.localHistoryReady&&mutable.value.route in setOf(Route.Welcome,Route.Login)) {
            mutable.value=mutable.value.copy(startupStatus=null,route=Route.Home)
        }
        if(mutable.value.startupStatus!=null) {
            mutable.value=when {
                snapshot.authenticated&&snapshot.serverPresenceLive -> finishLocalStartup(mutable.value,true).copy(rememberMe=true)
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
        // Protected preferences do not gate the protected local conversation
        // index. Publish them independently when ready, without network work.
        viewModelScope.launch {
        try{qolState=withContext(configurationDispatcher){qol.load()};qolReady=true
            mutable.value=mutable.value.copy(attachmentSendMode=qolState.attachmentSendMode)
            earlyDrafts.forEach{(key,text)->
                qolState=qolState.copy(conversations=qolState.conversations+(key to (qolState.conversations[key] ?: ConversationPreference()).copy(draft=text)))
                savePreference(key){it.copy(draft=text)}
            };earlyDrafts.clear()}
        catch(failure:Exception){if(failure is CancellationException)throw failure;mutable.value=mutable.value.copy(notificationsEnabled=false,notice="Local preferences could not be verified; notifications remain unavailable.")}
        try{groupQolState=withContext(configurationDispatcher){groupQol.load()};groupQolReady=true
            earlyGroupDrafts.forEach{(key,text)->
                val update:(ConversationPreference)->ConversationPreference={it.copy(draft=text)}
                if(updateGroupPreference(key,update))saveGroupPreference(key,update)
            };earlyGroupDrafts.clear()}
        catch(failure:Exception){if(failure is CancellationException)throw failure;mutable.value=mutable.value.copy(notice="Saved group drafts and read position could not be verified.")}
        project()
        }
        var settingsVerified=false
        try {
            val saved=withContext(configurationDispatcher){serverConfiguration.load()}
            settingsVerified=true
            if(saved!=null){
                mutable.value=mutable.value.copy(server=ServerUi(saved.host,saved.port.toString(),saved.certificatePin),loginBusy=true,startupStatus="Signing in…")
                if(backend.restoreLocalHistory(saved))mutable.value=finishLocalStartup(mutable.value,true)
                try{if(backend.restoreRemembered(saved)) {
                    val available=backend.state.value.serverPresenceLive||backend.state.value.localHistoryReady
                    mutable.value=finishLocalStartup(mutable.value,available).copy(rememberMe=true,startupStatus=if(available)null else "Connecting…")
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
                mutable.value=finishLocalStartup(mutable.value,true).copy(loginBusy=false,loginError=null)
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
        if(!active){
            voicePreparationRevision++
            releasePreparedVoice(includeService=false)
            if(voiceStartPendingToken==null)mutable.value=mutable.value.copy(voiceBusy=false)
        }
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
    private fun beginLocalSend(conversation:String,text:String,command:suspend()->String,clear:()->Unit){
        if(localSends.size>=16){mutable.value=mutable.value.copy(notice="Wait for the local history view to settle before sending more messages.");return}
        // Retain the captured draft through the existing protected preference
        // path. Clearing the visible composer must not persist an empty draft
        // before the native owner confirms its local commit.
        dispatch(UiAction.Draft(conversation,text))
        val epoch=uiGeneration;val account=current.accountId
        val token="local-saving-"+java.util.UUID.randomUUID().toString()
        val row=LocalSendPresentation(token,conversation,text,System.currentTimeMillis(),mutable.value.conversations.find{it.id==conversation}?.messages?.map{it.id}?.toSet()?:emptySet())
        localSends[token]=row
        largeDrafts[conversation]=""
        val started=System.nanoTime()
        if(localSendStarted.size>=16)localSendStarted.remove(localSendStarted.keys.first())
        localSendStarted[token]=started to null
        mutable.value=mutable.value.copy(sendBusy=true,sendAccepted=false,localSendCount=localSends.size,drafts=mutable.value.drafts+(conversation to ""),
            conversations=presentLocalSends(mutable.value.conversations,localSends.values.toList(),current.localPeerId,current.username))
        viewModelScope.launch {
            var accepted=false
            try{
                val id=command();require(id.matches(Regex("[0-9a-f]{32}")))
                if(epoch!=uiGeneration||account!=current.accountId)return@launch
                localSendStarted[token]?.let{localSendStarted[token]=it.first to id}
                uiTimings.record(OwnerStage.UiDurableReturn,System.nanoTime()-started);mutable.value=mutable.value.copy(presentationDiagnostics=uiTimings.summary())
                accepted=true;localSends[token]=row.copy(nativeId=id)
                admittedLocalDeletion(token,id)
                localSends.replaceAll{other,pending->if(other!=token&&pending.conversation==conversation)pending.copy(baseline=pending.baseline+id)else pending}
                val nextDraft=mutable.value.drafts[conversation].orEmpty()
                if(localSends.values.none{it.conversation==conversation&&it.nativeId==null}){
                    if(nextDraft.isEmpty())clear() else dispatch(UiAction.Draft(conversation,nextDraft))
                }
            }catch(failure:Exception){
                if(failure is CancellationException)throw failure
                if(epoch==uiGeneration&&account==current.accountId){localSends[token]=row.copy(failed=true);mutable.value=mutable.value.copy(notice="Local save was not confirmed. The captured message is retained in its failed bubble and saved draft; check history before restoring it.")}
            }finally{
                if(epoch==uiGeneration&&account==current.accountId){project();mutable.value=mutable.value.copy(sendBusy=localSends.values.any{it.nativeId==null&&!it.failed},sendAccepted=accepted,sendCompletion=mutable.value.sendCompletion+1)}
            }
        }
    }
    private fun beginLocalAttachment(conversation:String,uri:android.net.Uri,mode:AttachmentSendMode,command:suspend((String)->Unit)->String){
        if(localAttachments.size>=16){mutable.value=mutable.value.copy(notice="Wait for attachment preparation to settle before sending another file.");return}
        val epoch=uiGeneration;val account=current.accountId
        val token="local-attachment-${UUID.randomUUID()}"
        val row=LocalAttachmentPresentation(token,conversation,uri,System.currentTimeMillis(),
            mutable.value.conversations.find{it.id==conversation}?.attachments?.map{it.id}?.toSet()?:emptySet(),mode)
        localAttachments[token]=row
        project() // The provisional bubble is published before metadata access or the suspend command.
        viewModelScope.launch {
            try {
                val metadata=withContext(Dispatchers.IO){try{
                    getApplication<Application>().contentResolver.query(uri,arrayOf(OpenableColumns.DISPLAY_NAME,OpenableColumns.SIZE),null,null,null)?.use{cursor->
                        if(cursor.moveToFirst()){
                            val nameIndex=cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                            val sizeIndex=cursor.getColumnIndex(OpenableColumns.SIZE)
                            val name=if(nameIndex>=0)cursor.getString(nameIndex).orEmpty().take(120).ifBlank{"Attachment"}else "Attachment"
                            val size=if(sizeIndex>=0&&!cursor.isNull(sizeIndex))cursor.getLong(sizeIndex).coerceIn(0,5L*1024*1024*1024)else 0L
                            name to size
                        }else null
                    }
                }catch(_:Exception){null}}
                if(epoch==uiGeneration&&account==current.accountId&&metadata!=null){
                    localAttachments[token]?.let{localAttachments[token]=it.copy(name=metadata.first,size=metadata.second);project()}
                }
            }catch(cancelled:CancellationException){throw cancelled}
        }
        localAttachmentJobs[token]=viewModelScope.launch {
            try{
                val id=command{queued->
                    if(queued.matches(Regex("[0-9a-f]{32}"))){
                        observedAttachmentIds[token]=Triple(epoch,account,queued)
                        viewModelScope.launch{if(epoch==uiGeneration&&account==current.accountId)project()}
                    }
                };require(id.matches(Regex("[0-9a-f]{32}")))
                if(epoch!=uiGeneration||account!=current.accountId)return@launch
                localAttachments[token]?.let{localAttachments[token]=it.copy(nativeId=id,phase=LocalAttachmentPhase.Preparing);localAttachmentStableIds[id]=token}
                admittedLocalDeletion(token,id)
                localAttachments.replaceAll{other,pending->if(other!=token&&pending.conversation==conversation)pending.copy(baseline=pending.baseline+id)else pending}
                project()
            }catch(required:AttachmentCompatibilityRequired){
                if(epoch==uiGeneration&&account==current.accountId&&current.authenticated&&mutable.value.route==Route.Conversation&&mutable.value.selectedId==conversation){
                    attachmentCompatibilityScope=account to epoch;attachmentCompatibilityConversation=conversation;attachmentCompatibilityLocalToken=token
                    localAttachments[token]?.let{localAttachments[token]=it.copy(phase=LocalAttachmentPhase.ChoosingMode)}
                    project();mutable.value=mutable.value.copy(attachmentCompatibility=required.choice,attachmentCompatibilityBusy=false)
                }else{
                    try{backend.resolveAttachmentCompatibility(required.choice.token,null)}catch(_:Exception){}
                    if(epoch==uiGeneration&&account==current.accountId){localAttachments.remove(token);project()}
                }
            }catch(admitted:QueuedAttachmentFailure){
                if(epoch==uiGeneration&&account==current.accountId){
                    localAttachments[token]?.let{localAttachments[token]=it.copy(nativeId=admitted.transferId,phase=LocalAttachmentPhase.Failed);localAttachmentStableIds[admitted.transferId]=token}
                    admittedLocalDeletion(token,admitted.transferId)
                    project();mutable.value=mutable.value.copy(notice="The attachment entered the transfer queue, but preparation reported an error. Check this transfer before retrying.")
                }
            }catch(admitted:QueuedAttachmentCancellation){
                if(epoch==uiGeneration&&account==current.accountId){
                    localAttachments[token]?.let{localAttachments[token]=it.copy(nativeId=admitted.transferId,phase=LocalAttachmentPhase.Cancelled);localAttachmentStableIds[admitted.transferId]=token}
                    admittedLocalDeletion(token,admitted.transferId)
                    project()
                }
                throw admitted
            }catch(cancelled:CancellationException){
                if(epoch==uiGeneration&&account==current.accountId){
                    localAttachments[token]?.let{if(it.nativeId==null)localAttachments[token]=it.copy(phase=LocalAttachmentPhase.Cancelled)}
                    project()
                }
                throw cancelled
            }catch(failure:Exception){
                if(epoch==uiGeneration&&account==current.accountId){
                    val failureText=attachmentSendFailureText(mode,failure)
                    retainDiagnostic("Attachment preparation","stage=before_admission reason=${attachmentPreparationFailureReason(failure)} mode=${mode.name}")
                    localAttachments[token]?.let{localAttachments[token]=it.copy(phase=LocalAttachmentPhase.Failed,failureText=failureText)}
                    project();mutable.value=mutable.value.copy(notice=failureText)
                }
            }finally{localAttachmentJobs.remove(token)}
        }
    }
    override fun dispatch(action:UiAction) {
        when(action) {
            is UiAction.CancelVoicePreparation -> {
                val consent=mutable.value.voiceConsent
                val inFlight=voicePermissionInFlight
                val pending=voiceStartPendingToken
                if(action.uiIdentity.isBlank()||action.uiIdentity!=mutable.value.voiceUiIdentity||!mutable.value.voiceBusy)return
                if(action.token!=null&&action.token !in listOfNotNull(consent?.token,inFlight?.token,pending))return
                ++voicePreparationRevision
                if(inFlight?.token==action.token){voicePermissionInFlight=null;voicePermissionTarget=null;voicePermissionRoomEpoch=""}
                if(consent?.token==action.token)mutable.value=mutable.value.copy(voiceConsent=null)
                if(pending==action.token){voiceStartPendingToken=null;voiceStartExpected=null}
                mutable.value=mutable.value.copy(voiceBusy=false,voicePreparationToken=null,voicePendingConversationId="",
                    voiceConversationId="",voicePeerName="",voiceUiIdentity="",voiceError=null,voicePendingRoomId="")
                viewModelScope.launch {
                    action.token?.let{token->
                        try{com.peerchat.android.platform.PeerChatVoiceService.cancel(getApplication(),token)}catch(_:Exception){}
                        try{backend.releaseVoice(token)}catch(_:Exception){}
                    }
                }
            }
            is UiAction.StartDmVoice,is UiAction.AnswerDmVoice -> {
                if(mutable.value.demo||!current.authenticated||!current.voice.available||mutable.value.voiceBusy||mutable.value.voiceConsent!=null||voicePermissionInFlight!=null)return
                val peer=when(action){
                    is UiAction.StartDmVoice->current.conversations.firstOrNull{"account:${it.friend.accountId}"==action.conversationId}?.friend?.peerId
                    is UiAction.AnswerDmVoice->action.target.takeIf{it==current.voice.target&&current.voice.direction==1L&&current.voice.state==2L}?.peer
                    else->null
                } ?: return
                if(action is UiAction.StartDmVoice&&current.voice.active)return
                val answer=(action as? UiAction.AnswerDmVoice)?.target
                val epoch=uiGeneration;val account=current.accountId;val revision=++voicePreparationRevision
                val pendingConversation=(action as? UiAction.StartDmVoice)?.conversationId.orEmpty()
                val pendingName=if(pendingConversation.isNotEmpty())current.conversations.firstOrNull{"account:${it.friend.accountId}"==pendingConversation}?.friend?.username.orEmpty() else mutable.value.voicePeerName
                mutable.value=mutable.value.copy(voiceBusy=true,voiceError=null,voicePendingRoomId="",
                    voicePendingConversationId=pendingConversation.ifEmpty{mutable.value.voicePendingConversationId},
                    voicePeerName=pendingName.ifEmpty{mutable.value.voicePeerName},
                    voiceUiIdentity=if(action is UiAction.StartDmVoice)"dm:${UUID.randomUUID()}" else mutable.value.voiceUiIdentity.ifEmpty{"incoming:${(action as UiAction.AnswerDmVoice).target.id}"})
                viewModelScope.launch {
                    try {
                        val consent=backend.prepareVoice(peer,answer)
                        if(revision==voicePreparationRevision&&epoch==uiGeneration&&account==current.accountId&&current.authenticated&&foreground&&
                            (answer==null||current.voice.target==answer)){
                            voicePermissionTarget=answer
                            voicePermissionRoomEpoch=""
                            mutable.value=mutable.value.copy(voiceConsent=consent,voicePreparationToken=consent.token)
                        }
                        else backend.releaseVoice(consent.token)
                    }catch(error:Exception){
                        if(error is CancellationException)throw error
                        android.util.Log.w("PeerChatVoice","stage=consent_prepare result="+error.javaClass.simpleName)
                        if(revision==voicePreparationRevision&&epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(voiceError="Call preparation failed. Check the current connection and retry.")
                    }finally{if(revision==voicePreparationRevision&&epoch==uiGeneration&&account==current.accountId&&mutable.value.voiceConsent==null)mutable.value=mutable.value.copy(voiceBusy=false)}
                }
            }
            is UiAction.JoinVoice -> {
                val group=mutable.value.selectedId?.takeIf{it.startsWith("group:")}?.removePrefix("group:") ?: return
                val roomState=current.voiceRooms.singleOrNull{it.group==group} ?: return
                if(roomState.rooms.none{it.id==action.roomId}||current.groups.none{it.id==group&&it.ready}||
                    mutable.value.demo||!current.authenticated||!current.voice.available||current.voice.active||
                    mutable.value.voiceBusy||mutable.value.voiceConsent!=null||voicePermissionInFlight!=null)return
                val generation=uiGeneration;val account=current.accountId;val revision=++voicePreparationRevision
                mutable.value=mutable.value.copy(voiceBusy=true,voiceError=null,voicePendingRoomId=action.roomId,
                    voicePendingConversationId="group:$group",voiceConversationId="group:$group",
                    voicePeerName=mutable.value.conversations.firstOrNull{it.id=="group:$group"}?.name.orEmpty(),
                    voiceUiIdentity="group:${UUID.randomUUID()}")
                viewModelScope.launch {
                    try {
                        val consent=backend.prepareGroupVoice(group,action.roomId)
                        val stillPresent=current.voiceRooms.singleOrNull{it.group==group&&it.epoch==roomState.epoch}
                            ?.rooms?.singleOrNull{it.id==action.roomId}!=null
                        if(revision==voicePreparationRevision&&generation==uiGeneration&&account==current.accountId&&
                            current.authenticated&&foreground&&!current.voice.active&&stillPresent&&
                            consent.group==group&&consent.id==action.roomId&&consent.peer==current.localPeerId&&!consent.answer){
                            voicePermissionTarget=null
                            voicePermissionRoomEpoch=roomState.epoch
                            mutable.value=mutable.value.copy(voiceConsent=consent,voicePreparationToken=consent.token)
                        }else backend.releaseVoice(consent.token)
                    }catch(error:Exception){
                        if(error is CancellationException)throw error
                        if(revision==voicePreparationRevision&&generation==uiGeneration&&account==current.accountId)
                            mutable.value=mutable.value.copy(voiceError="Voice room could not be joined. Check the current room and try again.")
                    }finally{if(revision==voicePreparationRevision&&generation==uiGeneration&&account==current.accountId&&mutable.value.voiceConsent==null)
                        mutable.value=mutable.value.copy(voiceBusy=false)}
                }
            }
            is UiAction.ControlDmVoice -> {
                if(action.target!=current.voice.target||mutable.value.voiceBusy)return
                val revision=++voiceControlRevision;val account=current.accountId;val epoch=uiGeneration
                mutable.value=mutable.value.copy(voiceBusy=true,voiceError=null)
                viewModelScope.launch{try{backend.controlVoice(action.target,action.control,action.value)}
                    catch(error:Exception){if(error is CancellationException)throw error
                        if(revision==voiceControlRevision&&account==current.accountId&&epoch==uiGeneration&&action.target==current.voice.target)
                            mutable.value=mutable.value.copy(voiceError="Call control could not complete. Review the current call and retry.")}
                    finally{if(revision==voiceControlRevision)mutable.value=mutable.value.copy(voiceBusy=false)}}
            }
            is UiAction.SpeakerDmVoice -> {
                if(action.target!=current.voice.target||mutable.value.voiceBusy)return
                val changed=try{com.peerchat.android.platform.PeerChatVoiceService.route(getApplication(),action.target,action.enabled)}catch(_:Exception){false}
                if(!changed)mutable.value=mutable.value.copy(voiceError="Speaker route could not change for this call.")
            }
            is UiAction.AudioRouteDmVoice -> {
                if(action.target!=current.voice.target||mutable.value.voiceBusy)return
                val changed=try{com.peerchat.android.platform.PeerChatVoiceService.route(getApplication(),action.target,action.deviceId)}catch(_:Exception){false}
                if(!changed)mutable.value=mutable.value.copy(voiceError="Audio route could not change. Choose a device that is still connected.")
            }
            UiAction.Back -> {
                if(mutable.value.route==Route.Conversation)navigationState.remove<String>("conversation")
                mutable.value=reduceUi(mutable.value,action)
                project()
            }
            is UiAction.Select -> {
                mutable.value=reduceUi(mutable.value,action)
                if(mutable.value.selectedId!=action.id||mutable.value.route!=Route.Conversation)return
                navigationRestored=true
                chatOpenedAt=System.nanoTime()
                preferenceAccountKey()?.let{navigationState["owner"]=it;navigationState["conversation"]=action.id}
                project()
                if(action.id.startsWith("group:")){
                    viewModelScope.launch{try{backend.groupHistory(action.id.removePrefix("group:"))}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group history requires current verified authorization.")}}
                    return
                }
                val conversation=current.conversations.find{"account:${it.friend.accountId}"==action.id} ?: return
                val epoch=uiGeneration;val account=current.accountId
                mutable.value=mutable.value.copy(olderLoading=true)
                viewModelScope.launch {
                    try{
                        val page=backend.older(conversation.friend.peerId,Long.MAX_VALUE)
                        if(epoch==uiGeneration&&account==current.accountId){if(page.size<8)exhausted.add(conversation.friend.accountId);exhausted.removeAll(older.put(conversation.friend.accountId,chronologicalMessages((older[conversation.friend.accountId]?:emptyList())+page)));project()}
                    }catch(_:Exception){if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(notice="Local history could not be read. Existing data is retained; retry opening this conversation.")}
                    finally{if(epoch==uiGeneration&&account==current.accountId&&mutable.value.selectedId==action.id)mutable.value=mutable.value.copy(olderLoading=false)}
                }
            }
            is UiAction.Draft -> {
                if(action.conversationId !in largeDrafts&&largeDrafts.size>=128){mutable.value=mutable.value.copy(notice="The local draft limit has been reached.");return}
                if(localSends.values.any{it.conversation==action.conversationId&&it.nativeId==null}){
                    // Keep the captured message's protected draft until its
                    // commit is confirmed. New typing remains in the composer;
                    // on success it is flushed through the normal draft path.
                    val limit=if(action.conversationId.startsWith("group:"))4096 else 512*1024
                    if(action.text.toByteArray(Charsets.UTF_8).size>limit)return
                    largeDrafts[action.conversationId]=action.text
                    mutable.value=reduceUi(mutable.value,action)
                    return
                }
                if(action.conversationId.startsWith("group:")){
                    val group=current.groups.find{"group:${it.id}"==action.conversationId} ?: return
                    if(action.text.toByteArray(Charsets.UTF_8).size>4096)return
                    val owner=preferenceAccountKey() ?: return
                    val key=groupPreferenceKey(owner,group.id)
                    val update:(ConversationPreference)->ConversationPreference={it.copy(draft=action.text)}
                    if(!groupQolReady){
                        if(key !in earlyGroupDrafts&&earlyGroupDrafts.size>=128)return
                        earlyGroupDrafts[key]=action.text
                    }else if(!updateGroupPreference(key,update))return
                    largeDrafts[action.conversationId]=action.text
                    mutable.value=reduceUi(mutable.value,action)
                    draftSaves[key]?.cancel();draftSaves[key]=viewModelScope.launch{delay(250);saveGroupPreference(key,update)}
                    return
                }
                val c=current.conversations.find{"account:${it.friend.accountId}"==action.conversationId} ?: return
                if(action.text.toByteArray().size>512*1024)return
                if(action.text.length>4096){largeDrafts[action.conversationId]=action.text;mutable.value=reduceUi(mutable.value,action);return}
                val accountKey=preferenceAccountKey() ?: return
                val key=conversationPreferenceKey(accountKey,c.friend.accountId)
                if(!qolReady){if(key !in earlyDrafts&&earlyDrafts.size>=128)return;earlyDrafts[key]=action.text}
                largeDrafts[action.conversationId]=action.text
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
            is UiAction.SaveAttachmentSendMode -> viewModelScope.launch {
                if(!qolReady){mutable.value=mutable.value.copy(notice="Saved preferences are still loading. Try again.");return@launch}
                val account=current.accountId;val requestGeneration=uiGeneration
                try{
                    val saved=withContext(configurationDispatcher){qol.update{it.copy(attachmentSendMode=action.mode)};qol.load()}
                    if(account==current.accountId&&requestGeneration==uiGeneration){
                        qolState=saved;mutable.value=mutable.value.copy(attachmentSendMode=saved.attachmentSendMode,
                            notice="Default sending mode saved: ${if(saved.attachmentSendMode==AttachmentSendMode.Direct)"Direct" else "Cached"}.")
                    }
                }catch(failure:Exception){if(failure is CancellationException)throw failure
                    if(account==current.accountId&&requestGeneration==uiGeneration)
                        mutable.value=mutable.value.copy(notice="Sending preference could not be saved securely; the previous default remains.")}
            }
            is UiAction.MailboxStorage -> viewModelScope.launch {
                try{backend.setMailboxStorageAllowed(action.allowed);qolState=qol.load()}
                catch(failure:Exception){mutable.value=mutable.value.copy(notice=if(failure is com.peerchat.android.application.MailboxConsentChangeUnavailable)failure.message else "Mailbox privacy preference could not be saved; existing consent is unchanged.")}
            }
            is UiAction.SelectIdentityAuthority,is UiAction.EndorseOwnIdentityDevice,is UiAction.VerifyContactAuthority,is UiAction.VerifyOwnAccountAuthority -> {
                if(mutable.value.identityBusy)return
                val epoch=uiGeneration;val account=current.accountId
                mutable.value=mutable.value.copy(identityBusy=true,notice=null)
                viewModelScope.launch{
                    try{
                        when(action){
                            is UiAction.SelectIdentityAuthority->backend.selectIdentityAuthority(action.confirmedFingerprint)
                            is UiAction.EndorseOwnIdentityDevice->backend.endorseOwnDevice(action.targetDevice,action.confirmedFingerprint)
                            is UiAction.VerifyContactAuthority->backend.verifyContactAuthority(action.account,action.confirmedFingerprint)
                            is UiAction.VerifyOwnAccountAuthority->backend.verifyContactAuthority(account,action.confirmedFingerprint)
                            else->error("Unreachable")
                        }
                        if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(notice="Identity verification saved for the exact fingerprint shown.")
                    }catch(failure:Exception){
                        if(failure is CancellationException)throw failure
                        retainDiagnostic("Identity","Identity verification failed closed")
                        if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(notice="Identity verification was not accepted. Check the exact fingerprint and current account, then retry.")
                    }finally{if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(identityBusy=false)}
                }
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
            is UiAction.CreateGroup -> viewModelScope.launch{try{backend.createGroup(action.name)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group creation failed; no server membership is claimed.")}}
            is UiAction.InviteGroup -> viewModelScope.launch{try{backend.inviteGroup(action.group,action.account)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group invitation was not accepted by the server.")}}
            UiAction.OpenTransferSettings,is UiAction.SaveTransferSettings -> {
                if(mutable.value.transferSettingsBusy)return
                val epoch=uiGeneration;val account=current.accountId
                mutable.value=mutable.value.copy(route=Route.TransferSettings,transferSettingsBusy=true,transferSettingsError=null,notice=null,
                    transferSettings=if(action==UiAction.OpenTransferSettings)null else mutable.value.transferSettings)
                viewModelScope.launch{
                    try{
                        if(action is UiAction.SaveTransferSettings)backend.saveTransferSettings(action.value)
                        val saved=backend.transferSettings()
                        if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(transferSettings=saved,
                            notice=if(action is UiAction.SaveTransferSettings)"Transfer settings saved." else null)
                    }catch(error:Exception){if(error is CancellationException)throw error
                        if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(transferSettingsError="Transfer settings could not be read or saved. Retry to verify the current saved values.")}
                    finally{if(epoch==uiGeneration&&account==current.accountId)mutable.value=mutable.value.copy(transferSettingsBusy=false)}
                }
            }
            is UiAction.Contacts -> {
                mutable.value=reduceUi(mutable.value,action)
                contactOperation("Requests and invitations refreshed."){backend.refreshContacts()}
                if(action.page==ContactPage.FindPeople)searchPeople(mutable.value.peopleQuery)
            }
            is UiAction.PeopleQuery -> searchPeople(action.text)
            UiAction.RefreshContacts -> {
                contactOperation("Requests and invitations refreshed."){backend.refreshContacts()}
                if(mutable.value.contactPage==ContactPage.FindPeople)searchPeople(mutable.value.peopleQuery)
            }
            is UiAction.CancelFriend -> contactOperation("Outgoing request cancelled."){backend.cancelFriend(action.accountId)}
            is UiAction.RespondGroup -> contactOperation(if(action.accept)"Invitation accepted. Enrollment waits for signed membership and the current group key." else "Invitation declined."){backend.respondGroup(action.id,action.accept)}
            is UiAction.RequestFriend -> contactOperation("Friend request sent through the authenticated server."){backend.requestFriend(action.username)}
            is UiAction.RespondFriend -> contactOperation(if(action.accept)"Friend request accepted. Any required device verification remains active." else "Friend request declined."){backend.respondFriend(action.accountId,action.accept)}
            is UiAction.AttachmentPreview -> {
                if(action.id in mutable.value.attachmentPreviews||!previewLoading.add(action.id))return
                val account=current.accountId;val requestGeneration=uiGeneration
                if(current.conversations.none{c->c.attachments.any{it.id==action.id}}&&current.groups.none{g->g.attachments.any{it.id==action.id}}){previewLoading.remove(action.id);return}
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
                if(mutable.value.attachmentCompatibility!=null)return
                if(action.contextGeneration!=null&&action.contextGeneration!=uiGeneration)return
                if(action.accountId!=null&&action.accountId!="account:${current.accountId}")return
                if(action.conversationId!=null&&action.conversationId!=mutable.value.selectedId)return
                val group=current.groups.find{"group:${it.id}"==mutable.value.selectedId}
                if(group!=null){
                    beginLocalAttachment("group:${group.id}",action.uri,AttachmentSendMode.Cached){onQueued->
                        (backend as? AttachmentAdmissionObserver)?.sendGroupAttachmentObserved(group.id,action.uri,onQueued)
                            ?:backend.sendGroupAttachment(group.id,action.uri)}
                    return
                }
                val c=current.conversations.find{"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                beginLocalAttachment("account:${c.friend.accountId}",action.uri,action.mode){onQueued->
                    (backend as? AttachmentAdmissionObserver)?.sendAttachmentObserved(c.friend.peerId,action.uri,action.mode,onQueued)
                        ?:backend.sendAttachment(c.friend.peerId,action.uri,action.mode)}
            }
            is UiAction.CancelLocalAttachment -> {
                val row=localAttachments[action.token] ?: return
                if(row.nativeId!=null||row.phase!=LocalAttachmentPhase.Preparing)return
                localAttachments[action.token]=row.copy(phase=LocalAttachmentPhase.Cancelled)
                project();localAttachmentJobs[action.token]?.cancel()
            }
            is UiAction.DismissLocalAttachment -> {
                val row=localAttachments[action.token] ?: return
                if(row.phase !in setOf(LocalAttachmentPhase.Cancelled,LocalAttachmentPhase.Failed))return
                localAttachments.remove(action.token);project()
            }
            is UiAction.RetryLocalAttachment -> {
                val row=localAttachments[action.token] ?: return
                if(row.phase!=LocalAttachmentPhase.Failed||row.nativeId!=null||mutable.value.attachmentCompatibility!=null||
                    mutable.value.selectedId!=row.conversation||!current.authenticated)return
                val group=current.groups.find{"group:${it.id}"==row.conversation}
                val direct=current.conversations.find{"account:${it.friend.accountId}"==row.conversation}
                if(group==null&&direct==null)return
                localAttachments.remove(action.token);project()
                if(group!=null)beginLocalAttachment(row.conversation,row.uri,AttachmentSendMode.Cached){onQueued->
                    (backend as? AttachmentAdmissionObserver)?.sendGroupAttachmentObserved(group.id,row.uri,onQueued)
                        ?:backend.sendGroupAttachment(group.id,row.uri)}
                else if(direct!=null)beginLocalAttachment(row.conversation,row.uri,row.mode){onQueued->
                    (backend as? AttachmentAdmissionObserver)?.sendAttachmentObserved(direct.friend.peerId,row.uri,row.mode,onQueued)
                        ?:backend.sendAttachment(direct.friend.peerId,row.uri,row.mode)}
            }
            is UiAction.DeleteLocalMessage -> {
                val conversation=action.conversationId
                if(current.accountId<=0||mutable.value.conversations.none{it.id==conversation})return
                val attachment=if(action.attachment)localAttachments.values.firstOrNull{it.conversation==conversation&&(it.token==action.id||it.nativeId==action.id)}else null
                val text=if(!action.attachment)localSends.values.firstOrNull{it.conversation==conversation&&(it.token==action.id||it.nativeId==action.id)}else null
                val token=attachment?.token?:text?.token
                val nativeId=attachment?.nativeId?:text?.nativeId?:action.id.takeIf{
                    it.matches(Regex("[0-9a-f]{32}"))||(conversation.startsWith("group:")&&it.matches(Regex("[0-9a-f]{64}")))
                }
                hideLocally(conversation,action.id)
                if(token!=null)hideLocally(conversation,token)
                if(nativeId==null&&token!=null)pendingLocalDeletion[token]=conversation to action.attachment
                else if(nativeId!=null){hideLocally(conversation,nativeId);persistLocalMessageDeletion(conversation,nativeId,action.attachment,token)}
                evictDeletedHistory(conversation)
                project()
            }
            is UiAction.DeleteLocalConversation -> {
                val conversation=action.conversationId
                if(current.accountId<=0||action.localAccount?.let{it!=current.accountId}==true||mutable.value.conversations.none{it.id==conversation})return
                val epoch=uiGeneration;val account=current.accountId
                val prior=locallyHiddenIds[conversation]?.toSet().orEmpty()
                val priorPending=pendingLocalDeletion.filterValues{it.first==conversation}
                locallyClearingConversations.add(conversation)
                mutable.value.conversations.find{it.id==conversation}?.let{row->
                    (row.messages.map{it.id}+row.attachments.map{it.id}).forEach{hideLocally(conversation,it)}
                }
                localSends.values.filter{it.conversation==conversation}.forEach{row->
                    hideLocally(conversation,row.token)
                    row.nativeId?.let{hideLocally(conversation,it)}?:run{pendingLocalDeletion[row.token]=conversation to false}
                }
                localAttachments.values.filter{it.conversation==conversation}.forEach{row->
                    hideLocally(conversation,row.token)
                    row.nativeId?.let{hideLocally(conversation,it)}?:run{pendingLocalDeletion[row.token]=conversation to true}
                }
                evictDeletedHistory(conversation);project()
                viewModelScope.launch{
                    try{backend.deleteLocalConversation(account,conversation)
                        if(epoch==uiGeneration&&account==current.accountId){locallyClearingConversations.remove(conversation);project()}}
                    catch(cancelled:CancellationException){throw cancelled}
                    catch(_:Exception){if(epoch==uiGeneration&&account==current.accountId){
                        locallyHiddenIds[conversation]=prior.toMutableSet()
                        pendingLocalDeletion.entries.removeAll{it.value.first==conversation}
                        pendingLocalDeletion.putAll(priorPending)
                        locallyClearingConversations.remove(conversation)
                        project();mutable.value=mutable.value.copy(notice="Could not delete this device's conversation history. It is visible again; retry when local storage is available.")
                    }}
                }
            }
            is UiAction.ResolveAttachmentCompatibility -> {
                val choice=mutable.value.attachmentCompatibility ?: return
                if(choice.token!=action.token||attachmentCompatibilityConversation!=mutable.value.selectedId||mutable.value.attachmentCompatibilityBusy||
                    action.mode==AttachmentSendMode.Direct&&!choice.directReady||action.mode==AttachmentSendMode.Cached&&!choice.cachedReady)return
                val scope=attachmentCompatibilityScope
                val localToken=attachmentCompatibilityLocalToken
                mutable.value=mutable.value.copy(attachmentCompatibilityBusy=true)
                viewModelScope.launch{try{
                    val onQueued:(String)->Unit={id->if(localToken!=null&&scope!=null&&id.matches(Regex("[0-9a-f]{32}"))){
                        observedAttachmentIds[localToken]=Triple(scope.second,scope.first,id)
                        viewModelScope.launch{if(scope==attachmentCompatibilityScope)project()}
                    }}
                    val id=if(backend is AttachmentAdmissionObserver)
                        backend.resolveAttachmentCompatibilityObserved(action.token,action.mode,onQueued)
                        else backend.resolveAttachmentCompatibility(action.token,action.mode)
                    if(scope==attachmentCompatibilityScope){
                        attachmentCompatibilityScope=null;attachmentCompatibilityConversation=null;attachmentCompatibilityLocalToken=null
                        localToken?.let{token->localAttachments[token]?.let{row->
                            if(id!=null&&id.matches(Regex("[0-9a-f]{32}"))){localAttachments[token]=row.copy(nativeId=id,phase=LocalAttachmentPhase.Preparing);localAttachmentStableIds[id]=token}
                            else localAttachments.remove(token)
                        }}
                        if(id!=null&&localToken!=null)admittedLocalDeletion(localToken,id)
                        project();mutable.value=mutable.value.copy(attachmentCompatibility=null,attachmentCompatibilityBusy=false)
                    }
                }catch(admitted:QueuedAttachmentFailure){if(scope==attachmentCompatibilityScope){
                    localToken?.let{token->localAttachments[token]?.let{localAttachments[token]=it.copy(nativeId=admitted.transferId,phase=LocalAttachmentPhase.Failed);localAttachmentStableIds[admitted.transferId]=token};admittedLocalDeletion(token,admitted.transferId)}
                    attachmentCompatibilityScope=null;attachmentCompatibilityConversation=null;attachmentCompatibilityLocalToken=null
                    project();mutable.value=mutable.value.copy(attachmentCompatibility=null,attachmentCompatibilityBusy=false,
                        notice="The attachment entered the transfer queue, but preparation reported an error. Check this transfer before retrying.")
                }}catch(admitted:QueuedAttachmentCancellation){if(scope==attachmentCompatibilityScope){
                    localToken?.let{token->localAttachments[token]?.let{localAttachments[token]=it.copy(nativeId=admitted.transferId,phase=LocalAttachmentPhase.Cancelled);localAttachmentStableIds[admitted.transferId]=token};admittedLocalDeletion(token,admitted.transferId)}
                    attachmentCompatibilityScope=null;attachmentCompatibilityConversation=null;attachmentCompatibilityLocalToken=null
                    project();mutable.value=mutable.value.copy(attachmentCompatibility=null,attachmentCompatibilityBusy=false)
                };throw admitted
                }catch(cancelled:CancellationException){throw cancelled
                }catch(failure:Exception){if(scope==attachmentCompatibilityScope){
                    localToken?.let{token->localAttachments[token]?.let{localAttachments[token]=it.copy(phase=LocalAttachmentPhase.Failed)}}
                    project();mutable.value=mutable.value.copy(attachmentCompatibility=choice.copy(directReady=false,cachedReady=false),attachmentCompatibilityBusy=false,
                        notice=attachmentSendFailureText(action.mode?:AttachmentSendMode.Direct,failure))
                }}}
            }
            is UiAction.AttachmentAction -> viewModelScope.launch{try{backend.attachmentAction(action.id,action.accept)}
                catch(_:Exception){mutable.value=mutable.value.copy(notice="File action could not complete. The protected transfer state is retained.")}}
            is UiAction.PreviewMessage -> {
                val group=mutable.value.selectedId?.removePrefix("group:")?.let{id->current.groups.find{it.id==id}}
                if(group!=null){
                    if(action.text.isBlank()||action.text.toByteArray(Charsets.UTF_8).size>4096)return
                    val key=preferenceAccountKey()?.let{groupPreferenceKey(it,group.id)}
                    beginLocalSend("group:${group.id}",action.text,{backend.sendGroup(group.id,action.text)}){
                            largeDrafts["group:${group.id}"]=""
                            if(key!=null&&!groupQolReady)earlyGroupDrafts[key]=""
                            if(key!=null){draftSaves.remove(key)?.cancel();val update:(ConversationPreference)->ConversationPreference={it.copy(draft="")};if(updateGroupPreference(key,update))saveGroupPreference(key,update)}
                            mutable.value=mutable.value.copy(drafts=mutable.value.drafts+("group:${group.id}" to ""))
                    }
                    return
                }
                val c=current.conversations.find {"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                if(action.text.isBlank()||action.text.toByteArray().size>512*1024)return
                val sendingAccount=current.accountId
                beginLocalSend("account:${c.friend.accountId}",action.text,{backend.sendToAccount(sendingAccount,c.friend.accountId,action.text)}){
                        largeDrafts["account:${c.friend.accountId}"]=""
                        val accountKey=preferenceAccountKey();if(accountKey!=null){
                            val key=conversationPreferenceKey(accountKey,c.friend.accountId);draftSaves.remove(key)?.cancel()
                            if(!qolReady)earlyDrafts[key]=""
                            qolState=qolState.copy(conversations=qolState.conversations+(key to (qolState.conversations[key]?:ConversationPreference()).copy(draft="")))
                            savePreference(key){it.copy(draft="")}
                        }
                        mutable.value=mutable.value.copy(drafts=mutable.value.drafts+("account:${c.friend.accountId}" to ""))
                }
            }
            UiAction.BeginOlder,UiAction.Older -> {
                val group=mutable.value.selectedId?.removePrefix("group:")?.let{id->current.groups.find{it.id==id}}
                if(group!=null){viewModelScope.launch{try{backend.groupHistory(group.id,groupHistoryBefore(group.messages))}catch(_:Exception){mutable.value=mutable.value.copy(notice="Group history page unavailable.")}};return}
                val c=current.conversations.find {"account:${it.friend.accountId}"==mutable.value.selectedId} ?: return
                val account=current.accountId;val epoch=uiGeneration
                if(mutable.value.olderLoading)return
                mutable.value=mutable.value.copy(olderLoading=true)
                viewModelScope.launch {
                    try {
                        val existing=(older[c.friend.accountId] ?: emptyList())+c.messages
                        val before=chronologicalMessages(existing).firstOrNull()?.sequence ?: Long.MAX_VALUE
                        val page=backend.older(c.friend.peerId,before)
                        if(epoch==uiGeneration&&current.accountId==account&&current.conversations.any {it.friend.accountId==c.friend.accountId}) {
                            if(page.size<8)exhausted.add(c.friend.accountId)
                            exhausted.removeAll(older.put(c.friend.accountId,((older[c.friend.accountId] ?: emptyList())+page).distinctBy {it.id}))
                            project()
                        }
                    }catch(_:Exception){if(epoch==uiGeneration&&current.accountId==account)mutable.value=mutable.value.copy(notice="Native history could not be read.")}
                    finally {if(epoch==uiGeneration&&current.accountId==account&&mutable.value.selectedId=="account:${c.friend.accountId}")mutable.value=mutable.value.copy(olderLoading=false)}
                }
            }
            UiAction.SignOut -> {voicePreparationRevision++;releasePreparedVoice();voiceStartPendingToken=null;voiceStartExpected=null;mutable.value=mutable.value.copy(voiceBusy=false);viewModelScope.launch {try {backend.signOut();mutable.value=mutable.value.copy(route=Route.Welcome,startupStatus=null,rememberMe=false)}
                catch(_:Exception){mutable.value=mutable.value.copy(route=Route.Login,startupStatus=null,notice="Sign-out revocation is pending; saved sign-in will not be reused.")}}
            }
            UiAction.ResetNetworkSession -> recoverNetwork(Route.Login)
            UiAction.DisconnectChangeServer -> recoverNetwork(Route.Server)
            UiAction.Explore -> mutable.value=mutable.value.copy(notice="Demo is available only through the explicit debug launch mode.")
            is UiAction.RetryPreview -> {
                val failed=localSends.values.firstOrNull{it.token==action.messageId&&it.failed}
                if(failed!=null){
                    if(!mutable.value.drafts[failed.conversation].isNullOrEmpty()){
                        mutable.value=mutable.value.copy(notice="Your new typing is retained. Clear or send it before restoring the captured message from this failed bubble.");return
                    }
                    localSends.remove(failed.token);dispatch(UiAction.Draft(failed.conversation,failed.text));project();mutable.value=mutable.value.copy(notice="Draft restored. Review local history, then press Send to retry explicitly.");return
                }
                val selected=mutable.value.selectedId
                if(selected?.startsWith("group:")==true)viewModelScope.launch{try{backend.retryGroup(selected.removePrefix("group:"),action.messageId)}catch(_:Exception){mutable.value=mutable.value.copy(notice="Exact group retry requires the current epoch and a live event channel. The saved message is retained.")}}
                else mutable.value=mutable.value.copy(notice="This retry capability is unavailable.")
            }
            is UiAction.LeaveVoice,is UiAction.ToggleMute,is UiAction.ToggleDeafen -> {
                val group=mutable.value.selectedId?.takeIf{it.startsWith("group:")}?.removePrefix("group:") ?: return
                val target=current.voice.target?.takeIf{it.group==group&&it.room==when(action){
                    is UiAction.LeaveVoice->action.roomId
                    is UiAction.ToggleMute->action.roomId
                    is UiAction.ToggleDeafen->action.roomId
                    else->""
                }} ?: return
                dispatch(when(action){
                    is UiAction.LeaveVoice->UiAction.ControlDmVoice(target,VoiceControl.End)
                    is UiAction.ToggleMute->UiAction.ControlDmVoice(target,VoiceControl.Mute,!current.voice.muted)
                    else->UiAction.ControlDmVoice(target,VoiceControl.Deafen,!current.voice.deafened)
                })
            }
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
    override fun onCleared() {loginAfterTrust=null;releasePreparedVoice(includeService=false);backend.close();super.onCleared()}
}
