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
    private var generation=0L
    private var network=0L
    private var refreshAt=0L
    private val remembered=AndroidRememberedLoginStore(this.context,profileName)
    private val pushRegistration=AndroidPushRegistration(this.context,profileName)
    private var pushRegisteredToken:String?=null
    private val executions=mutableMapOf<Long,AndroidRelayExecution>()
    @Volatile private var stopped=false
    @Volatile private var inUse=true
    internal fun setInUse(value:Boolean) {
        inUse=value
        if(!value)executor.execute {synchronized(gate){if(!inUse&&!stopped){
            generation++;clearAuthorization();try{if(remembered.load()==null)accounts?.logout()}catch(_:Exception){}
            accounts=null;friends=emptyList();index=null;pushRegisteredToken=null;profile?.close();profile=null
            mutable.value=MessagingSnapshot(localPeerId=mutable.value.localPeerId,status="Inactive; authentication/presence will be verified on resume")
        }}}
    }
    init {
        executor.scheduleWithFixedDelay({
            if(!stopped)try {synchronized(gate){tick()}}catch(failure: Exception){
                if(expiredServerSession(failure))synchronized(gate){
                    generation++;clearAuthorization();accounts=null;index=null;friends=emptyList()
                    mutable.value=mutable.value.copy(authenticated=false,serverPresenceLive=false,conversations=emptyList(),incomingRequests=emptyList(),status="Session expired; sign in again")
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
            val connection=ServerAccounts(endpoint,device)
            val authenticated=connection.login(username,password)
            try {
                connection.verifySession();connection.presence()
                val metadata=FriendIdentityIndex(context,profileName,endpoint,authenticated.accountId)
                accounts=connection;index=metadata;pushRegisteredToken=null;network++
                mutable.value=MessagingSnapshot(authenticated.accountId,authenticated.username,device,true,"Authenticated; refreshing contacts")
                refresh();refreshAt=System.nanoTime()/1_000_000+5000
                if(remember){val value=connection.remember(key){checkNotNull(ProfileBridge.sign(native.handle,it))};
                    try{remembered.save(value)}catch(failure:Exception){try{connection.revoke(value,key){checkNotNull(ProfileBridge.sign(native.handle,it))}}catch(_:Exception){};throw failure}}
                authenticated
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
        try{ServerAccounts(saved.endpoint,saved.device).revoke(saved,key){checkNotNull(ProfileBridge.sign(native.handle,it))}}
        catch(failure:ServerRejected){if(failure.code!="remember_revoked")throw failure}
        remembered.clear()
    }
    override suspend fun restoreRemembered(endpoint:ServerEndpoint):Boolean = withContext(Dispatchers.IO) {synchronized(gate){restoreRememberedLocked(endpoint)}}
    private fun restoreRememberedLocked(endpoint:ServerEndpoint):Boolean {
        check(!stopped&&inUse)
        accounts?.let{check(it.endpoint==endpoint);refresh();return true}
        val saved=remembered.load() ?: return false
        val native=profile ?: NativeProfile(context,profileName).also{profile=it}
        val key=checkNotNull(ProfileBridge.publicKey(native.handle));check(saved.device==PeerIdentityFormat.peerId(key))
        if(saved.revocationPending){revokeSaved(native,key);return false}
        check(saved.endpoint==endpoint){"Remembered credential belongs to different trusted server"}
        val connection=ServerAccounts(endpoint,saved.device)
        try{
            val authenticated=connection.resume(saved,key){checkNotNull(ProfileBridge.sign(native.handle,it))}
            connection.verifySession();connection.presence()
            accounts=connection;index=FriendIdentityIndex(context,profileName,endpoint,authenticated.accountId);pushRegisteredToken=null;network++
            mutable.value=MessagingSnapshot(authenticated.accountId,authenticated.username,saved.device,true,"Authenticated; refreshing contacts")
            refresh();refreshAt=System.nanoTime()/1_000_000+5000;return true
        }catch(failure:Exception){
            accounts=null;index=null;friends=emptyList();generation++;clearAuthorization()
            mutable.value=MessagingSnapshot(localPeerId=saved.device,status="Remembered authentication unavailable; sign in or retry")
            if(failure is ServerRejected&&failure.code=="remember_revoked")remembered.clear()
            throw failure
        }
    }
    override suspend fun send(peerId: String,text: String): String = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&accounts?.session!=null&&friends.any {it.peerId==peerId})
            val result=checkNotNull(ReliableBridge.send(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),text.toByteArray(Charsets.UTF_8)))
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
        val writer=NativeRecordWriter(1);writer.number(0)
        profile?.let {check(ReliableBridge.friends(it.handle,0,generation.coerceAtLeast(1),writer.bytes())==0)}
        executions.values.forEach {it.close()};executions.clear()
    }
    override suspend fun signOut():Unit = withContext(Dispatchers.IO) {
        synchronized(gate) {
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
        accounts.presence()
        val state=accounts.friends()
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
        val writer=NativeRecordWriter(1);writer.number(authoritative.size.toLong())
        authoritative.forEach {friend->writer.number(friend.accountId);writer.text(friend.peerId);writer.number(friend.knownPeerIds.size.toLong());friend.knownPeerIds.forEach {writer.text(it)}}
        generation++;val native=checkNotNull(profile).handle
        check(ReliableBridge.friends(native,active.accountId,generation,writer.bytes())==0)
        friends=authoritative
        mutable.value=mutable.value.copy(incomingRequests=requests.toList())
        friends.forEach {friend->check(ReliableBridge.presence(native,friend.peerId.toByteArray(Charsets.US_ASCII),friend.online,generation,network)==0)}
        mutable.value=mutable.value.copy(serverPresenceLive=true,status="Authenticated device presence refreshed; native delivery state active")
        try {val token=pushRegistration.token();if(token!=null&&token!=pushRegisteredToken){accounts.registerPush(token);pushRegisteredToken=token}}
        catch(_:Exception){/* Optional provider registration never invalidates messaging. */}
    }
    private fun tick() {
        if(!inUse||accounts?.session==null)return
        val now=System.nanoTime()/1_000_000
        if(now>=refreshAt) {
            refreshAt=now+5000
            try {refresh()}catch(failure:Exception){
                if(failure !is java.io.IOException)throw failure
                mutable.value=mutable.value.copy(serverPresenceLive=false,status=serverRefreshFailure(failure)+"; native delivery state retained")
                // A transient server refresh failure must not skip native route
                // retirement, TLS event draining, ACKs or owner-driven retry.
            }
        }
        val native=checkNotNull(profile).handle
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
        val conversations=friends.map {friend->
            val bytes=friend.peerId.toByteArray(Charsets.US_ASCII)
            val connection=connectionSnapshot(checkNotNull(ReliableBridge.state(native,bytes)))
            val history=messageSnapshots(checkNotNull(ReliableBridge.history(native,bytes,Long.MAX_VALUE)))
            ConversationSnapshot(friend,connection,history.toList())
        }
        mutable.value=mutable.value.copy(conversations=conversations.toList())
    }
    override fun close() {
        if(stopped)return;stopped=true
        // Do not block the UI waiting for an in-progress bounded server request.
        executor.execute {synchronized(gate){
            generation++;clearAuthorization();try {if(remembered.load()==null)accounts?.logout()}catch(_: Exception){}
            accounts=null;friends=emptyList();index=null;profile?.close();profile=null
        }}
        executor.shutdown()
    }
}
