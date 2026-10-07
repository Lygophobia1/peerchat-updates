package com.peerchat.android.platform
import org.junit.Assert.*
import org.junit.Test
class PresenceSequenceTest {
    @Test fun epochsOrderingAndAccounts(){
        val sequence=PresenceSequence();sequence.reset("first")
        assertTrue(sequence.accept("first",1,10));assertFalse(sequence.accept("first",1,9))
        assertFalse(sequence.accept("first",1,10));assertTrue(sequence.accept("first",2,2))
        sequence.reset("second");assertFalse(sequence.accept("first",1,100))
        assertTrue(sequence.accept("second",1,1));assertFalse(sequence.accept("second",0,2))
    }
}
