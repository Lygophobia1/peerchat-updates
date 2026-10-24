package com.peerchat.android.application

/** A finite sequence of bounded local turns, independent of network waits.
 * step owns its scope/gate; pause must run after that ownership is released. */
internal fun runLocalPreparationBurst(step:()->Boolean,pause:()->Unit,limit:Int=32):Int {
    require(limit in 1..32)
    var completed=0
    while(completed<limit) {
        if(!step())break
        completed++
        if(completed<limit)pause()
    }
    return completed
}

// Active media retains the original preparation cadence as well as call setup.
internal fun mayAccelerateLocalPreparation(preparingCall:Boolean,activeCall:Boolean,groupCall:Boolean)=
    !preparingCall&&!activeCall&&!groupCall
