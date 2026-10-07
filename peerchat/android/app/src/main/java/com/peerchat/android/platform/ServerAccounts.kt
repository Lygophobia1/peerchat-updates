package com.peerchat.android.platform

import com.peerchat.android.platform.tls.ServerChannel
import org.json.JSONObject

data class ServerEndpoint(val host: String, val port: Int, val certificatePin: String = "")
data class AccountSession(val accountId: Long, val username: String, internal val token: String, val relayOnly: Boolean,val bootstrapVersion:Int=0) {
    override fun toString() = "AccountSession(accountId=$accountId, username=$username, relayOnly=$relayOnly)"
}

// Counterpart of Desktop DiscoveryClient, not a native session/delivery owner.
// Passwords/tokens are never logged. A session remains scoped to this profile.
class ServerAccounts(val endpoint: ServerEndpoint, val device: String,
    private val operations:ServerChannel.OperationScope=ServerChannel.OperationScope()) {
    @Volatile var session: AccountSession? = null
        private set
    @Volatile internal var presenceEpoch=""
        private set
    init { require(device.matches(Regex("[0-9a-f]{64}"))) }
    private fun safeOperation(type:String)=if(type in setOf("remember_challenge","remember_resume","remember_revoke","remember_enroll_challenge","remember_enroll","session_info","presence","friend_state","messaging_bootstrap","push_register","push_hint","login","logout","register","search","friend_request","friend_respond"))type else "other"
    private fun channel(operation:String)=ServerChannel(endpoint.host,endpoint.port,endpoint.certificatePin,false,operations) {stage,elapsed->
        android.util.Log.i("PeerChatWake","stage=$stage operation=$operation "+(if(stage=="crypto_provider_initializations")"count=$elapsed" else "elapsed_ms=$elapsed"))
    }
    fun request(type: String, fields: JSONObject = JSONObject(), authenticated: Boolean = true): JSONObject {
        operations.check()
        val started=System.nanoTime()
        android.util.Log.i("PeerChatWake","stage=server_request_started operation="+safeOperation(type))
        fields.put("version", 1).put("type", type)
        if(authenticated) fields.put("sessionToken", checkNotNull(session).token)
        val bytes = fields.toString().toByteArray(Charsets.UTF_8)
        require(bytes.size <= 4096)
        val response=try {channel(safeOperation(type)).use { channel ->
            channel.write(bytes, 4096)
            android.util.Log.i("PeerChatWake","stage=server_request_written operation="+safeOperation(type)+" elapsed_ms="+(System.nanoTime()-started)/1_000_000)
            JSONObject(channel.read(16384).toString(Charsets.UTF_8))
        }} finally {android.util.Log.i("PeerChatWake","stage=server_request_returned operation="+safeOperation(type)+" elapsed_ms="+(System.nanoTime()-started)/1_000_000)}
        if(!response.optBoolean("ok")) throw ServerRejected(response.optString("error", "request_failed"))
        return response
    }
    private fun accept(response: JSONObject): AccountSession {
        val next = AccountSession(response.getLong("accountId"), response.getString("username"),
            response.getString("sessionToken"), response.optBoolean("relayOnlyPresence"),response.optInt("messagingBootstrapVersion",0))
        check(next.accountId > 0 && next.token.isNotEmpty() && next.relayOnly) { "Server lacks required relay-only capability" }
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
    fun friends(): JSONObject = request("friend_state")
    fun bootstrap():JSONObject {
        val active=checkNotNull(session)
        if(active.bootstrapVersion!=1){presence();return friends()} // Existing servers, same fail-closed checks.
        val response=request("messaging_bootstrap",JSONObject().put("publicIdentity",device).put("bootstrapVersion",1))
        return validateMessagingBootstrap(response,active.accountId,device)
    }
    fun registerPush(token:String) {require(token.length in 1..2048);request("push_register",JSONObject().put("publicIdentity",device).put("provider","fcm").put("pushToken",token))}
    fun hint(peerAccount:Long,peerDevice:String){request("push_hint",JSONObject().put("publicIdentity",device).put("targetAccountId",peerAccount.toString()).put("targetPublicIdentity",peerDevice))}
    fun openRelay(targetAccount: Long,targetDevice: String): RelayConnection {
        require(targetAccount>0 && targetDevice.matches(Regex("[0-9a-f]{64}")))
        val active=checkNotNull(session)
        require(active.accountId!=targetAccount)
        val channel=channel("relay_attach")
        try {
            channel.readTimeout(20000) // existing server pairing deadline is 15 s
            val fields=JSONObject().put("version",1).put("type","relay_attach").put("sessionToken",active.token)
                .put("publicIdentity",device).put("targetAccountId",targetAccount.toString()).put("targetPublicIdentity",targetDevice)
            channel.write(fields.toString().toByteArray(Charsets.UTF_8),4096)
            val response=JSONObject(channel.read(16384).toString(Charsets.UTF_8))
            if(!response.optBoolean("ok"))throw ServerRejected(response.optString("error","relay_rejected"))
            check(session===active && response.getString("type")=="relay_ready" && response.getString("purpose")=="friend" &&
                response.getString("groupId").isEmpty() && response.getLong("rosterGeneration")==0L)
            val id=response.getString("sessionId");com.peerchat.android.platform.tls.RelayFrames(id)
            val incoming=response.getBoolean("incoming");channel.readTimeout(0)
            return RelayConnection(channel,id,incoming)
        } catch(failure: Exception){channel.close();throw failure}
    }
    fun logout() { try { if(session != null) request("logout") } finally { session=null } }
    fun openEvents(): ServerChannel = openSubscription("events",JSONObject().put("clientHeartbeats",true))
    // Opaque mailbox application RPC over the existing authenticated/pinned
    // server TLS request adapter. No plaintext, local outbox, or retry owner here.
    fun mailbox(operation: String, payload: JSONObject): JSONObject {
        require(operation in setOf("mailbox_register","mailbox_revoke","mailbox_keys","mailbox_store","mailbox_fetch","mailbox_ack","mailbox_receipts"))
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
    private fun openSubscription(type: String, fields: JSONObject): ServerChannel {
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

internal fun validateMessagingBootstrap(response:JSONObject,account:Long,device:String):JSONObject {
    require(response.getInt("version")==1&&response.getInt("bootstrapVersion")==1)
    require(response.getLong("accountId")==account&&response.getString("registeredPublicIdentity")==device)
    require(response.getBoolean("presenceLive")&&response.getBoolean("relayOnlyPresence"))
    val state=response.getJSONObject("friendState")
    for(name in listOf("friends","incoming","outgoing","blocked"))require(state.getJSONArray(name).length()<=128)
    return state
}
