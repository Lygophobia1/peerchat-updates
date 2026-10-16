package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class BackendLeasePolicyTest {
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
