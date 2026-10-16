package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class RelayOutputStatusTest {
    @Test fun nativeClosedEndpointCannotKeepIdleRelayWriterAlive(){
        assertTrue(nativeRelayOpen(byteArrayOf(1,0)))
        assertTrue(nativeRelayOpen(byteArrayOf(0,0)))
        assertFalse(nativeRelayOpen(byteArrayOf(1,1)))
        assertFalse(nativeRelayOpen(null))
        assertFalse(nativeRelayOpen(byteArrayOf(1)))
    }
}
