package com.peerchat.android.application

import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.*

internal class QueuedAttachmentFailure(val transferId:String,cause:Exception):
    IllegalStateException("Attachment was queued; inspect its existing transfer",cause)
internal class QueuedAttachmentCancellation(val transferId:String,cause:CancellationException):
    CancellationException("Queued attachment cancellation requested") {init{initCause(cause)}}

/** Observe native admission before a dispatcher handoff can be cancelled. This
 * never retries admission or invents a native ID; cancellation retires only the
 * exact operation already admitted by this explicit send. */
internal suspend fun <T> explicitAttachmentPreparation(
    begin:((()->Unit)->String?),finish:(String?,String?)->Unit,
    cancelQueued:suspend(String)->Unit,command:suspend((String)->Unit)->T
):T=coroutineScope {
    val preparationJob=currentCoroutineContext().job
    val queued=AtomicReference<String?>()
    val token=begin { preparationJob.cancel(CancellationException("Attachment preparation cancelled")) }
    var cancelledCommand=false
    try {command {id->require(id.matches(Regex("[0-9a-f]{32}")));queued.set(id)}}
    catch(cancelled:CancellationException){
        cancelledCommand=true
        val id=queued.get();if(id!=null)throw QueuedAttachmentCancellation(id,cancelled) else throw cancelled
    }
    catch(failure:Exception){
        val id=queued.get();if(id!=null)throw QueuedAttachmentFailure(id,failure) else throw failure
    }
    finally {
        val id=queued.get()
        try {
            if((cancelledCommand||!currentCoroutineContext().isActive)&&id!=null)withContext(NonCancellable){cancelQueued(id)}
        } finally {finish(token,id)}
    }
}
