package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class BackgroundConnectionGraceTest {
    @Test fun retainsForSixtySecondsOnly() {
        val grace=BackgroundConnectionGrace()
        val token=grace.begin(1_000L)
        assertEquals(60_000_001_000L,grace.deadline(1_000L))
        assertEquals(60_000_001_000L,grace.deadline(60_000_000_999L))
        assertEquals(0L,grace.deadline(60_000_001_000L))
        assertTrue(grace.expire(token,60_000_001_000L))
        assertEquals(0L,grace.deadline(60_000_001_001L))
    }

    @Test fun foregroundReturnAndNewStopFenceOldExpiry() {
        val grace=BackgroundConnectionGrace()
        val old=grace.begin(1_000L)
        grace.cancel()
        val current=grace.begin(20_000L)
        assertFalse(grace.expire(old,60_000_001_000L))
        assertEquals(60_000_020_000L,grace.deadline(60_000_001_000L))
        assertFalse(grace.expire(current,60_000_019_999L))
        assertTrue(grace.expire(current,60_000_020_000L))
    }

    @Test fun explicitCloseCancelsAndLateCallbackCannotRetireNewScope() {
        val grace=BackgroundConnectionGrace()
        val token=grace.begin(10L)
        grace.cancel()
        assertFalse(grace.expire(token,60_000_000_010L))
        assertEquals(0L,grace.deadline(20L))
    }
}
