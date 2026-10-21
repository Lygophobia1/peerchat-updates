package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class PendingArchiveRelayDemandsTest {
    private fun scope(i:Int)=ArchiveRelayScope(8,"a".repeat(64),"a".repeat(64),"b".repeat(64),2,3,"%032x".format(i),"e".repeat(32))
    @Test fun boundedDuplicatesCannotExtendWakeLifetime(){
        val queue=PendingArchiveRelayDemands()
        repeat(40){queue.offer(scope(it),1,100)}
        queue.offer(scope(0),1,15000)
        assertEquals((0 until 32).map(::scope),generateSequence{queue.take(1,15099)}.toList())
        assertFalse(queue.hasPending(1,15099))
        queue.offer(scope(0),1,20000);queue.offer(scope(0),1,34999)
        assertNull(queue.take(1,35000))
    }
    @Test fun replacementAndLogoutDiscardOldAuthorityHints(){
        val queue=PendingArchiveRelayDemands()
        queue.offer(scope(0),1,100)
        assertNull(queue.take(2,101))
        queue.offer(scope(1),2,102);queue.clear()
        assertNull(queue.take(2,103))
        queue.offer(scope(2),0,104)
        assertFalse(queue.hasPending(0,105))
    }
}
