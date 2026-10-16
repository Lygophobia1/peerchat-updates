package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class TransferDiagnosticReportTest {
    @Test fun retainsLastOfSixtyFourRowsBeyondClipboardSizedCutWithoutUnboundingMemory() {
        val rows=(1..64).map{"transfer=$it "+"x".repeat(1800)}
        val report=transferDiagnosticReport(rows)
        assertTrue(report.length>65536);assertTrue(report.contains("transfer=64 "))
        assertFalse(report.contains("diagnostic_limit_reached"));assertTrue(report.length<=64*2049+128)
    }
    @Test fun explicitLimitsNeverSilentlyDropOversizedRows() {
        val report=transferDiagnosticReport(List(65){"x".repeat(10000)})
        assertTrue(report.contains("diagnostic_limit_reached=true clipped_rows=64 omitted_rows=1"))
        assertTrue(report.length<=64*2049+128)
    }
}
