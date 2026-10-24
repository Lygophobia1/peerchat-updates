package com.peerchat.android.platform

import org.junit.Assert.*
import org.junit.Test

class BackgroundCallWakeTest {
    @Test fun emptyMailboxDoesNotEndIncomingCallWindow(){
        assertFalse(backgroundWakeCanFinish(true,true,false,1000))
        assertFalse(backgroundWakeCanFinish(true,true,true,10000))
        assertFalse(backgroundWakeCanFinish(true,true,false,11000))
    }
    @Test fun everyWakeHasABoundAndOrdinaryMailboxCanFinishEarly(){
        assertTrue(backgroundWakeCanFinish(false,true,false,1000))
        assertFalse(backgroundWakeCanFinish(false,false,false,29999))
        assertTrue(backgroundWakeCanFinish(false,false,false,30000))
        assertTrue(backgroundWakeCanFinish(true,false,false,55000))
        assertFalse(backgroundWakeCanFinish(true,false,true,55000))
        assertTrue(backgroundWakeCanFinish(true,false,true,85000))
    }
}
