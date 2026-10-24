package com.peerchat.android.application

import com.peerchat.android.platform.AttachmentSendMode
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ExecutionException
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitCancellation
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.*
import org.junit.Test

class AttachmentReadinessTest {
    @Test fun acceptedEnrollmentProgressSurvivesThirtySecondColdStart()=runBlocking {
        var now=0L;var progress=0L
        val result=awaitAttachmentReadiness(probe={
            progress=when{now>=37->3;now>=20->2;now>=5->1;else->0}
            if(now>=45)"registered" else null
        },clock={now},wait={now++},timeoutNanos=30,progress={progress},maximumNanos=120)
        assertEquals("registered",result);assertEquals(45L,now)
    }
    @Test fun progressCannotRemoveAbsoluteLimitAndStallsStillTimeout()=runBlocking {
        for(advancing in listOf(false,true)){
            var now=0L
            assertThrows(AttachmentReadinessUnavailable::class.java){runBlocking{
                awaitAttachmentReadiness<String>(probe={null},clock={now},wait={now++},timeoutNanos=30,
                    progress={if(advancing)now else 0},maximumNanos=120)
            }}
            assertEquals(if(advancing)120L else 30L,now)
        }
    }
    @Test fun directOnlyRequiresAuthenticatedScopeWhileCachedChecksServerCapability(){
        var cacheChecks=0
        val unsupported:(String)->Boolean={cacheChecks++;throw AttachmentModeUnavailable("cached_unsupported")}
        assertEquals("authenticated-current-peer",attachmentModeReadiness(AttachmentSendMode.Direct,
            {"authenticated-current-peer"},unsupported))
        assertEquals(0,cacheChecks)
        val failure=assertThrows(AttachmentModeUnavailable::class.java){
            attachmentModeReadiness(AttachmentSendMode.Cached,{"authenticated-current-peer"},unsupported)}
        assertEquals("cached_unsupported",failure.reason);assertEquals(1,cacheChecks)
        assertNull(attachmentModeReadiness(AttachmentSendMode.Direct,{null as String?},unsupported))
        assertEquals(1,cacheChecks)
    }
    @Test fun readinessWaitReleasesOwnerGateAndResumesAfterAcceptance()=runBlocking {
        val gate=Any()
        var accepted=false
        val waiting=CompletableDeferred<Unit>()
        val resume=CompletableDeferred<Unit>()
        val result=async(start=CoroutineStart.UNDISPATCHED) {
            awaitAttachmentReadiness(
                probe={synchronized(gate){if(accepted) "accepted" else null}},
                wait={
                    assertFalse("Readiness wait held the owner gate",Thread.holdsLock(gate))
                    waiting.complete(Unit)
                    resume.await()
                },timeoutNanos=10_000_000_000L)
        }
        try {
            withTimeout(5000){waiting.await()}
            synchronized(gate){accepted=true}
            resume.complete(Unit)
            assertEquals("accepted",withTimeout(5000){result.await()})
        }finally{resume.complete(Unit);result.cancel()}
    }

    @Test fun readinessTimeoutUsesMonotonicDeadline()=runBlocking {
        var now=100L
        var waits=0
        var probes=0
        try {
            awaitAttachmentReadiness(
                probe={probes++;null as String?},
                clock={now},
                wait={waits++;now+=5},
                timeoutNanos=10)
            fail("Unaccepted registration cannot become ready at the deadline")
        }catch(failure:IllegalStateException){
            assertTrue(failure.message.orEmpty().contains("not ready"))
        }
        assertEquals(2,waits)
        assertEquals(3,probes)
    }

    @Test fun cancellationStopsWaitingBeforeAnOffer()=runBlocking {
        var offers=0
        val waiting=CompletableDeferred<Unit>()
        val attempt=async(start=CoroutineStart.UNDISPATCHED) {
            awaitAttachmentReadiness(probe={null as String?},wait={waiting.complete(Unit);awaitCancellation()})
            offers++
        }
        try {
            withTimeout(5000){waiting.await()}
            attempt.cancelAndJoin()
            assertTrue(attempt.isCancelled)
            assertEquals(0,offers)
        }finally{attempt.cancel()}
    }

    @Test fun supersededProbeCannotOffer()=runBlocking {
        var waits=0
        var offers=0
        try {
            awaitAttachmentReadiness<String>(probe={throw ScopedBackendWork.Superseded()},wait={waits++})
            offers++
        }catch(_:ScopedBackendWork.Superseded){}
        assertEquals(0,waits)
        assertEquals(0,offers)
    }

    @Test fun delayedImportAndPreflightLeaveGateAvailableAndFenceStaleCommit(){
        for(stage in listOf("import","preflight")){
            val gate=Any()
            val originalProfile=Any()
            var profile:Any=originalProfile
            var otherLocalWork=0
            var committed=0
            val entered=CountDownLatch(1)
            val resume=CountDownLatch(1)
            val worker=Executors.newSingleThreadExecutor()
            try {
                val scoped=ScopedBackendWork(gate){profile===originalProfile}
                val attempt=worker.submit {
                    scoped.network {
                        assertFalse("$stage held the owner gate",Thread.holdsLock(gate))
                        entered.countDown()
                        check(resume.await(5,TimeUnit.SECONDS))
                        stage
                    }
                    scoped.local{committed++}
                }
                assertTrue("$stage did not begin",entered.await(5,TimeUnit.SECONDS))
                scoped.local{otherLocalWork++}
                assertEquals(1,otherLocalWork)
                synchronized(gate){profile=Any()}
                resume.countDown()
                val failure=assertThrows(ExecutionException::class.java){attempt.get(5,TimeUnit.SECONDS)}
                assertTrue(failure.cause is ScopedBackendWork.Superseded)
                assertEquals("$stage committed after profile replacement",0,committed)
            }finally{
                resume.countDown()
                worker.shutdownNow()
                worker.awaitTermination(5,TimeUnit.SECONDS)
            }
        }
    }
}
