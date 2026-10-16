package com.peerchat.android.application

/** Shared boundary between native durable import, application projection and
 * wake completion. The backend supplies its real native-history projector. */
internal class MailboxPublicationGate(private val publish:(Set<String>)->Unit,
    private val now:()->Long=System::nanoTime) {
    private val fresh=linkedSetOf<String>()
    private var acknowledgedAt=0L
    private var serviceAt=0L
    fun freshIds():Set<String> = fresh.toSet()
    fun committed(messageId:String) {
        fresh.add(messageId)
        while(fresh.size>32)fresh.remove(fresh.first())
        publish(freshIds())
    }
    fun acknowledged(){acknowledgedAt=now()}
    fun servicePublished(){serviceAt=now()}
    fun outputDrained(afterNanos:Long,needsContinuation:Boolean):Boolean =
        !needsContinuation&&(acknowledgedAt>=afterNanos||serviceAt>=afterNanos)
    fun reset(){fresh.clear();acknowledgedAt=0L;serviceAt=0L}
}
