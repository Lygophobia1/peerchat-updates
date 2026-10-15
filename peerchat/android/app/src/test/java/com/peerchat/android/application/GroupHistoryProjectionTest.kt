package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

@org.junit.runner.RunWith(org.robolectric.RobolectricTestRunner::class)
@org.robolectric.annotation.Config(sdk=[34])
class GroupHistoryProjectionTest {
    private val group="a".repeat(32)
    private fun message(id:Char,order:Long,sequence:Long)=GroupMessageSnapshot(id.toString().repeat(32),"c".repeat(64),7,1000+order,order,sequence,"","Message $id")
    private inner class Native {
        var revision=1L;var ready=true;var reads=0;var reference=false
        var records=listOf(message('b',10,1))
        fun request(@Suppress("UNUSED_PARAMETER") handle:Long,operation:Int,@Suppress("UNUSED_PARAMETER") bytes:ByteArray):ByteArray {
            val w=NativeRecordWriter(27);w.text("PeerChat/AndroidGroups/v1");w.number(operation.toLong())
            when(operation){
                2->{w.number(1);w.text(group);w.text("d".repeat(32));w.number(3);w.text("Group");w.number(1);w.number(0);w.text("PeerChat/GroupHistoryRevision/v1");w.number(revision)}
                3->{reads++;val page=if(ready)records else emptyList();w.number(page.size.toLong());page.forEach { m->w.text(m.id);w.text(m.device);w.number(m.account);w.number(m.timestamp);w.number(m.order);w.number(m.sequence);w.text(m.status);w.text(m.text)};w.text("PeerChat/GroupHistoryPage/v1");w.number(if(ready)1 else 0)
                    if(reference){w.text("PeerChat/GroupReferencePage/v1");w.number(1);w.number(0);w.number(1);w.text("9".repeat(32));w.text("8".repeat(32));w.text("c".repeat(64));w.number(7);w.number(3);w.number(1234)}}
                else->error("Unexpected local operation")
            }
            return w.bytes()
        }
    }
    @Test fun nativeHistoryRevisionRefreshesExistingGroupWithoutLiveEvent(){
        val native=Native();val execution=GroupExecution(native::request)
        assertEquals(listOf(10L),execution.snapshot(1,7,1).single().messages.map{it.order})
        native.records=listOf(message('e',1,2),message('b',10,1));native.revision=3
        assertEquals(listOf(1L,10L),execution.snapshot(1,7,1).single().messages.map{it.order})
        val reads=native.reads;execution.snapshot(1,7,1);assertEquals(reads,native.reads)
        assertEquals(2L,groupHistoryBefore(execution.snapshot(1,7,1).single().messages))
    }
    @Test fun pendingBoundedIndexDoesNotClaimHistoryExhausted(){
        val native=Native().apply{ready=false};val execution=GroupExecution(native::request)
        val pending=execution.snapshot(1,7,1).single();assertTrue(pending.messages.isEmpty());assertTrue(pending.hasOlder)
        assertTrue(execution.diagnostic.contains("Preparing"))
        native.ready=true;native.revision++
        val completed=execution.snapshot(1,7,1).single();assertEquals(1,completed.messages.size);assertFalse(completed.hasOlder)
    }
    @Test fun profileResetDropsCachedMessagesEvenWhenRevisionMatches(){
        val native=Native();val execution=GroupExecution(native::request)
        execution.snapshot(1,7,1);execution.reset();native.records=listOf(message('f',2,9))
        assertEquals(listOf("f".repeat(32)),execution.snapshot(2,8,2).single().messages.map{it.id})
    }
    @Test fun opaqueCursorFollowsAuthenticatedOrderNotArrivalSequence(){
        assertEquals(99L,groupHistoryBefore(listOf(message('b',100,1),message('e',1,99))))
        assertEquals(Long.MAX_VALUE,groupHistoryBefore(emptyList()))
    }
    @Test fun publicReferenceHasNoInventedPrivateFieldsOrTextCursor(){
        val native=Native().apply{reference=true};val execution=GroupExecution(native::request)
        val group=execution.snapshot(1,7,1).single();val reference=group.messages.single{it.status=="attachment_reference"}
        assertEquals(0L,reference.timestamp);assertEquals(0L,reference.order);assertEquals(0L,reference.sequence)
        assertTrue(reference.text.contains("unavailable"));assertEquals(1L,groupHistoryBefore(group.messages))
    }
    @Test fun repeatedReferenceProjectionDeduplicatesAndProfileResetDropsIt(){
        val native=Native().apply{reference=true};val execution=GroupExecution(native::request)
        execution.snapshot(1,7,1);execution.load(1,7,1,group)
        assertEquals(2,execution.snapshot(1,7,1).single().messages.size)
        execution.reset();native.reference=false
        assertEquals(1,execution.snapshot(2,8,2).single().messages.size)
    }
}
