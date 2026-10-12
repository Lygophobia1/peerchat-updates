package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class BackendLeasePolicyTest {
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
