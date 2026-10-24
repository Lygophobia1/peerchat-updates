package com.peerchat.android.platform

import com.peerchat.android.platform.tls.ServerChannel
import com.peerchat.android.platform.tls.BoundedRpc
import org.json.JSONObject

data class ServerEndpoint(val host: String, val port: Int, val certificatePin: String = "")
data class AccountSession(val accountId: Long, val username: String, internal val token: String, val relayOnly: Boolean,val bootstrapVersion:Int=0) {
    override fun toString() = "AccountSession(accountId=$accountId, username=$username, relayOnly=$relayOnly)"
}

private const val CACHE_TRACE_TAG="PeerChatCacheTrace"

private val deliveryRpcOperations=setOf("mailbox_register","mailbox_revoke","mailbox_keys",
    "mailbox_store","mailbox_fetch","mailbox_ack","mailbox_receipts")
internal fun deliveryRpcOperation(type:String)=type in deliveryRpcOperations

// Only these two cache reads can be repeated after a lost response. The RPC
// resource owner has already retired a failed channel before control returns.
internal fun closedCacheReadTransport(operation:String,failure:Exception):Boolean {
    if(operation!="cache_dm_capability"&&operation!="cache_dm_prepare")return false
    var cause:Throwable?=failure
    val seen=java.util.Collections.newSetFromMap(java.util.IdentityHashMap<Throwable,Boolean>())
    while(cause!=null){
        if(seen.size>=16||!seen.add(cause)||cause is javax.net.ssl.SSLException||cause is java.security.cert.CertificateException)return false
        cause=cause.cause
    }
    return failure is java.io.EOFException||failure is java.net.SocketException&&
        failure.message?.lowercase(java.util.Locale.ROOT) in setOf("socket closed","socket is closed","connection reset","connection reset by peer","broken pipe")
}
internal fun <T> retryClosedCacheRead(operation:String,checkCurrent:()->Unit,request:()->T):T {
    checkCurrent()
    val result=try{request()}catch(failure:Exception){
        if(!closedCacheReadTransport(operation,failure))throw failure
        checkCurrent()
        request() // Exactly one fresh exchange, never a general RPC replay.
    }
    checkCurrent()
    return result
}

internal class CacheRpcPipeline(val exchange:(List<Pair<String,JSONObject>>)->List<JSONObject>)

// Opt-in only for one contiguous upload lease. Legacy and mixed windows keep
// their existing individually durable replies; native owns retry and offsets.
internal fun cacheAppendBatchCount(requests:List<Pair<String,JSONObject>>,advertised:Int):Int {
    if(requests.size !in 2..minOf(BoundedRpc.MAX_WINDOW,advertised))return 0
    var lease:String?=null;var next:Long?=null
    for((operation,fields) in requests){
        if(operation!="cache_dm_append"||fields.has("batchCount"))return 0
        val current=fields.opt("lease") as? String?:return 0
        if(current.isEmpty()||(lease!=null&&lease!=current))return 0
        val value=fields.opt("offset") as? Number?:return 0
        val offset=value.toLong();if(offset<0||value.toDouble()!=offset.toDouble()||(next!=null&&offset!=next))return 0
        val encoded=fields.opt("data") as? String?:return 0
        if(encoded.length !in 2..87382||encoded.length%4==1)return 0
        val length=encoded.length.toLong()*3/4
        if(length !in 1..65536||offset>Long.MAX_VALUE-length)return 0
        lease=current;next=offset+length
    }
    return requests.size
}


// Append fields are flat immutable values. Copy the small map without encoding
// and parsing its base64 chunk again; never mutate the native request's fields.
internal fun copyCacheAppendFields(fields:JSONObject):JSONObject=JSONObject().also { copy ->
    fields.keys().forEach { key ->
        val value=fields.get(key)
        require(value is String||value is Number||value is Boolean||value===JSONObject.NULL)
        copy.put(key,value)
    }
}

