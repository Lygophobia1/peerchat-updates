package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class MessagingFailureDiagnosticsTest {
    @Test fun nativeDecisionsRemainDistinctAndFailClosed(){
        assertEquals("accepted",initialFriendPinResult(byteArrayOf(1,0)))
        assertEquals("stale_identity_or_attempt",initialFriendPinResult(byteArrayOf(0,1)))
        assertEquals("no_current_authenticated_friendship",initialFriendPinResult(byteArrayOf(0,2)))
        assertEquals("existing_verified_account_device_requires_explicit_verification",initialFriendPinResult(byteArrayOf(0,3)))
        assertEquals("stored_key_mismatch",initialFriendPinResult(byteArrayOf(0,4)))
        for(value in listOf(null,byteArrayOf(),byteArrayOf(1),byteArrayOf(1,3),byteArrayOf(2,0),byteArrayOf(0,99),byteArrayOf(0,0)))
            assertEquals("decision_unavailable",initialFriendPinResult(value))
    }
    @Test fun untrustedMailboxStatesCannotLeakServerText(){
        for(value in listOf("accepted","ready","invalid_generation","key_conflict_or_revoked","not_authorized","rejected_or_storage_unavailable","malformed_request","invalid_key_or_signature"))
            assertEquals(value,safeMailboxState(value))
        for(value in listOf("","secret payload","accepted\ncredential=hidden","x".repeat(100000)))
            assertEquals("unrecognized_or_missing",safeMailboxState(value))
    }
}
