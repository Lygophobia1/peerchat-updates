package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class LocalHistoryProjectionTest {
    @Test fun idleSnapshotsAreReusedButEventsNeverWaitForReconciliation(){
        val projection=LocalHistoryProjection();val f=friend(1);var reads=0
        repeat(60){projection.refreshOne(listOf(f),it*1000L){reads++;emptyList()}}
        assertEquals(1,reads)
        projection.invalidateUrgent(f);projection.refreshOne(listOf(f),59_001){reads++;emptyList()}
        assertEquals(2,reads)
        projection.refreshOne(listOf(f),119_001){reads++;emptyList()};assertEquals(3,reads)
    }
    private fun friend(id:Long)=FriendIdentity(id,"contact","a".repeat(64),false)
    @Test fun readsOneBoundedPageAndReusesItUntilInvalidated(){
        val cache=LocalHistoryProjection();val friends=listOf(friend(1),friend(2));val reads=mutableListOf<Long>()
        assertTrue(cache.rows(friends[0]).isEmpty());assertTrue(reads.isEmpty())
        val read:(FriendIdentity)->List<MessageSnapshot> = {reads.add(it.accountId);emptyList()}
        cache.refreshOne(friends,0,read);assertEquals(listOf(1L),reads)
        cache.refreshOne(friends,1,read);assertEquals(listOf(1L,2L),reads)
        cache.refreshOne(friends,9999,read);assertEquals(2,reads.size)
        cache.invalidate(friends[1]);cache.refreshOne(friends,9999,read);assertEquals(2L,reads.last())
        cache.clear();cache.refreshOne(friends,10000,read);assertEquals(1L,reads.last())
    }
    @Test fun deviceChangeCannotExposeCachedRows(){
        val cache=LocalHistoryProjection();val f=friend(1)
        val rows=listOf(MessageSnapshot("c".repeat(32),"a".repeat(64),"b".repeat(64),1,1,1,2,"fixture"))
        cache.put(f,rows,0);assertEquals(rows,cache.rows(f))
        assertTrue(cache.rows(f.copy(peerId="b".repeat(64),knownPeerIds=listOf("b".repeat(64)))).isEmpty())
        assertThrows(IllegalArgumentException::class.java){cache.put(f,List(9){rows.single()},0)}
    }
    @Test fun durableInboundSenderGetsImmediatePageEvenWithEarlierDirtyFriend(){
        val cache=LocalHistoryProjection();val first=friend(1);val sender=friend(2)
        cache.put(first,emptyList(),0);cache.put(sender,emptyList(),0)
        cache.invalidate(first);cache.invalidateUrgent(sender)
        val reads=mutableListOf<Long>()
        cache.refreshOne(listOf(first,sender),1){reads+=it.accountId;emptyList()}
        assertEquals(listOf(2L),reads)
        assertTrue(cache.hasPending(listOf(first,sender)))
        cache.refreshOne(listOf(first,sender),2){reads+=it.accountId;emptyList()}
        assertEquals(listOf(2L,1L),reads)
        assertFalse(cache.hasPending(listOf(first,sender)))
    }
    @Test fun exactNativeLiveRowsUpdateCleanPagesWithoutReadingHistoricalRecords(){
        val cache=LocalHistoryProjection();val f=friend(1)
        val queued=MessageSnapshot("c".repeat(32),"b".repeat(64),f.peerId,1,1,1,0,"fixture")
        cache.put(f,listOf(queued),0)
        val nativeAck=queued.copy(delivery=2)
        assertTrue(cache.applyNativeMessage(f,nativeAck,1))
        val incoming=MessageSnapshot("d".repeat(32),f.peerId,"b".repeat(64),2,2,2,2,"reply")
        assertTrue(cache.applyNativeMessage(f,incoming,2))
        cache.refreshOne(listOf(f),3){fail("Live row caused full history reread");emptyList()}
        assertEquals(listOf(nativeAck,incoming),cache.rows(f))
        var reconciled=false;cache.refreshOne(listOf(f),60_000){reconciled=true;listOf(nativeAck,incoming)}
        assertTrue("Live updates postponed full reconciliation",reconciled)
    }
    @Test fun liveRowCannotClearUnrelatedDirtyHistoryOrCrossDeviceScope(){
        val cache=LocalHistoryProjection();val f=friend(1)
        val row=MessageSnapshot("c".repeat(32),f.peerId,"b".repeat(64),1,1,1,2,"fixture")
        assertFalse(cache.applyNativeMessage(f,row,0))
        cache.put(f,emptyList(),0);cache.invalidate(f)
        assertFalse(cache.applyNativeMessage(f,row,1));assertTrue(cache.hasPending(listOf(f)))
        cache.put(f,emptyList(),2)
        assertFalse(cache.applyNativeMessage(f,row.copy(senderDevice="c".repeat(64)),3))
        assertFalse(cache.applyNativeMessage(f.copy(knownPeerIds=listOf(f.peerId,"d".repeat(64))),row,3))
        assertTrue(cache.rows(f).isEmpty())
    }
    @Test fun verifiedReceiptInvalidatesOnlyTheCachedMessageOwner(){
        val cache=LocalHistoryProjection();val first=friend(1);val recipient=friend(2)
        val id="c".repeat(32)
        cache.put(first,emptyList(),0)
        cache.put(recipient,listOf(MessageSnapshot(id,"a".repeat(64),"b".repeat(64),1,1,1,2,"fixture")),0)
        assertTrue(cache.invalidateMessages(setOf(id)))
        val reads=mutableListOf<Long>()
        cache.refreshOne(listOf(first,recipient),1){reads+=it.accountId;emptyList()}
        assertEquals(listOf(2L),reads)
        assertFalse(cache.hasPending(listOf(first,recipient)))
    }
}

class OwnerProjectionCadenceTest {
    @Test fun idlePassesReuseProjectionUntilDueOrNativeEvent() {
        val cadence=OwnerProjectionCadence()
        var fullNativeReads=0
        fun ownerPass(now:Long){if(cadence.due(now)){fullNativeReads++;cadence.published(now,false)}}
        ownerPass(100)
        repeat(79){ownerPass(225L+it*125)}
        assertEquals(1,fullNativeReads)
        cadence.dirty()
        ownerPass(10101)
        assertEquals(2,fullNativeReads)
        ownerPass(20101)
        assertEquals(3,fullNativeReads)
    }

    @Test fun attachedPeerWorkKeepsOneSecondProjectionCadence() {
        val cadence=OwnerProjectionCadence()
        cadence.published(100,true)
        assertFalse(cadence.due(1099))
        assertTrue(cadence.due(1100))
    }
}
