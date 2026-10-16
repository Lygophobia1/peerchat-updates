package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class NativeEventDrainTest {
    @Test fun emptyAndClosedQueuesStopAfterOneRead() {
        for(empty in listOf<ByteArray?>(byteArrayOf(),null)) {
            var reads=0
            drainNativeEvents({reads++;empty}){fail("No event expected")}
            assertEquals(1,reads)
        }
    }
    @Test fun eventsRemainOrderedAndStopAtFirstEmptyQueue() {
        var reads=0;val values=mutableListOf<Int>()
        drainNativeEvents({reads++;if(reads<=3)byteArrayOf(reads.toByte()) else byteArrayOf()}){values.add(it[0].toInt())}
        assertEquals(listOf(1,2,3),values);assertEquals(4,reads)
    }
    @Test fun busyQueueRemainsBoundedToSixteenReads() {
        var reads=0;var consumed=0
        drainNativeEvents({reads++;byteArrayOf(1)}){consumed++}
        assertEquals(16,reads);assertEquals(16,consumed)
    }
}
