package com.peerchat.android.ui

import com.peerchat.android.application.*
import com.peerchat.android.platform.ServerRejected
import org.junit.Test
import org.junit.Assert.*

class RememberedAuthenticationTest {
    @Test fun transportFailureRetainsCredentialAndOnlyTransportIsRetried() {
        assertTrue(isRememberedNetworkFailure(java.net.ConnectException()))
        assertTrue(isRememberedNetworkFailure(java.net.SocketTimeoutException()))
        assertFalse(isRememberedNetworkFailure(javax.net.ssl.SSLHandshakeException("test")))
        assertFalse(isRememberedNetworkFailure(ServerRejected("remember_revoked")))
        assertFalse(isRememberedNetworkFailure(java.io.IOException("Malformed frame")))
    }
    @Test fun onlyExplicitCredentialRejectionRetiresCredential() {
        for(code in listOf("remember_revoked","remember_invalid","remember_proof_invalid"))
            assertTrue(isRememberedCredentialRejected(ServerRejected(code)))
        assertFalse(isRememberedCredentialRejected(ServerRejected("server_unavailable")))
        assertFalse(isRememberedCredentialRejected(java.io.IOException()))
    }
}
