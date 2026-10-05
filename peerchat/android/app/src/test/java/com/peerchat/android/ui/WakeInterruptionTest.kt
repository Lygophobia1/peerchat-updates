package com.peerchat.android.ui

import android.app.job.JobParameters
import com.peerchat.android.platform.retryInterruptedWake
import org.junit.Test
import org.junit.Assert.*

class WakeInterruptionTest {
    @Test fun systemConstraintInterruptionPreservesBoundedWake() {
        assertTrue(retryInterruptedWake(JobParameters.STOP_REASON_DEVICE_STATE))
        assertTrue(retryInterruptedWake(JobParameters.STOP_REASON_CONSTRAINT_CONNECTIVITY))
        assertTrue(retryInterruptedWake(JobParameters.STOP_REASON_QUOTA))
        assertTrue(retryInterruptedWake(JobParameters.STOP_REASON_TIMEOUT))
    }
    @Test fun explicitStopDoesNotReschedule() {
        assertFalse(retryInterruptedWake(JobParameters.STOP_REASON_USER))
        assertFalse(retryInterruptedWake(JobParameters.STOP_REASON_CANCELLED_BY_APP))
    }
}
