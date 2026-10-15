package com.peerchat.android.platform

import org.junit.Assert.*
import org.junit.Test

class RecipientFailureCodeTest {
    @Test fun onlyFixedFailureCodesAreExposed(){
        assertEquals("RECIPIENT_KEY_RECOVERY_REQUIRED",recipientFailureCode(ProviderFailure("RECIPIENT_KEY_RECOVERY_REQUIRED")))
        assertEquals("RECIPIENT_PROVIDER_OPERATION_FAILED",recipientFailureCode(ProviderFailure("secret alias or payload")))
        assertEquals("RECIPIENT_PROVIDER_OPERATION_FAILED",recipientFailureCode(IllegalStateException("secret message")))
    }
}
