package com.peerchat.android.platform

import com.peerchat.android.platform.tls.ServerChannel
import com.peerchat.android.platform.tls.BoundedRpc
import org.json.JSONObject

data class ServerEndpoint(val host: String, val port: Int, val certificatePin: String = "")
data class AccountSession(val accountId: Long, val username: String, internal val token: String, val relayOnly: Boolean,val bootstrapVersion:Int=0) {
    override fun toString() = "AccountSession(accountId=$accountId, username=$username, relayOnly=$relayOnly)"
}

private val deliveryRpcOperations=setOf("mailbox_register","mailbox_revoke","mailbox_keys",
    "mailbox_store","mailbox_fetch","mailbox_ack","mailbox_receipts")
internal fun deliveryRpcOperation(type:String)=type in deliveryRpcOperations

internal class RpcChannelPair<T:AutoCloseable>(ordinaryFactory:()->T,deliveryFactory:()->T,
    available:(T)->Boolean):AutoCloseable {
    private val ordinary=BoundedRpc<T>({ordinaryFactory()},{available(it)})
    private val delivery=BoundedRpc<T>({deliveryFactory()},{available(it)})
    private fun owner(type:String)=if(deliveryRpcOperation(type))delivery else ordinary
    fun <R> run(type:String,binding:String,operation:(T)->R):R=owner(type).run(binding){operation(it)}
    fun closeSelected(type:String)=owner(type).close()
    override fun close(){delivery.close();ordinary.close()}
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
    private val rpcOwners=RpcChannelPair({openRpcChannel("rpc")},{openRpcChannel("delivery_rpc")},{it.isOpen})
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
            connection.write(request.toString().toByteArray(Charsets.UTF_8),if(type.startsWith("mailbox_")||type.startsWith("cache_dm_"))100000 else 4096)
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
    init { require(device.matches(Regex("[0-9a-f]{64}"))) }
    private fun safeOperation(type:String)=if(type in setOf("remember_challenge","remember_resume","remember_revoke","remember_enroll_challenge","remember_enroll","session_info","presence","friend_state","messaging_bootstrap","push_register","push_hint","login","logout","register","search","people_search","friend_request","friend_respond","friend_cancel"))type else "other"
    private fun channel(operation:String)=ServerChannel(endpoint.host,endpoint.port,endpoint.certificatePin,false,operations) {stage,elapsed->
        android.util.Log.i("PeerChatWake","stage=$stage operation=$operation "+(if(stage=="crypto_provider_initializations")"count=$elapsed" else "elapsed_ms=$elapsed"))
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
        closeRpc();rpcAuthorized=false;ownMetadataPublished=false;supportsRpc=response.optInt("rpcVersion")==1
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
    fun directAttachmentCache(operation:String,payload:JSONObject):JSONObject=stages.measure(operation){
        require(operation in setOf("cache_dm_capability","cache_dm_register","cache_dm_prepare","cache_dm_admit",
            "cache_dm_status","cache_dm_context","cache_dm_acquire","cache_dm_append",
            "cache_dm_complete","cache_dm_read","cache_dm_progress","cache_dm_release","cache_dm_publish",
            "cache_dm_verified","cache_dm_receipts"))
        check(supportsRpc&&rpcAuthorized&&session!=null){"Direct attachment cache requires authenticated device RPC"}
        val result=rpc(operation,payload)
        require(result.getString("type")=="cache_result"&&result.getString("operation")==operation&&
            (result.getInt("cacheVersion")==2||operation=="cache_dm_capability"&&result.getInt("cacheVersion")==1))
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
