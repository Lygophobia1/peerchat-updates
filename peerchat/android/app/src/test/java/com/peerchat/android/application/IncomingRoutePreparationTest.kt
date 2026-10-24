package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class IncomingRoutePreparationTest {
    @Test fun duplicateHintsCannotPostponeBackgroundWorkIndefinitely(){
        val guard=IncomingRoutePreparation()
        guard.begin("peer",100)
        assertTrue(guard.pending(101))
        guard.begin("peer",9000)
        assertFalse(guard.pending(10100))
        guard.begin("peer",11000)
        assertFalse(guard.pending(11001))
        guard.begin("peer",15100)
        assertTrue(guard.pending(15101))
    }
    @Test fun unrelatedPeerDoesNotConsumeCallersPreparationWindow(){
        val guard=IncomingRoutePreparation()
        guard.begin("sync-peer",100);guard.ready("sync-peer")
        guard.begin("caller",200)
        assertTrue(guard.pending(201))
        guard.ready("sync-peer");assertTrue(guard.pending(202))
        guard.ready("caller");assertFalse(guard.pending(203))
    }
    @Test fun concurrentPreparationsHaveIndependentBoundedDeadlines(){
        val guard=IncomingRoutePreparation()
        guard.begin("first",100);guard.begin("second",300)
        assertTrue(guard.pending(10101))
        assertFalse(guard.pending(10300))
    }
    @Test fun retainedCooldownsAreBoundedAndExpire(){
        val guard=IncomingRoutePreparation()
        repeat(32){guard.begin("peer$it",100);guard.ready("peer$it")}
        guard.begin("overflow",200);assertFalse(guard.pending(201))
        guard.begin("new",15100);assertTrue(guard.pending(15101))
    }
    @Test fun OnlyMatchingReadyAndAuthorityResetReleasePreparation(){
        val guard=IncomingRoutePreparation()
        guard.begin("caller",100)
        guard.ready("other")
        assertTrue(guard.pending(200))
        guard.ready("caller")
        assertFalse(guard.pending(201))
        guard.clear();guard.begin("new-authority",202)
        assertTrue(guard.pending(203))
        guard.clear();assertFalse(guard.pending(204))
    }
}
