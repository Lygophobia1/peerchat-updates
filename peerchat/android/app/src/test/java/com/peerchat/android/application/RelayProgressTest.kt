package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class RelayProgressTest {
    @Test fun distinguishesRendezvousNativeOpenAndAuthenticatedPeerWithoutInventingLatency() {
        var now=0L;val progress=RelayProgress({123L},{now})
        progress.enter(RelayStage.RelayRendezvous);now=15_000_000
        progress.enter(RelayStage.NativePeerTlsOpen);now=20_000_000
        progress.enter(RelayStage.PeerTlsReceive)
        val pending=progress.snapshot()
        assertTrue(pending.contains("RelayRendezvous:15ms"));assertTrue(pending.contains("NativePeerTlsOpen:5ms"))
        assertFalse(pending.contains("stage=AuthenticatedPeer"))
        progress.enter(RelayStage.AuthenticatedPeer)
        assertTrue(progress.snapshot().contains("stage=AuthenticatedPeer"))
    }
    @Test fun closedAttemptCannotBeReopenedAndTransitionStorageStaysBounded() {
        var now=0L;val progress=RelayProgress({1L},{now})
        repeat(30){now+=1_000_000;progress.enter(if(it%2==0)RelayStage.PeerTlsReceive else RelayStage.AuthenticatedPeer)}
        assertTrue(progress.snapshot().count{it==','}<=7)
        progress.enter(RelayStage.Closed);progress.enter(RelayStage.AuthenticatedPeer)
        assertTrue(progress.snapshot().contains("stage=Closed"))
    }
}
