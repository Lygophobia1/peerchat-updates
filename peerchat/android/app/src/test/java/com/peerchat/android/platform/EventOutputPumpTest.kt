package com.peerchat.android.platform

import org.junit.Assert.*
import org.junit.Test
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

class EventOutputPumpTest {
    @Test fun stalledStreamDoesNotBlockOwnerAndKeepsQueueBounded(){
        val entered=CountDownLatch(1);val release=CountDownLatch(1)
        val pump=EventOutputPump({bytes,_->if(bytes[0].toInt()==1){entered.countDown();release.await()}},{throw AssertionError(it)})
        try{
            assertTrue(pump.offer(byteArrayOf(1)))
            assertTrue(entered.await(2,TimeUnit.SECONDS))
            // The socket writer is blocked. These producer operations must still
            // complete without waiting for it, including the rejected overflow.
            repeat(31){assertTrue(pump.offer(byteArrayOf(2)))}
            assertFalse(pump.offer(byteArrayOf(3)));assertEquals(32,pump.pendingCount())
        }finally{release.countDown();pump.close()}
    }
    @Test fun failedWriteKeepsFrameAndNeverReplaysOnAmbiguousStream(){
        val failed=CountDownLatch(1);val observed=java.util.concurrent.atomic.AtomicInteger()
        val pump=EventOutputPump({bytes,_->if(bytes[0].toInt()==1){observed.incrementAndGet();throw java.io.IOException("fixture")}}, {failed.countDown()})
        try{
            assertTrue(pump.offer(byteArrayOf(1)))
            assertTrue(failed.await(2,TimeUnit.SECONDS))
            assertEquals(1,observed.get());assertEquals(1,pump.pendingCount());assertFalse(pump.offer(byteArrayOf(2)))
        }finally{pump.close()}
    }
    @Test fun idleWriterSendsHeartbeatWithoutOwnerTicks(){
        val heartbeats=CountDownLatch(2)
        val pump=EventOutputPump({bytes,maximum->
            assertEquals("{\"type\":\"event_heartbeat\"}",bytes.toString(Charsets.UTF_8))
            assertEquals(4096,maximum);heartbeats.countDown()
        },{throw AssertionError(it)},heartbeatMillis=20)
        try{assertTrue(heartbeats.await(2,TimeUnit.SECONDS))}finally{pump.close()}
    }
}
