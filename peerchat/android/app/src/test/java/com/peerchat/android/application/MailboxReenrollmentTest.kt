package com.peerchat.android.application

import java.util.Base64
import org.junit.Assert.*
import org.junit.Test

class MailboxReenrollmentTest {
    private fun frame(account:Long=4,old:Long=19,generation:Long=3,device:String="a".repeat(64),key:String="b".repeat(32),
        pin:String="c".repeat(64),server:String="server.example:7443",nonce:String="d".repeat(32),binding:ByteArray=byteArrayOf(1)):ByteArray =
        NativeRecordWriter(3).also{w->w.text("PeerChat/MailboxReenrollment/v1");w.number(account);w.number(generation);w.number(old)
            w.text(device);w.text(key);w.number(2);w.text(server);w.text(pin);w.text(nonce);w.blob(binding)}.bytes()
    @Test fun exactNativeScopeIsPreservedForAcceptAndDecline(){
        val bytes=frame();val value=mailboxReenrollment(bytes)
        assertEquals(4L,value.account);assertEquals(19L,value.oldAccount);assertEquals(3L,value.generation);assertEquals(2L,value.sourceGeneration)
        assertArrayEquals(bytes,Base64.getDecoder().decode(value.proof))
        assertArrayEquals(bytes+byteArrayOf(1),mailboxReenrollmentDecision(value,true))
        assertArrayEquals(bytes+byteArrayOf(0),mailboxReenrollmentDecision(value,false))
        assertNotEquals(value,mailboxReenrollment(frame(generation=4)))
    }
    @Test fun malformedOrUnboundedNativeOffersAreRejected(){
        for(bytes in listOf(frame(account=0),frame(old=4),frame(generation=0),frame(device="invalid"),frame(key="invalid"),
            frame(pin="invalid"),frame(nonce="invalid"),frame(server="x".repeat(513)),frame(server="server\ncredential"),frame(binding=byteArrayOf()),
            frame(binding=ByteArray(8192)),frame()+byteArrayOf(0),frame().copyOf(20)))
            assertThrows(IllegalArgumentException::class.java){mailboxReenrollment(bytes)}
    }
    @Test fun displayedFieldsCannotBeChangedWithoutChangingTheNativeProof(){
        val value=mailboxReenrollment(frame())
        assertThrows(IllegalArgumentException::class.java){mailboxReenrollmentDecision(value.copy(account=5),true)}
        assertThrows(IllegalArgumentException::class.java){mailboxReenrollmentDecision(value.copy(proof="x".repeat(12000)),true)}
    }
    @Test fun renewedNativeNonceChangesTheExactDecisionProof(){
        val original=mailboxReenrollment(frame())
        val renewed=mailboxReenrollment(frame(nonce="e".repeat(32)))
        assertEquals(original.copy(proof=renewed.proof),renewed)
        assertNotEquals(original.proof,renewed.proof)
        assertFalse(mailboxReenrollmentDecision(original,true).contentEquals(mailboxReenrollmentDecision(renewed,true)))
    }
    @Test fun losingAuthenticationClearsTheConsentOffer(){
        val snapshot=MessagingSnapshot(accountId=4,authenticated=true,mailboxReenrollment=mailboxReenrollment(frame()),mailboxRecoveryError="Pending")
        val offline=localOfflineSnapshot(snapshot,"Offline")
        assertNull(offline.mailboxReenrollment);assertNull(offline.mailboxRecoveryError)
    }
}
