package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class ExplicitDeviceLoginTest {
    @Test fun enrollmentDenialCannotReachPersistenceOrResume() {
        val calls=mutableListOf<String>()
        assertThrows(IllegalStateException::class.java){establishDeviceBoundLogin<String,String>(true,
            enroll={throw IllegalStateException("revoked")},save={calls+="save"},
            retirePasswordSession={calls+="retire"},resume={calls+="resume";"session"},revoke={calls+="revoke"})}
        assertTrue(calls.isEmpty())
    }

    @Test fun failedPersistenceRevokesNewCredentialWithoutPublishingSession() {
        val calls=mutableListOf<String>()
        assertThrows(IllegalStateException::class.java){establishDeviceBoundLogin(true,
            enroll={"credential"},save={throw IllegalStateException("storage unavailable")},
            retirePasswordSession={calls+="retire"},resume={calls+="resume";"session"},revoke={calls+="revoke"})}
        assertEquals(listOf("revoke"),calls)
    }

    @Test fun transientResumeFailureKeepsSuccessfullySavedCredentialForRecovery() {
        var revoked=false
        assertThrows(java.io.IOException::class.java){establishDeviceBoundLogin<String,String>(true,
            enroll={"credential"},save={},retirePasswordSession={},
            resume={throw java.io.IOException("offline")},revoke={revoked=true})}
        assertFalse(revoked)
    }

    @Test fun explicitLoginWithoutRememberStillProvesAndResumesWithoutSaving() {
        val calls=mutableListOf<String>()
        val result=establishDeviceBoundLogin(false,
            enroll={calls+="signed enrollment";"credential"},
            save={calls+="persisted"},
            retirePasswordSession={calls+="password session retired"},
            resume={calls+="signed resume";"bound session"},
            revoke={calls+="revoked"})
        assertEquals(listOf("signed enrollment","password session retired","signed resume"),calls)
        assertEquals("bound session",result.session)
        assertEquals("credential",result.transientCredential)
    }

    @Test fun rememberChoicePersistsOnlyAfterProofAndReturnsNoTransientCredential() {
        val calls=mutableListOf<String>()
        val result=establishDeviceBoundLogin(true,
            enroll={calls+="signed enrollment";"credential"},
            save={calls+="persisted"},
            retirePasswordSession={calls+="password session retired"},
            resume={calls+="signed resume";"bound session"},
            revoke={calls+="revoked"})
        assertEquals(listOf("signed enrollment","persisted","password session retired","signed resume"),calls)
        assertEquals("bound session",result.session)
        assertNull(result.transientCredential)
    }

    @Test fun rejectedProofCannotReturnAuthenticatedSessionAndRevokesTransientCredential() {
        val calls=mutableListOf<String>()
        var authenticated=false
        assertThrows(IllegalStateException::class.java){establishDeviceBoundLogin(false,
            enroll={calls+="signed enrollment";"credential"},
            save={calls+="persisted"},
            retirePasswordSession={calls+="password session retired"},
            resume={calls+="signed resume";throw IllegalStateException("proof rejected")},
            revoke={calls+="revoked"}).also{authenticated=true}}
        assertFalse(authenticated)
        assertEquals(listOf("signed enrollment","password session retired","signed resume","revoked"),calls)
    }
}
