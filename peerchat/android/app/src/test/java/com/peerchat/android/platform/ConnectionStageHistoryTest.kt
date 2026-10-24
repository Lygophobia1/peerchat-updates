package com.peerchat.android.platform

import org.junit.Assert.*
import org.junit.Test

class ConnectionStageHistoryTest {
    @Test fun cacheOperationTimingLinksDistinctNativeAndActualRpcIdsWithBoundedRetention(){
        var nanos=1_000_000L;val history=ConnectionStageHistory({1L},{nanos})
        val native="a".repeat(32);val rpc="b".repeat(32)
        history.measure("cache_dm_append"){nanos=4_000_000L;history.rpcCorrelation("cache_dm_append",native,rpc,3)}
        val snapshot=history.snapshot()
        assertTrue(snapshot.contains("operation=cache_dm_append stage=complete elapsed_ms=3"))
        assertTrue(snapshot.contains("monotonic_ms=4 elapsed_ms=3 rpc_request_id=$rpc native_request_id=$native"))
        history.rpcCorrelation("cache_dm_append","secret",rpc,3)
        history.rpcCorrelation("cache_secret",native,rpc,3)
        history.rpcCorrelation("cache_dm_read",native,"token/private",3)
        assertEquals(snapshot,history.snapshot())
        repeat(100){history.rpcCorrelation("cache_dm_read",native,rpc,it.toLong())}
        assertEquals(64,history.snapshot().lines().count{it.startsWith("network_rpc ")})
        assertFalse(history.snapshot().contains("secret"));assertFalse(history.snapshot().contains("private"))
    }
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
    @Test fun channelFailureRetainsOnlyAllowlistedPhaseAndCategory(){
        val history=ConnectionStageHistory()
        history.channelFailure("cache_rpc","server_tcp_connected",java.io.IOException("private photo path"))
        assertTrue(history.snapshot().contains("channel=cache_rpc stage=server_tcp_connected reason=transport_io"))
        history.channelFailure("secret","private",java.io.IOException("secret"))
        assertFalse(history.snapshot().contains("secret"));assertFalse(history.snapshot().contains("private"))
    }
    @Test fun realSupersessionRemainsDistinguishableAndIsRethrown() {
        val history=ConnectionStageHistory({1L},{1L});val failure=ServerRejected("session_superseded")
        try{history.measure("events"){throw failure};fail("Must rethrow")}
        catch(caught:ServerRejected){assertSame(failure,caught)}
        assertTrue(history.snapshot().contains("reason=session_superseded"))
    }
}
