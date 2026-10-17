package com.peerchat.android.application

internal fun passiveEventDrainAllowed(stopped:Boolean,active:Boolean,currentSource:Boolean,deadline:Long,now:Long)=
    !stopped&&!active&&currentSource&&deadline>now

/** A process-local, monotonic deadline for passive socket retention. */
internal class BackgroundConnectionGrace(private val durationNanos:Long=60_000_000_000L) {
    private var generation=0L
    private var deadline=0L

    init { require(durationNanos>0) }

    fun begin(nowNanos:Long):Long {
        generation++
        deadline=if(nowNanos>Long.MAX_VALUE-durationNanos) Long.MAX_VALUE else nowNanos+durationNanos
        return generation
    }

    fun cancel() {
        generation++
        deadline=0L
    }

    fun deadline(nowNanos:Long):Long=if(deadline!=0L&&nowNanos<deadline)deadline else 0L

    fun remaining(expectedGeneration:Long,nowNanos:Long):Long=
        if(expectedGeneration==generation&&deadline!=0L&&nowNanos<deadline)deadline-nowNanos else 0L

    fun expire(expectedGeneration:Long,nowNanos:Long):Boolean {
        if(expectedGeneration!=generation||deadline==0L||nowNanos<deadline)return false
        cancel()
        return true
    }
}
