package com.peerchat.android.application

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

@org.junit.runner.RunWith(org.robolectric.RobolectricTestRunner::class)
@org.robolectric.annotation.Config(sdk=[34])
class GroupExecutionTest {
    private val group="a".repeat(32);private val message="b".repeat(32);private val device="c".repeat(64)
    private class Native {
        var status="Android group queued";var receive="accepted";var failAfterCommit=false
        var failOperation:Int?=null
        var pending=emptyList<Pair<String,ByteArray>>();var pendingConsumed=false
        val operations=mutableListOf<Int>();val inputs=mutableListOf<ByteArray>()
        fun request(h:Long,op:Int,bytes:ByteArray):ByteArray?{
            operations.add(op);inputs.add(bytes);if((failAfterCommit&&op!=5)||op==failOperation)return null
            val w=NativeRecordWriter(27);w.text("PeerChat/AndroidGroups/v1");w.number(op.toLong())
            when(op){
                2->{w.number(1);w.text("a".repeat(32));w.text("d".repeat(32));w.number(3);w.text("Signed group");w.number(1);w.number(0)}
                3->{w.number(1);w.text("b".repeat(32));w.text("c".repeat(64));w.number(7);w.number(1000);w.number(2);w.number(9);w.text(status);w.text("Hello \uD83D\uDE80")}
                5->{w.text("accepted");w.text("b".repeat(32));w.number(3);w.blob(byteArrayOf(1,2,3))}
                6->w.text(receive)
                8->{w.text("b".repeat(32));w.number(3);w.blob(byteArrayOf(1,2,3))}
                12->{val rows=if(pendingConsumed)emptyList() else pending;pendingConsumed=true
                    w.number(0);w.number(rows.size.toLong());w.number(rows.size.toLong());rows.forEach{(id,wire)->w.text("a".repeat(32));w.text(id);w.number(3);w.blob(wire)}}
                13->pendingConsumed=false
            };return w.bytes()
        }
    }
    @Test fun refreshedNativeDeliveryStatusReplacesCachedCopy(){
        val n=Native();val e=GroupExecution(n::request)
        e.load(1,7,1,group);n.status="Server accepted (online recipients only; not E2E delivery)";e.load(1,7,1,group)
        assertEquals(n.status,e.snapshot(1,7,1).single().messages.single().status)
        val ui=com.peerchat.android.ui.projectMessaging(MessagingSnapshot(localPeerId=device,groups=e.snapshot(1,7,1)),com.peerchat.android.ui.PeerChatUiState())
        assertEquals(com.peerchat.android.ui.MessageStatus.Sent,ui.conversations.single().messages.single().status)
        assertNotEquals(com.peerchat.android.ui.MessageStatus.Delivered,ui.conversations.single().messages.single().status)
    }
    @Test fun committedMessageRemainsSuccessfulIfTransportOrReadoutFails(){
        val n=Native().apply{failAfterCommit=true};val e=GroupExecution(n::request)
        assertEquals(message,e.send(1,7,1,group,"text"){throw IllegalStateException("network unavailable")})
        assertTrue(e.diagnostic.contains("durably created"))
    }
    @Test fun retryReusesExactNativeCiphertextAndLogicalId(){
        val n=Native();val e=GroupExecution(n::request);val queued=mutableListOf<JSONObject>()
        e.retry(1,7,1,group,message){queued.add(it);true}
        assertEquals(listOf(8,7,3),n.operations);assertEquals(message,queued.single().getString("messageId"));assertEquals("AQID",queued.single().getString("envelope"))
    }
    @Test fun receiveAckAndUnreadDependOnNativeDurableResult(){
        val n=Native();val e=GroupExecution(n::request);val replies=mutableListOf<JSONObject>()
        fun event()=JSONObject().put("type","group_message").put("groupId",group).put("messageId",message).put("envelope","AQID")
        e.event(event(),1,7,1){replies.add(it);true}
        assertEquals("accepted",replies.last().getString("state"));assertEquals(1,e.snapshot(1,7,1).single().unread)
        val r=NativeRecord(n.inputs.first(),27);r.text();r.number();r.number();r.number();assertEquals(group,r.text());assertEquals(message,r.text());assertArrayEquals(byteArrayOf(1,2,3),r.bytes());r.end()
        n.receive="duplicate_message";e.event(event(),1,7,1){replies.add(it);true};assertEquals(1,e.snapshot(1,7,1).single().unread)
        n.receive="missing_group_key";e.event(event(),1,7,1){replies.add(it);true};assertEquals("rejected",replies.last().getString("state"))
        e.load(1,7,1,group,markRead=true);assertEquals(0,e.snapshot(1,7,1).single().unread)
    }
    @Test fun rejectedNativeReceiveCannotProducePositiveAck(){
        val e=GroupExecution{_,_,_->null};var queued=false
        assertThrows(IllegalStateException::class.java){e.event(JSONObject().put("type","group_message").put("groupId",group).put("messageId",message).put("envelope","AQID"),1,7,1){queued=true;true}}
        assertFalse(queued)
    }
    @Test fun invalidationIsNativeAndSignoutClearsPlaintextProjection(){
        val n=Native();val e=GroupExecution(n::request);e.load(1,7,1,group);e.invalidate(1,7,1);assertEquals(0,n.operations.last())
        e.reset();n.status="Fresh protected reload";assertEquals(n.status,e.snapshot(1,7,1).single().messages.single().status)
    }
    private fun pendingNative()=Native().apply{pending=listOf("b".repeat(32) to byteArrayOf(1,2,3),"e".repeat(32) to byteArrayOf(4,5,6))}
    @Test fun recoveryUsesOnlyExactNativeProjectionWithoutCreatingOrReencrypting(){
        val n=pendingNative();val e=GroupExecution(n::request);val queued=mutableListOf<JSONObject>()
        assertFalse(e.submitPending(1,7,1){queued.add(it);true})
        assertEquals(listOf(12,7,7),n.operations);assertEquals(listOf(message,"e".repeat(32)),queued.map{it.getString("messageId")})
        assertEquals(listOf("AQID","BAUG"),queued.map{it.getString("envelope")})
        assertTrue(queued.all{it.getString("groupId")==group&&it.getString("rosterGeneration")=="3"&&it.getString("type")=="group_send"})
        assertFalse(e.submitPending(1,7,1){fail("Completed native cursor replayed itself");true})
        assertFalse(n.operations.contains(5));assertFalse(n.operations.contains(8))
    }
    @Test fun partialQueueBackpressureResetsNativeCursorForUnsubmittedTail(){
        val n=pendingNative();val e=GroupExecution(n::request);var accepted=0
        assertTrue(e.submitPending(1,7,1){++accepted==1})
        assertEquals(listOf(12,7,13),n.operations)
        val recovered=mutableListOf<String>();assertFalse(e.submitPending(1,7,1){recovered.add(it.getString("messageId"));true})
        assertEquals(n.pending.map{it.first},recovered)
    }
    @Test fun throwingTransportResetsNativeCursorForUnsubmittedTail(){
        val n=pendingNative();val e=GroupExecution(n::request);var calls=0
        assertThrows(java.io.IOException::class.java){e.submitPending(1,7,1){if(++calls==2)throw java.io.IOException("synthetic transport failure");true}}
        assertEquals(13,n.operations.last())
        val recovered=mutableListOf<String>();assertFalse(e.submitPending(1,7,1){recovered.add(it.getString("messageId"));true})
        assertEquals(n.pending.map{it.first},recovered)
    }
    @Test fun statusReadoutFailureResetsNativeCursorForUnsubmittedTail(){
        val n=pendingNative().apply{failOperation=7};val e=GroupExecution(n::request)
        assertThrows(IllegalStateException::class.java){e.submitPending(1,7,1){true}}
        assertEquals(13,n.operations.last());n.failOperation=null
        val recovered=mutableListOf<String>();assertFalse(e.submitPending(1,7,1){recovered.add(it.getString("messageId"));true})
        assertEquals(n.pending.map{it.first},recovered)
    }
}
