package com.peerchat.android.application
import org.junit.Assert.*
import org.junit.Test
class RelayFailureDiagnosticTest {
    @Test fun failureDetailsAreFixedAndDistinguishTransportFromAdmission(){
        assertEquals("transport_timeout",relayFailureCode(java.net.SocketTimeoutException("secret token")))
        assertEquals("transport_io_failure",relayFailureCode(java.io.IOException("secret payload")))
        assertEquals("native_or_relay_admission_rejected",relayFailureCode(IllegalStateException("private identity")))
        assertEquals("relay_execution_failed",relayFailureCode(Exception("secret")))
    }
}
