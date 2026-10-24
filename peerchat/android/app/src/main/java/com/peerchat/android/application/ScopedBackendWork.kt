package com.peerchat.android.application

/** One existing owner temporarily borrows transport outside its profile gate.
 * This is neither a worker nor a queue; native access and application callbacks
 * are still serialized and checked against the captured authenticated scope. */
internal class ScopedBackendWork(private val gate:Any,private val current:()->Boolean) {
    private var defer:()->Boolean={false}
    /** Optional work yields BETWEEN atomic operations, never while owning gate.
     * The same authenticated lifetime is rechecked on every retry. */
    fun yieldingWhile(condition:()->Boolean)=ScopedBackendWork(gate,current).also{
        it.defer={defer()||condition()}
    }
    internal class Identity(private val profile:Any,private val owner:Any,private val session:Any,
                            private val account:Long,private val generation:Long,private val lifetime:Long=0){
        fun matches(profile:Any?,owner:Any?,session:Any?,account:Long,generation:Long,lifetime:Long=0):Boolean=
            profile===this.profile&&owner===this.owner&&session===this.session&&account==this.account&&generation==this.generation&&lifetime==this.lifetime
    }
    internal class Superseded:IllegalStateException("Backend operation scope changed")
    fun <T> local(operation:()->T):T {
        while(true){
            synchronized(gate){
                if(!current())throw Superseded()
                if(!defer())return operation()
            }
            check(!Thread.holdsLock(gate)){"Deferred work must run outside the owner gate"}
            Thread.sleep(10)
        }
    }
    fun <T> network(operation:()->T):T {
        local{}
        val value=try{operation()}catch(failure:Exception){local{throw failure}}
        return local{value}
    }
    fun finish(operation:()->Unit):Boolean=synchronized(gate){
        if(!current())false else {operation();true}
    }
}

/** Fetch without holding the profile gate, then install only for that same owner. */
internal fun <T> scopedBootstrap(scope:ScopedBackendWork,fetch:()->T,install:(T)->Unit,
    installFailure:(Exception)->Unit={}) {
    val response=scope.network(fetch)
    scope.local{try{install(response)}catch(failure:Exception){installFailure(failure);throw failure}}
}
