package com.peerchat.android.application

/** Parks idle relay output during passive grace without delaying receive-driven output. */
internal class RelayWorkSignal {
    private val monitor=java.lang.Object()
    private var revision=0L
    private var enabled=true
    private var closed=false
    val active:Boolean get()=synchronized(monitor){enabled}
    fun revision():Long=synchronized(monitor){revision}
    fun setActiveWork(value:Boolean)=synchronized(monitor){
        enabled=value
        revision++
        monitor.notifyAll()
    }
    fun signal()=synchronized(monitor){revision++;monitor.notifyAll()}
    fun awaitPassive(observed:Long)=synchronized(monitor){
        while(!closed&&!enabled&&revision==observed)monitor.wait()
    }
    fun shutdown()=synchronized(monitor){closed=true;revision++;monitor.notifyAll()}
}
