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
import kotlinx.coroutines.withContext

/** Application coordinator/platform execution only. Native owns profile,
 * messages/outbox/order/ACK/dedupe, trust and session/retry state. Immutable UI
 * snapshots are read from those owners, not a Kotlin messaging engine. */
class MessagingBackend(context: Context,private val profileName: String="default") : MessagingApplication {
    private val context=context.applicationContext
    private val gate=Any()
    private val executor=Executors.newSingleThreadScheduledExecutor {task->Thread(task,"PeerChat application projection").apply {isDaemon=true}}
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
    private val localContexts=LocalConversationContextStore(this.context,profileName)
    private var localEndpoint:ServerEndpoint?=null
    private var generation=0L
    private var network=0L
    private var archiveMetadataAt=0L
    private var archiveRetryAt=0L
    private var archiveServingUntil=0L
    private var archiveLoadedGeneration=-1L
    private var refreshAt=0L
    private var events:ForegroundEvents?=null
    private var eventRetryAt=0L
    private var eventRetryDelay=1000L
    private var eventWasConnected=false
    private var contactsDirty=false
    private var authorizationSnapshotInstalled=false
    private var rememberedRetryEndpoint:ServerEndpoint?=null
    private var rememberedRetryAt=0L
    private var rememberedRetryDelay=1000L
    private var loggedTickFailure:String?=null
    private var lastNativeReceiveNanos=0L
    private var lastNativeReceiveAttempt=0L
    private val mailboxPublication=MailboxPublicationGate({publish()})
    private var mailboxAt=0L
    private val mailboxExecution=MailboxExecution()
    private val eligibilitySnapshots=EligibilitySnapshotExecution()
    private val groupExecution=GroupExecution()
    private val voiceExecution=VoiceExecution()
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
        mailboxExecution.reset();mailboxAt=0L;mutable.value=mutable.value.copy(mailboxStorageAllowed=allowed)
    }}
    private fun configureMailboxEnrollment(owner:ServerAccounts,native:Long){
        val session=checkNotNull(owner.session)
        val server=owner.endpoint.host.lowercase(java.util.Locale.ROOT)+":"+owner.endpoint.port
        check(MailboxBridge.configureEnrollment(native,session.accountId,generation,server.toByteArray(Charsets.US_ASCII),owner.endpoint.certificatePin.lowercase(java.util.Locale.ROOT).toByteArray(Charsets.US_ASCII))==0){"Authenticated mailbox scope is unavailable"}
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
        if(accept){mailboxExecution.reset();mailboxAt=0L}
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
    @Volatile private var quiescent=java.util.concurrent.CountDownLatch(0)
    internal fun setInUse(value:Boolean,deadlineNanos:Long=Long.MAX_VALUE,allowPeerWork:Boolean=deadlineNanos==Long.MAX_VALUE) {
        inUse=value
        operationDeadline=deadlineNanos
        // A finite wake lease services server mailbox only. It never starts a
        // friend reconnect cycle to obtain history or bootstrap sender trust.
        livePeerWorkAllowed=allowPeerWork
        operations.configure(value&&!resetting,deadlineNanos)
        if(!value){val completed=java.util.concurrent.CountDownLatch(1);quiescent=completed
            executor.execute {try{synchronized(gate){if(!inUse&&!stopped){
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
    init {
        executor.scheduleWithFixedDelay({
            if(!stopped&&!resetting)try {synchronized(gate){if(!resetting)tick()}}catch(failure: Exception){
                if(loggedTickFailure!=failure.javaClass.simpleName){
                    loggedTickFailure=failure.javaClass.simpleName
                    android.util.Log.i("PeerChatWake","stage=backend_tick_failure result="+loggedTickFailure)
                }
                if(expiredServerSession(failure))synchronized(gate){
                    android.util.Log.w("PeerChatNetwork","event=authentication_invalidated generation=$generation reason="+(failure as ServerRejected).code)
                    generation++;clearAuthorization();accounts=null;index=null
                    friends=friends.map{it.copy(online=false)}
                    mutable.value=localOfflineSnapshot(mutable.value,"Session expired; local history retained; sign in again")
                    // Expiring access sessions are not remembered credentials.
                    // One bounded refresh through the same coordinator; never
                    // fight a superseded session or bypass server revocation.
                    if(inUse&&failure is ServerRejected&&failure.code=="session_invalid")try{
                        val saved=remembered.load();if(saved!=null&&!saved.revocationPending)restoreRememberedLocked(saved.endpoint)
                    }catch(_:Exception){/* Retain fail-closed offline state; lifecycle/wake may retry. */}
                    return@scheduleWithFixedDelay
                }
                synchronized(gate){mutable.value=mutable.value.copy(serverPresenceLive=false,status=if(failure is ServerRejected)serverRefreshFailure(failure) else "Application/native update validation failed; native delivery state retained")}
            }
        },125,125,TimeUnit.MILLISECONDS)
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
            if(remember)require(endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))
            // Explicit login replaces a previous remembered account only after
            // durable revocation. Network failure leaves a non-login tombstone.
            revokeSaved(native,key)
            val connection=ServerAccounts(endpoint,device,operations)
            // The same identity, trust, presence/bootstrap and Remember Me path.
            // Server registration rejects an identity already owned by another account.
            val authenticated=if(creating)connection.register(username,password) else connection.login(username,password)
            if(mutable.value.accountId!=authenticated.accountId){groupExecution.reset();mutable.value=mutable.value.copy(groups=emptyList(),groupInvitations=emptyList())}
            try {
                val metadata=FriendIdentityIndex(context,profileName,endpoint,authenticated.accountId)
                accounts=connection;index=metadata;pushRegisteredToken=null;network++
                mutable.value=MessagingSnapshot(authenticated.accountId,authenticated.username,device,true,"Authenticated; refreshing contacts")
                // Password authentication does not register this device. Use the
                // established authenticated presence path before the combined
                // bootstrap, which correctly requires prior account ownership.
                // A device owned by another account still fails closed here.
                connection.presence()
                refresh();refreshAt=System.nanoTime()/1_000_000+900000
                if(remember){val value=connection.remember(key){checkNotNull(ProfileBridge.sign(native.handle,it))};
                    try{remembered.save(value)}catch(failure:Exception){try{connection.revoke(value,key){checkNotNull(ProfileBridge.sign(native.handle,it))}}catch(_:Exception){};throw failure}
                    OpaquePushWake.refreshPeriodic(context)
                    // Enrollment leaves the password-login bearer unbound. Push
                    // registration requires the signed device-bound resume token
                    // immediately, not only after the next process restart.
                    generation++;clearAuthorization();accounts=null;index=null;friends=emptyList()
                    connection.logout();check(restoreRememberedLocked(endpoint))
                }
                checkNotNull(accounts?.session)
            } catch(failure: Exception){
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
        if(mutable.value.localHistoryReady){friends=emptyList();mutable.value=MessagingSnapshot(localPeerId=mutable.value.localPeerId)}
        val cached=localContexts.load(endpoint) ?: remembered.load()?.takeIf{it.endpoint==endpoint}?.let{saved->
            // Upgrade the existing protected display index in place; no remote
            // connection or trust promotion is needed to view old local history.
            LocalConversationContext(saved.accountId,"Account ${saved.accountId}",saved.device,
                FriendIdentityIndex(context,profileName,endpoint,saved.accountId).localHistoryBindings())
        } ?: return false
        val native=profile ?: measured("profile_open"){NativeProfile(context,profileName)}.also{profile=it}
        val identity=measured("local_history_identity"){PeerIdentityFormat.peerId(checkNotNull(ProfileBridge.publicKey(native.handle)))}
        check(identity==cached.device)
        // Cached names/bindings project local Storage ONLY. Never install them
        // into ReliableBridge.friends or grant route/mailbox authorization.
        friends=cached.friends
        localEndpoint=endpoint
        mutable.value=MessagingSnapshot(cached.account,cached.username,identity,false,"Offline; local history available",
            localHistoryReady=true)
        measured("local_history_projection"){publish()}
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
        synchronized(gate) {
            check(!stopped&&friends.any {it.peerId==peerId}&&(accounts?.session!=null||mutable.value.localHistoryReady))
            val utf8=text.toByteArray(Charsets.UTF_8);require(utf8.size<=512*1024&&!text.contains('\u0000'))
            val result=if(text.length>4096){check(authorizationSnapshotInstalled);checkNotNull(com.peerchat.android.TransferBridge.offerText(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),utf8))}
                else checkNotNull(ReliableBridge.queueLocal(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),utf8))
            val id=result.toString(Charsets.US_ASCII);check(id.matches(Regex("[0-9a-f]{32}")))
            mailboxAt=0L
            publish()
            val friend=friends.first{it.peerId==peerId};val owner=accounts
            // Best effort after native durable queueing. Not another retry policy.
            executor.execute{try{owner?.hint(friend.accountId,friend.peerId)}catch(_:Exception){}}
            id
        }
    }
    override suspend fun older(peerId: String,before: Long): List<MessageSnapshot> = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped)
            val friend=friends.single{it.knownPeerIds.contains(peerId)}
            accountHistoryPage(friend,before){peer,cursor->
                checkNotNull(ReliableBridge.history(checkNotNull(profile).handle,peer.toByteArray(Charsets.US_ASCII),cursor))}
        }
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
    private val transferDiagnosticStates=mutableMapOf<String,Pair<String,Long>>()
    private fun transfer(operation:Int,write:(NativeRecordWriter)->Unit={}):ByteArray {
        val request=NativeRecordWriter(30).also(write).bytes()
        return checkNotNull(com.peerchat.android.TransferBridge.request(checkNotNull(profile).handle,operation,request)){"Native transfer operation rejected"}
    }
    override suspend fun sendAttachment(peerId:String,uri:android.net.Uri):String=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped&&authorizationSnapshotInstalled&&friends.any{it.peerId==peerId})
        val directoryRecord=NativeRecord(transfer(0),30);val directory=java.io.File(directoryRecord.text());directoryRecord.end()
        val (path,name)=importAttachment(context,uri,directory)
        // A failed native journal may already retain the source. Never delete it
        // on a ambiguous offer failure; cleanup cannot destroy a resumable copy.
        val response=NativeRecord(transfer(1){it.text(peerId);it.text(path);it.text(name)},30)
        val id=response.text();response.end();publish();id
    }}
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
        owner.request("friend_request",org.json.JSONObject().put("accountId",account.toString()))
        refresh();publish()
    }}
    override suspend fun respondFriend(accountId:Long,accept:Boolean) = withContext(Dispatchers.IO) {synchronized(gate){
        require(accountId>0&&mutable.value.incomingRequests.any {it.accountId==accountId})
        checkNotNull(accounts).request("friend_respond",org.json.JSONObject().put("accountId",accountId.toString()).put("action",if(accept)"accept" else "reject"))
        refresh();publish()
    }}
    private fun clearAuthorization() {
        pendingDeviceVerifications.clear();declinedDeviceVerifications.clear();relayFailureDiagnostics.clear()
        mutable.value=mutable.value.copy(deviceVerifications=emptyList(),mailboxReenrollment=null,mailboxRecoveryError=null)
        groupRefreshAt=0L;groupProjectionAt=0L;groupCompletionAt=0L;groupCompletionObserved.clear()
        peerAuthenticationDiagnostics.clear()
        authorizationSnapshotInstalled=false
        events?.close();events=null;eventWasConnected=false;eventRetryAt=0L;eventRetryDelay=1000L
        mailboxExecution.reset();mailboxPublication.reset();eligibilitySnapshots.reset();mailboxAt=0L
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
            val old=accounts;accounts=null;friends=emptyList();index=null
            pushRegisteredToken=null
            try {profile?.let{revokeSaved(it,checkNotNull(ProfileBridge.publicKey(it.handle)))};old?.logout()}
            finally {groupExecution.reset();mutable.value=MessagingSnapshot(localPeerId=mutable.value.localPeerId)}
            Unit
        }
    }
    private fun refresh(bootstrap:Boolean=true) {
        val accounts=checkNotNull(accounts);val active=checkNotNull(accounts.session)
        val state=if(bootstrap)accounts.bootstrap() else accounts.friends()
        val pending=state.getJSONArray("incoming");require(pending.length()<=128)
        val requests=List(pending.length()){i->val item=pending.getJSONObject(i)
            val id=item.getLong("accountId");val name=item.getString("username");require(id>0&&name.length<=64)
            AccountRequestSnapshot(id,name)
        }
        val list=state.getJSONArray("friends");require(list.length()<=128)
        val projected=List(list.length()) {i->val item=list.getJSONObject(i)
            FriendIdentity(item.getLong("accountId"),item.getString("username"),item.getString("publicIdentity"),item.getBoolean("online"))
        };require(projected.none {it.accountId==active.accountId})
        val authoritative=if(projected.map{it.copy(knownPeerIds=emptyList())}==friends.map{it.copy(knownPeerIds=emptyList())})friends else checkNotNull(index).reconcile(projected)
        android.util.Log.i("PeerChatWake","stage=friend_devices_restored contacts=${authoritative.size} devices=${authoritative.sumOf{it.knownPeerIds.size}}")
        val writer=NativeRecordWriter(1);writer.number(authoritative.size.toLong())
        authoritative.forEach {friend->writer.number(friend.accountId);writer.text(friend.peerId);writer.number(friend.knownPeerIds.size.toLong());friend.knownPeerIds.forEach {writer.text(it)}}
        val identityChanged=!authorizationSnapshotInstalled||authoritative.map{it.copy(online=false)}!=friends.map{it.copy(online=false)}
        if(identityChanged){generation++;groupRefreshAt=0L;groupProjectionAt=0L}
        val native=checkNotNull(profile).handle
        if(identityChanged)check(ReliableBridge.friends(native,active.accountId,generation,writer.bytes())==0)
        authorizationSnapshotInstalled=true
        check(ReliableBridge.network(native,generation,network)==0)
        val changed=authoritative!=friends
        friends=authoritative
        localEndpoint=accounts.endpoint
        if(changed||bootstrap)localContexts.save(accounts.endpoint,LocalConversationContext(active.accountId,active.username,mutable.value.localPeerId,authoritative))
        mutable.value=mutable.value.copy(incomingRequests=requests.toList())
        friends.forEach {friend->check(ReliableBridge.presence(native,friend.peerId.toByteArray(Charsets.US_ASCII),friend.online,generation,network)==0)}
        val presenceLive=!livePeerWorkAllowed||events?.connected==true
        mutable.value=mutable.value.copy(serverPresenceLive=presenceLive,localHistoryReady=true,status=if(presenceLive)"Authenticated server presence available; message delivery requires a verified peer or mailbox ACK" else "Connecting authenticated server events; local history available")
        // Complete application projection as part of backend bootstrap, not a
        // later UI collector/tick or optional vendor registration side effect.
        publish()
        android.util.Log.i("PeerChatWake","stage=conversations_projected contacts=${mutable.value.conversations.size}")
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
        if(livePeerWorkAllowed||!mailboxAllowed()){
            if(events==null&&now>=eventRetryAt){events=ForegroundEvents(checkNotNull(accounts));android.util.Log.i("PeerChatNetwork","event=connecting")}
            events?.let {stream->
                if(stream.connected&&!eventWasConnected){
                    eventWasConnected=true;eventRetryDelay=1000L;refreshAt=0L;mailboxAt=0L
                    groupPendingMore=true;groupPendingAt=0L
                    try{groupExecution.resetPending(native,mutable.value.accountId,generation)}catch(_:Exception){}
                    android.util.Log.i("PeerChatNetwork","event=connected repair=requested")
                }
                if(stream.finished){
                    val failure=stream.failure;stream.close();events=null;eventWasConnected=false
                    mutable.value=mutable.value.copy(serverPresenceLive=false,status="Reconnecting authenticated server events; local history available")
                    eventRetryAt=now+eventRetryDelay;eventRetryDelay=(eventRetryDelay*2).coerceAtMost(30000)
                    refreshAt=minOf(refreshAt,now+120000)
                    android.util.Log.i("PeerChatNetwork","event=disconnected repair=bounded")
                    if(failure!=null&&expiredServerSession(failure))throw failure
                }else {
                    try{stream.heartbeat(now)}catch(_:java.io.IOException){stream.close()}
                    repeat(32){stream.poll()?.let{event->handleServerEvent(event)}}
                }
            }
        }else {events?.close();events=null;eventWasConnected=false}
        // Server authorization and stored delivery precede NEW peer attempts.
        // Existing peer executions remain independent platform workers.
        if(now>=refreshAt||contactsDirty) {
            val repair=now>=refreshAt
            contactsDirty=false
            try {refresh(repair)}catch(failure:Exception){
                if(failure !is java.io.IOException)throw failure
                mutable.value=mutable.value.copy(serverPresenceLive=false,status=serverRefreshFailure(failure)+"; local history retained")
            }finally {refreshAt=System.nanoTime()/1_000_000+if(events?.connected==true)900000 else 120000}
        }
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&now>=groupRefreshAt){
            try{groupExecution.refresh(checkNotNull(accounts),native,mutable.value.accountId,generation);groupRefreshAt=now+60000;groupProjectionAt=0L;groupPendingMore=true}
            catch(failure:Exception){
                if(expiredServerSession(failure))throw failure
                try{groupExecution.invalidate(native,mutable.value.accountId,generation)}catch(_:Exception){}
                groupRefreshAt=now+60000;mutable.value=mutable.value.copy(groups=mutable.value.groups.map{it.copy(ready=false)},groupInvitations=emptyList(),groupDiagnostics="Group snapshot unavailable; authorization invalidated (${failure.javaClass.simpleName})")
            }
        }
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&now>=groupProjectionAt){
            groupProjectionAt=now+1000
            try{mutable.value=mutable.value.copy(voice=voiceExecution.snapshot(native,mutable.value.accountId,generation))}catch(_:Exception){mutable.value=mutable.value.copy(voice=VoiceSnapshot(diagnostic="Voice capability readout unavailable"))}
            try{mutable.value=mutable.value.copy(groups=groupExecution.snapshot(native,mutable.value.accountId,generation),groupInvitations=groupExecution.invitations,groupDiagnostics=groupExecution.diagnostic)}catch(_:Exception){}
        }
        if(authorizationSnapshotInstalled&&livePeerWorkAllowed&&events?.connected==true&&groupPendingMore&&now>=groupPendingAt){
            groupPendingAt=now+1000
            try{groupPendingMore=groupExecution.submitPending(native,mutable.value.accountId,generation){events?.queueGroup(it)==true}}
            catch(_:Exception){groupPendingAt=now+30000}
        }
        if(mutable.value.authenticated&&now>=mailboxAt) {
            try {
                measured("mailbox_service") {
                    configureMailboxEnrollment(checkNotNull(accounts),native)
                    mailboxExecution.service(checkNotNull(accounts),native,mailboxAllowed(),committed={ messageId ->
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
                }
                mutable.value=mutable.value.copy(mailboxRecoveryError=null)
                publish()
                mailboxPublication.servicePublished()
            } catch(failure:Exception) {
                if(expiredServerSession(failure))throw failure
                if(mailboxExecution.diagnostic.contains("stage=mailbox_register"))mutable.value=mutable.value.copy(
                    mailboxRecoveryError="The server did not accept this mailbox enrollment. Existing keys, the original binding and queued messages are preserved. Return the diagnostics; do not reset or retry with a different key.")
                // No plaintext/error message. An unavailable/rejected mailbox
                // does not manufacture a peer route or invalidate local history.
                android.util.Log.i("PeerChatWake","stage=mailbox_service_pending ${mailboxExecution.diagnostic}")
            } finally {
                try{projectMailboxRecovery(native)}catch(_:Exception){mutable.value=mutable.value.copy(mailboxReenrollment=null,mailboxRecoveryError="Mailbox recovery validation is unavailable. Preserve app data and return the diagnostics.")}
                mailboxAt=System.nanoTime()/1_000_000+if(mailboxExecution.needsContinuation)1000 else if(events?.connected==true)900000 else 120000
                if(mailboxExecution.nextRetryAt>0)mailboxAt=minOf(mailboxAt,mailboxExecution.nextRetryAt)
            }
        }
        if(mutable.value.authenticated&&authorizationSnapshotInstalled) {
            // Receiving/publication above takes priority. A finite background
            // wake refreshes only actual pending send destinations, not contacts.
            val recipients=if(livePeerWorkAllowed)friends.map{it.accountId} else try {
                val rows=NativeRecord(checkNotNull(com.peerchat.android.MailboxBridge.pending(native)),4)
                val count=rows.number();require(count<=64)
                List(count.toInt()){rows.text();rows.text();rows.number()}.also{rows.end()}
            }catch(_:Exception){emptyList()}
            if(eligibilitySnapshots.refresh(checkNotNull(accounts),native,mutable.value.accountId,generation,mutable.value.localPeerId,recipients,now))mailboxAt=0L
        }
        try {val token=pushRegistration.token();if(token!=null&&token!=pushRegisteredToken){accounts?.registerPush(token);pushRegisteredToken=token}}
        catch(_:Exception){/* Optional provider registration never invalidates messaging. */}
        if(!livePeerWorkAllowed&&mailboxAllowed()){publish();return}
        val initialArchive=archiveLoadedGeneration!=generation
        if(livePeerWorkAllowed&&now>=archiveRetryAt&&(initialArchive||mutable.value.archiveProgress.running||now<archiveServingUntil||now>=archiveMetadataAt)){
            val owner=checkNotNull(accounts);val active=checkNotNull(owner.session);val local=mutable.value.localPeerId
            val execution=archiveExecution(owner,checkNotNull(profile))
            try{if(initialArchive||now>=archiveMetadataAt){
                    if(initialArchive)execution.publishPending(active.accountId,generation,local) else execution.refreshCurrent(active.accountId,generation,local)
                    // Stay below the native 30-second authorization expiry.
                    archiveMetadataAt=now+if(mutable.value.archiveProgress.running)10000 else 20000
                }
                mutable.value=mutable.value.copy(archiveProgress=execution.progress(active.accountId,generation))
                if(mutable.value.archiveProgress.running)archiveMetadataAt=minOf(archiveMetadataAt,now+10000)
                archiveLoadedGeneration=generation
                archiveRetryAt=0L
            }catch(_:Exception){mutable.value=mutable.value.copy(archiveProgress=mutable.value.archiveProgress.copy(projectionFresh=false));archiveRetryAt=now+60000}
            finally{val admission=try{execution.authorizationDiagnostics(active.accountId,generation)}catch(error:Exception){"Admission snapshot unavailable: ${error.javaClass.simpleName}"};mutable.value=mutable.value.copy(archiveAuthorizationDiagnostics=admission)}
        }
        // Drain bounded events; payload history/delivery is always reread from
        // Storage. An event is never a second message/persistence authority.
        val iterator=executions.iterator()
        val checkGroupCompletion=now>=groupCompletionAt
        if(checkGroupCompletion)groupCompletionAt=now+500
        while(iterator.hasNext()) {
            val (attempt,execution)=iterator.next()
            if(execution.finished){
                execution.failureDiagnostic?.let{relayFailureDiagnostics[attempt]="relay_attempt=$attempt $it";while(relayFailureDiagnostics.size>16)relayFailureDiagnostics.remove(relayFailureDiagnostics.keys.first())}
                groupCompletionObserved.remove(attempt);iterator.remove();continue}
            if(!execution.attached)continue // rendezvous is not yet a native TLS execution
            val status=ReliableBridge.exchange(native,attempt,ReliableBridge.STATUS,ByteArray(0))
            if(status==null||status[1].toInt()!=0){execution.close();iterator.remove();continue}
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
            repeat(16) {
                val event=ReliableBridge.exchange(native,attempt,ReliableBridge.EVENT,ByteArray(0)) ?: return@repeat
                if(event.isEmpty())return@repeat
                val record=NativeRecord(event,3);val kind=record.number();record.text();record.text();record.number()
                if(kind==1L)android.util.Log.i("PeerChatWake","stage=peer_session_ready")
                if(kind==2L){
                    lastNativeReceiveNanos=System.nanoTime();lastNativeReceiveAttempt=attempt
                    android.util.Log.i("PeerChatWake","stage=native_history_received_ack_queued")
                }
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
            val friend=friends.find {it.peerId==request.peerId}
            val archive=request.archive;val session=accounts?.session
            val group=request.group
            val admitted=if(group!=null)session!=null&&group.account==session.accountId&&group.authenticationGeneration==generation&&mutable.value.groups.any{it.id==group.group&&it.epoch==group.epoch}
                else if(archive==null)friend!=null else session!=null&&archive.account==session.accountId&&
                ((archive.reader==mutable.value.localPeerId&&archive.source==request.peerId)||(archive.source==mutable.value.localPeerId&&archive.reader==request.peerId))
            if(!admitted||executions.size>=32){ReliableBridge.close(native,request.attempt,admitted);continue}
            val execution=AndroidRelayExecution(native,request,checkNotNull(accounts),group?.targetAccount?:archive?.account?:checkNotNull(friend).accountId)
            executions[request.attempt]=execution;execution.start()
        }
        publish()
    }
    private fun handleServerEvent(event:org.json.JSONObject){
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
            "group_roster_changed","group_invite_changed"->{groupRefreshAt=0L}
            "mailbox_ready","mailbox_receipt_ready"->{mailboxAt=0L;android.util.Log.i("PeerChatNetwork","mailbox_trigger=$kind")}
            "presence_changed","friend_request","friend_request_updated","friendship_changed"->{contactsDirty=true}
            "relay_demand"->{
                if(event.optString("purpose")=="group_control"){
                    if(!livePeerWorkAllowed||event.optString("targetPublicIdentity")!=mutable.value.localPeerId)return
                    try{groupExecution.incomingControl(event,checkNotNull(profile).handle,mutable.value.accountId,generation)}
                    catch(_:Exception){groupRefreshAt=0L}
                    return
                }
                if(event.optString("purpose")=="archive_history"){
                    if(!livePeerWorkAllowed||event.optString("targetPublicIdentity")!=mutable.value.localPeerId)return
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
                        mutable.value=mutable.value.copy(archiveProgress=mutable.value.archiveProgress.copy(state="Archive source unavailable; retry pending"))
                    }
                    return
                }
                if(event.optString("purpose")!="friend"||event.optString("targetPublicIdentity")!=mutable.value.localPeerId)return
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
    private fun publish() {
        pendingDeviceVerifications.entries.removeAll{it.value.generation!=generation}
        pendingDeviceVerifications.replaceAll{_,value->value.copy(live=value.attempt in executions&&executions[value.attempt]?.finished==false)}
        mutable.value=mutable.value.copy(deviceVerifications=pendingDeviceVerifications.values.toList())
        mutable.value=mutable.value.copy(messagingDiagnostics=buildString {
            appendLine("mailbox_storage_allowed=${mailboxAllowed()} ${mailboxExecution.diagnostic}")
            appendLine(mailboxExecution.inboundDiagnostic)
            appendLine(eligibilitySnapshots.diagnostic)
            appendLine(profile?.recipientDiagnostics() ?: "stage=no_local_profile")
            appendLine(profile?.let{com.peerchat.android.MailboxBridge.registrationDiagnostic(it.handle)?.toString(Charsets.US_ASCII)} ?: "native_registration_stage=unavailable")
            peerAuthenticationDiagnostics.values.forEach{appendLine(it)}
            relayFailureDiagnostics.values.forEach{appendLine(it)}
        })
        val native=checkNotNull(profile).handle
        val conversations=nativeConversationProjection(friends,
            {peer->checkNotNull(ReliableBridge.state(native,peer.toByteArray(Charsets.US_ASCII)))},
            {peer->checkNotNull(ReliableBridge.history(native,peer.toByteArray(Charsets.US_ASCII),Long.MAX_VALUE))})
        val projectionNow=android.os.SystemClock.elapsedRealtime()
        if(native!=attachmentProjectionHandle||generation!=attachmentProjectionGeneration||projectionNow>=attachmentProjectionAt){
            attachmentProjection=attachmentSnapshots(transfer(4));attachmentProjectionHandle=native;attachmentProjectionGeneration=generation;attachmentProjectionAt=projectionNow+1000
        }
        val attachments=attachmentProjection
        val diagnosticNow=android.os.SystemClock.elapsedRealtime()
        if(attachments.isNotEmpty()&&diagnosticNow>=transferDiagnosticsAt){
            transferDiagnosticsAt=diagnosticNow+1000
            val record=NativeRecord(transfer(5),30)
            val measured=StringBuilder()
            val currentIds=mutableSetOf<String>()
            repeat(record.number().toInt().coerceIn(0,64)){
                val row=record.text();val id=row.substringBefore(' ');currentIds.add(id)
                measured.appendLine(row.take(2048))
                val signature=row.substringBefore(" elapsed_ms=");val previous=transferDiagnosticStates[id]
                val active=!row.contains(" state=4 ")&&!row.contains(" state=5 ")&&!row.contains(" state=6 ")&&!row.contains(" state=8 ")
                if(previous?.first!=signature||(active&&diagnosticNow-(previous?.second?:0L)>=10000)){
                    android.util.Log.i("PeerChatTransfer",row+" projection_observed_ms="+diagnosticNow)
                    transferDiagnosticStates[id]=signature to diagnosticNow
                }
            }
            record.end()
            mutable.value=mutable.value.copy(transferDiagnostics=measured.toString().take(32768))
            transferDiagnosticStates.keys.retainAll(currentIds)
        }
        mutable.value=mutable.value.copy(conversations=conversations.map{c->c.copy(attachments=attachments.filter{it.peer in c.friend.knownPeerIds}.sortedBy{it.created})},mailboxStorageAllowed=mailboxAllowed(),freshMailboxMessageIds=mailboxPublication.freshIds())
    }
    override fun backgroundOutputDrained(afterNanos:Long):Boolean=synchronized(gate){
        val receiver=executions[lastNativeReceiveAttempt]
        mailboxPublication.outputDrained(afterNanos,mailboxExecution.needsContinuation)||
            (receiver!=null&&nativeWakeOutputComplete(lastNativeReceiveNanos,afterNanos,receiver.attached,receiver.finished,receiver.lastOutputIdleNanos))
    }
    override fun close() {
        if(stopped)return;stopped=true
        operations.configure(false,Long.MAX_VALUE)
        // Do not block the UI waiting for an in-progress bounded server request.
        executor.execute {synchronized(gate){
            generation++;clearAuthorization();try {if(remembered.load()==null)accounts?.logout()}catch(_: Exception){}
            accounts?.closeRpc();accounts=null;friends=emptyList();index=null;profile?.close();profile=null
        }}
        executor.shutdown()
    }
}
