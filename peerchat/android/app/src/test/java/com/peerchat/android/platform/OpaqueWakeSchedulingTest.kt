package com.peerchat.android.platform

import android.app.job.JobInfo
import android.app.job.JobScheduler
import android.content.ComponentName
import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.Shadows.shadowOf

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class OpaqueWakeSchedulingTest {
    private val context=ApplicationProvider.getApplicationContext<Context>()
    private val scheduler=context.getSystemService(JobScheduler::class.java)
    private val hint=mapOf("event" to "peerchat_pending","version" to "1")
    @Before fun reset(){scheduler.cancelAll();OpaquePushWake.finished(OpaquePushWake.JOB_ID);OpaquePushWake.finished(OpaquePushWake.RETRY_JOB_ID)}
    @Test fun freshWakeIsImmediateExpedited(){
        assertTrue(OpaquePushWake.schedule(context,hint))
        val job=checkNotNull(scheduler.getPendingJob(OpaquePushWake.JOB_ID))
        assertEquals(0,job.minLatencyMillis);assertTrue(job.isExpedited)
    }
    @Test fun repeatedImmediatePendingHintsCoalesce(){
        repeat(100){assertTrue(OpaquePushWake.schedule(context,hint))}
        assertEquals(1,scheduler.allPendingJobs.size)
    }
    @Test fun runningWakeCoalescesWithoutAnotherJob(){
        OpaquePushWake.started(OpaquePushWake.JOB_ID)
        assertTrue(OpaquePushWake.schedule(context,hint));assertTrue(scheduler.allPendingJobs.isEmpty())
    }
    @Test fun delayedReconciliationCannotSuppressPush(){
        assertTrue(OpaquePushWake.reconcile(context))
        val retry=checkNotNull(scheduler.getPendingJob(OpaquePushWake.RETRY_JOB_ID))
        assertEquals(30000,retry.minLatencyMillis)
        repeat(100){assertTrue(OpaquePushWake.schedule(context,hint))}
        assertEquals(2,scheduler.allPendingJobs.size)
        assertSame(retry,scheduler.getPendingJob(OpaquePushWake.RETRY_JOB_ID))
    }
    @Test fun staleDelayedUrgentIdentityIsReplacedAfterRestart(){
        scheduler.schedule(JobInfo.Builder(OpaquePushWake.JOB_ID,ComponentName(context,PeerChatWakeJob::class.java)).setMinimumLatency(3600000).build())
        assertTrue(OpaquePushWake.schedule(context,hint))
        assertEquals(0,scheduler.getPendingJob(OpaquePushWake.JOB_ID)!!.minLatencyMillis)
    }
    @Test fun systemDelayAndFailedRunKeepDistinctFallback(){
        assertFalse(OpaquePushWake.equivalentImmediate(false,true,0,true))
        assertFalse(OpaquePushWake.equivalentImmediate(false,true,30000,false))
        assertTrue(OpaquePushWake.equivalentImmediate(true,true,30000,true))
        assertTrue(OpaquePushWake.reconcile(context));assertTrue(OpaquePushWake.reconcile(context))
        assertEquals(1,scheduler.allPendingJobs.size)
        assertFalse(scheduler.getPendingJob(OpaquePushWake.RETRY_JOB_ID)!!.isExpedited)
    }
    @Test fun expeditedRejectionUsesImmediateBoundedFallback(){
        shadowOf(scheduler).failExpeditedJob(true)
        assertTrue(OpaquePushWake.reconcile(context))
        assertTrue(OpaquePushWake.schedule(context,hint))
        val immediate=checkNotNull(scheduler.getPendingJob(OpaquePushWake.JOB_ID))
        assertFalse(immediate.isExpedited);assertEquals(0,immediate.minLatencyMillis)
        assertEquals(30000,immediate.maxExecutionDelayMillis)
        assertNotNull(scheduler.getPendingJob(OpaquePushWake.RETRY_JOB_ID))
    }
}
