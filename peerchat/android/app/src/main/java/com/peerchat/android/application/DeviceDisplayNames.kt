package com.peerchat.android.application

import org.json.JSONObject

/** Optional, authenticated display metadata. Invalid labels are ignored rather
 * than rejecting friendship or message delivery. Keys never add device trust. */
internal fun deviceDisplayNames(value:Any?):Map<String,String>? {
    val rows=value as? JSONObject?:return null
    if(rows.length()>64)return null
    val result=linkedMapOf<String,String>()
    rows.keys().forEach{peer->
        val label=rows.opt(peer) as? String
        if(peer.matches(Regex("[0-9a-f]{64}"))&&label!=null&&validDeviceDisplayName(label))result[peer]=label
    }
    return result.toMap()
}

internal fun validDeviceDisplayName(value:String):Boolean {
    if(value.isBlank()||value.length>64||value.toByteArray(Charsets.UTF_8).size>256||value.any{it.isISOControl()||Character.getType(it)==Character.FORMAT.toInt()})return false
    var i=0
    while(i<value.length){
        val c=value[i++]
        if(Character.isHighSurrogate(c)){if(i>=value.length||!Character.isLowSurrogate(value[i++]))return false}
        else if(Character.isLowSurrogate(c))return false
    }
    return true
}

/** Cosmetic metadata cannot advance the native authentication generation,
 * invalidate mailbox bindings or retire healthy peer routes. */
internal fun sameFriendAuthority(left:List<FriendIdentity>,right:List<FriendIdentity>):Boolean =
    left.map{it.copy(online=false,deviceNames=emptyMap())}==right.map{it.copy(online=false,deviceNames=emptyMap())}
