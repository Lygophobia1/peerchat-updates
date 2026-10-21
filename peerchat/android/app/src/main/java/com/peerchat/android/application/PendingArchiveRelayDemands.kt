package com.peerchat.android.application

/** Bounded hints only. The archive lane must refresh authorization and ask
 * native admission before opening a source route. Caller holds the owner gate. */
internal class PendingArchiveRelayDemands {
    private data class Hint(val scope:ArchiveRelayScope,val generation:Long,val expires:Long)
    private val hints=mutableListOf<Hint>()
    private fun prune(generation:Long,now:Long){hints.removeAll{it.generation!=generation||now>=it.expires}}
    fun offer(scope:ArchiveRelayScope,generation:Long,now:Long){
        prune(generation,now)
        if(generation>0&&hints.size<32&&hints.none{it.scope==scope})hints.add(Hint(scope,generation,now+15000))
    }
    fun hasPending(generation:Long,now:Long):Boolean {prune(generation,now);return hints.isNotEmpty()}
    fun take(generation:Long,now:Long):ArchiveRelayScope? {
        prune(generation,now);return if(hints.isEmpty())null else hints.removeAt(0).scope
    }
    fun clear(){hints.clear()}
}
