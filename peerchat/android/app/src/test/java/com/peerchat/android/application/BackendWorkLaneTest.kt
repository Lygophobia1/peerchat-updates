package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

class BackendWorkLaneTest {
    @Test fun oneTaskNoBacklogAndOldCompletionCannotReleaseNewOwner(){
        var now=0L;val lane=BackendWorkLane{now}
        val first=checkNotNull(lane.begin())
        repeat(1000){assertNull(lane.begin())}
        now=5_000_000;assertTrue(lane.diagnostic().contains("age_ms=5"))
        lane.finish(Any());assertTrue(lane.busy())
        lane.finish(first);val second=checkNotNull(lane.begin())
        lane.finish(first);assertTrue(lane.busy())
        lane.finish(second);assertFalse(lane.busy());assertTrue(lane.diagnostic().contains("completed=2"))
    }
    @Test fun blockedBulkAndMetadataDoNotOwnControlGateAndLateResultsAreDiscarded(){
        val gate=Any();var current=true;var applied=0
        val scope=ScopedBackendWork(gate){current}
        val lanes=List(3){BackendWorkLane()};val entered=CountDownLatch(3);val release=CountDownLatch(1)
        val pool=Executors.newFixedThreadPool(3)
        try{
            val futures=lanes.map{lane->val token=checkNotNull(lane.begin());pool.submit{
                try{scope.network{assertFalse(Thread.holdsLock(gate));entered.countDown();check(release.await(5,TimeUnit.SECONDS))}
                    scope.local{applied++}
                }catch(_:ScopedBackendWork.Superseded){}finally{lane.finish(token)}
            }}
            assertTrue(entered.await(5,TimeUnit.SECONDS))
            // This represents the owner control/event pass while the three
            // real scope wrappers are waiting for independent bounded I/O.
            synchronized(gate){assertTrue(lanes.all{it.busy()});current=false}
            release.countDown();futures.forEach{it.get(5,TimeUnit.SECONDS)}
            assertEquals(0,applied);assertTrue(lanes.none{it.busy()})
        }finally{release.countDown();pool.shutdownNow()}
    }
}
