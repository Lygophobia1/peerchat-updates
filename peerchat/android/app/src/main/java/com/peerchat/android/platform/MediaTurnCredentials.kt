package com.peerchat.android.platform

import org.json.JSONObject

/** Short-lived authenticated route input. Never persisted or projected into UI. */
class MediaTurnCredentials internal constructor(
    internal val host:String,internal val port:Int,internal val username:String,
    internal val password:String,internal val expiresUtc:Long
) {
    internal fun usableAt(nowUtc:Long):Boolean = nowUtc>=0 && expiresUtc>nowUtc && expiresUtc-nowUtc>30
    override fun toString()="MediaTurnCredentials(redacted)"
}

internal fun parseMediaTurnCredentials(value:JSONObject,nowUtc:Long):MediaTurnCredentials {
    fun text(name:String,maximum:Int):String {
        val result=value.opt(name) as? String ?: throw IllegalArgumentException("Invalid media credential response")
        require(result.isNotEmpty()&&result.toByteArray(Charsets.UTF_8).size<=maximum&&result.none{it.code<0x21||it.code==0x7f}) {"Invalid media credential response"}
        return result
    }
    fun integer(name:String):Long = when(val raw=value.opt(name)) {
        is Int->raw.toLong()
        is Long->raw
        else->throw IllegalArgumentException("Invalid media credential response")
    }
    require(value.opt("ok")==true&&integer("version")==1L) {"Invalid media credential response"}
    require(text("transport",3).equals("udp",ignoreCase=true)) {"Unsupported media transport"}
    val host=text("host",253)
    // The existing native TURN URI owns credential escaping. Host is a host,
    // never a user-info, path, query or an alternate URI supplied by a caller.
    require(host.matches(Regex("[A-Za-z0-9._-]+|\\[[A-Fa-f0-9:]+\\]"))) {"Invalid media host"}
    val port=integer("port");require(port in 1..65535) {"Invalid media port"}
    val result=MediaTurnCredentials(host,port.toInt(),text("username",1024),text("password",4096),integer("expiresUtc"))
    require(result.usableAt(nowUtc)) {"Media credentials expired or too close to expiry"}
    return result
}
