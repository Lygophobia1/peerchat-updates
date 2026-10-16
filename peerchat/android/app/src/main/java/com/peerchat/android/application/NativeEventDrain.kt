package com.peerchat.android.application

/** Stop at the first empty native queue. Each read also services native work;
 * empty reads are not free and must not consume all sixteen drain slots. */
internal inline fun drainNativeEvents(read:()->ByteArray?,consume:(ByteArray)->Unit) {
    repeat(16) {
        val event=read()?:return
        if(event.isEmpty())return
        consume(event)
    }
}
