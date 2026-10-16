package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class LocalHistoryProjectionTest {
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
}
