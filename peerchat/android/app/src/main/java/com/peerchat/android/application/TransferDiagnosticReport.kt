package com.peerchat.android.application

/** Same existing 64-row / 2048-character row limits, without the unrelated
 * clipboard-sized whole-report cut. Memory remains explicitly bounded. */
internal fun transferDiagnosticReport(rows:List<String>):String=buildString {
    var clipped=0
    rows.take(64).forEach{row->appendLine(row.take(2048));if(row.length>2048)clipped++}
    if(clipped>0||rows.size>64)appendLine("diagnostic_limit_reached=true clipped_rows=$clipped omitted_rows=${(rows.size-64).coerceAtLeast(0)}")
}
