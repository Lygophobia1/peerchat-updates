package com.peerchat.android.application

import org.json.JSONObject

/** A disposable choice, not transfer authority or another outbox. */
data class AttachmentCompatibilityChoice(val token:String,val name:String,val directReady:Boolean,val device:String,
    val preflightTransportUnavailable:Boolean=false,val cachedReady:Boolean=false,val selectedDirect:Boolean=false) {
    init{require(token.matches(Regex("[0-9a-f]{32}"))&&device.matches(Regex("[0-9a-f]{64}")))}
}
class AttachmentCompatibilityRequired(val choice:AttachmentCompatibilityChoice):Exception("Explicit attachment mode choice required")
class AttachmentCachePreflightRejected(val state:String):Exception("Cached transfer rejected at recipient preflight: $state")
class AttachmentModeUnavailable(val reason:String):Exception(reason)

internal enum class AttachmentRecipientPreflight { Ready, UpgradeRequired }
internal enum class AttachmentSendPreflight { Cached, UpgradeChoice, TransportChoice }
/** Wraps only cache_dm_prepare, before a new cache job exists. A bounded socket
 * timeout/disconnect may offer the existing explicit direct confirmation. */
internal fun attachmentSendPreflight(recipient:Long,current:()->Boolean,
    request:(String,JSONObject)->JSONObject):AttachmentSendPreflight {
    check(current()){"Attachment authorization changed"}
    try {
        return when(attachmentRecipientPreflight(recipient,current,request)){
            AttachmentRecipientPreflight.Ready->AttachmentSendPreflight.Cached
            AttachmentRecipientPreflight.UpgradeRequired->AttachmentSendPreflight.UpgradeChoice
        }
    }catch(failure:java.net.SocketTimeoutException){
        check(current()){"Attachment authorization changed; private source retained"}
        return AttachmentSendPreflight.TransportChoice
    }catch(failure:java.net.SocketException){
        check(current()){"Attachment authorization changed; private source retained"}
        return AttachmentSendPreflight.TransportChoice
    }
}
internal suspend fun awaitAttachmentDirectRoute(probe:()->Boolean?,demand:()->Boolean,wait:suspend()->Unit={kotlinx.coroutines.delay(100)}):Boolean {
    // null is unverified/stale, never permission to initiate a connection.
    val initial=probe();if(initial!=false)return initial==true
    if(!demand())return false
    repeat(250){wait();val ready=probe();if(ready!=false)return ready==true}
    return false
}
internal fun attachmentRecipientPreflight(recipient:Long,current:()->Boolean,request:(String,JSONObject)->JSONObject):AttachmentRecipientPreflight {
    require(recipient>0);check(current()){"Attachment authorization changed"}
    // ServerAccounts' existing RPC verifies request correlation and authenticated
    // device/session ownership. Native subsequently validates a fresh audience.
    val reply=request("cache_dm_prepare",JSONObject().put("recipientAccountId",recipient))
    check(current()){"Attachment authorization changed"}
    require(reply.getString("type")=="cache_result"&&reply.getString("operation")=="cache_dm_prepare"&&reply.getInt("cacheVersion")==2)
    return when(reply.getString("state")){
        "accepted"->{require(reply.getInt("dmAttachmentVersion")==1);AttachmentRecipientPreflight.Ready}
        "cache_dm_recipient_upgrade_required"->AttachmentRecipientPreflight.UpgradeRequired
        else->throw AttachmentCachePreflightRejected(reply.getString("state"))
    }
}
