package com.peerchat.android.application

/** Bounded process-local timing diagnostics. No identities, response bodies or
 * exception messages; measurements never authorize or retry an operation. */
internal enum class OwnerStage { GateWait,Tick,Contacts,Groups,Mailbox,Eligibility,Archive,NativeRelay,Projection,LocalPage,LocalIndex,ProfileOpen,SendWait,LocalCommit,UiLocalListFrame,UiChatFrame,UiSendFrame,UiDurableReturn }
internal class OwnerStageTimings(private val clock:()->Long=System::nanoTime,
    private val slow:(String,Long)->Unit={_,_->}) {
    private data class Cost(var calls:Long=0,var last:Long=0,var maximum:Long=0,var failures:Long=0)
    private val costs=java.util.EnumMap<OwnerStage,Cost>(OwnerStage::class.java)
    fun record(stage:OwnerStage,nanos:Long,failed:Boolean=false){
        val elapsed=nanos.coerceAtLeast(0)/1_000_000
        synchronized(costs){val row=costs.getOrPut(stage){Cost()};row.calls++;row.last=elapsed;row.maximum=maxOf(row.maximum,elapsed);if(failed)row.failures++}
        if(elapsed>=250)try{slow(stage.name,elapsed)}catch(_:RuntimeException){}
    }
    fun <T> measure(stage:OwnerStage,operation:()->T):T {
        val started=clock();var failed=true
        try{return operation().also{failed=false}}finally{record(stage,clock()-started,failed)}
    }
    fun summary():String=synchronized(costs){costs.entries.joinToString("\n"){(stage,row)->
        "owner_stage=${stage.name} calls=${row.calls} last_ms=${row.last} max_ms=${row.maximum} failures=${row.failures}"
    }}
}
