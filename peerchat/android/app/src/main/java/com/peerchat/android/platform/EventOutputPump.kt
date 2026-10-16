package com.peerchat.android.platform

/** Bounded execution of the existing authenticated event stream's output.
 * Producers never perform socket I/O. Native group pending state remains the
 * durable authority when a failed stream reconnects; no frame is replayed here. */
internal class EventOutputPump(
    private val write:(ByteArray,Int)->Unit,
    private val failed:(Exception)->Unit,
    private val heartbeatMillis:Long=20_000,
    private val timing:(Long)->Unit={}
):AutoCloseable {
    private val gate=java.lang.Object()
    private val pending=java.util.ArrayDeque<ByteArray>()
    private var closed=false
    private val worker=Thread({run()},"PeerChat authenticated event output").apply{isDaemon=true;start()}
    fun offer(bytes:ByteArray):Boolean=synchronized(gate){
        require(bytes.size in 1..100000)
        if(closed||pending.size>=32)return@synchronized false
        pending.addLast(bytes.copyOf());gate.notifyAll();true
    }
    internal fun pendingCount():Int=synchronized(gate){pending.size}
    private fun run(){
        var heartbeatAt=0L
        try {
            while(true){
                val candidate=synchronized(gate){
                    var now=System.nanoTime()
                    while(!closed&&pending.isEmpty()&&now<heartbeatAt){
                        gate.wait(((heartbeatAt-now)/1_000_000).coerceAtLeast(1))
                        now=System.nanoTime()
                    }
                    if(closed)return
                    if(now>=heartbeatAt)null else pending.peekFirst()
                }
                val started=System.nanoTime()
                try {
                    write(candidate?:"{\"type\":\"event_heartbeat\"}".toByteArray(Charsets.UTF_8),if(candidate==null)4096 else 100000)
                } finally {try{timing((System.nanoTime()-started)/1_000_000)}catch(_:RuntimeException){}}
                if(candidate==null)heartbeatAt=System.nanoTime()+heartbeatMillis*1_000_000
                else synchronized(gate){pending.removeFirst()}
            }
        }catch(error:Exception){
            // Leave the failed/queued frames present until stream retirement.
            // The owner requeues native durable work after authenticated reconnect.
            val report=synchronized(gate){if(closed)false else{closed=true;true}}
            if(report)failed(error)
        }
    }
    override fun close(){synchronized(gate){closed=true;gate.notifyAll()};worker.interrupt()}
}
