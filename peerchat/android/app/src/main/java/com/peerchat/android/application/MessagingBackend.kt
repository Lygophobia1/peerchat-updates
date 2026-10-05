package com.peerchat.android.application

import android.content.Context
import com.peerchat.android.NativeProfile
import com.peerchat.android.ProfileBridge
import com.peerchat.android.ReliableBridge
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
    override val state: StateFlow<MessagingSnapshot> = mutable.asStateFlow()
    private var profile: NativeProfile?=null
    private var accounts: ServerAccounts?=null
    private var index: FriendIdentityIndex?=null
    private var friends=emptyList<FriendIdentity>()
    private val localContexts=LocalConversationContextStore(this.context,profileName)
    private var localEndpoint:ServerEndpoint?=null
    private var generation=0L
    private var network=0L
    private var refreshAt=0L
    private var rememberedRetryEndpoint:ServerEndpoint?=null
    private var rememberedRetryAt=0L
    private var rememberedRetryDelay=1000L
    private var loggedTickFailure:String?=null
    private var lastNativeReceiveNanos=0L
    private var lastNativeReceiveAttempt=0L
    private var lastMailboxAcknowledgedNanos=0L
    private var mailboxAt=0L
    private val mailboxExecution=MailboxExecution()
    // Explicit profile-local consent; absence is FORBIDDEN. Retention/history
    // policy is separate. The settings UI will expose this preference.
    private val mailboxPreferences=AndroidQolStore(this.context,profileName)
    private fun mailboxAllowed():Boolean=try{mailboxPreferences.load().mailboxStorageAllowed}catch(_:Exception){false}
    override suspend fun setMailboxStorageAllowed(allowed:Boolean)=withContext(Dispatchers.IO){synchronized(gate){
        check(!stopped);mailboxPreferences.update{it.copy(mailboxStorageAllowed=allowed)}
        mailboxExecution.reset();mailboxAt=0L;mutable.value=mutable.value.copy(mailboxStorageAllowed=allowed)
    }}
    private val remembered=AndroidRememberedLoginStore(this.context,profileName)
    private val pushRegistration=AndroidPushRegistration(this.context,profileName)
    private var pushRegisteredToken:String?=null
    private val executions=mutableMapOf<Long,AndroidRelayExecution>()
    private val operations= com.peerchat.android.platform.tls.ServerChannel.OperationScope()
    @Volatile private var stopped=false
    @Volatile private var inUse=true
    @Volatile private var livePeerWorkAllowed=true
    @Volatile private var quiescent=java.util.concurrent.CountDownLatch(0)
    internal fun setInUse(value:Boolean,deadlineNanos:Long=Long.MAX_VALUE) {
        inUse=value
        // A finite wake lease services server mailbox only. It never starts a
        // friend reconnect cycle to obtain history or bootstrap sender trust.
        livePeerWorkAllowed=deadlineNanos==Long.MAX_VALUE
        operations.configure(value,deadlineNanos)
        if(!value){val completed=java.util.concurrent.CountDownLatch(1);quiescent=completed
            executor.execute {try{synchronized(gate){if(!inUse&&!stopped){
            generation++;clearAuthorization();try{if(remembered.load()==null)accounts?.logout()}catch(_:Exception){}
            accounts=null;index=null;pushRegisteredToken=null
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
            if(!stopped)try {synchronized(gate){tick()}}catch(failure: Exception){
                if(loggedTickFailure!=failure.javaClass.simpleName){
                    loggedTickFailure=failure.javaClass.simpleName
                    android.util.Log.i("PeerChatWake","stage=backend_tick_failure result="+loggedTickFailure)
                }
                if(expiredServerSession(failure))synchronized(gate){
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
    override suspend fun signInRemembered(endpoint:ServerEndpoint,username:String,password:String,remember:Boolean):AccountSession = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&accounts==null)
            val native=profile ?: NativeProfile(context,profileName).also {profile=it}
            val key=checkNotNull(ProfileBridge.publicKey(native.handle))
            val device=PeerIdentityFormat.peerId(key)
            if(remember)require(endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))
            // Explicit login replaces a previous remembered account only after
            // durable revocation. Network failure leaves a non-login tombstone.
            revokeSaved(native,key)
            val connection=ServerAccounts(endpoint,device,operations)
            val authenticated=connection.login(username,password)
            try {
                val metadata=FriendIdentityIndex(context,profileName,endpoint,authenticated.accountId)
                accounts=connection;index=metadata;pushRegisteredToken=null;network++
                mutable.value=MessagingSnapshot(authenticated.accountId,authenticated.username,device,true,"Authenticated; refreshing contacts")
                refresh();refreshAt=System.nanoTime()/1_000_000+5000
                if(remember){val value=connection.remember(key){checkNotNull(ProfileBridge.sign(native.handle,it))};
                    try{remembered.save(value)}catch(failure:Exception){try{connection.revoke(value,key){checkNotNull(ProfileBridge.sign(native.handle,it))}}catch(_:Exception){};throw failure}
                    // Enrollment leaves the password-login bearer unbound. Push
                    // registration requires the signed device-bound resume token
                    // immediately, not only after the next process restart.
                    generation++;clearAuthorization();accounts=null;index=null;friends=emptyList()
                    connection.logout();check(restoreRememberedLocked(endpoint))
                }
                checkNotNull(accounts?.session)
            } catch(failure: Exception){
                accounts=null;index=null;friends=emptyList();generation++
                clearAuthorization();try {connection.logout()}catch(_: Exception){}
                mutable.value=MessagingSnapshot(localPeerId=device,status="Login setup failed closed")
                throw failure
            }
        }
    }
    private fun revokeSaved(native:NativeProfile,key:ByteArray) {
        val saved=remembered.load() ?: return
        check(saved.device==PeerIdentityFormat.peerId(key))
        remembered.save(saved.pendingRevocation())
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
        val identity=PeerIdentityFormat.peerId(checkNotNull(ProfileBridge.publicKey(native.handle)))
        check(identity==cached.device)
        // Cached names/bindings project local Storage ONLY. Never install them
        // into ReliableBridge.friends or grant route/mailbox authorization.
        friends=cached.friends
        localEndpoint=endpoint
        mutable.value=MessagingSnapshot(cached.account,cached.username,identity,false,"Offline; local history available",
            localHistoryReady=true)
        publish()
        android.util.Log.i("PeerChatWake","stage=local_history_projected contacts=${friends.size}")
        return true
    }
    private fun restoreRememberedLocked(endpoint:ServerEndpoint):Boolean {
        check(!stopped&&inUse)
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
            refresh();refreshAt=System.nanoTime()/1_000_000+5000
            rememberedRetryEndpoint=null;rememberedRetryDelay=1000;return true
        }catch(failure:Exception){
            accounts=null;index=null;generation++;clearAuthorization()
            rememberedRetryEndpoint=null
            if(isRememberedNetworkFailure(failure)) {
                rememberedRetryEndpoint=endpoint
                rememberedRetryAt=System.nanoTime()/1_000_000+rememberedRetryDelay
                rememberedRetryDelay=(rememberedRetryDelay*2).coerceAtMost(30000)
            }
            friends=friends.map{it.copy(online=false)}
            mutable.value=localOfflineSnapshot(mutable.value,if(rememberedRetryEndpoint!=null)"Reconnecting; saved authorization retained" else "Saved authentication failed; sign in again").copy(localPeerId=saved.device)
            if(isRememberedCredentialRejected(failure))remembered.clear()
            throw failure
        }
    }
    override suspend fun send(peerId: String,text: String): String = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&friends.any {it.peerId==peerId}&&(accounts?.session!=null||mutable.value.localHistoryReady))
            val result=checkNotNull(ReliableBridge.queueLocal(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),text.toByteArray(Charsets.UTF_8)))
            val id=result.toString(Charsets.US_ASCII);check(id.matches(Regex("[0-9a-f]{32}")))
            publish()
            val friend=friends.first{it.peerId==peerId};val owner=accounts
            // Best effort after native durable queueing. Not another retry policy.
            executor.execute{try{owner?.hint(friend.accountId,friend.peerId)}catch(_:Exception){}}
            id
        }
    }
    override suspend fun older(peerId: String,before: Long): List<MessageSnapshot> = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&friends.any {it.knownPeerIds.contains(peerId)})
            messageSnapshots(checkNotNull(ReliableBridge.history(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),before)))
        }
    }
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
        mailboxExecution.reset();lastMailboxAcknowledgedNanos=0L;mailboxAt=0L
        val writer=NativeRecordWriter(1);writer.number(0)
        profile?.let {check(ReliableBridge.friends(it.handle,0,generation.coerceAtLeast(1),writer.bytes())==0)}
        executions.values.forEach {it.close()};executions.clear()
    }
    override suspend fun signOut():Unit = withContext(Dispatchers.IO) {
        synchronized(gate) {
            rememberedRetryEndpoint=null;rememberedRetryDelay=1000
            generation++;clearAuthorization()
            val old=accounts;accounts=null;friends=emptyList();index=null
            pushRegisteredToken=null
            try {profile?.let{revokeSaved(it,checkNotNull(ProfileBridge.publicKey(it.handle)))};old?.logout()}
            finally {mutable.value=MessagingSnapshot(localPeerId=mutable.value.localPeerId)}
            Unit
        }
    }
    private fun refresh() {
        val accounts=checkNotNull(accounts);val active=checkNotNull(accounts.session)
        val state=accounts.bootstrap()
        val pending=state.getJSONArray("incoming");require(pending.length()<=128)
        val requests=List(pending.length()){i->val item=pending.getJSONObject(i)
            val id=item.getLong("accountId");val name=item.getString("username");require(id>0&&name.length<=64)
            AccountRequestSnapshot(id,name)
        }
        val list=state.getJSONArray("friends");require(list.length()<=128)
        val projected=List(list.length()) {i->val item=list.getJSONObject(i)
            FriendIdentity(item.getLong("accountId"),item.getString("username"),item.getString("publicIdentity"),item.getBoolean("online"))
        };require(projected.none {it.accountId==active.accountId})
        val authoritative=checkNotNull(index).reconcile(projected)
        android.util.Log.i("PeerChatWake","stage=friend_devices_restored contacts=${authoritative.size} devices=${authoritative.sumOf{it.knownPeerIds.size}}")
        val writer=NativeRecordWriter(1);writer.number(authoritative.size.toLong())
        authoritative.forEach {friend->writer.number(friend.accountId);writer.text(friend.peerId);writer.number(friend.knownPeerIds.size.toLong());friend.knownPeerIds.forEach {writer.text(it)}}
        generation++;val native=checkNotNull(profile).handle
        check(ReliableBridge.friends(native,active.accountId,generation,writer.bytes())==0)
        friends=authoritative
        localEndpoint=accounts.endpoint
        localContexts.save(accounts.endpoint,LocalConversationContext(active.accountId,active.username,mutable.value.localPeerId,authoritative))
        mutable.value=mutable.value.copy(incomingRequests=requests.toList())
        friends.forEach {friend->check(ReliableBridge.presence(native,friend.peerId.toByteArray(Charsets.US_ASCII),friend.online,generation,network)==0)}
        mutable.value=mutable.value.copy(serverPresenceLive=true,localHistoryReady=true,status="Authenticated device presence refreshed; native delivery state active")
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
        // Server authorization and stored delivery precede NEW peer attempts.
        // Existing peer executions remain independent platform workers.
        if(now>=refreshAt) {
            try {refresh()}catch(failure:Exception){
                if(failure !is java.io.IOException)throw failure
                mutable.value=mutable.value.copy(serverPresenceLive=false,status=serverRefreshFailure(failure)+"; local history retained")
            }finally {refreshAt=System.nanoTime()/1_000_000+5000}
        }
        if(mutable.value.authenticated&&now>=mailboxAt) {
            try {
                measured("mailbox_service") {
                    mailboxExecution.service(checkNotNull(accounts),native,mailboxAllowed()) { inserted ->
                        lastMailboxAcknowledgedNanos=System.nanoTime()
                        if(inserted)android.util.Log.i("PeerChatWake","stage=mailbox_native_history_received_server_acknowledged")
                    }
                }
                publish()
            } catch(failure:Exception) {
                if(expiredServerSession(failure))throw failure
                // No plaintext/error message. An unavailable/rejected mailbox
                // does not manufacture a peer route or invalidate local history.
                android.util.Log.i("PeerChatWake","stage=mailbox_service_pending result=${failure.javaClass.simpleName}")
            } finally {mailboxAt=System.nanoTime()/1_000_000+5000}
        }
        try {val token=pushRegistration.token();if(token!=null&&token!=pushRegisteredToken){accounts?.registerPush(token);pushRegisteredToken=token}}
        catch(_:Exception){/* Optional provider registration never invalidates messaging. */}
        if(!livePeerWorkAllowed){publish();return}
        // Drain bounded events; payload history/delivery is always reread from
        // Storage. An event is never a second message/persistence authority.
        val iterator=executions.iterator()
        while(iterator.hasNext()) {
            val (attempt,execution)=iterator.next()
            if(execution.finished){iterator.remove();continue}
            if(!execution.attached)continue // rendezvous is not yet a native TLS execution
            val status=ReliableBridge.exchange(native,attempt,ReliableBridge.STATUS,ByteArray(0))
            if(status==null||status[1].toInt()!=0){execution.close();iterator.remove();continue}
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
                    if(accepted?.firstOrNull()?.toInt()!=1)mutable.value=mutable.value.copy(status="Device verification required; automatic replacement refused")
                }
            }
        }
        for(request in relayExecutions(checkNotNull(ReliableBridge.requests(native)))) {
            val friend=friends.find {it.peerId==request.peerId}
            if(friend==null||executions.size>=32){ReliableBridge.close(native,request.attempt,friend!=null);continue}
            val execution=AndroidRelayExecution(native,request,checkNotNull(accounts),friend.accountId)
            executions[request.attempt]=execution;execution.start()
        }
        publish()
    }
    private fun publish() {
        val native=checkNotNull(profile).handle
        val conversations=nativeConversationProjection(friends,
            {peer->checkNotNull(ReliableBridge.state(native,peer.toByteArray(Charsets.US_ASCII)))},
            {peer->checkNotNull(ReliableBridge.history(native,peer.toByteArray(Charsets.US_ASCII),Long.MAX_VALUE))})
        mutable.value=mutable.value.copy(conversations=conversations.toList(),mailboxStorageAllowed=mailboxAllowed())
    }
    override fun backgroundOutputDrained(afterNanos:Long):Boolean=synchronized(gate){
        val receiver=executions[lastNativeReceiveAttempt]
        lastMailboxAcknowledgedNanos>=afterNanos||
            (receiver!=null&&nativeWakeOutputComplete(lastNativeReceiveNanos,afterNanos,receiver.attached,receiver.finished,receiver.lastOutputIdleNanos))
    }
    override fun close() {
        if(stopped)return;stopped=true
        operations.configure(false,Long.MAX_VALUE)
        // Do not block the UI waiting for an in-progress bounded server request.
        executor.execute {synchronized(gate){
            generation++;clearAuthorization();try {if(remembered.load()==null)accounts?.logout()}catch(_: Exception){}
            accounts=null;friends=emptyList();index=null;profile?.close();profile=null
        }}
        executor.shutdown()
    }
}