internal class RpcChannelPair<T:AutoCloseable>(ordinaryFactory:()->T,deliveryFactory:()->T,
    available:(T)->Boolean,cacheFactory:()->T=ordinaryFactory):AutoCloseable {
    private val ordinary=BoundedRpc<T>({ordinaryFactory()},{available(it)})
    private val delivery=BoundedRpc<T>({deliveryFactory()},{available(it)})
    // File payloads have one bounded resource of their own. Ordinary metadata
    // and mailbox delivery retain their existing owners and TLS implementation.
    private val cache=BoundedRpc<T>({cacheFactory()},{available(it)},64)
    private fun owner(type:String)=when {deliveryRpcOperation(type)->delivery;type.startsWith("cache_")->cache;else->ordinary}
    fun <R> run(type:String,binding:String,operation:(T)->R):R=owner(type).run(binding){operation(it)}
    fun <R> runWindow(type:String,binding:String,count:Int,operation:(T)->R):R=owner(type).run(binding,count){operation(it)}
    fun closeSelected(type:String)=owner(type).close()
    override fun close(){cache.close();delivery.close();ordinary.close()}
}

// Counterpart of Desktop DiscoveryClient, not a native session/delivery owner.
// Passwords/tokens are never logged. A session remains scoped to this profile.
class ServerAccounts(val endpoint: ServerEndpoint, val device: String,
    private val operations:ServerChannel.OperationScope=ServerChannel.OperationScope()) {
    @Volatile var session: AccountSession? = null
        private set
    @Volatile internal var presenceEpoch=""
        private set
    private var supportsRpc=false
    private var rpcAuthorized=false
    @Volatile private var dmAppendBatchCapability:Pair<AccountSession,Int>?=null
    @Volatile private var ownMetadataPublished=false
    private val stages=ConnectionStageHistory()
    fun diagnostics():String=stages.snapshot()
    private fun openRpcChannel(label:String):ServerChannel {
        val connection=channel(label)
        try {
            val hello=JSONObject().put("version",1).put("type","rpc_open")
                .put("sessionToken",checkNotNull(session).token).put("publicIdentity",device)
            connection.write(hello.toString().toByteArray(Charsets.UTF_8),4096)
            val ready=JSONObject(connection.read(16384).toString(Charsets.UTF_8))
            if(!ready.optBoolean("ok"))throw ServerRejected(ready.optString("error","session_invalid"))
            check(ready.getString("type")=="rpc_ready"&&ready.getInt("rpcVersion")==1)
            return connection
        }catch(error:Exception){connection.close();throw error}
    }
    private val rpcOwners=RpcChannelPair({openRpcChannel("rpc")},{openRpcChannel("delivery_rpc")},{it.isOpen},{openRpcChannel("cache_rpc")})
    fun closeRpc(){rpcOwners.close()}
    private fun rpc(type:String,fields:JSONObject):JSONObject {
        operations.check()
        val active=checkNotNull(session)
        val token=active.token
        val began=System.nanoTime()
        return rpcOwners.run(type,token){connection->
            check(session===active)
            connection.readTimeout(10000)
            val id=java.util.UUID.randomUUID().toString().replace("-","")
            try{
            val request=JSONObject(fields.toString()).put("version",1).put("type",type)
                .put("rpcRequestId",id).put("sessionToken",token).put("publicIdentity",device)
            if(type.startsWith("mailbox_")||type.startsWith("cache_dm_"))request.put("requestId",id)
            connection.write(request.toString().toByteArray(Charsets.UTF_8),if(type.startsWith("mailbox_")||type.startsWith("cache_"))100000 else 4096)
            val reply=JSONObject(connection.read(if(type.startsWith("device_identity_"))131072 else 100000).toString(Charsets.UTF_8))
            check(reply.getString("type")=="rpc_result"&&reply.getString("rpcRequestId")==id&&reply.getString("operation")==type)
            val body=reply.getJSONObject("body")
            if(type.startsWith("cache_dm_"))require(body.getString("requestId")==id)
            check(session===active)
            if(!reply.optBoolean("more"))rpcOwners.closeSelected(type) else connection.readTimeout(10000)
            body
            }finally{stages.rpcCorrelation(type,fields.optString("requestId"),id,(System.nanoTime()-began)/1_000_000)}
        }
    }
    internal val cachePipeline=CacheRpcPipeline { requests ->
        require(requests.size in 1..BoundedRpc.MAX_WINDOW&&requests.all{it.first=="cache_dm_append"||it.first=="cache_append"})
        operations.check();val active=checkNotNull(session)
        check(supportsRpc&&rpcAuthorized)
        rpcOwners.runWindow(requests.first().first,active.token,requests.size){connection->
            check(session===active);connection.readTimeout(10000)
            val began=System.nanoTime();var encodedAt=0L;var writtenAt=0L;var wireBytes=0
            val cpuBegan=android.os.Debug.threadCpuTimeNanos();var encodedCpu=0L;var writtenCpu=0L
            try {
                val batchCount=cacheAppendBatchCount(requests,dmAppendBatchCapability?.takeIf{it.first===active}?.second?:0)
                if(batchCount>1&&android.util.Log.isLoggable(CACHE_TRACE_TAG,android.util.Log.DEBUG))android.util.Log.d(CACHE_TRACE_TAG,"stage=cache_atomic_batch chunks=$batchCount")
                val frames=requests.mapIndexed{index,(type,fields)->
                    val id=java.util.UUID.randomUUID().toString().replace("-","")
                    val request=copyCacheAppendFields(fields).put("version",1).put("type",type)
                        .put("rpcRequestId",id).put("sessionToken",active.token).put("publicIdentity",device)
                    if(type=="cache_dm_append")request.put("requestId",id)
                    if(index==0&&batchCount>1)request.put("batchCount",batchCount)
                    id to request.toString().toByteArray(Charsets.UTF_8).also{require(it.size<=100000)}
                }
                // Ordered bounded frames on one existing TLS owner. Never replay an
                // ambiguous window; native durable offsets remain the recovery owner.
                encodedAt=System.nanoTime();encodedCpu=android.os.Debug.threadCpuTimeNanos();wireBytes=frames.sumOf{4+it.second.size}
                connection.writeWindow(frames.map{it.second},100000)
                writtenAt=System.nanoTime();writtenCpu=android.os.Debug.threadCpuTimeNanos()
                requests.mapIndexed{i,(type,fields)->
                    val id=frames[i].first
                    val reply=JSONObject(connection.read(100000).toString(Charsets.UTF_8))
                    check(reply.getString("type")=="rpc_result"&&reply.getString("rpcRequestId")==id&&reply.getString("operation")==type)
                    val body=reply.getJSONObject("body")
                    require(body.getString("type")=="cache_result"&&body.getString("operation")==type)
                    if(type=="cache_dm_append")require(body.getString("requestId")==id)
                    else require(body.getString("requestId")==fields.getString("requestId"))
                    check(session===active)
                    stages.rpcCorrelation(type,fields.optString("requestId"),id,(System.nanoTime()-began)/1_000_000)
                    if(!reply.optBoolean("more")){
                        check(i==requests.lastIndex){"RPC window interrupted"};rpcOwners.closeSelected(type)
                    }
                    body
                }
            } finally {
                val ended=System.nanoTime();val endedCpu=android.os.Debug.threadCpuTimeNanos()
                try { if(android.util.Log.isLoggable(CACHE_TRACE_TAG,android.util.Log.DEBUG)) {
                    val encodeEnd=if(encodedAt>0)encodedAt else ended
                    val writeEnd=if(writtenAt>0)writtenAt else ended
                    android.util.Log.d(CACHE_TRACE_TAG,"stage=cache_pipeline chunks=${requests.size} wire_bytes=$wireBytes "+
                        "encode_ms=${(encodeEnd-began)/1_000_000} write_ms=${if(encodedAt>0)(writeEnd-encodedAt)/1_000_000 else 0} "+
                        "read_decode_ms=${if(writtenAt>0)(ended-writtenAt)/1_000_000 else 0} write_completed=${if(writtenAt>0)1 else 0} "+
                        "encode_cpu_ms=${if(encodedCpu>0)(encodedCpu-cpuBegan)/1_000_000 else 0} write_cpu_ms=${if(writtenCpu>0)(writtenCpu-encodedCpu)/1_000_000 else 0} read_decode_cpu_ms=${if(writtenCpu>0)(endedCpu-writtenCpu)/1_000_000 else 0}")
                }}catch(_:RuntimeException){/* Numeric diagnostics cannot affect transport or ACKs. */}
            }
        }
    }
    init { require(device.matches(Regex("[0-9a-f]{64}"))) }
    private fun safeOperation(type:String)=if(type in setOf("remember_challenge","remember_resume","remember_revoke","remember_enroll_challenge","remember_enroll","session_info","presence","friend_state","messaging_bootstrap","push_register","push_hint","login","logout","register","search","people_search","friend_request","friend_respond","friend_cancel"))type else "other"
    private fun channel(operation:String):ServerChannel {
        var phase="opening"
        try{return ServerChannel(endpoint.host,endpoint.port,endpoint.certificatePin,false,operations,{stage,elapsed->
            if(stage in setOf("tls_context_ready","server_tcp_connected","server_tls_ready"))phase=stage
            android.util.Log.i("PeerChatWake","stage=$stage operation=$operation "+(if(stage=="crypto_provider_initializations")"count=$elapsed" else "elapsed_ms=$elapsed"))
        },operation=="cache_rpc")}
        catch(failure:Exception){stages.channelFailure(operation,phase,failure);throw failure}
    }
    fun request(type: String, fields: JSONObject = JSONObject(), authenticated: Boolean = true,allowRpc:Boolean=true): JSONObject =
        stages.measure(type){requestMeasured(type,fields,authenticated,allowRpc)}
    private fun requestMeasured(type:String,fields:JSONObject,authenticated:Boolean,allowRpc:Boolean):JSONObject {
        operations.check()
        val started=System.nanoTime()
        android.util.Log.i("PeerChatWake","stage=server_request_started operation="+safeOperation(type))
        fields.put("version", 1).put("type", type)
        if(authenticated) fields.put("sessionToken", checkNotNull(session).token)
        val bytes = fields.toString().toByteArray(Charsets.UTF_8)
        require(bytes.size <= 4096)
        val response=try {if(allowRpc&&authenticated&&supportsRpc&&rpcAuthorized&&(!fields.has("publicIdentity")||fields.optString("publicIdentity")==device)&&!type.startsWith("remember_"))rpc(type,fields) else channel(safeOperation(type)).use { channel ->
            channel.write(bytes, 4096)
            android.util.Log.i("PeerChatWake","stage=server_request_written operation="+safeOperation(type)+" elapsed_ms="+(System.nanoTime()-started)/1_000_000)
            JSONObject(channel.read(if(type.startsWith("device_identity_"))131072 else if(type=="device_inventory"||type=="device_metadata")100000 else if(type=="friend_state")65536 else 16384).toString(Charsets.UTF_8))
        }} finally {android.util.Log.i("PeerChatWake","stage=server_request_returned operation="+safeOperation(type)+" elapsed_ms="+(System.nanoTime()-started)/1_000_000)}
        if(!response.optBoolean("ok")) {
            val code=response.optString("error", "request_failed")
            // Log only fixed protocol categories, never arbitrary server text or payloads.
            val category=if(code in setOf("login_failed","session_invalid","session_superseded",
                "device_session_mismatch","device_account_mismatch","bootstrap_version_unsupported",
                "bootstrap_too_large","unsupported_request","invalid_presence"))code else "request_rejected"
            android.util.Log.w("PeerChatWake","stage=server_request_rejected operation="+safeOperation(type)+" reason="+category)
            throw ServerRejected(code)
        }
        if(response.optInt("rpcVersion")==1)supportsRpc=true
        if(authenticated&&(type=="presence"||type=="messaging_bootstrap")&&response.optString("registeredPublicIdentity")==device)rpcAuthorized=true
        return response
    }
    private fun accept(response: JSONObject): AccountSession {
        val next = AccountSession(response.getLong("accountId"), response.getString("username"),
            response.getString("sessionToken"), response.optBoolean("relayOnlyPresence"),response.optInt("messagingBootstrapVersion",0))
        check(next.accountId > 0 && next.token.isNotEmpty() && next.relayOnly) { "Server lacks required relay-only capability" }
        closeRpc();rpcAuthorized=false;dmAppendBatchCapability=null;ownMetadataPublished=false;supportsRpc=response.optInt("rpcVersion")==1
        session = next
        return next
    }
    fun register(username: String, password: String): AccountSession {
        check(session==null) { "Log out before authenticating another session" }
        return accept(request("register",JSONObject().put("username",username)
            .put("password",password).put("publicIdentity",device), false))
    }
    fun login(username: String, password: String): AccountSession {
        check(session==null) { "Log out before authenticating another session" }
        return accept(request("login",JSONObject().put("username",username).put("password",password), false))
    }
    private fun deviceProof(challenge:JSONObject,purpose:String,binding:String,key:ByteArray,sign:(ByteArray)->ByteArray):JSONObject {
        val id=challenge.getString("challengeId");require(id.matches(Regex("[0-9a-f]{64}")))
        val account=challenge.getLong("accountId");require(account>0)
        require(challenge.getString("publicIdentity")==device&&challenge.getString("purpose")==purpose&&PeerIdentityFormat.peerId(key)==device)
        val hash=java.security.MessageDigest.getInstance("SHA-256").digest(binding.toByteArray(Charsets.UTF_8)).joinToString(""){"%02x".format(it)}
        val input="PeerChat remembered device v1|$purpose|$account|$device|$id|$hash".toByteArray(Charsets.US_ASCII)
        require(java.util.Base64.getDecoder().decode(challenge.getString("signatureInput")).contentEquals(input))
        val signature=sign(input);require(signature.size==64)
        return JSONObject().put("publicIdentity",device).put("challengeId",id)
            .put("publicKey",java.util.Base64.getEncoder().encodeToString(key))
            .put("signature",java.util.Base64.getEncoder().encodeToString(signature))
    }
    fun remember(key:ByteArray,sign:(ByteArray)->ByteArray):RememberedLogin {
        require(endpoint.certificatePin.matches(Regex("[0-9a-f]{64}")))
        val active=checkNotNull(session)
        val challenge=request("remember_enroll_challenge",JSONObject().put("publicIdentity",device))
        require(challenge.getLong("accountId")==active.accountId)
        val fields=deviceProof(challenge,"enroll",active.token,key,sign)
        val response=request("remember_enroll",fields)
        check(response.getLong("accountId")==active.accountId&&response.getString("publicIdentity")==device)
        return RememberedLogin(endpoint,active.accountId,device,response.getString("credential"))
    }
    fun resume(value:RememberedLogin,key:ByteArray,sign:(ByteArray)->ByteArray):AccountSession {
        check(session==null&&!value.revocationPending&&value.device==device&&value.endpoint==endpoint)
        val response=rememberedAction(value,"resume",key,sign)
        require(response.getLong("accountId")==value.accountId)
        return accept(response)
    }
    fun revoke(value:RememberedLogin,key:ByteArray,sign:(ByteArray)->ByteArray) {
        require(value.device==device&&value.endpoint==endpoint)
        rememberedAction(value,"revoke",key,sign)
    }
    private fun rememberedAction(value:RememberedLogin,purpose:String,key:ByteArray,sign:(ByteArray)->ByteArray):JSONObject {
        val challenge=request("remember_challenge",JSONObject().put("publicIdentity",device).put("credential",value.credential).put("purpose",purpose),false)
        require(challenge.getLong("accountId")==value.accountId)
        return request("remember_$purpose",deviceProof(challenge,purpose,value.credential,key,sign).put("credential",value.credential),false)
    }
    fun presence(): JSONObject {
        val result=request("presence",JSONObject().put("publicIdentity",device).put("directCandidates",false))
        check(result.getLong("accountId")==checkNotNull(session).accountId && result.getString("registeredPublicIdentity")==device)
        return result
    }
    fun verifySession(): JSONObject {
        val result=request("session_info")
        check(result.getLong("accountId")==checkNotNull(session).accountId)
        return result
    }
    fun friends(): JSONObject = friendStateWithOptionalNames{fields->request("friend_state",fields)}
    // The call owner must fence the captured session/generation before and after
    // this network wait. Never put media setup behind mailbox/ACK RPC work.
    fun mediaTurnCredentials():MediaTurnCredentials = parseMediaTurnCredentials(
        request("media_turn_credentials",JSONObject().put("publicIdentity",device),allowRpc=false),
        System.currentTimeMillis()/1000)
    // A bounded independent connection: optional inventory may not queue text
    // delivery or ACK work behind the shared persistent RPC owner's lock.
    fun ownDevices():JSONObject {
        val active=checkNotNull(session)
        if(!ownMetadataPublished){
            try {
                val response=request("device_metadata",JSONObject().put("publicIdentity",device)
                    .put("platform","Android").put("appVersion",com.peerchat.android.BuildConfig.VERSION_NAME),allowRpc=false)
                if(session===active)ownMetadataPublished=true
                return response
            }catch(failure:ServerRejected){
                if(failure.code!="unsupported_request")throw failure
                // A server supporting inventory but not the optional self-report
                // remains usable. Never infer a platform from a fingerprint.
                if(session===active)ownMetadataPublished=true
            }
        }
        return request("device_inventory",JSONObject().put("publicIdentity",device),allowRpc=false)
    }
    // Every request uses this session's token and its registered public identity.
    // The caller fences account/profile/generation on either side of the wait.
    fun identityRegister(proof:String):JSONObject=request("device_identity_register",JSONObject().put("proof",proof).put("publicIdentity",device),allowRpc=false)
    fun identitySnapshot(account:Long):JSONObject {
        require(account>0)
        return request("device_identity_snapshot",JSONObject().put("accountId",account.toString()).put("publicIdentity",device),allowRpc=false)
    }
    fun identityChallenge(endorsementTarget:String?=null):JSONObject {
        require(endorsementTarget==null||endorsementTarget.matches(Regex("[0-9a-f]{64}")))
        return request(if(endorsementTarget==null)"device_identity_authority_challenge" else "device_identity_endorse_challenge",
            JSONObject().put("publicIdentity",device).also{if(endorsementTarget!=null)it.put("targetDevice",endorsementTarget)},allowRpc=false)
    }
    fun identityCommit(proof:String,endorsement:Boolean):JSONObject=request(
        if(endorsement)"device_identity_endorse_commit" else "device_identity_authority_commit",
        JSONObject().put("proof",proof).put("publicIdentity",device),allowRpc=false)
    fun bootstrap():JSONObject {
        val active=checkNotNull(session)
        if(active.bootstrapVersion!=1){presence();return friends()} // Existing servers, same fail-closed checks.
        val response=request("messaging_bootstrap",JSONObject().put("publicIdentity",device).put("bootstrapVersion",1))
        return validateMessagingBootstrap(response,active.accountId,device)
    }
    fun registerPush(token:String) {require(token.length in 1..2048);request("push_register",JSONObject().put("publicIdentity",device).put("provider","fcm").put("pushToken",token))}
    fun hint(peerAccount:Long,peerDevice:String){request("push_hint",JSONObject().put("publicIdentity",device).put("targetAccountId",peerAccount.toString()).put("targetPublicIdentity",peerDevice))}
    fun openRelay(targetAccount: Long,targetDevice: String,archive:JSONObject?=null,diagnosticAttempt:Long=0,onOpened:(ServerChannel)->Unit={}): RelayConnection =
        stages.measure("relay_attach"){openRelayMeasured(targetAccount,targetDevice,archive,diagnosticAttempt,onOpened)}
    private fun openRelayMeasured(targetAccount:Long,targetDevice:String,archive:JSONObject?,diagnosticAttempt:Long,onOpened:(ServerChannel)->Unit):RelayConnection {
        require(targetAccount>0 && targetDevice.matches(Regex("[0-9a-f]{64}")))
        val active=checkNotNull(session)
        val group=archive?.optString("purpose")=="group_key"
        if(group)require(archive!!.getString("groupId").matches(Regex("[0-9a-f]{32}"))&&archive.getLong("rosterGeneration")>0)
        else if(archive==null)require(active.accountId!=targetAccount)
        else require(active.accountId==targetAccount&&archive.getString("purpose")=="archive_history"&&
            ((archive.getString("archiveSource")==device&&archive.getString("archiveReader")==targetDevice)||(archive.getString("archiveReader")==device&&archive.getString("archiveSource")==targetDevice)))
        val channel=channel("relay_attach")
        try {
            onOpened(channel) // Own cancellation before rendezvous blocks.
            channel.readTimeout(20000) // existing server pairing deadline is 15 s
            val fields=JSONObject().put("version",1).put("type","relay_attach").put("sessionToken",active.token)
                .put("publicIdentity",device).put("targetAccountId",targetAccount.toString()).put("targetPublicIdentity",targetDevice)
            if(diagnosticAttempt>0)fields.put("diagnosticAttempt",diagnosticAttempt.toString())
            archive?.keys()?.forEach{key->require(key in (if(group)setOf("purpose","groupId","rosterGeneration")else setOf("purpose","archiveIssuer","archiveSource","archiveReader","archiveSourceGeneration","archiveReaderGeneration","archiveSourceGrantId","archiveReaderGrantId")));fields.put(key,archive.get(key))}
            channel.write(fields.toString().toByteArray(Charsets.UTF_8),4096)
            val response=JSONObject(channel.read(16384).toString(Charsets.UTF_8))
            if(!response.optBoolean("ok"))throw ServerRejected(response.optString("error","relay_rejected"))
            check(session===active && response.getString("type")=="relay_ready" && response.getString("purpose")== (if(group)"group_key" else if(archive==null)"friend" else "archive_history"))
            archive?.keys()?.forEach{key->check(response.getString(key)==archive.getString(key))}
            if(!group)check(response.getString("groupId").isEmpty() && response.getLong("rosterGeneration")==0L)
            val id=response.getString("sessionId");com.peerchat.android.platform.tls.RelayFrames(id)
            val incoming=response.getBoolean("incoming");channel.readTimeout(0)
            return RelayConnection(channel,id,incoming)
        } catch(failure: Exception){channel.close();throw failure}
    }
    fun logout() { try { if(session != null) request("logout") } finally { closeRpc();session=null } }
    fun openEvents(): ServerChannel = openSubscription("events",JSONObject().put("clientHeartbeats",true))
    // Opaque mailbox application RPC over the existing authenticated/pinned
    // server TLS request adapter. No plaintext, local outbox, or retry owner here.
    fun archive(operation:String,payload:JSONObject=JSONObject()):JSONObject=stages.measure(operation){archiveMeasured(operation,payload)}
    private fun archiveMeasured(operation: String, payload: JSONObject): JSONObject {
        require(operation in setOf("archive_descriptor_register","archive_grant_register","archive_authorization_state"))
        check(supportsRpc&&rpcAuthorized&&session!=null) { "Archive requires authenticated RPC support" }
        return rpc(operation,payload)
    }
    /** Existing authenticated, device-bound TLS RPC only. Native code owns the
     * descriptor, encrypted file, frozen recipients, offsets and retry journal. */
    fun groupAttachmentCache(operation:String,payload:JSONObject):JSONObject=stages.measure(operation){
        require(operation in setOf("cache_capability","cache_admit","cache_status","cache_context","cache_acquire","cache_append","cache_complete","cache_read","cache_progress","cache_release","cache_recipient_keys","cache_publish_key","cache_pending_keys","cache_key_ack","cache_verified","cache_receipts"))
        check(supportsRpc&&rpcAuthorized&&session!=null){"Group attachments require authenticated device RPC"}
        val result=rpc(operation,payload)
        require(result.getString("type")=="cache_result"&&result.getString("operation")==operation)
        result
    }
    fun directAttachmentCache(operation:String,payload:JSONObject):JSONObject=stages.measure(operation){
        require(operation in setOf("cache_dm_capability","cache_dm_register","cache_dm_prepare","cache_dm_admit",
            "cache_dm_status","cache_dm_context","cache_dm_acquire","cache_dm_append",
            "cache_dm_complete","cache_dm_read","cache_dm_progress","cache_dm_release","cache_dm_publish",
            "cache_dm_verified","cache_dm_receipts"))
        check(supportsRpc&&rpcAuthorized&&session!=null){"Direct attachment cache requires authenticated device RPC"}
        val active=checkNotNull(session)
        val result=retryClosedCacheRead(operation,{
            operations.check()
            check(session===active&&supportsRpc&&rpcAuthorized)
        }){rpc(operation,payload)}
        require(result.getString("type")=="cache_result"&&result.getString("operation")==operation&&
            (result.getInt("cacheVersion")==2||operation=="cache_dm_capability"&&result.getInt("cacheVersion")==1))
        check(session===active)
        if(operation=="cache_dm_capability")dmAppendBatchCapability=active to
            (if(result.optString("state")=="accepted"&&result.optInt("cacheVersion")==2)result.optInt("dmAppendBatchMaxChunks",0).coerceIn(0,BoundedRpc.MAX_WINDOW) else 0)
        result
    }
    fun mailbox(operation: String, payload: JSONObject): JSONObject =stages.measure(operation){mailboxMeasured(operation,payload)}
    private fun mailboxMeasured(operation:String,payload:JSONObject):JSONObject {
        require(operation in setOf("mailbox_register","mailbox_revoke","mailbox_keys","mailbox_store","mailbox_fetch","mailbox_ack","mailbox_receipts","mailbox_eligibility_snapshot","mailbox_eligibility_validate"))
        if(supportsRpc&&rpcAuthorized)return rpc(operation,payload)
        val requestId=java.util.UUID.randomUUID().toString().replace("-","")
        val connection=channel("mailbox")
        try {
            connection.readTimeout(10000)
            val hello=JSONObject().put("version",1).put("type","mailbox_rpc").put("requestId",requestId)
                .put("sessionToken",checkNotNull(session).token).put("publicIdentity",device)
            connection.write(hello.toString().toByteArray(Charsets.UTF_8),4096)
            val ready=JSONObject(connection.read(16384).toString(Charsets.UTF_8))
            if(!ready.optBoolean("ok"))throw ServerRejected(ready.optString("error","mailbox_rejected"))
            check(ready.getString("type")=="mailbox_ready"&&ready.getString("requestId")==requestId)
            payload.put("type",operation).put("requestId",requestId)
            connection.write(payload.toString().toByteArray(Charsets.UTF_8),100000)
            connection.readTimeout(10000)
            val reply=JSONObject(connection.read(100000).toString(Charsets.UTF_8))
            check(reply.getString("type")=="mailbox_result"&&reply.getString("requestId")==requestId&&reply.getString("operation")==operation)
            return reply
        } finally {connection.close()}
    }
    private fun openSubscription(type:String,fields:JSONObject):ServerChannel=stages.measure(type){openSubscriptionMeasured(type,fields)}
    private fun openSubscriptionMeasured(type: String, fields: JSONObject): ServerChannel {
        val channel=channel("events")
        try {
            fields.put("version",1).put("type",type).put("sessionToken",checkNotNull(session).token).put("publicIdentity",device)
            channel.write(fields.toString().toByteArray(Charsets.UTF_8),4096)
            val ready=JSONObject(channel.read(100000).toString(Charsets.UTF_8))
            if(!ready.optBoolean("ok"))throw ServerRejected(ready.optString("error","subscription_failed"))
            check(ready.getString("type")=="events_ready") { "Unexpected subscription response" }
            presenceEpoch=ready.optString("serverEpoch")
            channel.readTimeout(0)
            return channel
        } catch(failure: Exception){channel.close();throw failure}
    }
}
// No generated toString: never stringify transport/session data.
class RelayConnection(val channel: ServerChannel,val sessionId: String,val incoming: Boolean)
class ServerRejected(val code: String) : Exception("Server request rejected: " +
    code.take(80).filter { it.isLetterOrDigit() || it=='_' })

/** Optional labels cannot suppress a fresh friendship/revocation snapshot. */
internal fun friendStateWithOptionalNames(request:(JSONObject)->JSONObject):JSONObject = try {
    request(JSONObject().put("deviceNamesVersion",1))
}catch(failure:ServerRejected){
    if(failure.code !in setOf("device_names_response_too_large","device_names_unavailable"))throw failure
    request(JSONObject())
}

internal fun validateMessagingBootstrap(response:JSONObject,account:Long,device:String):JSONObject {
    require(response.getInt("version")==1&&response.getInt("bootstrapVersion")==1)
    require(response.getLong("accountId")==account&&response.getString("registeredPublicIdentity")==device)
    require(response.getBoolean("presenceLive")&&response.getBoolean("relayOnlyPresence"))
    val state=response.getJSONObject("friendState")
    for(name in listOf("friends","incoming","outgoing","blocked"))require(state.getJSONArray(name).length()<=128)
    return state
}
