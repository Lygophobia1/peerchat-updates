package com.peerchat.android.application

internal enum class RelayIoStage { NativeReceive,NativeOutput,SocketRead,SocketWrite }

/** Fixed in-memory counters, read only with the existing diagnostic snapshot.
 * Socket read includes idle/network wait, not RTT. Native calls include JNI and
 * native lock wait; native diagnostics separate lock wait from processing. */
internal class RelayIoTimings(private val nanoTime:()->Long=System::nanoTime) {
    private class Sample(var count:Long=0,var total:Long=0,var max:Long=0)
    private val samples=Array(RelayIoStage.entries.size){Sample()}
    fun <T> measure(stage:RelayIoStage,action:()->T):T {
        val start=nanoTime()
        try{return action()}finally{record(stage,(nanoTime()-start).coerceAtLeast(0)/1000)}
    }
    @Synchronized private fun record(stage:RelayIoStage,microseconds:Long){
        val sample=samples[stage.ordinal];sample.count++;sample.total+=microseconds;sample.max=maxOf(sample.max,microseconds)
    }
    @Synchronized fun snapshot():String=RelayIoStage.entries.joinToString(" "){stage->
        val s=samples[stage.ordinal]
        "io_${stage.name}_samples=${s.count} io_${stage.name}_us=${s.total} io_${stage.name}_max_us=${s.max}"
    }
}
