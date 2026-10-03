package com.peerchat.android.platform

import android.content.Context
import org.json.JSONObject

/** Vendor token boundary. A configured vendor adapter may replace the token;
 * no vendor SDK/configuration or service credential is shipped by default. */
class AndroidPushRegistration(context:Context,profile:String="default") {
    private val state=ProfileSecurity(context.applicationContext,profile)
    private val records by lazy{AndroidRecordProtector(context.applicationContext,profile)}
    private val label="push-token-v1".toByteArray(Charsets.US_ASCII)
    fun replaceFcmToken(token:String) {
        require(token.length in 1..2048&&token.all{it.code in 33..126})
        val blob=records.protect(JSONObject().put("version",1).put("provider","fcm").put("token",token).toString().toByteArray(Charsets.UTF_8),label)
        state.locked{state.write("push-token.pcr",blob)}
    }
    internal fun token():String? {
        val blob=state.locked{state.read("push-token.pcr")} ?: return null
        require(blob.size in 1..4096)
        val value=JSONObject(records.unprotect(blob,label).toString(Charsets.UTF_8))
        require(value.getInt("version")==1&&value.getString("provider")=="fcm")
        return value.getString("token").also{require(it.length in 1..2048&&it.all{c->c.code in 33..126})}
    }
}
