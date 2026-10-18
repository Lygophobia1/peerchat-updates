package com.peerchat.android.application

/** One outstanding task per purpose, with no queue of stale refresh requests.
 * The caller still fences every result with ScopedBackendWork. Keeping the lane
 * occupied until the old task exits also bounds work across login replacement. */
internal class BackendWorkLane(private val clock:()->Long=System::nanoTime) {
    private var owner:Any?=null
    private var began=0L
    private var completed=0L
    @Synchronized fun begin():Any? {
        if(owner!=null)return null
        return Any().also{owner=it;began=clock()}
    }
    @Synchronized fun finish(token:Any){if(owner===token){owner=null;completed++}}
    @Synchronized fun busy()=owner!=null
    @Synchronized fun diagnostic()="busy=${if(owner!=null)1 else 0} age_ms=${if(owner==null)0 else (clock()-began).coerceAtLeast(0)/1_000_000} completed=$completed"
}
