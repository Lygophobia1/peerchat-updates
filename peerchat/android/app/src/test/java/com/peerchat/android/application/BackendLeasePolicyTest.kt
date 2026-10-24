package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class BackendLeasePolicyTest {
    @Test fun authenticatedPeerWakeIsBoundedAndDoesNotSurviveItsLease(){
        assertEquals(BackendLeasePolicy(60,true,true),backendLeasePolicy(listOf(60L to false),peerWakeDeadlines=listOf(60)))
        assertEquals(BackendLeasePolicy(30,false,true),backendLeasePolicy(listOf(30L to false),archiveDeadlines=listOf(30)))
        assertEquals(BackendLeasePolicy(Long.MAX_VALUE,true,true),backendLeasePolicy(listOf(null to false,60L to false),peerWakeDeadlines=listOf(60)))
    }

    @Test fun repeatedForegroundAndVoiceLeasesDoNotRestartMetadataButResumingWorkDoes(){
        assertFalse(backendRefreshRequired(true,true,true,true,true,true))
        assertFalse(backendRefreshRequired(true,true,true,false,true,false))
        assertFalse(backendRefreshRequired(true,false,true,false,true,false))
        assertTrue(backendRefreshRequired(false,true,false,true,false,true))
        assertTrue(backendRefreshRequired(true,true,false,true,false,true))
        assertTrue(backendRefreshRequired(true,true,false,false,false,true))
    }
    @Test fun voiceLeaseDoesNotChangeForegroundPolicyButRetainsItAfterMinimize(){
        val foreground=listOf<Pair<Long?,Boolean>>(null to false,30L to false)
        val withVoice=foreground+(Long.MAX_VALUE to true)
        assertEquals(backendLeasePolicy(foreground),backendLeasePolicy(withVoice))
        assertEquals(BackendLeasePolicy(Long.MAX_VALUE,true,true),backendLeasePolicy(withVoice.filter{it.first!=null}))
        assertEquals(BackendLeasePolicy(30,false),backendLeasePolicy(listOf(30L to false)))
    }
    @Test fun archiveWakeDoesNotAcquireFriendOrTransferCapability(){
        assertEquals(BackendLeasePolicy(30,false,true),backendLeasePolicy(listOf(30L to false),listOf(30L)))
        assertEquals(BackendLeasePolicy(90,false,true),backendLeasePolicy(listOf(30L to false,90L to false),listOf(90L)))
        assertEquals(BackendLeasePolicy(1800,true,true),backendLeasePolicy(listOf(1800L to true,30L to false),listOf(30L)))
    }
    @Test fun mailboxWakeNeverAcquiresPeerTransferCapability(){
        assertEquals(BackendLeasePolicy(30,false),backendLeasePolicy(listOf(30L to false)))
        assertFalse(backendLeasePolicy(emptyList()).peerWork)
    }
    @Test fun explicitTransferRetainsBoundedRoutesAcrossLockAndWake(){
        assertEquals(BackendLeasePolicy(1800,true),backendLeasePolicy(listOf(1800L to true)))
        assertEquals(BackendLeasePolicy(1800,true),backendLeasePolicy(listOf(30L to false,1800L to true)))
        assertEquals(BackendLeasePolicy(Long.MAX_VALUE,true),backendLeasePolicy(listOf(null to false,1800L to true)))
        assertEquals(BackendLeasePolicy(30,false),backendLeasePolicy(listOf(30L to false)))
    }
}
