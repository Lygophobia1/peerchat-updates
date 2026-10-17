package com.peerchat.android.application

/** One existing owner temporarily borrows transport outside its profile gate.
 * This is neither a worker nor a queue; native access and application callbacks
 * are still serialized and checked against the captured authenticated scope. */
internal class ScopedBackendWork(private val gate:Any,private val current:()->Boolean) {
    internal class Identity(private val profile:Any,private val owner:Any,private val session:Any,
                            private val account:Long,private val generation:Long){
        fun matches(profile:Any?,owner:Any?,session:Any?,account:Long,generation:Long):Boolean=
            profile===this.profile&&owner===this.owner&&session===this.session&&account==this.account&&generation==this.generation
    }
    internal class Superseded:IllegalStateException("Backend operation scope changed")
    fun <T> local(operation:()->T):T=synchronized(gate){
        if(!current())throw Superseded()
        operation()
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
