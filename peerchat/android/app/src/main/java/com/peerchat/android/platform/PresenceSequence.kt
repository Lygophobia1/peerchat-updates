package com.peerchat.android.platform

/** Event projection guard only: server owns presence; this owns no lease or route. */
internal class PresenceSequence {
    private var epoch=""
    private val revisions=mutableMapOf<Long,Long>()
    fun reset(serverEpoch:String){epoch=serverEpoch;revisions.clear()}
    fun accept(serverEpoch:String,account:Long,revision:Long):Boolean {
        if(epoch.isEmpty())return serverEpoch.isEmpty() // Older server compatibility only.
        if(serverEpoch!=epoch||account<=0||revision<=0||revision<=(revisions[account]?:0L))return false
        revisions[account]=revision
        return true
    }
}
