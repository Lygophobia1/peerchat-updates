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

/** Audit characterizations of current production behavior, not repaired behavior. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class NetworkingReadinessAuditTest {
    private class Native:MailboxNative {
        var accepted=false
        var registrations=0
        override fun identity(handle:Long)=byteArrayOf(1)
        override fun registration(handle:Long,allowed:Boolean)=byteArrayOf(2)
        override fun registered(handle:Long,binding:ByteArray):Int {accepted=true;registrations++;return 0}
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
    @Test fun sameMailboxExecutorDoesNotReconcileClearedNativeBinding(){
        val native=Native();val execution=MailboxExecution(native)
        execution.service(::reply,1,true){}
        assertTrue(native.accepted);assertEquals(1,native.registrations)
        // Models configureEnrollment's proven generation-change reset, without
        // replacing the Kotlin executor, as refreshLocked currently behaves.
        native.accepted=false
        execution.service(::reply,1,true){}
        assertFalse(native.accepted);assertEquals(1,native.registrations)
        assertTrue(execution.diagnostic.contains("registered=true"))
        execution.reset();execution.service(::reply,1,true){}
        assertTrue(native.accepted);assertEquals(2,native.registrations)
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
