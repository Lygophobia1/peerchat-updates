package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class MailboxConsentGuardTest {
    @Test fun recoveredEnrollmentCannotFalselyClaimServerStorageWasDisabled(){
        assertTrue(assertThrows(MailboxConsentChangeUnavailable::class.java){requireMailboxConsentChangeAllowed(1)}.recovered)
        assertTrue(assertThrows(MailboxConsentChangeUnavailable::class.java){requireMailboxConsentChangeAllowed(1)}.message!!.contains("remains ON"))
    }
    @Test fun unreadableScopeCannotSilentlySaveAnOffPreference(){
        assertFalse(assertThrows(MailboxConsentChangeUnavailable::class.java){requireMailboxConsentChangeAllowed(-1)}.recovered)
        requireMailboxConsentChangeAllowed(0)
    }
}
