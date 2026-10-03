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
class MessagingBackend(context: Context,private val profileName: String="default") : AutoCloseable {
    private val context=context.applicationContext
    private val gate=Any()
    private val executor=Executors.newSingleThreadScheduledExecutor {task->Thread(task,"PeerChat application projection").apply {isDaemon=true}}
    private val mutable=MutableStateFlow(MessagingSnapshot())
    val state: StateFlow<MessagingSnapshot> = mutable.asStateFlow()
    private var profile: NativeProfile?=null
    private var accounts: ServerAccounts?=null
    private var index: FriendIdentityIndex?=null
    private var friends=emptyList<FriendIdentity>()
    private var generation=0L
    private var network=0L
    private var refreshAt=0L
    private val executions=mutableMapOf<Long,AndroidRelayExecution>()
    @Volatile private var stopped=false
    init {
        executor.scheduleWithFixedDelay({
            if(!stopped)try {synchronized(gate){tick()}}catch(failure: Exception){
                if(failure is ServerRejected&&failure.code in listOf("session_invalid","event_session_superseded"))synchronized(gate){
                    generation++;clearAuthorization();accounts=null;index=null;friends=emptyList()
                    mutable.value=mutable.value.copy(authenticated=false,conversations=emptyList(),status="Session expired; sign in again")
                    return@scheduleWithFixedDelay
                }
                mutable.value=mutable.value.copy(status="Connection refresh failed; native delivery state retained")
            }
        },125,125,TimeUnit.MILLISECONDS)
    }
    suspend fun signIn(endpoint: ServerEndpoint,username: String,password: String): AccountSession = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&accounts==null)
            val native=profile ?: NativeProfile(context,profileName).also {profile=it}
            val key=checkNotNull(ProfileBridge.publicKey(native.handle))
            val device=PeerIdentityFormat.peerId(key)
            val connection=ServerAccounts(endpoint,device)
            val authenticated=connection.login(username,password)
            try {
                connection.verifySession();connection.presence()
                val metadata=FriendIdentityIndex(context,profileName,endpoint,authenticated.accountId)
                accounts=connection;index=metadata;network++
                mutable.value=MessagingSnapshot(authenticated.accountId,authenticated.username,device,true,"Authenticated; refreshing contacts")
                refresh();refreshAt=System.nanoTime()/1_000_000+5000
                authenticated
            } catch(failure: Exception){
                accounts=null;index=null;friends=emptyList();generation++
                clearAuthorization();try {connection.logout()}catch(_: Exception){}
                mutable.value=MessagingSnapshot(localPeerId=device,status="Login setup failed closed")
                throw failure
            }
        }
    }
    suspend fun send(peerId: String,text: String): String = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&accounts?.session!=null&&friends.any {it.peerId==peerId})
            val result=checkNotNull(ReliableBridge.send(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),text.toByteArray(Charsets.UTF_8)))
            val id=result.toString(Charsets.US_ASCII);check(id.matches(Regex("[0-9a-f]{32}")))
            publish();id
        }
    }
    suspend fun older(peerId: String,before: Long): List<MessageSnapshot> = withContext(Dispatchers.IO) {
        synchronized(gate) {
            check(!stopped&&friends.any {it.knownPeerIds.contains(peerId)})
            messageSnapshots(checkNotNull(ReliableBridge.history(checkNotNull(profile).handle,peerId.toByteArray(Charsets.US_ASCII),before)))
        }
    }
    private fun clearAuthorization() {
        val writer=NativeRecordWriter(1);writer.number(0)
        profile?.let {check(ReliableBridge.friends(it.handle,0,generation.coerceAtLeast(1),writer.bytes())==0)}
        executions.values.forEach {it.close()};executions.clear()
    }
    suspend fun signOut() = withContext(Dispatchers.IO) {
        synchronized(gate) {
            generation++;clearAuthorization()
            val old=accounts;accounts=null;friends=emptyList();index=null
            try {old?.logout()}finally {mutable.value=MessagingSnapshot(localPeerId=mutable.value.localPeerId)}
        }
    }
    private fun refresh() {
        val accounts=checkNotNull(accounts);val active=checkNotNull(accounts.session)
        accounts.presence()
        val list=accounts.friends().getJSONArray("friends");require(list.length()<=128)
        val projected=List(list.length()) {i->val item=list.getJSONObject(i)
            FriendIdentity(item.getLong("accountId"),item.getString("username"),item.getString("publicIdentity"),item.getBoolean("online"))
        };require(projected.none {it.accountId==active.accountId})
        val authoritative=checkNotNull(index).reconcile(projected)
        val writer=NativeRecordWriter(1);writer.number(authoritative.size.toLong())
        authoritative.forEach {friend->writer.number(friend.accountId);writer.text(friend.peerId);writer.number(friend.knownPeerIds.size.toLong());friend.knownPeerIds.forEach {writer.text(it)}}
        generation++;val native=checkNotNull(profile).handle
        check(ReliableBridge.friends(native,active.accountId,generation,writer.bytes())==0)
        friends=authoritative
        friends.forEach {friend->check(ReliableBridge.presence(native,friend.peerId.toByteArray(Charsets.US_ASCII),friend.online,generation,network)==0)}
    }
    private fun tick() {
        if(accounts?.session==null)return
        val now=System.nanoTime()/1_000_000
        if(now>=refreshAt) {refreshAt=now+5000;refresh()}
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
            generation++;clearAuthorization();try {accounts?.logout()}catch(_: Exception){}
            accounts=null;friends=emptyList();index=null;profile?.close();profile=null
        }}
        executor.shutdown()
    }
}
