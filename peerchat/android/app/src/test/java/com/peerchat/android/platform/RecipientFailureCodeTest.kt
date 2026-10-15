package com.peerchat.android.platform

import org.junit.Assert.*
import org.junit.Test

class RecipientFailureCodeTest {
    @Test fun registryFailureClassificationsDoNotExposeMessages(){
        assertEquals("REGISTRY_DATABASE_LOCKED",recipientFailureCode(android.database.sqlite.SQLiteDatabaseLockedException("secret")))
        assertEquals("REGISTRY_SQL_FAILURE",recipientFailureCode(android.database.sqlite.SQLiteException("secret")))
        assertEquals("IDENTITY_RECOVERY_REQUIRED",recipientFailureCode(ProviderFailure("IDENTITY_RECOVERY_REQUIRED")))
    }
    @Test fun firstFailureSurvivesRollbackAndOutgoingUntilNextRegistration(){
        val evidence=RecipientDiagnostic();evidence.begin()
        evidence.record("stage=registry_record_unprotect reason=STORAGE_RECOVERY_REQUIRED")
        evidence.record("stage=registry_execute result=ready")
        evidence.record("stage=recipient_public result=ready")
        assertEquals("stage=registry_record_unprotect reason=STORAGE_RECOVERY_REQUIRED",evidence.value)
        evidence.begin();evidence.record("stage=recipient_public result=ready")
        assertEquals("stage=recipient_public result=ready",evidence.value)
        assertEquals("STORAGE_RECOVERY_REQUIRED",recipientFailureCode(ProviderFailure("STORAGE_RECOVERY_REQUIRED")))
    }
    @Test fun onlyFixedFailureCodesAreExposed(){
        assertEquals("RECIPIENT_KEY_RECOVERY_REQUIRED",recipientFailureCode(ProviderFailure("RECIPIENT_KEY_RECOVERY_REQUIRED")))
        assertEquals("RECIPIENT_PROVIDER_OPERATION_FAILED",recipientFailureCode(ProviderFailure("secret alias or payload")))
        assertEquals("RECIPIENT_PROVIDER_OPERATION_FAILED",recipientFailureCode(IllegalStateException("secret message")))
    }
}
