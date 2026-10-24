package com.peerchat.android.application

import com.peerchat.android.platform.AttachmentSendMode
import kotlinx.coroutines.ensureActive

class AttachmentReadinessUnavailable:IllegalStateException("Attachment connection is not ready. Reconnect and retry; no attachment was sent.")

/** Direct needs an authenticated scoped peer, never cache registration or cache capability. */
internal fun <T:Any> attachmentModeReadiness(mode:AttachmentSendMode,scope:()->T?,cacheReady:(T)->Boolean):T? {
    val current=scope()?:return null
    return if(mode==AttachmentSendMode.Direct||cacheReady(current))current else null
}

/** Bounded wait only for an explicit user selection, never a background poller.
 * The probe must recheck the selected profile/account and may reject supersession.
 * It must not perform network I/O or fabricate authorization while waiting. */
internal suspend fun <T:Any> awaitAttachmentReadiness(
    probe:()->T?,clock:()->Long=System::nanoTime,
    wait:suspend()->Unit={kotlinx.coroutines.delay(100)},timeoutNanos:Long=30_000_000_000L,
    progress:()->Long={0L},maximumNanos:Long=timeoutNanos
):T {
    require(timeoutNanos>0&&maximumNanos>=timeoutNanos)
    val began=clock();var advancedAt=began;var observed=progress()
    while(true){
        kotlinx.coroutines.currentCoroutineContext().ensureActive()
        probe()?.let{return it}
        val now=clock();val current=progress()
        // Successful enrollment steps may span a busy cold start. Mere polling,
        // receipt traffic and repeated network failures cannot extend the wait.
        if(current!=observed){observed=current;advancedAt=now}
        if(now-began>=maximumNanos||now-advancedAt>=timeoutNanos)throw AttachmentReadinessUnavailable()
        wait()
    }
}
