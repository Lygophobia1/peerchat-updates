package com.peerchat.android.application

import android.content.Context
import com.peerchat.android.NativeProfile
import com.peerchat.android.ProfileBridge
import com.peerchat.android.ReliableBridge
import com.peerchat.android.MailboxBridge
import com.peerchat.android.platform.*
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.withContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.cancel

/** Accessed under the backend gate; a completed choice may outlive its presenter. */
internal class AttachmentSelectionOwner<T> {
    var pending:T?=null
    private var preparation:Any?=null

    fun <R> begin(validate:()->R):Pair<Any,R> {
        check(preparation==null){"Another attachment selection is pending"}
        val selected=validate()
        pending=null // The validated new selection supersedes an uncommitted choice.
        return Any().also{preparation=it} to selected
    }
    fun isPreparing(intent:Any)=preparation===intent
    fun finish(intent:Any){if(preparation===intent)preparation=null}
    fun clearIfCurrent(choice:T){if(pending===choice)pending=null}
}

/** Application coordinator/platform execution only. Native owns profile,
 * messages/outbox/order/ACK/dedupe, trust and session/retry state. Immutable UI
 * snapshots are read from those owners, not a Kotlin messaging engine. */
class MessagingBackend(context: Context,private val profileName: String="default") : MessagingApplication {
    private val context=context.applicationContext
    private val gate=Any()
    private val ownerTimings=OwnerStageTimings(slow={stage,elapsed->android.util.Log.i("PeerChatWake","stage=owner_$stage elapsed_ms=$elapsed")})
    private val executor=Executors.newSingleThreadScheduledExecutor {task->Thread(task,"PeerChat application projection").apply {isDaemon=true}}
    private val metadataWork=CoroutineScope(SupervisorJob()+Dispatchers.IO)
    private val contactLane=BackendWorkLane()
    private val groupLane=BackendWorkLane()
    private val eligibilityLane=BackendWorkLane()
    private val mailboxLane=BackendWorkLane()
    private val cacheLane=BackendWorkLane()
    private val archiveLane=BackendWorkLane()
    private val pushLane=BackendWorkLane()
    private var eligibilityAt=0L
    private var pushAt=0L
    private var nativeDiagnosticAt=0L
    private var nativeDiagnosticSampleAt=0L
    private var nativeDiagnostic="native_registration_stage=waiting_for_idle_diagnostic_sample"
    private val ownDeviceProjection=OwnDeviceInventoryProjection()
    private var ownDeviceRequest:Any?=null
    private var ownDeviceAt=0L
    private var ownDeviceStatus="not_requested"
    private val mutable=MutableStateFlow(MessagingSnapshot())
    private val relayFailureDiagnostics=linkedMapOf<Long,String>()
    private val pendingDeviceVerifications=linkedMapOf<String,DeviceVerification>()
    private val declinedDeviceVerifications=mutableSetOf<String>()
    private val peerAuthenticationDiagnostics=linkedMapOf<String,String>()
    override val state: StateFlow<MessagingSnapshot> = mutable.asStateFlow()
    private var profile: NativeProfile?=null
    private var accounts: ServerAccounts?=null
    private var index: FriendIdentityIndex?=null
    private var friends=emptyList<FriendIdentity>()
    private val historyProjection=LocalHistoryProjection()
    private data class LocalReadContext(val handle:Long,val account:Long,val endpoint:ServerEndpoint,val friends:List<FriendIdentity>)
    @Volatile private var localReadContext:LocalReadContext?=null
    @Volatile private var preferredLocalAccount=0L
    private var projectedHistoryRevision=-1L
    private val projectionCadence=OwnerProjectionCadence()
    private fun publishLocalReadContext(handle:Long,account:Long,endpoint:ServerEndpoint){
        val next=LocalReadContext(handle,account,endpoint,friends.map{it.copy(online=false)})
        localReadContext?.let{old->if(old.handle!=handle||old.account!=account||old.endpoint!=endpoint)historyProjection.clear()}
        if(localReadContext!=next)localReadContext=next
    }
    private val localContexts=LocalConversationContextStore(this.context,profileName)
    private var localEndpoint:ServerEndpoint?=null
    @Volatile private var generation=0L
    private var network=0L
    private var archiveMetadataAt=0L
    private var archiveRetryAt=0L
    private var archiveServingUntil=0L
    private var archiveLoadedGeneration=-1L
    private var refreshAt=0L
    @Volatile private var events:ForegroundEvents?=null
    private val passiveEventQueued=java.util.concurrent.atomic.AtomicBoolean(false)
    private var eventRetryAt=0L
    private var eventRetryDelay=1000L
    private var eventWasConnected=false
    private var contactsDirty=false
    private var contactAuthorityHint=0L
    private var groupAuthorityHint=0L
    @Volatile private var authorizationSnapshotInstalled=false
    private var rememberedRetryEndpoint:ServerEndpoint?=null
    private var rememberedRetryAt=0L
    private var rememberedRetryDelay=1000L
    private var loggedTickFailure:String?=null
    private var lastNativeReceiveNanos=0L
    private var lastNativeReceiveAttempt=0L
    private val mailboxPublication=MailboxPublicationGate({publishMessageProjection()})
    private var mailboxAt=0L
    private var mailboxExecution=MailboxExecution()
    private var mailboxEnrollmentScope:ScopedBackendWork.Identity?=null
    private var mailboxEnrollmentEndpoint:ServerEndpoint?=null
    private var mailboxInFlight:MailboxExecution?=null
    private val dmAttachmentExecution=DmAttachmentExecution()
    private var eligibilitySnapshots=EligibilitySnapshotExecution()
    private var eligibilityOwner:ScopedBackendWork.Identity?=null
    private val groupExecution=GroupExecution()
    private val voiceExecution=VoiceExecution()
    private val groupVoiceExecution=GroupVoiceExecution()
    private val groupVoiceRooms=GroupVoiceRooms()
    private var groupVoiceBinding:GroupNativeBinding?=null
    private var groupVoiceGeneration=0L
    private var groupVoiceSnapshotAt=0L
    private data class PendingVoiceSignal(val signal:GroupNativeSignal,val expires:Long,var request:String?=null,var attempts:Int=0)
    private val groupVoiceSignals=mutableListOf<PendingVoiceSignal>()
    private data class VoiceBinding(val identity:ScopedBackendWork.Identity,val handle:Long,val account:Long,
        val generation:Long,val answer:VoiceCallTarget?,val group:String="",val room:String="",val epoch:String="")
    private val voiceConsents=VoiceConsentRegistry<VoiceBinding>()
    private var voiceScopeIdentity:ScopedBackendWork.Identity?=null
    private var voiceOwner=""
    private var voiceOwnedCall:Pair<String,VoiceCallTarget>?=null
    private var voiceCredentials:MediaTurnCredentials?=null
    private var voiceCredentialGeneration=0L
    private var voiceCredentialRefresh:Any?=null
    private var voiceCredentialRetryAt=0L
    private var voicePlatformDiagnostic=""
    private var voiceSpeaker=false
    private val voiceNotifications=VoiceNotifications(this.context)
    private fun voiceIdentity():ScopedBackendWork.Identity {
        check(!stopped&&!resetting&&inUse&&authorizationSnapshotInstalled&&mutable.value.authenticated){"Authenticated voice session unavailable"}
        val owner=checkNotNull(accounts);val active=checkNotNull(owner.session);val opened=checkNotNull(profile)
        if(voiceScopeIdentity?.matches(opened,owner,active,active.accountId,generation)!=true){
            voiceConsents.reset();voiceOwnedCall=null;voiceCredentials=null;voiceCredentialGeneration=0
            voiceCredentialRefresh=null;voiceCredentialRetryAt=0L
            voicePlatformDiagnostic="";voiceSpeaker=false;voiceOwner=java.util.UUID.randomUUID().toString()
            voiceScopeIdentity=ScopedBackendWork.Identity(opened,owner,active,active.accountId,generation)
        }
        return checkNotNull(voiceScopeIdentity)
    }
    private fun publishVoice():VoiceSnapshot {
        voiceIdentity()
        groupVoiceBinding?.let {binding->
            val native=groupVoiceExecution.snapshot(checkNotNull(profile).handle,mutable.value.accountId,generation)
            if(!native.active||native.binding!=binding){stopGroupVoice("Group audio ended; join again to retry")}
            else {
                val room=groupVoiceRooms.snapshot(binding.group)?.rooms?.singleOrNull{it.id==binding.room}
                val status=when {
                    room==null->"In voice room · refreshing participant state"
                    room.members.size<=1->"In voice room · waiting for participants"
                    native.totalRoutes==0L->"In voice room · waiting for authenticated audio setup"
                    native.connectedRoutes<native.totalRoutes->"Connecting group audio (${native.connectedRoutes}/${native.totalRoutes})"
                    else->"Connected · group voice"
                }
                val value=VoiceSnapshot(available=native.available,diagnostic=status,active=true,id=binding.room,
                    peer=mutable.value.localPeerId,lifetime=binding.mediaGeneration,
                    state=if(native.totalRoutes==0L||native.connectedRoutes==native.totalRoutes)5 else 4,
                    muted=native.muted,deafened=native.deafened,
                    owner=voiceOwner,platformDiagnostic=voicePlatformDiagnostic,speaker=voiceSpeaker,
                    group=binding.group,room=binding.room,epoch=binding.epoch,participation=binding.participation,
                    mediaDiagnostics="group_room_participants=${room?.members?.size?:-1}\ngroup_room_state_fresh=${room!=null}\ngroup_connected_routes=${native.connectedRoutes}\ngroup_total_routes=${native.totalRoutes}"+
                        "\ngroup_encoded_packets=${native.encodedPackets}\ngroup_received_packets=${native.receivedPackets}"+
                        "\ngroup_mixed_10ms_blocks=${native.mixedBlocks}\ngroup_underrun_10ms_blocks=${native.underrunBlocks}"+
                        "\ngroup_per_source_jitter=not exposed\n"+com.peerchat.android.platform.audio.AndroidAudioDevice.diagnostics())
                mutable.value=mutable.value.copy(voice=value);voiceNotifications.update(value);return value
            }
        }
        val measured=voiceExecution.snapshot(checkNotNull(profile).handle,mutable.value.accountId,generation)
        val value=measured.copy(mediaDiagnostics=measured.mediaDiagnostics+"\n"+com.peerchat.android.platform.audio.AndroidAudioDevice.diagnostics())
            .copy(owner=voiceOwner,platformDiagnostic=voicePlatformDiagnostic,speaker=voiceSpeaker)
        mutable.value=mutable.value.copy(voice=value)
        voiceNotifications.update(value)
        return value
    }
    internal fun canLeaseVoice(token:String):Boolean=synchronized(gate){
        !stopped&&!resetting&&inUse&&authorizationSnapshotInstalled&&mutable.value.authenticated&&voiceConsents.valid(token)
    }
    override suspend fun prepareVoice(peerId:String,answer:VoiceCallTarget?):VoiceConsent=withContext(Dispatchers.IO){synchronized(gate){
        val identity=voiceIdentity();val voice=publishVoice()
        check(voice.available&&voiceOwnedCall==null){"Voice is unavailable or another call owns audio"}
        check(friends.any{peerId in it.knownPeerIds&&peerId !in it.revokedPeerIds}){"Recipient is not currently authorized"}
        if(answer==null)check(!voice.active){"Another call is active"}
        else check(answer==voice.target&&answer.peer==peerId&&voice.state==2L&&voice.direction==1L){"Incoming call changed"}
        voicePlatformDiagnostic="";voiceSpeaker=false
        voiceConsents.create(peerId,answer?.id?:java.util.UUID.randomUUID().toString().replace("-",""),answer!=null,
            VoiceBinding(identity,checkNotNull(profile).handle,mutable.value.accountId,generation,answer))
    }}
    override suspend fun prepareGroupVoice(group:String,room:String):VoiceConsent=withContext(Dispatchers.IO){synchronized(gate){
        val identity=voiceIdentity();val current=publishVoice()
        check(current.available&&!current.active&&voiceOwnedCall==null&&events?.connected==true){"Another call is active or voice is unavailable"}
        val state=checkNotNull(groupVoiceRooms.snapshot(group)){"Waiting for authenticated voice rooms"}
        val selected=checkNotNull(state.rooms.singleOrNull{it.id==room}){"Voice room unavailable"}
        validateVoiceRoom(state,selected)
        check(selected.members.size<=32){"Voice room exceeds this device's supported participant limit"}
        voicePlatformDiagnostic="";voiceSpeaker=false
        voiceConsents.create(mutable.value.localPeerId,room,false,
            VoiceBinding(identity,checkNotNull(profile).handle,mutable.value.accountId,generation,null,group,room,state.epoch),group)
    }}
    private fun validateVoiceRoom(state:GroupVoiceRoomsSnapshot,room:GroupVoiceRoom){
        val group=checkNotNull(mutable.value.groups.singleOrNull{it.id==state.group&&it.ready&&it.epoch==state.roster}){"Group authorization changed"}
        check(group.memberRows.any{!it.invited&&it.account==mutable.value.accountId&&it.device==mutable.value.localPeerId})
        check(room.members.all{member->group.memberRows.any{!it.invited&&it.account==member.account&&it.device==member.device}}){"Room participant authorization changed"}
    }
    private fun stopGroupVoice(reason:String=""){
        val binding=groupVoiceBinding;groupVoiceBinding=null;groupVoiceSignals.clear()
        if(binding!=null){
            profile?.let{runCatching{groupVoiceExecution.stop(it.handle,mutable.value.accountId,generation,binding)}}
            runCatching{groupVoiceRooms.request(binding.group,"leave_voice",binding.room){events?.queueGroup(it)==true}}
        }
        if(reason.isNotEmpty())voicePlatformDiagnostic=reason
    }
    override suspend fun activateVoice(token:String):VoiceSnapshot=withContext(Dispatchers.IO){
        val prepared=synchronized(gate){
            val (consent,binding)=voiceConsents.claim(token)
            check(voiceIdentity()===binding.identity){"Call authorization changed"}
            val scope=checkNotNull(backendWorkScope{voiceConsents.current(token,binding)})
            Triple(consent,binding,scope) to checkNotNull(accounts)
        }
        val (consent,binding,scope)=prepared.first
        try {
            val cached=scope.local{voiceCredentials?.takeIf{it.usableAt(System.currentTimeMillis()/1000)}}
            // Existing authenticated TURN request is on its independent bounded
            // connection, never while holding the application/native gate.
            val credentials=cached?:scope.network{prepared.second.mediaTurnCredentials()}
            if(binding.group.isNotEmpty()){
                val request=scope.local{
                    check(events?.connected==true&&groupVoiceRooms.snapshot(binding.group)?.epoch==binding.epoch)
                    checkNotNull(groupVoiceRooms.request(binding.group,"join_voice",binding.room){events?.queueGroup(it)==true})
                }
                kotlinx.coroutines.withTimeout(15_000){while(scope.local{groupVoiceRooms.result(request)}=="pending")kotlinx.coroutines.delay(100)}
                return@withContext scope.local{
                    check(groupVoiceRooms.result(request)=="accepted"&&events?.connected==true){"Group join was rejected"}
                    val state=checkNotNull(groupVoiceRooms.snapshot(binding.group));check(state.epoch==binding.epoch)
                    val room=checkNotNull(state.rooms.singleOrNull{it.id==binding.room});validateVoiceRoom(state,room)
                    val member=checkNotNull(room.members.singleOrNull{it.account==binding.account&&it.device==mutable.value.localPeerId})
                    val current=voiceExecution.snapshot(binding.handle,binding.account,binding.generation)
                    check(!current.active&&groupVoiceBinding==null&&credentials.usableAt(System.currentTimeMillis()/1000))
                    val target=GroupNativeBinding(binding.group,binding.room,binding.epoch,member.participation,++groupVoiceGeneration)
                    val result=groupVoiceExecution.start(binding.handle,binding.account,binding.generation,target,state.revision,state.roster,
                        room.members.filter{it.device!=mutable.value.localPeerId}.map{GroupNativeSource(it.account,it.device,it.participation)},credentials,++voiceCredentialGeneration)
                    check(result.accepted&&result.active&&result.binding==target){"Group audio could not start"}
                    groupVoiceBinding=target;voiceCredentials=credentials
                    voiceOwnedCall=token to VoiceCallTarget(voiceOwner,mutable.value.localPeerId,target.room,target.mediaGeneration,target.group,target.room,target.epoch,target.participation)
                    voiceConsents.clear(token);publishVoice()
                }
            }
            scope.local{check(ReliableBridge.demand(binding.handle,consent.peer.toByteArray(Charsets.US_ASCII))==0){"Verified call route unavailable"}}
            // An idle authenticated friend need not already have a live relay.
            // Demand the existing route and await it without blocking native,
            // mailbox or UI ownership. Native rechecks pin/capability at Start.
            kotlinx.coroutines.withTimeout(20_000){
                while(!scope.local{connectionSnapshot(checkNotNull(ReliableBridge.state(binding.handle,
                        consent.peer.toByteArray(Charsets.US_ASCII)))).ready})kotlinx.coroutines.delay(125)
            }
            kotlin.coroutines.coroutineContext.ensureActive()
            scope.local{
                check(credentials.usableAt(System.currentTimeMillis()/1000)){"Media credentials expired"}
                check(voiceIdentity()===binding.identity)
                val before=publishVoice()
                if(binding.answer==null)check(!before.active){"Another call became active"}
                else check(before.target==binding.answer&&before.state==2L&&before.direction==1L){"Incoming call expired"}
                check(voiceExecution.configureIce(binding.handle,binding.account,binding.generation,credentials.host,credentials.port,
                    credentials.username,credentials.password,credentials.expiresUtc,++voiceCredentialGeneration).accepted){"Media configuration rejected"}
                voiceCredentials=credentials
                val result=if(binding.answer==null)voiceExecution.start(binding.handle,binding.account,binding.generation,consent.peer,consent.id)
                    else voiceExecution.answer(binding.handle,binding.account,binding.generation,CallTarget(binding.answer.peer,binding.answer.id,binding.answer.lifetime))
                check(result.accepted&&result.active&&result.id==consent.id&&result.peer==consent.peer){"Call could not start on the verified connection"}
                voiceOwnedCall=token to VoiceCallTarget(voiceOwner,result.peer,result.id,result.lifetime)
                voiceConsents.clear(token)
                publishVoice()
            }
        }catch(failure:Exception){
            synchronized(gate){
                // Publish failure before consuming this consent. A delayed
                // failure from an old profile/call must not overwrite a newer
                // call, but the current UI must not wait for its watchdog.
                if(voiceScopeIdentity===binding.identity&&voiceConsents.current(token,binding)){
                    if(binding.group.isNotEmpty())runCatching{groupVoiceRooms.request(binding.group,"leave_voice",binding.room){events?.queueGroup(it)==true}}
                    voicePlatformDiagnostic="Call preparation failed"
                    mutable.value=mutable.value.copy(voice=mutable.value.voice.copy(platformDiagnostic=voicePlatformDiagnostic))
                }
                voiceConsents.clear(token)
            }
            throw failure
        }
    }
    override suspend fun releaseVoice(token:String):Unit=withContext(Dispatchers.IO){synchronized(gate){
        voiceConsents.clear(token)
        val owned=voiceOwnedCall?.takeIf{it.first==token}?:return@synchronized
        voiceOwnedCall=null
        if(!stopped&&!resetting&&authorizationSnapshotInstalled&&mutable.value.authenticated){
            val current=publishVoice()
            if(current.target==owned.second){
                if(current.group.isNotEmpty())stopGroupVoice()
                else voiceExecution.end(checkNotNull(profile).handle,mutable.value.accountId,generation,CallTarget(current.peer,current.id,current.lifetime))
            }
            publishVoice()
        }
    }}
    override suspend fun controlVoice(target:VoiceCallTarget,action:VoiceControl,value:Boolean):Unit=withContext(Dispatchers.IO){synchronized(gate){
        val voice=publishVoice();check(voice.target==target){"Call changed; action ignored"}
        if(target.group.isNotEmpty()){
            val binding=checkNotNull(groupVoiceBinding);val native=checkNotNull(profile).handle
            when(action){
                VoiceControl.End,VoiceControl.Decline->stopGroupVoice()
                VoiceControl.Mute->check(groupVoiceExecution.mute(native,mutable.value.accountId,generation,binding,value).accepted)
                VoiceControl.Deafen->check(groupVoiceExecution.deafen(native,mutable.value.accountId,generation,binding,value).accepted)
            }
            publishVoice();return@synchronized
        }
        val native=checkNotNull(profile).handle;val call=CallTarget(target.peer,target.id,target.lifetime)
        val result=when(action){
            VoiceControl.Decline->voiceExecution.decline(native,mutable.value.accountId,generation,call)
            VoiceControl.End->voiceExecution.end(native,mutable.value.accountId,generation,call)
            VoiceControl.Mute->voiceExecution.mute(native,mutable.value.accountId,generation,call,value)
            VoiceControl.Deafen->voiceExecution.deafen(native,mutable.value.accountId,generation,call,value)
        }
        check(result.accepted){"Call control rejected"};publishVoice();Unit
    }}
    override suspend fun voicePlatformState(token:String,diagnostic:String,speaker:Boolean):Unit=withContext(Dispatchers.IO){synchronized(gate){
        if(!voiceConsents.valid(token)&&voiceOwnedCall?.first!=token)return@synchronized
        require(diagnostic in setOf("","Microphone permission required","Audio focus or device unavailable","Call service unavailable","Call preparation failed","Audio route unavailable"))
        voicePlatformDiagnostic=diagnostic;voiceSpeaker=speaker
        if(authorizationSnapshotInstalled&&mutable.value.authenticated)publishVoice()
    }}
    private val groupNotifications=GroupNotifications(this.context,profileName)
    private var groupRefreshAt=0L
    private var groupProjectionAt=0L
    private var groupPendingAt=0L
    private var groupPendingMore=true
    private var groupCompletionAt=0L
    private val groupCompletionObserved=mutableMapOf<Long,Long>()
    // Profile-local product default is ON only when absent. Explicit OFF persists;
    // unreadable protected preferences fail closed. History policy is separate.
    private val mailboxPreferences=AndroidQolStore(this.context,profileName)
    private fun mailboxAllowed():Boolean=try{mailboxPreferences.load().mailboxStorageAllowed}catch(_:Exception){false}
    override suspend fun setMailboxStorageAllowed(allowed:Boolean)=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped)
        if(!allowed){
            val owner=accounts;val native=profile
            if(owner?.session==null||native==null||!authorizationSnapshotInstalled)throw MailboxConsentChangeUnavailable(false)
            configureMailboxEnrollment(owner,native.handle)
            requireMailboxConsentChangeAllowed(MailboxBridge.hasScopedEnrollment(native.handle))
        }
        mailboxPreferences.update{it.copy(mailboxStorageAllowed=allowed)}
        OpaquePushWake.refreshPeriodic(context)
        mailboxExecution=MailboxExecution();mailboxAt=0L;mutable.value=mutable.value.copy(mailboxStorageAllowed=allowed)
    }}
    private fun configureMailboxScope(owner:ServerAccounts,native:Long){
        val session=checkNotNull(owner.session)
        val server=owner.endpoint.host.lowercase(java.util.Locale.ROOT)+":"+owner.endpoint.port
        check(MailboxBridge.configureEnrollment(native,session.accountId,generation,server.toByteArray(Charsets.US_ASCII),owner.endpoint.certificatePin.lowercase(java.util.Locale.ROOT).toByteArray(Charsets.US_ASCII))==0){"Authenticated mailbox scope is unavailable"}
        if(mailboxEnrollmentScope?.matches(profile,owner,session,session.accountId,generation)!=true||mailboxEnrollmentEndpoint!=owner.endpoint){
            // Replace rather than mutate an executor borrowed by an old session.
            // Native invalidation remains authoritative; the existing key must
            // be accepted again through the authenticated registration path.
            mailboxExecution=MailboxExecution();mailboxAt=0L
            mailboxEnrollmentScope=ScopedBackendWork.Identity(checkNotNull(profile),owner,session,session.accountId,generation)
            mailboxEnrollmentEndpoint=owner.endpoint
        }
    }
    private fun configureMailboxEnrollment(owner:ServerAccounts,native:Long){
        configureMailboxScope(owner,native)
        val session=checkNotNull(owner.session)
        val scope=NativeRecordWriter(30).also{it.number(session.accountId);it.number(generation)}.bytes()
        // Optional capability setup cannot block ordinary text mailbox work.
        try{com.peerchat.android.TransferBridge.request(native,11,scope)}catch(_:Exception){}
    }
    private fun mailboxRecoveryFailure(code:String):String=when(code){
        "approval_expired_or_session_changed"->"The mailbox confirmation expired or the account/server session changed. Reconnect and review the current confirmation. Existing keys and messages are preserved."
        "key_possession_or_protected_enrollment_failed"->"The existing mailbox key could not be verified or the protected enrollment could not be saved. Do not reset app data or keys; return the diagnostics for recovery."
        else->"The existing mailbox binding or key could not be validated. Keep app data and keys intact and return the messaging diagnostics."
    }
    override suspend fun reenrollMailbox(value:MailboxReenrollment,accept:Boolean)=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled&&mutable.value.authenticated&&mutable.value.serverPresenceLive&&mailboxAllowed())
        val owner=checkNotNull(accounts);val native=checkNotNull(profile).handle
        check(mutable.value.mailboxReenrollment==value&&owner.session?.accountId==value.account&&generation==value.generation&&mutable.value.localPeerId==value.device){"Mailbox confirmation is stale"}
        if(accept){
            // Reuse the existing pinned messaging bootstrap, which verifies the
            // authenticated account and registered device identity on the server.
            refresh(true)
            check(owner===accounts&&owner.session?.accountId==value.account&&generation==value.generation){"Authenticated mailbox ownership changed"}
        }
        configureMailboxEnrollment(owner,native)
        val result=MailboxBridge.reenrollmentDecision(native,mailboxReenrollmentDecision(value,accept))
        if(result!=0){
            val reason=MailboxBridge.reenrollmentError(native)?.toString(Charsets.US_ASCII)?:"unavailable"
            mutable.value=mutable.value.copy(mailboxReenrollment=null,mailboxRecoveryError=mailboxRecoveryFailure(reason))
            error(mailboxRecoveryFailure(reason))
        }
        mutable.value=mutable.value.copy(mailboxReenrollment=null,mailboxRecoveryError=null)
        if(accept){mailboxExecution=MailboxExecution();mailboxAt=0L}
    }}
    private fun projectMailboxRecovery(native:Long){
        if(!mailboxAllowed()||!mutable.value.authenticated||!mutable.value.serverPresenceLive){mutable.value=mutable.value.copy(mailboxReenrollment=null);return}
        val offer=MailboxBridge.reenrollmentOffer(native)
        val error=MailboxBridge.reenrollmentError(native)?.toString(Charsets.US_ASCII)
        mutable.value=mutable.value.copy(mailboxReenrollment=offer?.let{mailboxReenrollment(it)},
            mailboxRecoveryError=if(error!=null&&error!="none")mailboxRecoveryFailure(error) else mutable.value.mailboxRecoveryError)
    }
    private fun archiveExecution(owner:ServerAccounts,native:NativeProfile)=ArchiveEnrollmentExecution(
        {op,bytes->checkNotNull(com.peerchat.android.ArchiveBridge.request(native.handle,op,bytes)){"Native archive operation rejected"}},
        owner::archive)
    override suspend fun archiveDevices():ArchiveEnrollmentSnapshot=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled&&mutable.value.serverPresenceLive)
        val owner=checkNotNull(accounts);val session=checkNotNull(owner.session);val native=checkNotNull(profile)
        val local=mutable.value.localPeerId;val execution=archiveExecution(owner,native)
        execution.publishPending(session.accountId,generation,local)
        val devices=execution.devices(session.accountId,generation,execution.refresh(session.accountId,generation,local))
        mutable.value=mutable.value.copy(archiveProgress=execution.progress(session.accountId,generation))
        ArchiveEnrollmentSnapshot(session.accountId,generation,local,devices,execution.historySync(session.accountId,generation))
    }}
    override suspend fun synchronizeArchive(snapshot:ArchiveEnrollmentSnapshot,source:String?,pause:Boolean):ArchiveEnrollmentSnapshot=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled&&mutable.value.serverPresenceLive&&livePeerWorkAllowed)
        val owner=checkNotNull(accounts);val session=checkNotNull(owner.session);val native=checkNotNull(profile);val local=mutable.value.localPeerId
        require(snapshot.accountId==session.accountId&&snapshot.authenticationGeneration==generation&&snapshot.localPeerId==local)
        require(pause||mutable.value.ownDevices?.devices?.none{it.peerId==source&&it.revoked}!=false){"The selected account device has been revoked"}
        val execution=archiveExecution(owner,native);val rows=execution.refreshCurrent(session.accountId,generation,local)
        if(pause)execution.pauseSync(session.accountId,generation)else{require(snapshot.devices.any{it.peerId==source&&!it.local&&it.mayServe});execution.startSync(session.accountId,generation,checkNotNull(source))}
        mutable.value=mutable.value.copy(archiveProgress=execution.progress(session.accountId,generation));archiveMetadataAt=System.nanoTime()/1_000_000+10000
        snapshot.copy(devices=execution.devices(session.accountId,generation,rows),historySyncEnabled=execution.historySync(session.accountId,generation))
    }}
    override suspend fun archiveConsent(snapshot:ArchiveEnrollmentSnapshot,action:ArchiveDeviceAction,device:String,read:Boolean,serve:Boolean):ArchiveEnrollmentSnapshot=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled&&mutable.value.serverPresenceLive)
        val owner=checkNotNull(accounts);val session=checkNotNull(owner.session);val native=checkNotNull(profile)
        val local=mutable.value.localPeerId
        require(snapshot.accountId==session.accountId&&snapshot.authenticationGeneration==generation&&snapshot.localPeerId==local&&snapshot.devices.any{it.peerId==device})
        require(mutable.value.ownDevices?.devices?.none{it.peerId==device&&it.revoked}!=false){"The selected account device has been revoked"}
        val execution=archiveExecution(owner,native)
        val currentRows=execution.devices(session.accountId,generation,execution.refresh(session.accountId,generation,local))
        val capabilities=(if(read)1L else 0L) or (if(serve)2L else 0L)
        require(action!=ArchiveDeviceAction.Authorize||capabilities!=0L)
        when(action){
            ArchiveDeviceAction.SetUp->{require(device==local);execution.prepareFirstDevice(session.accountId,generation,local,true)}
            ArchiveDeviceAction.ApproveIssuer->execution.confirmExistingIssuer(session.accountId,generation,device,true)
            else->{
                if(action==ArchiveDeviceAction.Authorize)check(currentRows.single{it.peerId==device}.mayAuthorize){"Approve from an enabled trusted history device."}
                val operation=when(action){ArchiveDeviceAction.TrustAuthorizer->4;ArchiveDeviceAction.Authorize->5;ArchiveDeviceAction.Revoke->6;ArchiveDeviceAction.DisableAuthorizer->7;else->error("Unsupported history action")}
                execution.consent(session.accountId,generation,operation,device,capabilities,true)
            }
        }
        execution.publishPending(session.accountId,generation,local)
        val verified=execution.devices(session.accountId,generation,execution.refresh(session.accountId,generation,local))
        if(action==ArchiveDeviceAction.Authorize||action==ArchiveDeviceAction.SetUp){
            val row=verified.single{it.peerId==device};val wanted=if(action==ArchiveDeviceAction.SetUp)3L else capabilities
            check(row.approvalKnown&&row.localGrantPresent&&row.localGrantRead==((wanted and 1L)!=0L)&&row.localGrantServe==((wanted and 2L)!=0L)){"Requested permissions were not verified by the server; no approval is claimed."}
        }
        mutable.value=mutable.value.copy(archiveProgress=execution.progress(session.accountId,generation))
        ArchiveEnrollmentSnapshot(session.accountId,generation,local,verified,execution.historySync(session.accountId,generation))
    }}
    override suspend fun setHistorySync(snapshot:ArchiveEnrollmentSnapshot,enabled:Boolean):ArchiveEnrollmentSnapshot=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled&&mutable.value.serverPresenceLive)
        val owner=checkNotNull(accounts);val session=checkNotNull(owner.session);val native=checkNotNull(profile);val local=mutable.value.localPeerId
        require(snapshot.accountId==session.accountId&&snapshot.authenticationGeneration==generation&&snapshot.localPeerId==local)
        val execution=archiveExecution(owner,native);execution.refresh(session.accountId,generation,local)
        execution.historySync(session.accountId,generation,enabled)
        mutable.value=mutable.value.copy(archiveProgress=execution.progress(session.accountId,generation))
        ArchiveEnrollmentSnapshot(session.accountId,generation,local,execution.devices(session.accountId,generation,execution.refresh(session.accountId,generation,local)),execution.historySync(session.accountId,generation))
    }}
    private val remembered=AndroidRememberedLoginStore(this.context,profileName)
    private val pushRegistration=AndroidPushRegistration(this.context,profileName)
    private var pushRegisteredToken:String?=null
    private val executions=mutableMapOf<Long,AndroidRelayExecution>()
    private val operations= com.peerchat.android.platform.tls.ServerChannel.OperationScope()
    @Volatile private var stopped=false
    @Volatile private var resetting=false
    @Volatile private var resumeSuppressed=false
    @Volatile private var inUse=true
    @Volatile private var operationDeadline=Long.MAX_VALUE
    @Volatile private var livePeerWorkAllowed=true
    @Volatile private var archiveWorkAllowed=true
    @Volatile private var quiescent=java.util.concurrent.CountDownLatch(0)
    @Volatile private var retentionUntil=0L
    @Volatile private var requestedRetentionUntil=0L
    @Volatile private var invalidRetentionUntil=0L
    @Volatile private var lifetimeRevision=0L
    private var ownerLoop:java.util.concurrent.ScheduledFuture<*>?=null
    @Synchronized internal fun setInUse(value:Boolean,deadlineNanos:Long=Long.MAX_VALUE,allowPeerWork:Boolean=deadlineNanos==Long.MAX_VALUE,allowArchiveWork:Boolean=allowPeerWork,retainConnectionsUntilNanos:Long=0L) {
        if(stopped)return
        val revision=++lifetimeRevision
        requestedRetentionUntil=retainConnectionsUntilNanos
        inUse=value
        operationDeadline=deadlineNanos
        // Background archive capability is separate from friend/transfer work;
        // signed native grants still admit every archive attempt.
        livePeerWorkAllowed=allowPeerWork
        archiveWorkAllowed=allowArchiveWork
        retentionUntil=if(!value&&!resetting&&authorizationSnapshotInstalled&&mutable.value.authenticated&&retainConnectionsUntilNanos>invalidRetentionUntil)retainConnectionsUntilNanos else 0L
        val retained=retentionUntil>System.nanoTime()
        operations.configure((value||retained)&&!resetting&&!stopped,if(retained)retentionUntil else deadlineNanos)
        if(value&&!stopped){if(ownerLoop==null||ownerLoop!!.isCancelled)startOwnerLoop()}
        else {ownerLoop?.cancel(false);ownerLoop=null}
        executor.execute {synchronized(gate){if(revision==lifetimeRevision){
            executions.values.forEach{it.setActiveWork(value)}
            if(value){
                mailboxAt=0L;refreshAt=0L;groupRefreshAt=0L;eligibilityAt=0L;pushAt=0L
                archiveMetadataAt=0L
            }
            else events?.let(::eventActivity) // Include events queued just before the foreground lease ended.
        }}}
        if(!value&&!retained){val completed=java.util.concurrent.CountDownLatch(1);quiescent=completed
            executor.execute {try{synchronized(gate){if(!inUse&&!stopped){
            if(revision!=lifetimeRevision||retentionUntil>System.nanoTime())return@synchronized
            generation++;clearAuthorization();try{if(remembered.load()==null)accounts?.logout()}catch(_:Exception){}
            accounts?.closeRpc();accounts=null;index=null;pushRegisteredToken=null
            // Quiesce NETWORK ownership, not local Profile/Storage. The one
            // process-local backend retains its open profile and immutable
            // history projection; foreground resume must not rebuild them.
            friends=friends.map{it.copy(online=false)}
            mutable.value=localOfflineSnapshot(mutable.value,"Offline; local history retained; server authorization will be verified on resume")
        }}}finally{completed.countDown()}}}
    }
    override suspend fun awaitQuiescence(milliseconds:Long):Boolean=withContext(Dispatchers.IO){quiescent.await(milliseconds,TimeUnit.MILLISECONDS)}
    private fun <T> measured(stage:String,operation:()->T):T {
        val start=System.nanoTime();android.util.Log.i("PeerChatWake","stage=${stage}_started")
        try{return operation()}finally{android.util.Log.i("PeerChatWake","stage=${stage}_returned elapsed_ms="+(System.nanoTime()-start)/1_000_000)}
    }
    init {startOwnerLoop()}
    private fun startOwnerLoop() {
        ownerLoop=executor.scheduleWithFixedDelay({
            val waiting=System.nanoTime()
            if(inUse&&!stopped&&!resetting)try {
                synchronized(gate){
                    ownerTimings.record(OwnerStage.GateWait,System.nanoTime()-waiting)
                    if(!stopped&&!resetting)try{
                        ownerTimings.measure(OwnerStage.Tick){tick()}
                        if(backendWorkScope()!=null){
                            // Call-control/events above never wait for the mailbox
                            // RPC script, cache append or History Sync round trip.
                            if(!mailboxLane.busy())borrowMailboxRequest()?.let{work->
                                launchBackendWork(mailboxLane){scope->scope.local{};work()}
                            }
                            val archive=ownerTimings.measure(OwnerStage.Tick){tickAfterMailbox()}
                            if(!cacheLane.busy())launchBackendWork(cacheLane){scope->
                                dmAttachmentExecution.serviceOutsideGate(gate){scope.local{borrowDmAttachmentRequest()}}
                            }
                            if(archive!=null)launchBackendWork(archiveLane){scope->scope.local{};archive()}
                        }
                    }catch(failure:Exception){handleTickFailure(failure)}
                }
            }catch(failure:Exception){
                // Native/auth failures are handled atomically in their scoped
                // locked phase above, not after reacquiring a replacement login.
                android.util.Log.i("PeerChatWake","stage=backend_tick_failure result="+failure.javaClass.simpleName)
            }
        },125,125,TimeUnit.MILLISECONDS)
    }
    /** Called under gate. No queued backlog and no authority captured from UI.
     * Scope checks fence native access and publication after every network wait. */
    private fun launchBackendWork(lane:BackendWorkLane,work:(ScopedBackendWork)->Unit):Boolean {
        val scope=backendWorkScope()?:return false
        val token=lane.begin()?:return false
        metadataWork.launch {
            try{work(scope)}catch(_:ScopedBackendWork.Superseded){
                // A logout/replacement owns the current state; discard old work.
            }catch(failure:Exception){scope.finish{handleTickFailure(failure)}}
            finally{lane.finish(token)}
        }
        return true
    }
    private fun backendWorkScope(extra:()->Boolean={true}):ScopedBackendWork? {
        if(stopped||resetting||!inUse||!authorizationSnapshotInstalled||!mutable.value.authenticated)return null
        val owner=accounts?:return null;val active=owner.session?:return null;val opened=profile?:return null;val revision=generation
        val identity=ScopedBackendWork.Identity(opened,owner,active,active.accountId,revision,lifetimeRevision)
        return ScopedBackendWork(gate){!stopped&&!resetting&&inUse&&authorizationSnapshotInstalled&&mutable.value.authenticated&&
            identity.matches(profile,accounts,owner.session,mutable.value.accountId,generation,lifetimeRevision)&&extra()}
    }
    // Called only while holding gate, including cache failures after the exact
    // borrowed scope guard. A stale response cannot log out a replacement login.
    private fun handleTickFailure(failure:Exception){
        if(loggedTickFailure!=failure.javaClass.simpleName){
            loggedTickFailure=failure.javaClass.simpleName
            android.util.Log.i("PeerChatWake","stage=backend_tick_failure result="+loggedTickFailure)
        }
        if(expiredServerSession(failure)){
            android.util.Log.w("PeerChatNetwork","event=authentication_invalidated generation=$generation reason="+(failure as ServerRejected).code)
            if(failure.code=="device_revoked")resumeSuppressed=true
            accounts?.closeRpc();generation++;clearAuthorization();accounts=null;index=null
            friends=friends.map{it.copy(online=false)}
            mutable.value=localOfflineSnapshot(mutable.value,if(failure.code=="device_revoked")"This device has been revoked. Local keys, history and queued messages are retained; automatic re-enrollment is not permitted." else "Session expired; local history retained; sign in again")
            // Expiring access sessions are not remembered credentials.
            // One bounded refresh through the same coordinator; never
            // fight a superseded session or bypass server revocation.
            if(inUse&&failure.code=="session_invalid")try{
                val saved=remembered.load();if(saved!=null&&!saved.revocationPending)restoreRememberedLocked(saved.endpoint)
            }catch(_:Exception){/* Retain fail-closed offline state; lifecycle/wake may retry. */}
            return
        }
        mutable.value=mutable.value.copy(serverPresenceLive=false,status=if(failure is ServerRejected)serverRefreshFailure(failure) else "Application/native update validation failed; native delivery state retained")
    }
    private fun borrowDmAttachmentRequest():DmAttachmentExecution.Borrowed? {
        if(stopped||resetting||!inUse||!mutable.value.authenticated||!authorizationSnapshotInstalled||!livePeerWorkAllowed)return null
        val owner=checkNotNull(accounts);val active=checkNotNull(owner.session);val opened=checkNotNull(profile);val revision=generation
        val lifetime=lifetimeRevision
        return try{
            configureMailboxEnrollment(owner,opened.handle)
            if(!mailboxExecution.registrationAccepted)return null
            dmAttachmentExecution.borrow(opened.handle,owner::directAttachmentCache,
                {!stopped&&!resetting&&inUse&&lifetimeRevision==lifetime&&profile===opened&&accounts===owner&&owner.session===active&&generation==revision&&
                    authorizationSnapshotInstalled&&mutable.value.authenticated&&mutable.value.accountId==active.accountId&&livePeerWorkAllowed},
                {mailboxAt=minOf(mailboxAt,System.nanoTime()/1_000_000+1000)},
                {failure->if(expiredServerSession(failure))handleTickFailure(failure) /* Other failures retain native protected retries. */})
        }catch(failure:Exception){if(expiredServerSession(failure))throw failure;null}
    }
    override suspend fun signIn(endpoint: ServerEndpoint,username: String,password: String): AccountSession = signInRemembered(endpoint,username,password,false)
    override suspend fun signInRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession =
        authenticate(endpoint,username,password,remember,false)
    override suspend fun createAccountRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession =
        authenticate(endpoint,username,password,remember,true)
    private suspend fun authenticate(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean,creating:Boolean):AccountSession = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&!resetting&&accounts==null)
            resumeSuppressed=false
            val native=profile ?: NativeProfile(context,profileName).also {profile=it}
            val key=checkNotNull(ProfileBridge.publicKey(native.handle))
            val device=PeerIdentityFormat.peerId(key)
            require(endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))
            // Explicit login replaces a previous remembered account only after
            // durable revocation. Network failure leaves a non-login tombstone.
            revokeSaved(native,key)
            val connection=ServerAccounts(endpoint,device,operations)
            // The same identity, trust, presence/bootstrap and Remember Me path.
            // Server registration rejects an identity already owned by another account.
            val authenticated=if(creating)connection.register(username,password) else connection.login(username,password)
            if(mutable.value.accountId!=authenticated.accountId){groupExecution.reset();mutable.value=mutable.value.copy(groups=emptyList(),groupInvitations=emptyList())}
            var transientCredential:RememberedLogin?=null
            try {
                val metadata=FriendIdentityIndex(context,profileName,endpoint,authenticated.accountId)
                accounts=connection;index=metadata;pushRegisteredToken=null;network++
                mutable.value=MessagingSnapshot(authenticated.accountId,authenticated.username,device,false,"Verifying device")
                // Password authentication does not register this device. Use the
                // established authenticated presence path before the combined
                // bootstrap, which correctly requires prior account ownership.
                // A device owned by another account still fails closed here.
                connection.presence()
                // Enrollment leaves the password-login bearer unbound. Resume
                // with the signed device challenge for this login even when the
                // credential is not saved for a future automatic login.
                val bound=establishDeviceBoundLogin(remember,
                    enroll={connection.remember(key){checkNotNull(ProfileBridge.sign(native.handle,it))}},
                    save={remembered.save(it)},
                    retirePasswordSession={generation++;clearAuthorization();accounts=null;index=null;friends=emptyList();connection.logout()},
                    resume={value->
                        if(remember){check(restoreRememberedLocked(endpoint));checkNotNull(accounts?.session)} else {
                            val resumedConnection=ServerAccounts(endpoint,device,operations)
                            val resumed=resumedConnection.resume(value,key){checkNotNull(ProfileBridge.sign(native.handle,it))}
                            check(resumed.accountId==authenticated.accountId)
                            accounts=resumedConnection;index=FriendIdentityIndex(context,profileName,endpoint,resumed.accountId);pushRegisteredToken=null;network++
                            mutable.value=MessagingSnapshot(resumed.accountId,resumed.username,device,true,"Authenticated; refreshing contacts")
                            refresh();refreshAt=System.nanoTime()/1_000_000+900000
                            resumed
                        }
                    },
                    revoke={value->connection.revoke(value,key){checkNotNull(ProfileBridge.sign(native.handle,it))}})
                transientCredential=bound.transientCredential
                if(remember)OpaquePushWake.refreshPeriodic(context)
                checkNotNull(accounts?.session)
            } catch(failure: Exception){
                if(!remember){try{accounts?.logout()}catch(_:Exception){}
                    transientCredential?.let{try{ServerAccounts(endpoint,device,operations).revoke(it,key){input->checkNotNull(ProfileBridge.sign(native.handle,input))}}catch(_:Exception){}}
                }
                accounts?.closeRpc();accounts=null;index=null;friends=emptyList();generation++
                clearAuthorization();try {connection.logout()}catch(_: Exception){}
                mutable.value=MessagingSnapshot(localPeerId=device,status="Login setup failed closed")
                throw MessagingStartupFailure(failure)
            }
        }
    }
    private fun revokeSaved(native:NativeProfile,key:ByteArray) {
        val saved=remembered.load() ?: run{OpaquePushWake.maintainPeriodic(context,false);return}
        check(saved.device==PeerIdentityFormat.peerId(key))
        remembered.save(saved.pendingRevocation())
        OpaquePushWake.maintainPeriodic(context,false)
        try{ServerAccounts(saved.endpoint,saved.device,operations).revoke(saved,key){checkNotNull(ProfileBridge.sign(native.handle,it))}}
        catch(failure:ServerRejected){if(failure.code!="remember_revoked")throw failure}
        remembered.clear()
    }
    override suspend fun restoreRemembered(endpoint:ServerEndpoint):Boolean = withContext(Dispatchers.IO) {
        val start=System.nanoTime()
        synchronized(gate){android.util.Log.i("PeerChatWake","stage=bootstrap_gate_acquired elapsed_ms="+(System.nanoTime()-start)/1_000_000);restoreRememberedLocked(endpoint)}
    }
    override suspend fun restoreLocalHistory(endpoint:ServerEndpoint):Boolean=withContext(Dispatchers.IO){synchronized(gate){
        restoreLocalLocked(endpoint)
    }}
    private fun restoreLocalLocked(endpoint:ServerEndpoint):Boolean {
        check(!stopped&&inUse)
        if(mutable.value.localHistoryReady&&localEndpoint==endpoint)return true
        if(mutable.value.localHistoryReady){localReadContext=null;historyProjection.clear();friends=emptyList();mutable.value=MessagingSnapshot(localPeerId=mutable.value.localPeerId)}
        val cached=ownerTimings.measure(OwnerStage.LocalIndex){localContexts.load(endpoint)} ?: remembered.load()?.takeIf{it.endpoint==endpoint}?.let{saved->
            // Upgrade the existing protected display index in place; no remote
            // connection or trust promotion is needed to view old local history.
            LocalConversationContext(saved.accountId,"Account ${saved.accountId}",saved.device,
                FriendIdentityIndex(context,profileName,endpoint,saved.accountId).localHistoryBindings())
        } ?: return false
        val native=profile ?: ownerTimings.measure(OwnerStage.ProfileOpen){measured("profile_open"){NativeProfile(context,profileName)}}.also{profile=it}
        val identity=measured("local_history_identity"){PeerIdentityFormat.peerId(checkNotNull(ProfileBridge.publicKey(native.handle)))}
        check(identity==cached.device)
        // Cached names/bindings project local Storage ONLY. Never install them
        // into ReliableBridge.friends or grant route/mailbox authorization.
        friends=cached.friends
        localEndpoint=endpoint
        historyProjection.clear()
        publishLocalReadContext(native.handle,cached.account,endpoint)
        mutable.value=MessagingSnapshot(cached.account,cached.username,identity,false,"Offline; local history available",
            localHistoryReady=true)
        // Publish the protected conversation index before authentication, native
        // history decryption for other chats, attachment scans or server work.
        mutable.value=mutable.value.copy(conversations=friends.map{ConversationSnapshot(it.copy(online=false),ConnectionSnapshot(),emptyList(),attachmentsHaveOlder=true)})
        android.util.Log.i("PeerChatWake","stage=local_history_projected contacts=${friends.size}")
        return true
    }
    private fun restoreRememberedLocked(endpoint:ServerEndpoint):Boolean {
        check(!stopped&&inUse)
        if(resetting||resumeSuppressed)return false
        operations.check()
        accounts?.let{check(it.endpoint==endpoint);publish();return true}
        restoreLocalLocked(endpoint)
        val saved=remembered.load() ?: return false
        val native=profile ?: measured("profile_open"){NativeProfile(context,profileName)}.also{profile=it}
        val key=measured("identity_read"){checkNotNull(ProfileBridge.publicKey(native.handle))};check(saved.device==PeerIdentityFormat.peerId(key))
        if(saved.revocationPending){revokeSaved(native,key);return false}
        check(saved.endpoint==endpoint){"Remembered credential belongs to different trusted server"}
        val connection=ServerAccounts(endpoint,saved.device,operations)
        try{
            mutable.value=mutable.value.copy(authenticated=false,serverPresenceLive=false,status="Signing in with saved authorization")
            val authenticated=connection.resume(saved,key){input->measured("identity_sign"){checkNotNull(ProfileBridge.sign(native.handle,input))}}
            mutable.value=mutable.value.copy(accountId=authenticated.accountId,username=authenticated.username,localPeerId=saved.device,authenticated=true,status="Connecting; establishing authenticated presence")
            // refresh below performs validated presence before friend/device setup.
            accounts=connection;index=FriendIdentityIndex(context,profileName,endpoint,authenticated.accountId);pushRegisteredToken=null;network++
            mutable.value=mutable.value.copy(status="Authenticated; refreshing contacts")
            refresh();refreshAt=System.nanoTime()/1_000_000+900000
            rememberedRetryEndpoint=null;rememberedRetryDelay=1000;return true
        }catch(failure:Exception){
            accounts?.closeRpc();accounts=null;index=null;generation++;clearAuthorization()
            rememberedRetryEndpoint=null
            if(isRememberedNetworkFailure(failure)) {
                rememberedRetryEndpoint=endpoint
                rememberedRetryAt=System.nanoTime()/1_000_000+rememberedRetryDelay
                rememberedRetryDelay=(rememberedRetryDelay*2).coerceAtMost(30000)
            }
            friends=friends.map{it.copy(online=false)}
            mutable.value=localOfflineSnapshot(mutable.value,if(rememberedRetryEndpoint!=null)"Reconnecting; saved authorization retained" else "Saved authentication failed; sign in again").copy(localPeerId=saved.device)
            if(isRememberedCredentialRejected(failure)){remembered.clear();OpaquePushWake.refreshPeriodic(context)}
            throw failure
        }
    }
    override suspend fun send(peerId: String,text: String): String = withContext(Dispatchers.IO) {
        val waiting=System.nanoTime()
        synchronized(gate) {
            ownerTimings.record(OwnerStage.SendWait,System.nanoTime()-waiting)
            check(!stopped&&friends.any {it.peerId==peerId}&&(accounts?.session!=null||mutable.value.localHistoryReady))
            val utf8=text.toByteArray(Charsets.UTF_8);require(utf8.size<=512*1024&&!text.contains('\u0000'))
            val result=ownerTimings.measure(OwnerStage.LocalCommit){if(text.length>4096){check(authorizationSnapshotInstalled);checkNotNull(com.peerchat.android.TransferBridge.offerText(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),utf8))}
                else checkNotNull(ReliableBridge.queueLocal(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),utf8))}
            val id=result.toString(Charsets.US_ASCII);check(id.matches(Regex("[0-9a-f]{32}")))
            mailboxAt=0L
            historyProjection.invalidate(friends.first{it.peerId==peerId})
            // The durable command already succeeded. A later projection/network
            // failure must not turn it into a failed Send or cause a second ID.
            val committedProfile=profile
            try{executor.execute{try{synchronized(gate){if(!stopped&&profile===committedProfile)publish()}}catch(_:Exception){}}}catch(_:java.util.concurrent.RejectedExecutionException){}
            val friend=friends.first{it.peerId==peerId};val owner=accounts
            // Best effort after native durable queueing. Not another retry policy.
            try{executor.execute{try{owner?.hint(friend.accountId,friend.peerId)}catch(_:Exception){}}}catch(_:java.util.concurrent.RejectedExecutionException){}
            id
        }
    }
    override suspend fun older(peerId: String,before: Long): List<MessageSnapshot> = withContext(Dispatchers.IO) {
        // Read-only JNI lookup admits its own native instance lease and mutex.
        // Do not wait behind authenticated RPCs holding the application gate.
        val scope=checkNotNull(localReadContext);check(!stopped&&!resetting)
        val friend=scope.friends.single{it.knownPeerIds.contains(peerId)}
        preferredLocalAccount=friend.accountId
        val rows=ownerTimings.measure(OwnerStage.LocalPage){accountHistoryPage(friend,before){peer,cursor->
            checkNotNull(ReliableBridge.history(scope.handle,peer.toByteArray(Charsets.US_ASCII),cursor))}}
        check(localReadContext===scope&&!stopped&&!resetting){"Local history scope changed"}
        try{olderAttachments(scope,friend)}catch(_:Exception){
            // An isolated new cache record failure must not hide an already
            // read legacy text page or erase any protected attachment bytes.
            mutable.update{value->if(localReadContext!==scope)value else value.copy(transferDiagnostics="Protected attachment history unavailable; original records and copies retained")}
        }
        rows
    }
    override suspend fun createGroup(name:String)=withContext(Dispatchers.IO){synchronized(gate){
        check(authorizationSnapshotInstalled&&livePeerWorkAllowed);val s=mutable.value
        groupExecution.create(checkNotNull(accounts),checkNotNull(profile).handle,s.accountId,generation,name,s.username);groupRefreshAt=0L
    }}
    override suspend fun inviteGroup(id:String,target:Long)=withContext(Dispatchers.IO){synchronized(gate){
        check(authorizationSnapshotInstalled);val g=mutable.value.groups.single{it.id==id};require(friends.any{it.accountId==target})
        groupExecution.invite(checkNotNull(accounts),id,g.epoch,target);groupRefreshAt=0L
    }}
    override suspend fun respondGroup(id:String,accept:Boolean)=withContext(Dispatchers.IO){synchronized(gate){
        check(authorizationSnapshotInstalled);groupExecution.respond(checkNotNull(accounts),id,accept);groupRefreshAt=0L
    }}
    override suspend fun sendGroup(id:String,text:String):String=withContext(Dispatchers.IO){synchronized(gate){
        check(authorizationSnapshotInstalled&&livePeerWorkAllowed);require(text.isNotBlank()&&text.length<=4096)
        val message=groupExecution.send(checkNotNull(profile).handle,mutable.value.accountId,generation,id,text){events?.queueGroup(it)==true}
        groupPendingMore=true;groupPendingAt=0L
        try{mutable.value=mutable.value.copy(groups=groupExecution.snapshot(checkNotNull(profile).handle,mutable.value.accountId,generation))}catch(_:Exception){}
        message
    }}
    override suspend fun groupHistory(id:String,before:Long):List<GroupMessageSnapshot> = withContext(Dispatchers.IO){synchronized(gate){
        check(authorizationSnapshotInstalled);val page=groupExecution.load(checkNotNull(profile).handle,mutable.value.accountId,generation,id,before,true)
        mutable.value=mutable.value.copy(groups=groupExecution.snapshot(checkNotNull(profile).handle,mutable.value.accountId,generation));page
    }}
    override suspend fun retryGroup(id:String,message:String)=withContext(Dispatchers.IO){synchronized(gate){
        check(authorizationSnapshotInstalled&&livePeerWorkAllowed)
        groupExecution.retry(checkNotNull(profile).handle,mutable.value.accountId,generation,id,message){events?.queueGroup(it)==true}
        mutable.value=mutable.value.copy(groups=groupExecution.snapshot(checkNotNull(profile).handle,mutable.value.accountId,generation))
    }}
    private var transferDiagnosticsAt=0L
    private var attachmentProjectionAt=0L;private var attachmentProjectionHandle=0L;private var attachmentProjectionGeneration=0L
    private var attachmentProjection=emptyList<AttachmentSnapshot>()
    private data class AttachmentDisplayHistory(val rows:LinkedHashMap<String,AttachmentSnapshot> = linkedMapOf(),var cursor:String="",var more:Boolean=true)
    private val attachmentHistoryGate=Any()
    private var attachmentHistoryScope:LocalReadContext?=null
    private val attachmentDisplayHistory=mutableMapOf<String,AttachmentDisplayHistory>()
    private fun olderAttachments(scope:LocalReadContext,friend:FriendIdentity){
        val display=synchronized(attachmentHistoryGate){
            if(attachmentHistoryScope!==scope){attachmentDisplayHistory.clear();attachmentHistoryScope=scope}
            val history=attachmentDisplayHistory.getOrPut(friend.peerId){AttachmentDisplayHistory()}
            if(history.more){
                val fields=NativeRecordWriter(30).also{w->w.number(scope.account);w.text(scope.endpoint.host.lowercase(java.util.Locale.ROOT)+":"+scope.endpoint.port);w.text(scope.endpoint.certificatePin.lowercase(java.util.Locale.ROOT));w.text("");w.text(history.cursor)}.bytes()
                val page=dmLocalAttachmentPage(checkNotNull(com.peerchat.android.TransferBridge.request(scope.handle,15,fields)){"Protected attachment history is unavailable"})
                check(localReadContext===scope&&!stopped&&!resetting){"Local attachment history scope changed"}
                page.rows.filter{it.peer in friend.knownPeerIds}.forEach{history.rows[it.id]=it};history.cursor=page.cursor;history.more=page.more
            }
            history.rows.values.toList() to history.more
        }
        mutable.update{value->if(localReadContext!==scope)value else value.copy(conversations=value.conversations.map{c->
            if(c.friend.accountId!=friend.accountId)c else c.copy(attachments=(display.first+c.attachments).associateBy{it.id}.values.sortedBy{it.created},attachmentsHaveOlder=display.second)
        })}
    }
    private val transferDiagnosticStates=mutableMapOf<String,Pair<String,Long>>()
    private fun transfer(operation:Int,write:(NativeRecordWriter)->Unit={}):ByteArray {
        val request=NativeRecordWriter(30).also(write).bytes()
        return checkNotNull(com.peerchat.android.TransferBridge.request(checkNotNull(profile).handle,operation,request)){"Native transfer operation rejected"}
    }
    private data class PendingAttachmentCompatibility(val token:String,val owner:ServerAccounts,val session:com.peerchat.android.platform.AccountSession,
        val generation:Long,val peer:String,val recipient:Long,val path:String,val name:String,
        val preflightTransportUnavailable:Boolean=false,val cachedReady:Boolean=false,val selectedDirect:Boolean=false)
    private val attachmentSelection=AttachmentSelectionOwner<PendingAttachmentCompatibility>()
    private fun compatibilityScope(p:PendingAttachmentCompatibility)=!stopped&&authorizationSnapshotInstalled&&accounts===p.owner&&
        p.owner.session===p.session&&generation==p.generation&&friends.any{it.peerId==p.peer&&it.accountId==p.recipient}
    private fun directCompatibility(p:PendingAttachmentCompatibility,commit:Boolean):ByteArray=transfer(16){w->
        w.number(p.session.accountId);w.number(p.generation);w.number(p.recipient);w.text(p.peer);w.number(if(commit)1 else 0)
        w.text(if(commit)p.path else "");w.text(if(commit)p.name else "")
    }
    private fun directCompatibilityStatus(p:PendingAttachmentCompatibility):Boolean?=try{
        check(compatibilityScope(p));val r=NativeRecord(directCompatibility(p,false),30);val ready=r.number();r.end();require(ready in 0..1);ready==1L
    }catch(_:Exception){null}
    private fun attachmentCacheReady(p:PendingAttachmentCompatibility):Boolean {
        check(compatibilityScope(p))
        configureMailboxEnrollment(p.owner,checkNotNull(profile).handle)
        if(!mailboxExecution.registrationAccepted){if(mailboxInFlight==null)mailboxAt=0L;return false}
        val r=NativeRecord(transfer(11){it.number(p.session.accountId);it.number(p.generation)},30)
        val ready=r.number();r.end()
        if(ready==2L)throw AttachmentModeUnavailable("cached_unsupported")
        return ready==1L
    }
    override suspend fun sendAttachment(peerId:String,uri:android.net.Uri,mode:AttachmentSendMode):String=withContext(Dispatchers.IO){
        val (intent, selected)=synchronized(gate){
            attachmentSelection.begin {
                check(!stopped&&!resetting){"Attachment selection is unavailable"}
                val local=checkNotNull(localReadContext){"Open the intended conversation before selecting an attachment"}
                check(local.friends.any{it.peerId==peerId}){"The selected recipient is unavailable"}
                checkNotNull(profile) to local
            }
        }
        val (selectedProfile, selectedLocal)=selected
        try{
        // A picker may return before remembered authentication. Wait outside
        // the owner gate, bound to the original local profile/account/server.
        val pending=awaitAttachmentReadiness(probe={synchronized(gate){attachmentModeReadiness(mode,{
            check(!stopped&&!resetting&&attachmentSelection.isPreparing(intent)&&profile===selectedProfile&&
                localReadContext?.account==selectedLocal.account&&localReadContext?.endpoint==selectedLocal.endpoint){"Attachment profile changed; nothing was sent"}
            if(!inUse||!authorizationSnapshotInstalled||!mutable.value.authenticated)return@attachmentModeReadiness null
            val owner=accounts?:return@attachmentModeReadiness null
            val session=owner.session?:return@attachmentModeReadiness null
            check(session.accountId==selectedLocal.account&&owner.endpoint==selectedLocal.endpoint)
            check(friends.any{it.peerId==peerId}){"Recipient authorization changed"}
            // Native direct route validation needs only this authenticated scope,
            // not a registered cache key or cache capability.
            configureMailboxScope(owner,selectedProfile.handle)
            PendingAttachmentCompatibility(java.util.UUID.randomUUID().toString().replace("-",""),owner,session,generation,
                peerId,friends.single{it.peerId==peerId}.accountId,"","")
        },{attachmentCacheReady(it)})}})
        val work=ScopedBackendWork(gate){attachmentSelection.isPreparing(intent)&&profile===selectedProfile&&inUse&&compatibilityScope(pending)}
        val directory=work.local{val r=NativeRecord(transfer(0),30);java.io.File(r.text()).also{r.end()}}
        // Slow SAF providers, copying and fsync must not hold the messaging gate.
        // The private source is retained on ambiguous offer/auth failure.
        val imported=work.network{importAttachment(context,uri,directory)}
        kotlinx.coroutines.currentCoroutineContext().ensureActive()
        val bound=pending.copy(path=imported.first,name=imported.second)
        if(mode==AttachmentSendMode.Direct){
            val ready=awaitAttachmentDirectRoute(
                {synchronized(gate){if(!attachmentSelection.isPreparing(intent))null else directCompatibilityStatus(bound)}},
                {synchronized(gate){attachmentSelection.isPreparing(intent)&&compatibilityScope(bound)&&
                    ReliableBridge.demand(checkNotNull(profile).handle,bound.peer.toByteArray(Charsets.US_ASCII))==0}})
            kotlinx.coroutines.currentCoroutineContext().ensureActive()
            if(ready)return@withContext work.local{
                val response=NativeRecord(directCompatibility(bound,true),30);val id=response.text();response.end();publish();id
            }
            // An offline Direct selection may offer Cached only after the same
            // registration/capability gate as an explicit Cached selection.
            try{awaitAttachmentReadiness(probe={synchronized(gate){
                check(attachmentSelection.isPreparing(intent)&&compatibilityScope(bound))
                if(attachmentCacheReady(bound))bound else null
            }})}catch(failure:AttachmentModeUnavailable){
                if(failure.reason=="cached_unsupported")throw AttachmentModeUnavailable("direct_device_offline; cached_unsupported")
                throw failure
            }catch(_:AttachmentReadinessUnavailable){throw AttachmentModeUnavailable("direct_device_offline; cached_readiness_timeout")}
            val alternative=work.network{attachmentSendPreflight(bound.recipient,
                {synchronized(gate){attachmentSelection.isPreparing(intent)&&compatibilityScope(bound)}},bound.owner::directAttachmentCache)}
            if(alternative!=AttachmentSendPreflight.Cached)throw AttachmentModeUnavailable(
                if(alternative==AttachmentSendPreflight.UpgradeChoice)"direct_device_offline; cached_recipient_upgrade_required"
                else "direct_device_offline; cached_preflight_unavailable")
            val pending=work.local{bound.copy(cachedReady=true,selectedDirect=true).also{attachmentSelection.pending=it}}
            throw AttachmentCompatibilityRequired(AttachmentCompatibilityChoice(pending.token,pending.name,false,pending.peer,
                cachedReady=true,selectedDirect=true))
        }
        val preflight=work.network{attachmentSendPreflight(bound.recipient,
            {synchronized(gate){attachmentSelection.isPreparing(intent)&&compatibilityScope(bound)}},bound.owner::directAttachmentCache)}
        kotlinx.coroutines.currentCoroutineContext().ensureActive()
        if(preflight==AttachmentSendPreflight.Cached)return@withContext work.local{
            val response=NativeRecord(transfer(10){it.text(peerId);it.text(bound.path);it.text(bound.name)},30)
            val id=response.text();response.end();publish();id
        }
        val prepared=work.local{bound.copy(preflightTransportUnavailable=preflight==AttachmentSendPreflight.TransportChoice).also{attachmentSelection.pending=it}}
        val ready=try{awaitAttachmentDirectRoute(
            {synchronized(gate){if(attachmentSelection.pending!==prepared)null else directCompatibilityStatus(prepared)}},
            {synchronized(gate){compatibilityScope(prepared)&&attachmentSelection.pending===prepared&&
                ReliableBridge.demand(checkNotNull(profile).handle,prepared.peer.toByteArray(Charsets.US_ASCII))==0}})
        }catch(failure:Throwable){synchronized(gate){attachmentSelection.clearIfCurrent(prepared)};throw failure}
        synchronized(gate){if(!compatibilityScope(prepared)||attachmentSelection.pending!==prepared){
            attachmentSelection.clearIfCurrent(prepared)
            error("Attachment authorization changed; private source retained")
        }}
        throw AttachmentCompatibilityRequired(AttachmentCompatibilityChoice(prepared.token,prepared.name,ready,prepared.peer,prepared.preflightTransportUnavailable))
        }finally{synchronized(gate){attachmentSelection.finish(intent)}}
    }
    override suspend fun resolveAttachmentCompatibility(token:String,mode:AttachmentSendMode?):String?=withContext(Dispatchers.IO){
        val pending=synchronized(gate){attachmentSelection.pending}
        if(mode==null){synchronized(gate){if(pending?.token==token)attachmentSelection.clearIfCurrent(pending)};return@withContext null}
        check(pending!=null&&pending.token==token&&synchronized(gate){compatibilityScope(pending)}){"Attachment authorization changed; private source retained"}
        if(mode==AttachmentSendMode.Cached){
            check(pending.cachedReady){"Cached transfer was not explicitly offered"}
            val work=ScopedBackendWork(gate){attachmentSelection.pending===pending&&compatibilityScope(pending)}
            val preflight=work.network{attachmentSendPreflight(pending.recipient,
                {synchronized(gate){attachmentSelection.pending===pending&&compatibilityScope(pending)}},pending.owner::directAttachmentCache)}
            if(preflight!=AttachmentSendPreflight.Cached)throw AttachmentModeUnavailable(
                if(preflight==AttachmentSendPreflight.UpgradeChoice)"cached_recipient_upgrade_required" else "cached_preflight_unavailable")
            return@withContext work.local{
                if(!attachmentCacheReady(pending))throw AttachmentModeUnavailable("cached_readiness_lost")
                val response=NativeRecord(transfer(10){it.text(pending.peer);it.text(pending.path);it.text(pending.name)},30)
                val id=response.text();response.end();attachmentSelection.clearIfCurrent(pending);publish();id
            }
        }
        synchronized(gate){
            check(!pending.selectedDirect&&attachmentSelection.pending===pending&&compatibilityScope(pending))
            val response=NativeRecord(directCompatibility(pending,true),30);val id=response.text();response.end()
            attachmentSelection.clearIfCurrent(pending);publish();id
        }
    }
    override suspend fun attachmentAction(id:String,accept:Boolean)=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled);transfer(if(accept)2 else 3){it.text(id)};publish();Unit
    }}
    override suspend fun attachmentText(id:String):String=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped);val bytes=checkNotNull(com.peerchat.android.TransferBridge.readText(checkNotNull(profile).handle,id.toByteArray(Charsets.US_ASCII)))
        Charsets.UTF_8.newDecoder().onMalformedInput(java.nio.charset.CodingErrorAction.REPORT).onUnmappableCharacter(java.nio.charset.CodingErrorAction.REPORT).decode(java.nio.ByteBuffer.wrap(bytes)).toString()
    }}
    override suspend fun requestFriend(username:String) = withContext(Dispatchers.IO) {synchronized(gate){
        require(username.isNotBlank()&&username.length<=64)
        val owner=checkNotNull(accounts)
        val found=owner.request("search",org.json.JSONObject().put("username",username))
        val account=found.getLong("accountId");require(account>0&&account!=owner.session?.accountId)
        check(friends.none{it.accountId==account}&&mutable.value.incomingRequests.none{it.accountId==account}&&mutable.value.outgoingRequests.none{it.accountId==account}){"Relationship already exists or a request is pending"}
        owner.request("friend_request",org.json.JSONObject().put("accountId",account.toString()))
        refresh();publish()
    }}
    override suspend fun searchPeople(query:String):List<DirectoryAccountSnapshot> = withContext(Dispatchers.IO){
        val value=query.trim();require(value.length in 1..32&&value.all{it.isLetterOrDigit()||it=='-'||it=='_'}&&(value.length>=2||value.toLongOrNull()!=null))
        val scope=synchronized(gate){check(!stopped&&authorizationSnapshotInstalled&&mutable.value.authenticated);Triple(checkNotNull(accounts),generation,mutable.value.accountId)}
        // Optional directory lookup never holds the message/archive owner lock
        // or monopolizes its authenticated persistent RPC channel.
        val rows=scope.first.request("people_search",org.json.JSONObject().put("query",value),allowRpc=false).getJSONArray("accounts");require(rows.length()<=15)
        synchronized(gate){check(!stopped&&accounts===scope.first&&generation==scope.second&&mutable.value.accountId==scope.third&&authorizationSnapshotInstalled)
            List(rows.length()){i->val row=rows.getJSONObject(i);val id=row.getLong("accountId");val name=row.getString("username");require(id>0&&id!=scope.third&&name.length<=64);DirectoryAccountSnapshot(id,name,row.optBoolean("online"))}}
    }
    override suspend fun cancelFriend(accountId:Long)=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled&&mutable.value.outgoingRequests.any{it.accountId==accountId})
        checkNotNull(accounts).request("friend_cancel",org.json.JSONObject().put("accountId",accountId.toString()));refresh();publish()
    }}
    override suspend fun refreshContacts()=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled)
        contactsDirty=true;refreshAt=0L;groupRefreshAt=0L;projectionCadence.dirty()
    }}
    override suspend fun transferSettings():TransferSettingsSnapshot=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&profile!=null);TransferSettingsSnapshot.decode(transfer(6))
    }}
    override suspend fun saveTransferSettings(value:TransferSettingsSnapshot)=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&profile!=null);value.validate();TransferSettingsSnapshot.decode(transfer(7){it.number(value.preferredChunkBytes.toLong());it.number(value.preferredWindow.toLong());it.number(value.uploadBitsPerSecond);it.number(value.downloadBitsPerSecond)});Unit
    }}
    override suspend fun respondFriend(accountId:Long,accept:Boolean) = withContext(Dispatchers.IO) {synchronized(gate){
        require(accountId>0&&mutable.value.incomingRequests.any {it.accountId==accountId})
        checkNotNull(accounts).request("friend_respond",org.json.JSONObject().put("accountId",accountId.toString()).put("action",if(accept)"accept" else "reject"))
        refresh();publish()
    }}
    private fun clearAuthorization() {
        stopGroupVoice();groupVoiceRooms.reset();groupVoiceSnapshotAt=0L
        voiceConsents.reset();voiceOwnedCall=null;voiceScopeIdentity=null;voiceOwner="";voiceCredentials=null
        voiceCredentialRefresh=null;voiceCredentialRetryAt=0L
        voicePlatformDiagnostic="";voiceSpeaker=false
        mutable.value=mutable.value.copy(voice=VoiceSnapshot(diagnostic="Voice authorization unavailable"),voiceRooms=emptyList())
        voiceNotifications.update(mutable.value.voice)
        ownDeviceRequest=null;ownDeviceAt=0L;ownDeviceProjection.reset();ownDeviceStatus="not_requested"
        attachmentSelection.pending=null // Private source bytes are retained.
        invalidRetentionUntil=maxOf(invalidRetentionUntil,retentionUntil,requestedRetentionUntil)
        retentionUntil=0L
        if(!inUse)operations.configure(false,Long.MAX_VALUE)
        mailboxEnrollmentScope=null;mailboxEnrollmentEndpoint=null
        profile?.let{try{com.peerchat.android.TransferBridge.request(it.handle,11,NativeRecordWriter(30).also{w->w.number(0);w.number(0)}.bytes())}catch(_:Exception){}}
        pendingDeviceVerifications.clear();declinedDeviceVerifications.clear();relayFailureDiagnostics.clear()
        mutable.value=mutable.value.copy(deviceVerifications=emptyList(),mailboxReenrollment=null,mailboxRecoveryError=null,ownDevices=null)
        groupRefreshAt=0L;groupProjectionAt=0L;groupCompletionAt=0L;groupCompletionObserved.clear()
        peerAuthenticationDiagnostics.clear()
        authorizationSnapshotInstalled=false
        eligibilityAt=0L;pushAt=0L;nativeDiagnosticAt=0L;nativeDiagnosticSampleAt=0L
        nativeDiagnostic="native_registration_stage=waiting_for_idle_diagnostic_sample"
        events?.close();events=null;eventWasConnected=false;eventRetryAt=0L;eventRetryDelay=1000L
        mailboxExecution=MailboxExecution();mailboxPublication.reset();eligibilitySnapshots=EligibilitySnapshotExecution();eligibilityOwner=null;mailboxAt=0L
        val writer=NativeRecordWriter(1);writer.number(0)
        profile?.let {check(ReliableBridge.friends(it.handle,0,generation.coerceAtLeast(1),writer.bytes())==0)}
        executions.values.forEach {it.close()};executions.clear()
    }
    override suspend fun resetNetworkSession() {
        // Interrupt socket I/O BEFORE acquiring the coordinator lock held by
        // startup. Otherwise recovery queues behind the operation it must stop.
        resetting=true;resumeSuppressed=true
        operations.configure(false,Long.MAX_VALUE)
        try {withContext(Dispatchers.IO){synchronized(gate){
            generation++;network++
            rememberedRetryEndpoint=null;rememberedRetryAt=0L;rememberedRetryDelay=1000L
            val old=accounts;accounts=null;index=null;pushRegisteredToken=null
            old?.closeRpc()
            try {clearAuthorization()}finally {
                executions.values.forEach{it.close()};executions.clear()
                friends=friends.map{it.copy(online=false)}
                refreshAt=0L;archiveMetadataAt=0L;archiveRetryAt=0L;archiveServingUntil=0L;archiveLoadedGeneration=-1L
                contactsDirty=false;loggedTickFailure=null
                mutable.value=localOfflineSnapshot(mutable.value,"Network session reset; sign in to reconnect")
            }
            android.util.Log.i("PeerChatNetwork","event=session_reset result=disconnected generation=$generation")
        }}}finally {resetting=false;operations.configure(inUse&&!stopped,operationDeadline)}
    }
    override suspend fun signOut():Unit = withContext(Dispatchers.IO) {
        synchronized(gate) {
            OpaquePushWake.maintainPeriodic(context,false)
            rememberedRetryEndpoint=null;rememberedRetryDelay=1000
            generation++;clearAuthorization()
            localReadContext=null;historyProjection.clear()
            val old=accounts;accounts=null;friends=emptyList();index=null
            pushRegisteredToken=null
            try {profile?.let{revokeSaved(it,checkNotNull(ProfileBridge.publicKey(it.handle)))};old?.logout()}
            finally {groupExecution.reset();mutable.value=MessagingSnapshot(localPeerId=mutable.value.localPeerId)}
            Unit
        }
    }
    private fun refresh(bootstrap:Boolean=true)=ownerTimings.measure(OwnerStage.Contacts){refreshLocked(bootstrap)}
    private fun refreshLocked(bootstrap:Boolean) {
        val accounts=checkNotNull(accounts);val active=checkNotNull(accounts.session)
        val state=if(bootstrap)accounts.bootstrap() else accounts.friends()
        applyContacts(accounts,active,state,bootstrap)
    }
    private fun applyContacts(accounts:ServerAccounts,active:AccountSession,state:org.json.JSONObject,bootstrap:Boolean) {
        val pending=state.getJSONArray("incoming");require(pending.length()<=128)
        val requests=List(pending.length()){i->val item=pending.getJSONObject(i)
            val id=item.getLong("accountId");val name=item.getString("username");require(id>0&&name.length<=64)
            AccountRequestSnapshot(id,name)
        }
        val list=state.getJSONArray("friends");require(list.length()<=128)
        val projected=List(list.length()) {i->val item=list.getJSONObject(i)
            val revoked=item.optJSONArray("revokedPublicIdentities")
            require(revoked==null||revoked.length()<=64)
            FriendIdentity(item.getLong("accountId"),item.getString("username"),item.getString("publicIdentity"),item.getBoolean("online"),
                revokedPeerIds=if(revoked==null)emptyList()else List(revoked.length()){revoked.getString(it)},deviceNames=deviceDisplayNames(item.opt("deviceNames")))
        }.map{row->row.copy(deviceNames=row.deviceNames?:friends.firstOrNull{it.accountId==row.accountId}?.deviceNames)}
        require(projected.none {it.accountId==active.accountId})
        val authoritative=if(projected.map{it.copy(knownPeerIds=emptyList())}==friends.map{it.copy(knownPeerIds=emptyList())})friends else checkNotNull(index).reconcile(projected)
        android.util.Log.i("PeerChatWake","stage=friend_devices_restored contacts=${authoritative.size} devices=${authoritative.sumOf{it.knownPeerIds.size}}")
        val writer=NativeRecordWriter(1);writer.number(authoritative.size.toLong())
        authoritative.forEach {friend->writer.number(friend.accountId);writer.text(friend.peerId);writer.number(friend.knownPeerIds.size.toLong());friend.knownPeerIds.forEach {writer.text(it)}}
        // Optional local JNI extension, not a messaging wire change. Historical
        // identities remain intact; native authority keeps a separate deny set.
        writer.text("PeerChat/RevokedFriendDevices/v1");writer.number(authoritative.size.toLong())
        authoritative.forEach{friend->writer.number(friend.accountId);writer.number(friend.revokedPeerIds.size.toLong());friend.revokedPeerIds.forEach{writer.text(it)}}
        val identityChanged=!authorizationSnapshotInstalled||!sameFriendAuthority(authoritative,friends)
        if(identityChanged){
            invalidRetentionUntil=maxOf(invalidRetentionUntil,retentionUntil,requestedRetentionUntil)
            generation++;groupRefreshAt=0L;groupProjectionAt=0L
        }
        val native=checkNotNull(profile).handle
        if(identityChanged)check(ReliableBridge.friends(native,active.accountId,generation,writer.bytes())==0)
        authorizationSnapshotInstalled=true
        if(identityChanged)configureMailboxEnrollment(accounts,native)
        check(ReliableBridge.network(native,generation,network)==0)
        val changed=authoritative!=friends
        friends=authoritative
        localEndpoint=accounts.endpoint
        publishLocalReadContext(native,active.accountId,accounts.endpoint)
        if(changed||bootstrap)localContexts.save(accounts.endpoint,LocalConversationContext(active.accountId,active.username,mutable.value.localPeerId,authoritative))
        val outgoing=state.getJSONArray("outgoing");require(outgoing.length()<=128)
        val sent=List(outgoing.length()){i->val row=outgoing.getJSONObject(i);val id=row.getLong("accountId");val name=row.getString("username");require(id>0&&name.length<=64);AccountRequestSnapshot(id,name)}
        mutable.value=mutable.value.copy(incomingRequests=requests.toList(),outgoingRequests=sent)
        friends.filter{it.peerId !in it.revokedPeerIds}.forEach {friend->check(ReliableBridge.presence(native,friend.peerId.toByteArray(Charsets.US_ASCII),friend.online,generation,network)==0)}
        val presenceLive=!livePeerWorkAllowed||events?.connected==true
        mutable.value=mutable.value.copy(serverPresenceLive=presenceLive,localHistoryReady=true,status=if(presenceLive)"Authenticated server presence available; message delivery requires a verified peer or mailbox ACK" else "Connecting authenticated server events; local history available")
        // Complete application projection as part of backend bootstrap, not a
        // later UI collector/tick or optional vendor registration side effect.
        // Publish the cheap verified contact list first. Protected history and
        // diagnostics are separate bounded projection work, not contact refresh.
        mutable.value=mutable.value.copy(conversations=authoritative.map{friend->
            mutable.value.conversations.firstOrNull{it.friend.accountId==friend.accountId&&it.friend.peerId==friend.peerId}?.copy(friend=friend)
                ?:ConversationSnapshot(friend,ConnectionSnapshot(),historyProjection.rows(friend))
        })
        projectionCadence.dirty()
        android.util.Log.i("PeerChatWake","stage=conversations_projected contacts=${mutable.value.conversations.size}")
        // Bootstrap intentionally omits optional labels. Reuse the ordinary
        // friend refresh once, without delaying first local/online projection.
        if(bootstrap)contactsDirty=true
    }
    private fun refreshContactsAsync(bootstrap:Boolean) {
        val owner=checkNotNull(accounts);val active=checkNotNull(owner.session)
        val hint=contactAuthorityHint
        if(contactLane.busy())return
        contactsDirty=false
        refreshAt=System.nanoTime()/1_000_000+if(events?.connected==true)900000 else 120000
        launchBackendWork(contactLane){scope->
            try{
                val response=ownerTimings.measure(OwnerStage.ContactNetwork){scope.network{if(bootstrap)owner.bootstrap()else owner.friends()}}
                scope.local{
                    if(hint!=contactAuthorityHint){contactsDirty=true;return@local}
                    ownerTimings.measure(OwnerStage.ContactApply){applyContacts(owner,active,response,bootstrap)}
                }
            }catch(failure:java.io.IOException){scope.finish{
                mutable.value=mutable.value.copy(status=serverRefreshFailure(failure)+"; local history retained")
                refreshAt=minOf(refreshAt,System.nanoTime()/1_000_000+15000)
            }}
        }
    }
    /** One optional, bounded metadata request at a time. It borrows the exact
     * login/session/profile epoch, never holds gate during TLS, and uses no
     * persistent message or transfer RPC lane. Settings need not be open. */
    private fun refreshOwnDevices(now:Long) {
        if(ownDeviceRequest!=null||now<ownDeviceAt)return
        val owner=accounts?:return;val active=owner.session?:return
        val epoch=owner.presenceEpoch
        if(!epoch.matches(Regex("[0-9a-f]{32}")))return
        ownDeviceProjection.bind(active.accountId,epoch)
        if(mutable.value.ownDevices?.epoch!=epoch)mutable.value=mutable.value.copy(ownDevices=null)
        val scope=backendWorkScope{accounts===owner&&owner.session===active&&owner.presenceEpoch==epoch}?:return
        val request=Any();ownDeviceRequest=request;ownDeviceAt=now+60000
        metadataWork.launch {
            try {
                val response=scope.network{owner.ownDevices()}
                val inventory=decodeOwnDeviceInventory(response,active.accountId,owner.device,epoch)
                scope.finish {
                    if(ownDeviceRequest!==request)return@finish
                    if(ownDeviceProjection.accept(inventory)){
                        if(inventory.devices.single{it.current}.revoked){handleTickFailure(ServerRejected("device_revoked"));return@finish}
                        ownDeviceStatus="current";mutable.value=mutable.value.copy(ownDevices=inventory)
                    }else {ownDeviceStatus="stale_snapshot_rejected";ownDeviceAt=System.nanoTime()/1_000_000+5000}
                }
            }catch(failure:Exception){scope.finish {
                if(ownDeviceRequest!==request)return@finish
                if(expiredServerSession(failure)){handleTickFailure(failure);return@finish}
                val unsupported=failure is ServerRejected&&failure.code in setOf("unsupported_request","unknown_request","device_management_session_required")
                ownDeviceStatus=if(unsupported)"unavailable_for_server_or_session" else "refresh_failed"
                // Older servers and unbound password sessions are not silently
                // enrolled. A new authenticated session may retry support.
                if(unsupported)ownDeviceAt=Long.MAX_VALUE
            }}finally {synchronized(gate){if(ownDeviceRequest===request)ownDeviceRequest=null}}
        }
    }
    private fun tick() {
        if(!inUse)return
        operations.check()
        val now=System.nanoTime()/1_000_000
        if(accounts?.session==null) {
            rememberedRetryEndpoint?.let {endpoint->if(now>=rememberedRetryAt) {
                try {restoreRememberedLocked(endpoint)}catch(_:Exception){/* Restore classifies and schedules only transient failure. */}
            }}
            return
        }
        val native=checkNotNull(profile).handle
        if(livePeerWorkAllowed||archiveWorkAllowed||!mailboxAllowed()){
            if(events==null&&now>=eventRetryAt){events=ForegroundEvents(checkNotNull(accounts),::eventActivity);android.util.Log.i("PeerChatNetwork","event=connecting")}
            events?.let {stream->
                if(stream.connected&&!eventWasConnected){
                    eventWasConnected=true;eventRetryDelay=1000L;refreshAt=0L;mailboxAt=0L;ownDeviceAt=0L
                    groupPendingMore=true;groupPendingAt=0L
                    try{groupExecution.resetPending(native,mutable.value.accountId,generation)}catch(_:Exception){}
                    android.util.Log.i("PeerChatNetwork","event=connected repair=requested")
                }
                if(stream.finished){
                    stopGroupVoice("Voice event channel disconnected; join again after reconnecting")
                    groupVoiceRooms.reset();groupVoiceSnapshotAt=0L;mutable.value=mutable.value.copy(voiceRooms=emptyList())
                    val failure=stream.failure;stream.close();events=null;eventWasConnected=false
                    mutable.value=mutable.value.copy(serverPresenceLive=false,status="Reconnecting authenticated server events; local history available")
                    eventRetryAt=now+eventRetryDelay;eventRetryDelay=(eventRetryDelay*2).coerceAtMost(30000)
                    refreshAt=minOf(refreshAt,now+120000)
                    android.util.Log.i("PeerChatNetwork","event=disconnected repair=bounded")
                    if(failure!=null&&expiredServerSession(failure))throw failure
                }else {
                    repeat(32){stream.poll()?.let{event->handleServerEvent(event)}}
                }
            }
        }else {stopGroupVoice();groupVoiceRooms.reset();groupVoiceSnapshotAt=0L;mutable.value=mutable.value.copy(voiceRooms=emptyList());events?.close();events=null;eventWasConnected=false}
        if(events?.connected==true&&authorizationSnapshotInstalled)refreshOwnDevices(now)
        // Drain call control before optional metadata/history projection. The
        // server channel remains the sole authority for room membership.
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&events?.connected==true)pumpGroupVoice(now)
        // Server authorization and stored delivery precede NEW peer attempts.
        // Existing peer executions remain independent platform workers.
        if(now>=refreshAt||contactsDirty) {
            refreshContactsAsync(now>=refreshAt)
        }
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&now>=groupRefreshAt&&!groupLane.busy())refreshGroupsAsync(now)
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&now>=groupProjectionAt){
            groupProjectionAt=now+1000
            try{publishVoice()}catch(_:Exception){mutable.value=mutable.value.copy(voice=VoiceSnapshot(diagnostic="Voice capability readout unavailable"))}
            try{mutable.value=mutable.value.copy(groups=groupExecution.snapshot(native,mutable.value.accountId,generation),groupInvitations=groupExecution.invitations,groupDiagnostics=groupExecution.diagnostic)}catch(_:Exception){}
        }
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&events?.connected==true&&groupPendingMore&&now>=groupPendingAt){
            groupPendingAt=now+1000
            try{groupPendingMore=groupExecution.submitPending(native,mutable.value.accountId,generation){events?.queueGroup(it)==true}}
            catch(_:Exception){groupPendingAt=now+30000}
        }
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&events?.connected==true)pumpGroupVoice(now)
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed)refreshVoiceCredentials(now)
    }
    private fun refreshGroupsAsync(now:Long){
        val owner=checkNotNull(accounts);val native=checkNotNull(profile).handle;val account=mutable.value.accountId;val revision=generation
        val hint=groupAuthorityHint
        groupRefreshAt=now+60000
        launchBackendWork(groupLane){scope->
            try{
                val prepared=ownerTimings.measure(OwnerStage.GroupNetwork){scope.network{groupExecution.fetch(owner,native,account,revision)}}
                scope.local{
                    if(hint!=groupAuthorityHint){groupRefreshAt=0L;return@local}
                    ownerTimings.measure(OwnerStage.GroupApply){groupExecution.apply(native,account,revision,prepared)}
                    groupProjectionAt=0L;groupPendingMore=true}
            }catch(failure:Exception){
                if(failure is ScopedBackendWork.Superseded||expiredServerSession(failure))throw failure
                scope.finish{
                    if(hint!=groupAuthorityHint)return@finish
                    try{groupExecution.invalidate(native,account,revision)}catch(_:Exception){}
                    mutable.value=mutable.value.copy(groups=mutable.value.groups.map{it.copy(ready=false)},groupInvitations=emptyList(),groupDiagnostics="Group snapshot unavailable; authorization invalidated (${failure.javaClass.simpleName})")
                }
            }
        }
    }
    private fun pumpGroupVoice(now:Long){
        groupVoiceRooms.reconcileAuthorized(mutable.value.groups)
        groupVoiceRooms.dueSnapshotGroup(groupVoiceBinding?.group)?.let{group->
            groupVoiceRooms.request(group,"snapshot"){events?.queueGroup(it)==true}
        }
        val visibleRooms=groupVoiceRooms.snapshots()
        if(mutable.value.voiceRooms!=visibleRooms)mutable.value=mutable.value.copy(voiceRooms=visibleRooms)
        val binding=groupVoiceBinding?:return
        try {
            val state=checkNotNull(groupVoiceRooms.snapshot(binding.group));check(state.epoch==binding.epoch)
            val room=checkNotNull(state.rooms.singleOrNull{it.id==binding.room});validateVoiceRoom(state,room)
            check(room.members.any{it.account==mutable.value.accountId&&it.device==mutable.value.localPeerId&&it.participation==binding.participation})
            val handle=checkNotNull(profile).handle;val account=mutable.value.accountId
            val sources=room.members.filter{it.device!=mutable.value.localPeerId}.map{GroupNativeSource(it.account,it.device,it.participation)}
            check(groupVoiceExecution.reconcile(handle,account,generation,binding,state.revision,state.roster,sources).accepted)
            groupVoiceRooms.take(binding.group,binding.room,account,mutable.value.localPeerId).forEach{s->
                groupVoiceExecution.signal(handle,account,generation,binding,GroupNativeSignal(
                    "${s.getLong("sourceAccountId")}:${s.getString("sourceDevice")}:${s.getString("sourceGeneration")}",
                    s.getString("routeId"),s.getLong("mediaGeneration"),s.getString("kind"),s.getString("data"),s.getString("mid")))
            }
            val output=groupVoiceExecution.takeSignals(handle,account,generation,binding)
            check(output.accepted&&groupVoiceSignals.size+output.signals.size<=64)
            output.signals.forEach{groupVoiceSignals+=PendingVoiceSignal(it,now+10_000)}
            val iterator=groupVoiceSignals.iterator()
            while(iterator.hasNext()){
                val pending=iterator.next();val status=pending.request?.let(groupVoiceRooms::result)
                if(now>=pending.expires||status=="forwarded"||status!=null&&status !in setOf("pending","stale_room_state")){
                    iterator.remove();continue
                }
                if(status=="pending")continue
                if(pending.attempts>=4){iterator.remove();continue}
                val s=pending.signal
                pending.request=groupVoiceRooms.send(binding.group,binding.room,account,mutable.value.localPeerId,
                    s.sourceKey,s.routeId,s.generation,s.kind,s.data,s.mid){events?.queueGroup(it)==true}
                pending.attempts++
            }
        }catch(_:Exception){stopGroupVoice("Group authorization or media changed; join again to retry");publishVoice()}
    }
    private fun refreshVoiceCredentials(now:Long){
        val owned=voiceOwnedCall?:return
        val target=mutable.value.voice.target?:return
        if(target!=owned.second||voiceCredentialRefresh!=null||now<voiceCredentialRetryAt||
            (voiceCredentials?.expiresUtc?:0)>System.currentTimeMillis()/1000+120)return
        val owner=accounts?:return
        val scope=backendWorkScope{voiceOwnedCall==owned&&mutable.value.voice.target==target}?:return
        val request=Any();voiceCredentialRefresh=request;voiceCredentialRetryAt=now+30_000
        metadataWork.launch {
            try{
                val credentials=scope.network{owner.mediaTurnCredentials()}
                scope.finish{
                    val handle=checkNotNull(profile).handle
                    val accepted=if(target.group.isEmpty())voiceExecution.configureIce(handle,mutable.value.accountId,generation,
                        credentials.host,credentials.port,credentials.username,credentials.password,credentials.expiresUtc,++voiceCredentialGeneration).accepted
                    else groupVoiceExecution.configure(handle,mutable.value.accountId,generation,checkNotNull(groupVoiceBinding),credentials,++voiceCredentialGeneration).accepted
                    if(accepted)voiceCredentials=credentials
                }
            }catch(_:Exception){/* Existing media remains live; expired credentials cannot authorize a new route. */}
            finally{synchronized(gate){if(voiceCredentialRefresh===request)voiceCredentialRefresh=null}}
        }
    }
    // Called under gate; the existing owner invokes the returned bounded script
    // outside it. Reset replaces the script instance instead of racing its state.
    private fun borrowMailboxRequest():(()->Unit)? {
        if(!mutable.value.authenticated||System.nanoTime()/1_000_000<mailboxAt)return null
        val owner=checkNotNull(accounts);val native=checkNotNull(profile).handle;val allowed=mailboxAllowed()
        configureMailboxEnrollment(owner,native)
        val execution=mailboxExecution
        val scope=backendWorkScope{mailboxExecution===execution}?:return null
        // Consume this hint, preserving any new Send/event hint arriving during
        // transport wait: completion must not overwrite a concurrent zero.
        mailboxAt=Long.MAX_VALUE;mailboxInFlight=execution
        return {
            try {
                ownerTimings.measure(OwnerStage.Mailbox){measured("mailbox_service") {
                    execution.serviceScoped(scope,owner::mailbox,native,allowed,committed={ messageId,sender ->
                        val friend=friends.firstOrNull{sender in it.knownPeerIds}
                        if(friend!=null)historyProjection.invalidateUrgent(friend)
                        else {historyProjection.invalidate();contactsDirty=true}
                        // Publish at the durable-import boundary. A later ACK,
                        // receipt lookup or outgoing failure cannot hide this row.
                        mailboxPublication.committed(messageId)
                        android.util.Log.i("PeerChatWake","stage=mailbox_native_history_published")
                    }) { inserted ->
                        if(inserted)android.util.Log.i("PeerChatWake","stage=mailbox_native_history_received_server_acknowledged")
                        // The publication above happens before this completion
                        // marker can release a background wake.
                        mailboxPublication.acknowledged()
                    }
                }}
                scope.local{
                    mutable.value=mutable.value.copy(mailboxRecoveryError=null)
                    publishMailboxChanges(execution)
                    mailboxPublication.servicePublished()
                }
            } catch(failure:Exception) {
                scope.finish{
                    if(execution.projectionChanged)try{publishMailboxChanges(execution)}catch(projectionFailure:Exception){
                        projectionCadence.dirty()
                        android.util.Log.i("PeerChatWake","stage=mailbox_receipt_projection_pending result="+projectionFailure.javaClass.simpleName)
                    }
                    if(expiredServerSession(failure))handleTickFailure(failure)
                    else {
                        if(execution.diagnostic.contains("stage=mailbox_register"))mutable.value=mutable.value.copy(
                            mailboxRecoveryError="The server did not accept this mailbox enrollment. Existing keys, the original binding and queued messages are preserved. Return the diagnostics; do not reset or retry with a different key.")
                        // No plaintext/error message. An unavailable/rejected mailbox
                        // does not manufacture a peer route or invalidate local history.
                        android.util.Log.i("PeerChatWake","stage=mailbox_service_pending ${execution.diagnostic}")
                    }
                }
            } finally {
                scope.finish{
                    try{projectMailboxRecovery(native)}catch(_:Exception){mutable.value=mutable.value.copy(mailboxReenrollment=null,mailboxRecoveryError="Mailbox recovery validation is unavailable. Preserve app data and return the diagnostics.")}
                    val next=System.nanoTime()/1_000_000+if(execution.needsContinuation)1000 else if(events?.connected==true)900000 else 120000
                    mailboxAt=minOf(mailboxAt,next)
                    if(execution.nextRetryAt>0)mailboxAt=minOf(mailboxAt,execution.nextRetryAt)
                }
                synchronized(gate){if(mailboxInFlight===execution)mailboxInFlight=null}
            }
        }
    }
    private fun tickAfterMailbox():(()->Unit)? {
        val now=System.nanoTime()/1_000_000;val native=checkNotNull(profile).handle
        if(mutable.value.authenticated&&authorizationSnapshotInstalled&&now>=eligibilityAt&&!eligibilityLane.busy()) {
            eligibilityAt=now+1000
            // Receiving/publication above takes priority. A finite background
            // wake refreshes only actual pending send destinations, not contacts.
            val recipients=if(livePeerWorkAllowed)friends.map{it.accountId} else try {
                val rows=NativeRecord(checkNotNull(com.peerchat.android.MailboxBridge.pending(native)),4)
                val count=rows.number();require(count<=64)
                List(count.toInt()){rows.text();rows.text();rows.number()}.also{rows.end()}
            }catch(_:Exception){emptyList()}
            val owner=checkNotNull(accounts);val session=checkNotNull(owner.session);val opened=checkNotNull(profile)
            val account=mutable.value.accountId;val revision=generation;val device=mutable.value.localPeerId
            val scope=backendWorkScope()?:return null
            if(eligibilityOwner?.matches(opened,owner,session,account,revision,lifetimeRevision)!=true){
                eligibilityOwner=ScopedBackendWork.Identity(opened,owner,session,account,revision,lifetimeRevision)
                eligibilitySnapshots=EligibilitySnapshotExecution(install={handle,wire->scope.local{MailboxBridge.eligibility(handle,wire)}})
            }
            val execution=eligibilitySnapshots
            launchBackendWork(eligibilityLane){current->
                val changed=ownerTimings.measure(OwnerStage.Eligibility){execution.refreshWith(native,account,revision,device,recipients,System.nanoTime()/1_000_000){recipient->
                    current.network{owner.mailbox("mailbox_eligibility_snapshot",org.json.JSONObject().put("recipientAccountId",recipient.toString()))}
                }}
                if(changed)current.finish{mailboxAt=0L}
            }
        }
        if(now>=pushAt&&!pushLane.busy()){
            pushAt=now+60000
            val owner=accounts
            launchBackendWork(pushLane){scope->try{
                val token=pushRegistration.token()
                if(token!=null&&scope.local{token!=pushRegisteredToken}){
                    scope.network{owner?.registerPush(token)};scope.finish{pushRegisteredToken=token}
                }
            }catch(failure:Exception){if(failure is ScopedBackendWork.Superseded||expiredServerSession(failure))throw failure}}
        }
        if(!livePeerWorkAllowed&&!archiveWorkAllowed&&mailboxAllowed()){publishIfDue();return null}
        val archive=if(archiveLane.busy())null else borrowPeriodicArchive(now)
        // Existing peer controls and local projection precede borrowed history RPC.
        ownerTimings.measure(OwnerStage.NativeRelay){servicePeerExecutions(native,System.nanoTime()/1_000_000)}
        publishIfDue()
        return archive
    }
    private fun borrowPeriodicArchive(now:Long):(()->Unit)? {
        val initialArchive=archiveLoadedGeneration!=generation
        if(archiveWorkAllowed&&now>=archiveRetryAt&&(initialArchive||mutable.value.archiveProgress.running||now<archiveServingUntil||now>=archiveMetadataAt)){
            val owner=checkNotNull(accounts);val active=checkNotNull(owner.session);val local=mutable.value.localPeerId;val revision=generation
            val opened=checkNotNull(profile);val scope=backendWorkScope{archiveWorkAllowed}?:return null
            val refresh=initialArchive||now>=archiveMetadataAt
            val execution=ArchiveEnrollmentExecution(
                {op,bytes->scope.local{checkNotNull(com.peerchat.android.ArchiveBridge.request(opened.handle,op,bytes)){"Native archive operation rejected"}}},
                {op,body->scope.network{owner.archive(op,body)}})
            return {
            try{ownerTimings.measure(OwnerStage.Archive){if(refresh){
                    if(initialArchive)execution.publishPending(active.accountId,revision,local) else execution.refreshCurrent(active.accountId,revision,local)
                    // Stay below the native 30-second authorization expiry.
                    scope.local{archiveMetadataAt=System.nanoTime()/1_000_000+if(mutable.value.archiveProgress.running)10000 else 20000}
                }
                val progress=execution.progress(active.accountId,revision)
                scope.local{
                mutable.value=mutable.value.copy(archiveProgress=progress)
                if(mutable.value.archiveProgress.running)archiveMetadataAt=minOf(archiveMetadataAt,System.nanoTime()/1_000_000+10000)
                archiveLoadedGeneration=revision
                archiveRetryAt=0L
                }
            }}catch(failure:Exception){scope.finish{
                if(expiredServerSession(failure))handleTickFailure(failure)
                else {mutable.value=mutable.value.copy(archiveProgress=mutable.value.archiveProgress.copy(projectionFresh=false));archiveRetryAt=System.nanoTime()/1_000_000+60000}
            }}finally{
                val admission=try{execution.authorizationDiagnostics(active.accountId,revision)}catch(error:Exception){"Admission snapshot unavailable: ${error.javaClass.simpleName}"}
                scope.finish{mutable.value=mutable.value.copy(archiveAuthorizationDiagnostics=admission)}
            }
            }
        }
        return null
    }
    private fun servicePeerExecutions(native:Long,now:Long){
        val iterator=executions.iterator()
        val checkGroupCompletion=now>=groupCompletionAt
        if(checkGroupCompletion)groupCompletionAt=now+500
        while(iterator.hasNext()) {
            val (attempt,execution)=iterator.next()
            if(execution.finished){
                // Retain successful retirement as well as failure: no invented
                // sessions, only the last sixteen observed attempt snapshots.
                relayFailureDiagnostics[attempt]=execution.diagnostics()
                while(relayFailureDiagnostics.size>16)relayFailureDiagnostics.remove(relayFailureDiagnostics.keys.first())
                groupCompletionObserved.remove(attempt);iterator.remove();projectionCadence.dirty();continue}
            if(!execution.attached)continue // rendezvous is not yet a native TLS execution
            val status=ReliableBridge.exchange(native,attempt,ReliableBridge.STATUS,ByteArray(0))
            if(status==null||status[1].toInt()!=0){execution.close();iterator.remove();projectionCadence.dirty();continue}
            if(checkGroupCompletion&&execution.groupScope!=null){
                try{
                    if(groupExecution.controlSettled(native,mutable.value.accountId,generation,attempt)){
                        val observed=groupCompletionObserved.getOrPut(attempt){System.nanoTime()}
                        // Writer publishes this timestamp only after prior bytes
                        // completed their synchronous authenticated socket write.
                        if(execution.lastOutputIdleNanos>observed){
                            val finished=groupExecution.finishControl(native,mutable.value.accountId,generation,attempt);groupCompletionObserved.remove(attempt)
                            if(finished){execution.close();iterator.remove();continue}
                        }
                    }else groupCompletionObserved.remove(attempt)
                }catch(_:Exception){groupCompletionObserved.remove(attempt)}
            }
            drainNativeEvents({ReliableBridge.exchange(native,attempt,ReliableBridge.EVENT,ByteArray(0))}) { event ->
                val record=NativeRecord(event,3);val kind=record.number();record.text();record.text();record.number()
                if(kind in 0L..3L)projectionCadence.dirty()
                if(kind==1L){execution.observedAuthenticatedPeer();android.util.Log.i("PeerChatWake","stage=peer_session_ready")}
                if(kind==2L){
                    lastNativeReceiveNanos=System.nanoTime();lastNativeReceiveAttempt=attempt
                    android.util.Log.i("PeerChatWake","stage=native_history_received_ack_queued")
                }
                if(kind==2L||kind==3L)friends.filter{execution.requestPeerId in it.knownPeerIds}.forEach{historyProjection.invalidate(it)}
                if(kind==3L)android.util.Log.i("PeerChatWake","stage=delivery_ack_received")
                if(kind==0L) {
                    val accepted=ReliableBridge.exchange(native,attempt,ReliableBridge.INITIAL_AUTHENTICATED_FRIEND_PIN,ByteArray(0))
                    val outcome=initialFriendPinResult(accepted)
                    val detail="peer=${execution.requestPeerId} attempt=$attempt authentication_generation=$generation initial_pin=$outcome"
                    peerAuthenticationDiagnostics[execution.requestPeerId]=detail
                    while(peerAuthenticationDiagnostics.size>32)peerAuthenticationDiagnostics.remove(peerAuthenticationDiagnostics.keys.first())
                    android.util.Log.i("PeerChatWake",detail)
                    if(outcome=="existing_verified_account_device_requires_explicit_verification"){
                        val challenge=ReliableBridge.exchange(native,attempt,ReliableBridge.DEVICE_VERIFICATION,ByteArray(0))
                        val friend=friends.singleOrNull{it.peerId==execution.requestPeerId}
                        if(challenge!=null&&friend!=null){
                            val value=deviceVerification(challenge,friend.username)
                            if(value.localAccount==mutable.value.accountId&&value.generation==generation&&value.account==friend.accountId&&value.presented==friend.peerId&&
                                value.presented !in declinedDeviceVerifications&&(pendingDeviceVerifications.containsKey(value.presented)||pendingDeviceVerifications.size<32))pendingDeviceVerifications[value.presented]=value
                        }
                        mutable.value=mutable.value.copy(status="Server online; device verification required for messaging")
                    }else if(accepted?.firstOrNull()?.toInt()!=1)mutable.value=mutable.value.copy(status="Peer authentication rejected ($outcome); server presence does not establish messaging")
                }
            }
        }
        for(request in relayExecutions(checkNotNull(ReliableBridge.requests(native)))) {
            // A bounded archive wake must never broaden into a friend reconnect
            // or group transfer. Existing explicit transfer leases retain those.
            if(!livePeerWorkAllowed&&(request.archive==null||!archiveWorkAllowed)){
                ReliableBridge.close(native,request.attempt,false);continue
            }
            val friend=friends.find {it.peerId==request.peerId}
            val archive=request.archive;val session=accounts?.session
            val group=request.group
            val admitted=if(group!=null)session!=null&&group.account==session.accountId&&group.authenticationGeneration==generation&&mutable.value.groups.any{it.id==group.group&&it.epoch==group.epoch}
                else if(archive==null)friend!=null else session!=null&&archive.account==session.accountId&&
                ((archive.reader==mutable.value.localPeerId&&archive.source==request.peerId)||(archive.source==mutable.value.localPeerId&&archive.reader==request.peerId))
            if(!admitted||executions.size>=32){ReliableBridge.close(native,request.attempt,admitted);continue}
            val owner=checkNotNull(accounts);val ownerSession=owner.session;val revision=generation
            val execution=AndroidRelayExecution(native,request,owner,group?.targetAccount?:archive?.account?:checkNotNull(friend).accountId){source,failure->
                if(failure!=null&&expiredServerSession(failure)&&!stopped)try{executor.execute{synchronized(gate){
                    if(executions[request.attempt]===source&&accounts===owner&&owner.session===ownerSession&&generation==revision){
                        if(!inUse)retirePassiveConnections() else handleTickFailure(failure)
                    }
                }}}catch(_:java.util.concurrent.RejectedExecutionException){}
            }
            executions[request.attempt]=execution;execution.start();projectionCadence.dirty()
        }
    }
    /** Event-driven passive work only: no reconnect, projections timer or new
     * relay attempts. Authority changes retire retention before more work. */
    private fun eventActivity(source:ForegroundEvents){
        if(!passiveEventDrainAllowed(stopped,inUse,events===source,retentionUntil,System.nanoTime())||!passiveEventQueued.compareAndSet(false,true))return
        try{executor.execute{
            try{synchronized(gate){
                if(inUse||stopped||events!==source||retentionUntil<=System.nanoTime())return@synchronized
                if(source.finished){retirePassiveConnections();return@synchronized}
                repeat(128){
                    val event=source.poll()?:return@repeat
                    val kind=event.optString("event")
                    if(event.optString("type")=="friend_event"&&kind !in setOf("mailbox_ready","mailbox_receipt_ready","presence_changed","relay_demand")){
                        retirePassiveConnections();return@synchronized
                    }
                    // No new peer/archive work is admitted while passive. Group
                    // ciphertext already delivered is still validated and saved
                    // by the existing owner, rather than discarded as a hint.
                    handleServerEvent(event)
                }
            }}catch(_:Exception){synchronized(gate){if(!inUse&&events===source)retirePassiveConnections()}}
            finally{passiveEventQueued.set(false);if(events===source&&source.hasPending())eventActivity(source)}
        }}catch(_:java.util.concurrent.RejectedExecutionException){passiveEventQueued.set(false)}
    }
    private fun retirePassiveConnections(){
        generation++;clearAuthorization()
        accounts?.closeRpc();accounts=null;index=null
        mutable.value=localOfflineSnapshot(mutable.value,"Connection changed while backgrounded; local history retained")
    }
    private fun handleServerEvent(event:org.json.JSONObject){
        if(event.optString("type") in setOf("group_room_snapshot","group_room_result","group_media_signal","group_media_result")){
            try{
                groupVoiceRooms.event(event,mutable.value.accountId,mutable.value.localPeerId)
                mutable.value=mutable.value.copy(voiceRooms=groupVoiceRooms.snapshots())
                pumpGroupVoice(System.nanoTime()/1_000_000)
            }catch(_:Exception){stopGroupVoice("Group room state rejected; join again after refresh");groupVoiceRooms.reset();groupVoiceSnapshotAt=0L}
            return
        }
        if(event.optString("type") in setOf("group_message","group_delivery")){
            try{groupExecution.event(event,checkNotNull(profile).handle,mutable.value.accountId,generation){events?.queueGroup(it)==true};groupProjectionAt=0L
                if(event.optString("type")=="group_message"){
                    mutable.value=mutable.value.copy(groups=groupExecution.snapshot(checkNotNull(profile).handle,mutable.value.accountId,generation))
                    localEndpoint?.let{groupNotifications.publish(it,mutable.value)}
                }}
            catch(_:Exception){mutable.value=mutable.value.copy(groupDiagnostics="Group event rejected by native admission or bounded projection")}
            return
        }
        when(val kind=event.optString("event")){
            "device_management_changed"->{
                if(ownDeviceProjection.hint(event.optLong("accountId"),event.optString("serverEpoch"),event.optLong("authorityRevision")))ownDeviceAt=0L
                // Reconcile existing group/archive permission owners as well as
                // metadata. An event itself never changes a grant or receipt.
                if(event.optLong("accountId")==mutable.value.accountId&&event.optString("serverEpoch")==accounts?.presenceEpoch){
                    contactAuthorityHint++;groupAuthorityHint++;groupRefreshAt=0L;archiveMetadataAt=0L;contactsDirty=true
                }
            }
            "group_roster_changed"->{
                groupAuthorityHint++;groupRefreshAt=0L
                // A hint revokes stale readiness, never grants a new roster/key.
                // Existing native invalidation retains all protected history.
                stopGroupVoice("Group membership changed; refreshing authorization")
                groupVoiceRooms.reset()
                groupExecution.invalidate(checkNotNull(profile).handle,mutable.value.accountId,generation)
                mutable.value=mutable.value.copy(groups=mutable.value.groups.map{it.copy(ready=false)},voiceRooms=emptyList())
            }
            "group_invite_changed"->{groupAuthorityHint++;groupRefreshAt=0L}
            "mailbox_ready","mailbox_receipt_ready"->{mailboxAt=0L;android.util.Log.i("PeerChatNetwork","mailbox_trigger=$kind")}
            "friendship_changed"->{contactAuthorityHint++;contactsDirty=true}
            "presence_changed","friend_request","friend_request_updated"->{contactsDirty=true}
            "relay_demand"->{
                if(event.optString("purpose")=="group_key"){
                    if(!livePeerWorkAllowed||event.optString("targetPublicIdentity")!=mutable.value.localPeerId)return
                    try{groupExecution.incomingControl(event,checkNotNull(profile).handle,mutable.value.accountId,generation)}
                    catch(_:Exception){groupRefreshAt=0L}
                    return
                }
                if(event.optString("purpose")=="archive_history"){
                    if(!archiveWorkAllowed||event.optString("targetPublicIdentity")!=mutable.value.localPeerId)return
                    try {
                        val scope=ArchiveRelayScope.event(event);val owner=checkNotNull(accounts);val session=checkNotNull(owner.session)
                        // Source attachment echoes reach the reader on this same
                        // account. They are not new source work or auth failure.
                        if(!incomingArchiveSource(scope,session.accountId,mutable.value.localPeerId,event.optString("publicIdentity"))){
                            android.util.Log.i("PeerChatArchive","event=demand_ignored reason=not_source generation=$generation");return
                        }
                        val execution=archiveExecution(owner,checkNotNull(profile));execution.refreshCurrent(session.accountId,generation,mutable.value.localPeerId);execution.incoming(session.accountId,generation,scope)
                        val now=System.nanoTime()/1_000_000;archiveServingUntil=now+60000;archiveMetadataAt=now+10000
                        android.util.Log.i("PeerChatArchive","event=source_demand_admitted generation=$generation")
                    }catch(failure:Exception){
                        if(expiredServerSession(failure))throw failure
                        android.util.Log.w("PeerChatArchive","event=demand_failed generation=$generation result=${failure.javaClass.simpleName} cleanup=archive_only")
                        mutable.value=mutable.value.copy(archiveProgress=mutable.value.archiveProgress.copy(state="Archive source unavailable; retry pending",complete=false,projectionFresh=false))
                    }
                    return
                }
                if(!livePeerWorkAllowed||event.optString("purpose")!="friend"||event.optString("targetPublicIdentity")!=mutable.value.localPeerId)return
                val peer=event.optString("publicIdentity")
                val friend=friends.singleOrNull{it.accountId==event.optLong("accountId")&&it.peerId==peer} ?: run{contactsDirty=true;return}
                check(ReliableBridge.demand(checkNotNull(profile).handle,friend.peerId.toByteArray(Charsets.US_ASCII))==0)
                android.util.Log.i("PeerChatNetwork","route_demand=authorized_incoming")
            }
        }
    }
    override suspend fun verifyDevice(value:DeviceVerification,accept:Boolean)=withContext(Dispatchers.IO){synchronized(gate){
        val current=pendingDeviceVerifications[value.presented]
        check(authorizationSnapshotInstalled&&value.localAccount==mutable.value.accountId&&value.generation==generation&&
            current!=null&&current.account==value.account&&current.localAccount==value.localAccount&&current.generation==value.generation){"Verification session changed"}
        val native=checkNotNull(profile).handle
        if(!accept){
            // Rejection never grants trust and may dismiss a matching fingerprint
            // even if its old TLS attempt expired while the user was comparing.
            declinedDeviceVerifications.add(value.presented);pendingDeviceVerifications.remove(value.presented)
            ReliableBridge.exchange(native,current.attempt,ReliableBridge.VERIFY_PRESENTED_DEVICE,verificationDecision(current,false))
            executions.remove(current.attempt)?.close()
            mutable.value=mutable.value.copy(deviceVerifications=pendingDeviceVerifications.values.toList(),status="Device remains untrusted; server presence does not establish messaging");return@synchronized
        }
        check(value.live&&current==value){"Verification presentation changed"}
        val result=ReliableBridge.exchange(native,value.attempt,ReliableBridge.VERIFY_PRESENTED_DEVICE,verificationDecision(value,true))
        pendingDeviceVerifications.remove(value.presented)
        mutable.value=mutable.value.copy(deviceVerifications=pendingDeviceVerifications.values.toList())
        check(result?.contentEquals(byteArrayOf(1))==true){"Identity presentation expired; reconnect and compare again"}
        mutable.value=mutable.value.copy(status="Device explicitly verified; delivery awaits native acknowledgment")
        mailboxAt=0L;check(ReliableBridge.demand(native,value.presented.toByteArray(Charsets.US_ASCII))==0)
    }}
    private fun publishIfDue(){if(projectionCadence.due(System.nanoTime()/1_000_000))publish()}
    private fun publishMailboxChanges(execution:MailboxExecution){
        if(!execution.projectionChanged)return
        if(!historyProjection.invalidateMessages(execution.changedMessageIds))historyProjection.invalidate()
        publishMessageProjection()
    }
    /** Commit-to-ACK publication must not enumerate transfers, all connections,
     * or diagnostic records. The same native history page remains authoritative. */
    private fun publishMessageProjection(){
        val native=checkNotNull(profile).handle
        ownerTimings.measure(OwnerStage.LocalPage){historyProjection.refreshOne(friends.sortedBy{it.accountId!=preferredLocalAccount},System.nanoTime()/1_000_000){friend->
            accountHistoryPage(friend,Long.MAX_VALUE){peer,cursor->checkNotNull(ReliableBridge.history(native,peer.toByteArray(Charsets.US_ASCII),cursor))}
        }}
        mutable.value=mutable.value.copy(conversations=mutable.value.conversations.map{c->c.copy(messages=historyProjection.rows(c.friend))},freshMailboxMessageIds=mailboxPublication.freshIds())
        projectionCadence.dirty()
    }
    private fun publish(includeDiagnostics:Boolean=true){
        ownerTimings.measure(OwnerStage.Projection){publishLocked(includeDiagnostics)}
        val now=System.nanoTime()/1_000_000
        projectionCadence.published(now,executions.isNotEmpty())
        if(historyProjection.hasPending(friends))projectionCadence.remainingDirty(now)
    }
    private fun publishLocked(includeDiagnostics:Boolean) {
        val diagnosticNow=android.os.SystemClock.elapsedRealtime()
        // This diagnostic enumerates/decrypts retained delivery targets. It is
        // not a UI delivery-state source and must not run inside every durable
        // import/ACK publication or compete with active voice and transfers.
        if(includeDiagnostics&&diagnosticNow>=nativeDiagnosticAt&&!mailboxLane.busy()&&!mutable.value.voice.active&&
            attachmentProjection.none{it.active}){
            nativeDiagnosticAt=diagnosticNow+60000
            nativeDiagnostic=profile?.let{com.peerchat.android.MailboxBridge.registrationDiagnostic(it.handle)?.toString(Charsets.US_ASCII)}?:"native_registration_stage=unavailable"
            nativeDiagnosticSampleAt=diagnosticNow
        }
        pendingDeviceVerifications.entries.removeAll{it.value.generation!=generation}
        pendingDeviceVerifications.replaceAll{_,value->value.copy(live=value.attempt in executions&&executions[value.attempt]?.finished==false)}
        mutable.value=mutable.value.copy(deviceVerifications=pendingDeviceVerifications.values.toList())
        mutable.value=mutable.value.copy(messagingDiagnostics=buildString {
            appendLine("mailbox_storage_allowed=${mailboxAllowed()} ${mailboxExecution.diagnostic}")
            appendLine(mailboxExecution.inboundDiagnostic)
            appendLine(eligibilitySnapshots.diagnostic)
            appendLine(ownerTimings.summary())
            listOf("contacts" to contactLane,"groups" to groupLane,"eligibility" to eligibilityLane,
                "mailbox" to mailboxLane,"cache" to cacheLane,"archive" to archiveLane).forEach{(name,lane)->appendLine("backend_lane=$name ${lane.diagnostic()}")}
            appendLine("event_session_connected=${events?.connected==true} retry_at_monotonic_ms=$eventRetryAt retry_delay_ms=$eventRetryDelay next_mailbox_monotonic_ms=$mailboxAt next_archive_monotonic_ms=$archiveRetryAt")
            appendLine(accounts?.diagnostics() ?: "network_history=not_retained_for_closed_session")
            appendLine("own_device_inventory=$ownDeviceStatus revision=${mutable.value.ownDevices?.revision?:0} devices=${mutable.value.ownDevices?.devices?.size?:0}")
            appendLine(profile?.recipientDiagnostics() ?: "stage=no_local_profile")
            appendLine(profile?.localDiagnostics() ?: "local_storage=unavailable")
            appendLine("native_diagnostic_sample_monotonic_ms=$nativeDiagnosticSampleAt age_ms=${if(nativeDiagnosticSampleAt==0L)-1 else diagnosticNow-nativeDiagnosticSampleAt} semantics=retained_idle_sample_not_live_authority")
            appendLine(nativeDiagnostic)
            peerAuthenticationDiagnostics.values.forEach{appendLine(it)}
            relayFailureDiagnostics.values.forEach{appendLine(it)}
            executions.values.take(32).forEach{appendLine(it.diagnostics())}
        })
        val native=checkNotNull(profile).handle
        if(projectedHistoryRevision!=mutable.value.archiveProgress.historyRevision){historyProjection.invalidate();projectedHistoryRevision=mutable.value.archiveProgress.historyRevision}
        ownerTimings.measure(OwnerStage.LocalPage){historyProjection.refreshOne(friends.sortedBy{it.accountId!=preferredLocalAccount},System.nanoTime()/1_000_000){friend->
            accountHistoryPage(friend,Long.MAX_VALUE){peer,cursor->checkNotNull(ReliableBridge.history(native,peer.toByteArray(Charsets.US_ASCII),cursor))}}}
        val conversations=friends.map{friend->ConversationSnapshot(friend,
            connectionSnapshot(checkNotNull(ReliableBridge.state(native,friend.peerId.toByteArray(Charsets.US_ASCII)))),historyProjection.rows(friend))}
        // Failed offers may never create an attachment row. Keep their bounded
        // native failure projection visible without polling an idle profile fast.
        if(diagnosticNow>=transferDiagnosticsAt){
            transferDiagnosticsAt=diagnosticNow+if(attachmentProjection.isNotEmpty())1000 else 10000
            val record=NativeRecord(transfer(5),30)
            val measured=mutableListOf<String>()
            val currentIds=mutableSetOf<String>()
            repeat(record.number().toInt().coerceIn(0,64)){
                val row=record.text();val id=row.substringBefore(' ');currentIds.add(id)
                measured.add(row)
                val signature=row.substringBefore(" elapsed_ms=");val previous=transferDiagnosticStates[id]
                val active=!row.contains(" state=4 ")&&!row.contains(" state=5 ")&&!row.contains(" state=6 ")&&!row.contains(" state=8 ")
                if(previous?.first!=signature||(active&&diagnosticNow-(previous?.second?:0L)>=10000)){
                    android.util.Log.i("PeerChatTransfer",row+" projection_observed_ms="+diagnosticNow)
                    transferDiagnosticStates[id]=signature to diagnosticNow
                }
            }
            record.end()
            mutable.value=mutable.value.copy(transferDiagnostics=transferDiagnosticReport(measured))
            transferDiagnosticStates.keys.retainAll(currentIds)
        }
        // Read failure diagnostics first: attachment setup itself can fail.
        val projectionNow=android.os.SystemClock.elapsedRealtime()
        if(native!=attachmentProjectionHandle||generation!=attachmentProjectionGeneration||projectionNow>=attachmentProjectionAt){
            attachmentProjection=attachmentSnapshots(transfer(4));attachmentProjectionHandle=native;attachmentProjectionGeneration=generation;attachmentProjectionAt=projectionNow+1000
        }
        val attachments=attachmentProjection
        mutable.value=mutable.value.copy(conversations=conversations.map{c->
            val history=synchronized(attachmentHistoryGate){if(attachmentHistoryScope===localReadContext)attachmentDisplayHistory[c.friend.peerId]?.let{it.rows.values.toList() to it.more}else null}
            c.copy(attachments=((history?.first?:emptyList())+attachments.filter{it.peer in c.friend.knownPeerIds}).associateBy{it.id}.values.sortedBy{it.created},attachmentsHaveOlder=history?.second?:true)
        },mailboxStorageAllowed=mailboxAllowed(),freshMailboxMessageIds=mailboxPublication.freshIds())
    }
    override fun backgroundOutputDrained(afterNanos:Long):Boolean=synchronized(gate){
        if(mailboxInFlight===mailboxExecution)return@synchronized false
        // Mailbox publication may finish before the first archive pass. Keep the
        // same bounded job alive until a fresh native projection settles, or its
        // deadline releases the lease and the scheduler defers recovery.
        if(archiveWorkAllowed&&(archiveLoadedGeneration!=generation||archiveWakePending(mutable.value.archiveProgress)))return@synchronized false
        val receiver=executions[lastNativeReceiveAttempt]
        mailboxPublication.outputDrained(afterNanos,mailboxExecution.needsContinuation)||
            (receiver!=null&&nativeWakeOutputComplete(lastNativeReceiveNanos,afterNanos,receiver.attached,receiver.finished,receiver.lastOutputIdleNanos))
    }
    override fun close() {
        if(stopped)return;stopped=true
        metadataWork.cancel()
        operations.configure(false,Long.MAX_VALUE)
        // Do not block the UI waiting for an in-progress bounded server request.
        executor.execute {synchronized(gate){
            generation++;clearAuthorization();try {if(remembered.load()==null)accounts?.logout()}catch(_: Exception){}
            localReadContext=null;historyProjection.clear()
            accounts?.closeRpc();accounts=null;friends=emptyList();index=null;profile?.close();profile=null
        }}
        executor.shutdown()
    }
}
