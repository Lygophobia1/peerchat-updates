package com.peerchat.android.platform

import org.junit.Assert.*
import org.junit.Test

class ConnectionStageHistoryTest {
    @Test fun capturesDurationFailureAndLastSuccessWithoutPrivateExceptionText() {
        var nanos=0L
        val history=ConnectionStageHistory({123L},{nanos})
        history.measure("mailbox_fetch"){nanos=7_000_000}
        try{history.measure("relay_attach"){nanos=19_000_000;throw java.io.IOException("secret message /private/path token=secret")}}catch(_:java.io.IOException){}
        val text=history.snapshot()
        assertTrue(text.contains("operation=mailbox_fetch stage=complete elapsed_ms=7"))
        assertTrue(text.contains("operation=relay_attach stage=failed elapsed_ms=12 reason=transport_io"))
        assertFalse(text.contains("secret"));assertFalse(text.contains("/private"))
    }
    @Test fun boundsRowsAndRejectsUnknownOperationLabels() {
        val history=ConnectionStageHistory({1L},{1L})
        repeat(100){history.measure("password=private"){}}
        val rows=history.snapshot().lines().filter{it.startsWith("network_event=")}
        assertEquals(64,rows.size);assertFalse(history.snapshot().contains("private"))
        assertTrue(rows.all{it.contains("operation=other")})
    }
    @Test fun realSupersessionRemainsDistinguishableAndIsRethrown() {
        val history=ConnectionStageHistory({1L},{1L});val failure=ServerRejected("session_superseded")
        try{history.measure("events"){throw failure};fail("Must rethrow")}
        catch(caught:ServerRejected){assertSame(failure,caught)}
        assertTrue(history.snapshot().contains("reason=session_superseded"))
    }
}
