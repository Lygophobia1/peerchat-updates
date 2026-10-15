package com.peerchat.android.application
import org.junit.Assert.*
import org.junit.Test
class DeviceVerificationTest {
    private fun frame(peer:String="a".repeat(64),prior:String="b".repeat(64),generation:Long=4):ByteArray {
        val w=NativeRecordWriter(3);w.text("PeerChat/DeviceVerification/v1");w.number(8);w.number(generation);w.number(7);w.number(9);w.text(peer);w.number(1);w.text(prior);return w.bytes()
    }
    @Test fun approvalAndRejectionPreserveExactNativeContext(){
        val wire=frame();val value=deviceVerification(wire,"Sammy")
        assertEquals(8L,value.localAccount);assertEquals(4L,value.generation);assertEquals(7L,value.attempt);assertEquals(9L,value.account)
        assertArrayEquals(wire+byteArrayOf(1),verificationDecision(value,true));assertArrayEquals(wire+byteArrayOf(0),verificationDecision(value,false))
        assertNotEquals(value,deviceVerification(frame(generation=5),"Sammy"));assertEquals(listOf("b".repeat(64)),value.previous)
    }
    @Test fun malformedSnapshotsCannotBecomeApproval(){
        assertThrows(IllegalArgumentException::class.java){deviceVerification(frame(peer="invalid"),"Sammy")}
        assertThrows(IllegalArgumentException::class.java){deviceVerification(frame(prior="invalid"),"Sammy")}
        assertThrows(IllegalArgumentException::class.java){deviceVerification(frame()+byteArrayOf(0),"Sammy")}
        assertThrows(IllegalArgumentException::class.java){deviceVerification(frame().copyOf(24),"Sammy")}
    }
}
