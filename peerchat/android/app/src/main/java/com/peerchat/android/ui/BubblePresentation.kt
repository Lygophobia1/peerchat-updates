package com.peerchat.android.ui

import com.peerchat.android.application.AttachmentSnapshot
import java.util.Locale
import kotlin.math.roundToInt

/** Compact labels derived only from the immutable message snapshot already on screen. */
internal fun compactMessageDelivery(message: MessageUi): String {
    if (!message.outgoing) return ""
    if (message.status in setOf(MessageStatus.Saving,MessageStatus.Failed,MessageStatus.Preview))
        return " · ${messageStatusDetail(message.status)}"
    val targets = message.deliveryTargets
    if (targets != null) {
        if (targets.selectionUnresolved) return when {
            targets.received > 0 -> " · Some devices received · selection unresolved"
            targets.pending > 0 -> " · Pending · selection unresolved"
            targets.unavailable > 0 -> " · Unavailable · selection unresolved"
            else -> " · Device status unavailable"
        }
        if (targets.targets.isNotEmpty()) return when {
            targets.received == targets.targets.size -> " · Received"
            targets.received > 0 -> " · Partially received"
            targets.pending > 0 -> " · Pending"
            targets.unavailable > 0 -> " · Unavailable"
            else -> " · Status unavailable"
        }
        return " · Status unavailable"
    }
    return " · ${messageStatusDetail(message.status)}"
}

internal fun messageStatusDetail(status: MessageStatus): String = when (status) {
    MessageStatus.Read -> "Read ✓✓"
    MessageStatus.Delivered -> "Delivered ✓"
    MessageStatus.Failed -> "Failed"
    MessageStatus.Saving -> "Saving locally…"
    MessageStatus.Queued -> "Queued"
    MessageStatus.Stored -> "Stored · awaiting recipient"
    MessageStatus.Preview -> "Local preview · not sent"
    MessageStatus.Sent -> "Sent"
    MessageStatus.Received -> "Received"
}

internal fun attachmentCompactStatus(row: AttachmentSnapshot): String = when (row.state) {
    0 -> "Preparing"
    1 -> "Waiting for recipient"
    2 -> if (row.awaitingAcceptance) "File offered" else "Offer sent"
    3 -> "Transferring"
    4 -> "Complete"
    5 -> "Cancelled"
    6 -> "Failed"
    7 -> "Recovering"
    else -> "Suspended"
}

internal fun attachmentCompactSummary(row: AttachmentSnapshot): String = when (row.state) {
    3 -> "${humanFileSize(row.size)} · ${if(row.size>0)(100.0*row.done/row.size).roundToInt().coerceIn(0,100) else 0}% · Transferring"
    else -> "${humanFileSize(row.size)} · ${attachmentCompactStatus(row)}"
}

internal fun attachmentShowsCompactProgress(row: AttachmentSnapshot): Boolean = row.state == 3 && row.size > 0

internal fun attachmentHasCurrentTransferPhase(row:AttachmentSnapshot):Boolean = row.state in setOf(0,1,2,3,4,7)

internal fun attachmentTransferPhaseText(row:AttachmentSnapshot):String? = row.cachePhase.takeIf{it.isNotEmpty()}?.let{phase->
    if(row.state in setOf(5,6,8))"Last recorded phase (inactive): $phase" else phase
}

private fun humanFileSize(bytes:Long):String = when {
    bytes < 1024 -> "$bytes B"
    bytes < 1024L*1024 -> String.format(Locale.ROOT,"%.1f KB",bytes/1024.0)
    else -> String.format(Locale.ROOT,"%.1f MB",bytes/(1024.0*1024.0))
}
