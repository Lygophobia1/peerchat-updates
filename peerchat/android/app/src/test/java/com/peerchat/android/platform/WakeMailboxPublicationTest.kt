package com.peerchat.android.platform

import android.util.Base64
import com.peerchat.android.application.*
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** Exercises the service and the JobService's real notification-marker path. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class WakeMailboxPublicationTest {
    private class Store:AndroidQolRepository {
        var value=AndroidQolState()
        override fun load()=value
        override fun save(value:AndroidQolState){this.value=value}
    }
    private class Native:MailboxNative {
        override fun identity(handle:Long)=byteArrayOf(1)
        override fun registration(handle:Long,allowed:Boolean)=byteArrayOf(2)
        override fun registered(handle:Long,binding:ByteArray)=0
        override fun receive(handle:Long,wire:ByteArray)=NativeRecordWriter(3).also {
            it.number(1);it.blob(byteArrayOf(3));it.text("a".repeat(64));it.text("b".repeat(32))
        }.bytes()
        override fun receipt(handle:Long,wire:ByteArray)=0
        override fun pending(handle:Long)=NativeRecordWriter(4).also{it.number(0)}.bytes()
        override fun prepareDevices(handle:Long,id:ByteArray,bindings:ByteArray,allowed:Boolean)=null
        override fun prepare(handle:Long,id:ByteArray,binding:ByteArray,allowed:Boolean)=null
        override fun stored(handle:Long,wire:ByteArray)=0
    }
    @Test fun laterReceiptFailureStillPublishesAndNotifiesFreshFirstConversation() {
        val accountKey="f".repeat(64)
        val friend=FriendIdentity(2,"Friend","a".repeat(64),false)
        val local="c".repeat(64)
        val fresh="b".repeat(32)
        var snapshot=MessagingSnapshot(accountId=1,localPeerId=local,authenticated=true,serverPresenceLive=true)
        val events=mutableListOf<String>()
        var clock=2L
        // This is the same publication gate used by MessagingBackend; only
        // the native-history projection is supplied by the host fixture.
        val publication=MailboxPublicationGate({ ids->
            snapshot=snapshot.copy(conversations=listOf(ConversationSnapshot(friend,ConnectionSnapshot(),
                listOf(MessageSnapshot(fresh,friend.peerId,local,1,1,7,0,"hello")))),
                freshMailboxMessageIds=ids)
            events+="published"
        },{clock++})
        val store=Store()
        val shown=mutableListOf<LocalMessageNotification>()
        val operations=mutableListOf<String>()
        val failure=java.io.IOException("receipt phase unavailable")
        val rpc={operation:String,_:JSONObject->
            operations+=operation
            when(operation){
                "mailbox_register"->JSONObject().put("state","accepted")
                "mailbox_fetch"->JSONObject().put("state","ready").put("envelopes",JSONArray().put(JSONObject()
                    .put("envelope",Base64.encodeToString(byteArrayOf(4),Base64.NO_WRAP))))
                "mailbox_ack"->JSONObject().put("state","acknowledged")
                "mailbox_receipts"->throw failure
                else->error("Unexpected $operation")
            }
        }
        val execution=MailboxExecution(Native())
        assertSame(failure,assertThrows(java.io.IOException::class.java){
            execution.service(rpc,1,true,committed=publication::committed) {
                events+="acknowledged"
                publication.acknowledged()
            }
        })
        assertEquals(listOf("mailbox_register","mailbox_fetch","mailbox_ack","mailbox_receipts"),operations)
        assertEquals(listOf("published","acknowledged"),events)
        val drained=publication.outputDrained(1L,execution.needsContinuation)
        assertFalse(drained) // Later service failure still has retry work.
        assertFalse(runWakeNotificationStep(snapshot,1,accountKey,store,drained){shown+=it;true})
        assertEquals(1,shown.size)
        assertEquals("New message",shown.single().body)
        val key=conversationPreferenceKey(accountKey,friend.accountId)
        assertEquals(7L,store.value.conversations[key]?.notifiedSequence)
        runWakeNotificationStep(snapshot,1,accountKey,store,drained){shown+=it;true}
        assertEquals(1,shown.size)
    }
}
