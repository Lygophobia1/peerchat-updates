package com.peerchat.android.application

import androidx.test.core.app.ApplicationProvider
import com.peerchat.android.platform.tls.ServerChannel
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.net.Socket
import java.net.SocketTimeoutException

/** Audit regressions for mailbox registration and runtime ownership. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class NetworkingReadinessAuditTest {
    @Test fun mailboxTransportFailureRetriesSoonWithoutHintStorm(){
        var now=1_000L
        val retry=MailboxRetryCadence{now}
        retry.failed(SocketTimeoutException("offline"))
        assertEquals(3_000L,retry.notBefore)
        assertFalse(retry.ready(now,0)) // An authenticated event hint cannot erase the cooldown.
        now=3_000L;assertTrue(retry.ready(now,0))
        retry.failed(SocketTimeoutException("offline"))
        assertEquals(7_000L,retry.notBefore)
        repeat(8){now=retry.notBefore;retry.failed(SocketTimeoutException("offline"))}
        assertEquals(now+60_000L,retry.notBefore)
        retry.reset() // Successful service or a new authenticated scope.
        assertTrue(retry.ready(now,0))
        retry.failed(IllegalStateException("binding rejected"))
        assertEquals(now+900_000L,retry.notBefore)
        assertFalse(retry.ready(now+60_000L,0))
        retry.reset()
        retry.failed(javax.net.ssl.SSLHandshakeException("pin mismatch"))
        assertEquals(now+900_000L,retry.notBefore)
        retry.reset()
        retry.failed(java.io.IOException("TLS failed",java.security.cert.CertificateException("pin mismatch")))
        assertEquals(now+900_000L,retry.notBefore)
    }

    private class Native:MailboxNative {
        var accepted=false
        var registrations=0
        var registrationResult=0
        var bindingAvailable=true
        var dropBindingAfterAccepted=false
        override fun identity(handle:Long)=byteArrayOf(1)
        override fun registration(handle:Long,allowed:Boolean)=if(bindingAvailable)byteArrayOf(2) else null
        override fun registered(handle:Long,binding:ByteArray):Int {
            registrations++
            if(registrationResult==0){accepted=true;if(dropBindingAfterAccepted)bindingAvailable=false}
            return registrationResult
        }
        override fun receive(handle:Long,wire:ByteArray):ByteArray?=null
        override fun receipt(handle:Long,wire:ByteArray)=0
        override fun pending(handle:Long)=NativeRecordWriter(4).also{it.number(0)}.bytes()
        override fun prepareDevices(handle:Long,id:ByteArray,bindings:ByteArray,allowed:Boolean):ByteArray?=null
        override fun prepare(handle:Long,id:ByteArray,binding:ByteArray,allowed:Boolean):ByteArray?=null
        override fun stored(handle:Long,wire:ByteArray)=0
    }
    private fun reply(op:String,body:JSONObject):JSONObject=when(op){
        "mailbox_register"->JSONObject().put("state","accepted")
        "mailbox_fetch"->JSONObject().put("state","ready").put("envelopes",JSONArray())
        "mailbox_receipts"->JSONObject().put("state","ready").put("receipts",JSONArray())
        else->error("Unexpected audit RPC $op, fields=${body.length()}")
    }
    @Test fun sameMailboxExecutorReconcilesAfterScopedNativeInvalidation(){
        val native=Native();val execution=MailboxExecution(native)
        assertFalse(execution.registrationAccepted)
        execution.service(::reply,1,true){}
        assertTrue(native.accepted);assertTrue(execution.registrationAccepted);assertEquals(1,native.registrations)
        // A changed authenticated generation invalidates the native binding.
        // The executor must drop readiness before the next registration pass.
        native.accepted=false
        execution.invalidateRegistration()
        assertFalse(execution.registrationAccepted)
        execution.service(::reply,1,true){}
        assertTrue(native.accepted);assertTrue(execution.registrationAccepted);assertEquals(2,native.registrations)
        assertTrue(execution.diagnostic.contains("registered=true"))
        execution.reset()
        assertFalse(execution.registrationAccepted)
        execution.service(::reply,1,true){}
        assertEquals(3,native.registrations)
        assertTrue(execution.registrationAccepted)
    }
    @Test fun rejectedOrFailedRegistrationNeverReportsReady(){
        val native=Native();val execution=MailboxExecution(native)
        val rejected:(String,JSONObject)->JSONObject={op,body->
            if(op=="mailbox_register")JSONObject().put("state","not_authorized") else reply(op,body)
        }
        assertThrows(IllegalStateException::class.java){execution.service(rejected,1,true){}}
        assertEquals(0,native.registrations)
        assertFalse(execution.registrationAccepted)
        native.registrationResult=-1
        assertThrows(IllegalStateException::class.java){execution.service(::reply,1,true){}}
        assertEquals(1,native.registrations)
        assertFalse(native.accepted)
        assertFalse(execution.registrationAccepted)
        native.registrationResult=0
        execution.service(::reply,1,true){}
        assertTrue(execution.registrationAccepted)
    }
    @Test fun nativeBindingLostDuringAcceptanceRemainsUnready(){
        val native=Native().apply{dropBindingAfterAccepted=true}
        val execution=MailboxExecution(native)
        execution.service(::reply,1,true){}
        assertEquals(1,native.registrations)
        assertFalse(execution.registrationAccepted)
        assertTrue(execution.diagnostic.contains("registered=false"))
    }
    @Test fun lastLeaseQuiescenceImmediatelyClosesSocketsEvenIfForegroundReturns(){
        val backend=MessagingBackend(ApplicationProvider.getApplicationContext())
        val field=MessagingBackend::class.java.getDeclaredField("operations").also{it.isAccessible=true}
        val scope=field.get(backend) as ServerChannel.OperationScope
        val attach=ServerChannel.OperationScope::class.java.getDeclaredMethod("attach",Socket::class.java).also{it.isAccessible=true}
        val socket=Socket()
        try{
            attach.invoke(scope,socket);assertEquals(1,scope.activeSocketCount())
            backend.setInUse(false)
            assertTrue(socket.isClosed);assertEquals(0,scope.activeSocketCount())
            backend.setInUse(true)
            assertTrue("Returning foreground cannot revive closed TCP",socket.isClosed)
        }finally{socket.close();backend.close()}
    }
}
