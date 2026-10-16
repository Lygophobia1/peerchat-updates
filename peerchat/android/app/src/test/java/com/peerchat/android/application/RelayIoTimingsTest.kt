package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class RelayIoTimingsTest {
    @Test fun fixedCountersPreserveResultsAndExceptionsWithoutPayloads() {
        var nanos=0L;val timing=RelayIoTimings{nanos}
        assertEquals(42,timing.measure(RelayIoStage.NativeReceive){nanos=2000;42})
        try{timing.measure(RelayIoStage.NativeReceive){nanos=7000;error("private failure")};fail()}catch(_:IllegalStateException){}
        val text=timing.snapshot()
        assertTrue(text.contains("io_NativeReceive_samples=2 io_NativeReceive_us=7 io_NativeReceive_max_us=5"))
        assertTrue(text.contains("io_SocketWrite_samples=0"));assertFalse(text.contains("private"))
        assertEquals(12,text.split(' ').size)
    }
    @Test fun concurrentReadersAndWritersDoNotLoseObservations() {
        val timing=RelayIoTimings{0L}
        val workers=List(4){Thread{repeat(1000){timing.measure(RelayIoStage.NativeOutput){};timing.snapshot()}}}
        workers.forEach{it.start()};workers.forEach{it.join()}
        assertTrue(timing.snapshot().contains("io_NativeOutput_samples=4000"))
    }
}
