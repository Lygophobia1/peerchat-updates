package com.peerchat.android.application

import android.util.Base64
import com.peerchat.android.platform.ServerRejected
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class MailboxIsolationTest {
    private class Native:MailboxNative {
        var binding:ByteArray?=null
        var changingBindings=false
        var registrationReads=0
        var acceptance=0
        var receiptResult=0
        var receiveResult:ByteArray?=null
        var prepared:ByteArray?=byteArrayOf(5)
        var stored=0
        var receipts=0
        var receives=0
        var uploads=true
        override fun identity(handle:Long)=byteArrayOf(1)
        override fun registration(handle:Long,allowed:Boolean):ByteArray? {
            registrationReads++
            return if(changingBindings)byteArrayOf(registrationReads.toByte()) else binding
        }
        override fun registered(handle:Long,binding:ByteArray)=acceptance
        override fun receive(handle:Long,wire:ByteArray):ByteArray? {receives++;return receiveResult}
        override fun receipt(handle:Long,wire:ByteArray):Int {receipts++;return receiptResult}
        override fun pending(handle:Long)=NativeRecordWriter(4).also {w->
            w.number(if(uploads)1 else 0)
            if(uploads){w.text("a".repeat(32));w.text("b".repeat(64));w.number(2)}
        }.bytes()
        override fun prepareDevices(handle:Long,id:ByteArray,bindings:ByteArray,allowed:Boolean)=
            NativeRecordWriter(4).also{it.number(1);it.blob(byteArrayOf(4))}.bytes()
        override fun prepare(handle:Long,id:ByteArray,binding:ByteArray,allowed:Boolean)=prepared
        override fun stored(handle:Long,wire:ByteArray):Int {stored++;return 0}
    }
    private class Server {
        val operations=mutableListOf<String>()
        var rejection:Exception?=null
        var withReceipt=true
        var ackState="acknowledged"
        var keysState="ready"
        fun request(op:String,body:JSONObject):JSONObject {
            operations.add(op)
            rejection?.let{throw it}
            return when(op){
                "mailbox_register"->JSONObject().put("state","accepted")
                "mailbox_fetch"->JSONObject().put("state","ready").put("envelopes",JSONArray().put(JSONObject().put("envelope",encoded(byteArrayOf(2)))))
                "mailbox_receipts"->JSONObject().put("state","ready").put("receipts",JSONArray().also{
                    if(withReceipt)it.put(JSONObject().put("messageId","c".repeat(32)).put("receipt",encoded(byteArrayOf(3))))
                })
                "mailbox_keys"->JSONObject().put("state",keysState).put("keys",JSONArray().put(JSONObject().put("binding",encoded(byteArrayOf(4)))))
                "mailbox_store"->JSONObject().put("state","stored")
                "mailbox_ack"->JSONObject().put("state",ackState)
                else->error("Unexpected operation")
            }
        }
    }
    @Test fun missingLocalRecipientStillReconcilesAndUploadsWithoutReceiving(){
        val n=Native();val s=Server();val execution=MailboxExecution(n)
        execution.service(s::request,1,true){fail("No receive ACK permitted")}
        assertEquals(listOf("mailbox_receipts","mailbox_keys","mailbox_store"),s.operations)
        assertEquals(1,n.receipts);assertEquals(1,n.stored);assertEquals(0,n.receives)
        assertTrue(execution.diagnostic.contains("registered=false"))
        assertTrue(execution.diagnostic.contains("receive_blocked=native_recipient_registration"))
        assertFalse(execution.needsContinuation)
        // Retry in the same lifetime recovers only through native registration.
        n.binding=byteArrayOf(7);s.operations.clear()
        execution.service(s::request,1,true){fail("Unverified sender must not be acknowledged")}
        assertEquals("mailbox_register",s.operations.first())
        assertTrue(s.operations.contains("mailbox_fetch"));assertFalse(s.operations.contains("mailbox_ack"))
        assertEquals(1,n.receives);assertTrue(execution.diagnostic.contains("registered=true"))
    }
    @Test fun disabledStorageNeverUploadsButCanReconcileReceipts(){
        val n=Native();val s=Server();val execution=MailboxExecution(n)
        execution.service(s::request,1,false){fail()}
        assertEquals(listOf("mailbox_receipts"),s.operations);assertEquals(0,n.stored)
    }
    @Test fun nativeRejectionNeverProducesStoredOrAck(){
        val n=Native().apply{prepared=null};val s=Server()
        MailboxExecution(n).service(s::request,1,true){fail()}
        assertEquals(listOf("mailbox_receipts","mailbox_keys"),s.operations);assertEquals(0,n.stored)
    }
    @Test fun serverSessionFailurePropagatesWithoutUploading(){
        val n=Native();val failure=ServerRejected("session_invalid");val s=Server().apply{rejection=failure}
        val execution=MailboxExecution(n)
        assertSame(failure,assertThrows(ServerRejected::class.java){execution.service(s::request,1,true){fail()}})
        assertTrue(expiredServerSession(failure))
        assertEquals(listOf("mailbox_receipts"),s.operations);assertEquals(0,n.stored)
        assertFalse(execution.diagnostic.contains("Server request rejected"))
    }
    @Test fun rejectedNativeRegistrationDoesNotContinue(){
        val n=Native().apply{binding=byteArrayOf(7);acceptance=1};val s=Server()
        assertThrows(IllegalStateException::class.java){MailboxExecution(n).service(s::request,1,true){fail()}}
        assertEquals(listOf("mailbox_register"),s.operations);assertEquals(0,n.stored)
    }
    @Test fun registrationChurnRemainsBoundedAndFailsClosed(){
        val n=Native().apply{changingBindings=true};val s=Server()
        assertThrows(IllegalStateException::class.java){MailboxExecution(n).service(s::request,1,true){fail()}}
        assertEquals(listOf("mailbox_register","mailbox_register"),s.operations)
        assertEquals(3,n.registrationReads);assertEquals(0,n.stored)
    }
    @Test fun badReceiptStopsBeforeUploads(){
        val n=Native().apply{receiptResult=1};val s=Server()
        assertThrows(IllegalStateException::class.java){MailboxExecution(n).service(s::request,1,true){fail()}}
        assertEquals(listOf("mailbox_receipts"),s.operations);assertEquals(0,n.stored)
    }
    @Test fun notificationWaitsForDurableNativeReceiptAndServerAckIncludingDuplicates(){
        val n=Native().apply{binding=byteArrayOf(7);uploads=false}
        val s=Server().apply{withReceipt=false;ackState="not_authorized"}
        fun received(inserted:Long)=NativeRecordWriter(3).also{
            it.number(inserted);it.blob(byteArrayOf(8));it.text("peer");it.text("message")
        }.bytes()
        val execution=MailboxExecution(n);val notifications=mutableListOf<Boolean>()
        n.receiveResult=received(1)
        assertThrows(IllegalStateException::class.java){execution.service(s::request,1,true){notifications.add(it)}}
        assertTrue(notifications.isEmpty())
        s.ackState="acknowledged";n.receiveResult=received(0)
        execution.service(s::request,1,true){notifications.add(it)}
        assertEquals(listOf(false),notifications)
        execution.reset();s.operations.clear();n.binding=null
        execution.service(s::request,1,true){fail("Reset cannot reuse receive authorization")}
        assertEquals(listOf("mailbox_receipts"),s.operations)
    }
    @Test fun inboundProgressRemainsVisibleWhenHistoricalOutgoingLookupIsRejected(){
        val n=Native().apply{binding=byteArrayOf(7);receiveResult=received(1)}
        val s=Server().apply{withReceipt=false;keysState="rejected_or_storage_unavailable"}
        val execution=MailboxExecution(n);val notifications=mutableListOf<Boolean>()
        execution.service(s::request,1,true){notifications.add(it)}
        assertEquals(listOf(true),notifications)
        assertTrue(execution.inboundDiagnostic.contains("receive=acknowledged"))
        assertTrue(execution.inboundDiagnostic.contains("imported=1 acknowledged=1"))
        assertTrue(execution.diagnostic.contains("outgoing_retry=1"))
        assertTrue(execution.diagnostic.contains("retry_reason=rejected_or_storage_unavailable"))
        assertEquals(0,n.stored)
    }
    @Test fun lostServerAckReopensRegistrationAndDrainsDuplicateWithoutAnotherImport(){
        val n=Native().apply{binding=byteArrayOf(7);uploads=false;receiveResult=received(1)}
        val s=Server().apply{withReceipt=false;ackState="not_authorized"}
        val first=MailboxExecution(n)
        assertThrows(IllegalStateException::class.java){first.service(s::request,1,true){fail()}}
        assertTrue(first.inboundDiagnostic.contains("receive=durable_import_receipt_ready"))
        assertTrue(first.inboundDiagnostic.contains("imported=1 acknowledged=0"))
        n.receiveResult=received(0);s.ackState="acknowledged";s.operations.clear()
        val reopened=MailboxExecution(n);val notifications=mutableListOf<Boolean>()
        reopened.service(s::request,1,true){notifications.add(it)}
        assertEquals("mailbox_register",s.operations.first())
        assertEquals(listOf(false),notifications)
        assertTrue(reopened.inboundDiagnostic.contains("imported=0 acknowledged=1"))
        assertArrayEquals(byteArrayOf(7),n.binding)
    }
    @Test fun localNativeReceiveFailureIsNotMisreportedAsVerificationOrAcknowledged(){
        val n=Native().apply{binding=byteArrayOf(7);uploads=false}
        val s=Server().apply{withReceipt=false}
        val execution=MailboxExecution(n)
        execution.service(s::request,1,true){fail()}
        assertTrue(execution.inboundDiagnostic.contains("pending_native_validation=1"))
        assertFalse(execution.diagnostic.contains("awaiting_verification"))
        assertFalse(s.operations.contains("mailbox_ack"))
        assertEquals(1,n.receives)
    }
    @Test fun transientFetchDisconnectRetriesWithoutAcknowledgingOrReplacingRegistration(){
        val n=Native().apply{binding=byteArrayOf(7);uploads=false;receiveResult=received(1)}
        val s=Server().apply{withReceipt=false}
        val execution=MailboxExecution(n);var disconnect=true
        val transport={op:String,body:JSONObject->
            if(op=="mailbox_fetch"&&disconnect){disconnect=false;throw java.io.IOException("isolated fetch interrupted")}
            s.request(op,body)
        }
        val notifications=mutableListOf<Boolean>()
        assertThrows(java.io.IOException::class.java){execution.service(transport,1,true){notifications.add(it)}}
        assertTrue(notifications.isEmpty());assertEquals(0,n.receives)
        assertEquals(listOf("mailbox_register"),s.operations)
        assertEquals(2,n.registrationReads)
        execution.service(transport,1,true){notifications.add(it)}
        assertEquals(listOf(true),notifications)
        assertEquals(2,n.registrationReads)
        assertArrayEquals(byteArrayOf(7),n.binding)
        assertTrue(execution.inboundDiagnostic.contains("imported=1 acknowledged=1"))
    }
    private fun received(inserted:Long)=NativeRecordWriter(3).also{
        it.number(inserted);it.blob(byteArrayOf(8));it.text("peer");it.text("message")
    }.bytes()
    companion object {private fun encoded(bytes:ByteArray)=Base64.encodeToString(bytes,Base64.NO_WRAP)}
}
