package com.peerchat.android.application

import kotlinx.coroutines.delay
import kotlinx.coroutines.withTimeout

/** A room revision can advance between consent and admission. Retry that one
 * conflict only; the owner must revalidate the authenticated scope and roster.
 * Both attempts share a deadline and cancellation remains cooperative. */
internal suspend fun awaitGroupVoiceJoin(initial:String,result:(String)->String,retry:()->String):String = withTimeout(15_000) {
    var request=initial
    var retried=false
    while(true) {
        val state=result(request)
        when {
            state=="pending"->delay(50)
            state=="stale_room_state"&&!retried->{retried=true;request=retry()}
            else->return@withTimeout state
        }
    }
    @Suppress("UNREACHABLE_CODE")
    "unavailable"
}
