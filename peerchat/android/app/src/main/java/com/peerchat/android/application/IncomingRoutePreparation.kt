package com.peerchat.android.application

/** Reuses the call preparation fast path for authenticated incoming rendezvous.
 * No routes, trust, retry decisions or microphone consent are owned here.
 * Repeated hints cannot extend a window and all work resumes at its deadline. */
internal class IncomingRoutePreparation {
    private data class Window(var until:Long,val retryAfter:Long)
    private val peers=linkedMapOf<String,Window>()
    @Synchronized fun begin(value:String,now:Long){
        peers.entries.removeAll{now>=it.value.retryAfter}
        if(value in peers||peers.size>=32)return
        peers[value]=Window(now+10_000,now+15_000)
    }
    @Synchronized fun pending(now:Long)=peers.values.any{now<it.until}
    @Synchronized fun ready(value:String){peers[value]?.until=0}
    @Synchronized fun clear(){peers.clear()}
}
