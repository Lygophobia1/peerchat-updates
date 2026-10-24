package com.peerchat.android.application

import kotlinx.coroutines.*
import org.junit.Assert.*
import org.junit.Test

class ExplicitAttachmentPreparationTest {
    private val id="b".repeat(32)
    @Test fun lifetimeBeginsBeforeSlowWorkAndHandsOffExactNativeId()=runBlocking {
        val order=mutableListOf<String>()
        val result=explicitAttachmentPreparation(begin={order+="begin";"token"},
            finish={token,queued->assertEquals("token",token);assertEquals(id,queued);order+="finish"},
            cancelQueued={fail("unexpected cancellation")}){queued->
            order+="prepare";delay(5);queued(id);order+="native";id
        }
        assertEquals(id,result);assertEquals(listOf("begin","prepare","native","finish"),order)
    }
    @Test fun notificationCancellationBeforeAdmissionCannotInventAQueuedId()=runBlocking {
        var cancel:(()->Unit)?=null;var finished=false;val started=CompletableDeferred<Unit>()
        val job=launch {
            explicitAttachmentPreparation(begin={cancel=it;"token"},
                finish={_,queued->assertNull(queued);finished=true},cancelQueued={fail()}){_->started.complete(Unit);awaitCancellation()}
        }
        started.await();checkNotNull(cancel).invoke();job.join();assertTrue(finished)
    }
    @Test fun cancellationAfterAdmissionCancelsOnlyObservedNativeTransfer()=runBlocking {
        var cancel:(()->Unit)?=null;var cancelledId:String?=null;var finishedId:String?=null
        val admitted=CompletableDeferred<Unit>()
        val job=launch {
            explicitAttachmentPreparation(begin={cancel=it;"token"},finish={_,queued->finishedId=queued},
                cancelQueued={cancelledId=it}){queued->queued(id);admitted.complete(Unit);awaitCancellation()}
        }
        admitted.await();checkNotNull(cancel).invoke();job.join()
        assertEquals(id,cancelledId);assertEquals(id,finishedId)
    }
    @Test fun preparationFailureClosesLifetimeWithoutDeliveryClaim()=runBlocking {
        var closed=false
        try {explicitAttachmentPreparation(begin={"token"},finish={_,id->assertNull(id);closed=true},cancelQueued={fail()}){_->error("denied")};fail()}
        catch(_:IllegalStateException){}
        assertTrue(closed)
    }
    @Test fun ambiguousFailureCarriesExactAdmittedIdAndDoesNotRetry()=runBlocking {
        var admissions=0;var finalId:String?=null
        try {
            explicitAttachmentPreparation(begin={"token"},finish={_,id->finalId=id},cancelQueued={fail()}){queued->
                admissions++;queued(id);error("projection failed after durable admission")
            }
            fail()
        }catch(failure:QueuedAttachmentFailure){assertEquals(id,failure.transferId)}
        assertEquals(1,admissions);assertEquals(id,finalId)
    }

}
