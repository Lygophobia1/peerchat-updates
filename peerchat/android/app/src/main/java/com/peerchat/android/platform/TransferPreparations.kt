package com.peerchat.android.platform

/** Process-local cancellation handles only; no URI, content, account or delivery state. */
internal class TransferPreparations(private val maximum:Int=64) {
    private val pending=linkedMapOf<String,()->Unit>()
    @Synchronized fun add(token:String,cancel:()->Unit):Boolean {
        if(!token.matches(Regex("[0-9a-f]{32}"))||token in pending||pending.size>=maximum)return false
        pending[token]=cancel;return true
    }
    @Synchronized fun remove(token:String):Boolean=pending.remove(token)!=null
    fun cancel(tokens:Collection<String>) {
        val callbacks=synchronized(this){tokens.mapNotNull{pending.remove(it)}}
        callbacks.forEach{it()}
    }
}
