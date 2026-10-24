package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class LocalPreparationBurstTest {
    @Test fun releasesGateBetweenBoundedTurns() {
        val gate=Any();var turns=0;var pauses=0
        assertEquals(3,runLocalPreparationBurst({synchronized(gate){++turns<=3}}, {
            assertFalse(Thread.holdsLock(gate));pauses++
        }))
        assertEquals(3,pauses)
    }
    @Test fun finiteEvenWhenMoreWorkRemains() {
        var turns=0
        assertEquals(32,runLocalPreparationBurst({turns++;true},{}))
        assertEquals(32,turns)
    }
    @Test fun cancellationAtYieldPreventsNextNativeTurn() {
        val gate=Any();var current=true;var turns=0
        val scope=ScopedBackendWork(gate){current}
        try {
            runLocalPreparationBurst({scope.local{turns++;true}},{current=false})
            fail("stale preparation must stop")
        } catch(_:ScopedBackendWork.Superseded) { assertEquals(1,turns) }
    }
    @Test fun voiceOrNoWorkStopsWithoutSpinning() {
        assertEquals(0,runLocalPreparationBurst({false},{fail("must not pause")}))
    }
    @Test fun accelerationYieldsForSetupAndActiveMedia() {
        for(preparing in listOf(false,true))for(active in listOf(false,true))for(group in listOf(false,true))
            assertEquals(!preparing&&!active&&!group,mayAccelerateLocalPreparation(preparing,active,group))
    }
}
