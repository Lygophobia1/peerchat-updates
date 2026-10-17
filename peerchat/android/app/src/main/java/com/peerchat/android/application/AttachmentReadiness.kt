package com.peerchat.android.application

import kotlinx.coroutines.ensureActive

class AttachmentReadinessUnavailable:IllegalStateException("Attachment connection is not ready. Reconnect and retry; no attachment was sent.")

/** Bounded wait only for an explicit user selection, never a background poller.
 * The probe must recheck the selected profile/account and may reject supersession.
 * It must not perform network I/O or fabricate authorization while waiting. */
internal suspend fun <T:Any> awaitAttachmentReadiness(
    probe:()->T?,clock:()->Long=System::nanoTime,
    wait:suspend()->Unit={kotlinx.coroutines.delay(100)},timeoutNanos:Long=30_000_000_000L
):T {
    val began=clock()
    while(true){
        kotlinx.coroutines.currentCoroutineContext().ensureActive()
        probe()?.let{return it}
        if(clock()-began>=timeoutNanos)throw AttachmentReadinessUnavailable()
        wait()
    }
}
