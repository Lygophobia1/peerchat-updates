package com.peerchat.android.platform

import com.peerchat.android.platform.tls.ServerChannel
import org.json.JSONObject

data class ServerEndpoint(val host: String, val port: Int, val certificatePin: String = "")
data class AccountSession(val accountId: Long, val username: String, internal val token: String, val relayOnly: Boolean) {
    override fun toString() = "AccountSession(accountId=$accountId, username=$username, relayOnly=$relayOnly)"
}

// Counterpart of Desktop DiscoveryClient, not a native session/delivery owner.
// Passwords/tokens are never logged. A session remains scoped to this profile.
class ServerAccounts(val endpoint: ServerEndpoint, val device: String) {
    var session: AccountSession? = null
        private set
    init { require(device.matches(Regex("[0-9a-f]{64}"))) }
    fun request(type: String, fields: JSONObject = JSONObject(), authenticated: Boolean = true): JSONObject {
        fields.put("version", 1).put("type", type)
        if(authenticated) fields.put("sessionToken", checkNotNull(session).token)
        val bytes = fields.toString().toByteArray(Charsets.UTF_8)
        require(bytes.size <= 4096)
        val response = ServerChannel(endpoint.host, endpoint.port, endpoint.certificatePin, false).use { channel ->
            channel.write(bytes, 4096)
            JSONObject(channel.read(16384).toString(Charsets.UTF_8))
        }
        if(!response.optBoolean("ok")) throw ServerRejected(response.optString("error", "request_failed"))
        return response
    }
    private fun accept(response: JSONObject): AccountSession {
        val next = AccountSession(response.getLong("accountId"), response.getString("username"),
            response.getString("sessionToken"), response.optBoolean("relayOnlyPresence"))
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
    fun logout() { try { if(session != null) request("logout") } finally { session=null } }
    fun openEvents(): ServerChannel = openSubscription("events",JSONObject().put("clientHeartbeats",true))
    private fun openSubscription(type: String, fields: JSONObject): ServerChannel {
        val channel=ServerChannel(endpoint.host,endpoint.port,endpoint.certificatePin,false)
        try {
            fields.put("version",1).put("type",type).put("sessionToken",checkNotNull(session).token).put("publicIdentity",device)
            channel.write(fields.toString().toByteArray(Charsets.UTF_8),4096)
            val ready=JSONObject(channel.read(16384).toString(Charsets.UTF_8))
            if(!ready.optBoolean("ok"))throw ServerRejected(ready.optString("error","subscription_failed"))
            check(ready.getString("type")=="events_ready") { "Unexpected subscription response" }
            channel.readTimeout(0)
            return channel
        } catch(failure: Exception){channel.close();throw failure}
    }
}
class ServerRejected(val code: String) : Exception("Server request rejected: " +
    code.take(80).filter { it.isLetterOrDigit() || it=='_' })
