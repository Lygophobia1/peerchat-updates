package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class OwnerStageTimingsTest {
    @Test fun serializedNetworkStageCostsAndGateWaitRemainSeparate(){
        var now=0L;val slow=mutableListOf<String>()
        val timings=OwnerStageTimings({now},{stage,elapsed->slow.add("$stage:$elapsed")})
        timings.measure(OwnerStage.Tick){
            timings.measure(OwnerStage.Groups){now+=2_000_000_000}
            timings.measure(OwnerStage.Archive){now+=3_000_000_000}
        }
        timings.record(OwnerStage.GateWait,4_000_000_000)
        val summary=timings.summary()
        assertTrue(summary.contains("owner_stage=Groups calls=1 last_ms=2000 max_ms=2000 failures=0"))
        assertTrue(summary.contains("owner_stage=Archive calls=1 last_ms=3000 max_ms=3000 failures=0"))
        assertTrue(summary.contains("owner_stage=Tick calls=1 last_ms=5000 max_ms=5000 failures=0"))
        assertTrue(summary.contains("owner_stage=GateWait calls=1 last_ms=4000"))
        assertEquals(4,slow.size)
    }
    @Test fun failuresAreTimedWithoutRecordingExceptionMessages(){
        var now=0L;val timings=OwnerStageTimings({now})
        val error=java.io.IOException("secret fixture response")
        try{timings.measure(OwnerStage.Mailbox){now+=700_000_000;throw error};fail()}catch(actual:java.io.IOException){assertSame(error,actual)}
        assertTrue(timings.summary().contains("last_ms=700 max_ms=700 failures=1"))
        assertFalse(timings.summary().contains("secret"))
        timings.record(OwnerStage.Mailbox,5_000_000)
        assertTrue(timings.summary().contains("calls=2 last_ms=5 max_ms=700 failures=1"))
    }
}
