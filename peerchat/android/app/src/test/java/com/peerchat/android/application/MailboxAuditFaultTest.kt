package com.peerchat.android.application

import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** Records the current post-ACK failure boundary for the background wake audit. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class MailboxAuditFaultTest {
    private class Native : MailboxNative {
        var outgoingIds: List<String> = emptyList()
        override fun identity(handle: Long) = byteArrayOf(1)
        override fun registration(handle: Long, allowed: Boolean) = byteArrayOf(2)
        override fun registered(handle: Long, binding: ByteArray) = 0
        override fun receive(handle: Long, wire: ByteArray) = NativeRecordWriter(3).also {
            it.number(1)
            it.blob(byteArrayOf(3))
            it.text("sender")
            it.text("message")
        }.bytes()
        override fun receipt(handle: Long, wire: ByteArray) = 0
        override fun pending(handle: Long) = NativeRecordWriter(4).also { writer ->
            writer.number(outgoingIds.size.toLong())
            outgoingIds.forEach { id -> writer.text(id); writer.text("b".repeat(64)); writer.number(2) }
        }.bytes()
        override fun prepareDevices(handle: Long, id: ByteArray, bindings: ByteArray, allowed: Boolean) =
            NativeRecordWriter(4).also { it.number(1); it.blob(byteArrayOf(5)) }.bytes()
        override fun prepare(handle: Long, id: ByteArray, binding: ByteArray, allowed: Boolean) = id
        override fun stored(handle: Long, wire: ByteArray) = 0
    }

    @Test fun acknowledgedInboundCanBeFollowedByReceiptFailureBeforeServiceReturns() {
        val operations = mutableListOf<String>()
        val callbacks = mutableListOf<Boolean>()
        val execution = MailboxExecution(Native())
        val receiptFailure = java.io.IOException("controlled receipt lookup failure")
        val request = { operation: String, _: JSONObject ->
            operations += operation
            when (operation) {
                "mailbox_register" -> JSONObject().put("state", "accepted")
                "mailbox_fetch" -> JSONObject().put("state", "ready").put("envelopes", JSONArray()
                    .put(JSONObject().put("envelope", Base64.encodeToString(byteArrayOf(4), Base64.NO_WRAP))))
                "mailbox_ack" -> JSONObject().put("state", "acknowledged")
                "mailbox_receipts" -> throw receiptFailure
                else -> error("Unexpected operation: $operation")
            }
        }

        assertEquals(receiptFailure, assertThrows(java.io.IOException::class.java) {
            execution.service(request, 1, true) { callbacks += it }
        })
        assertEquals(listOf("mailbox_register", "mailbox_fetch", "mailbox_ack", "mailbox_receipts"), operations)
        assertEquals(listOf(true), callbacks)
        assertTrue(execution.inboundDiagnostic.contains("receive=acknowledged"))
    }

    @Test fun stableRejectedFirstFourOutgoingCandidatesStarveFifthAcrossPasses() {
        val ids = (1..5).map { it.toString(16).padStart(32, '0') }
        val native = Native().apply { outgoingIds = ids }
        val execution = MailboxExecution(native)
        val storedAttempts = mutableListOf<String>()
        val request = { operation: String, body: JSONObject ->
            when (operation) {
                "mailbox_register" -> JSONObject().put("state", "accepted")
                "mailbox_fetch" -> JSONObject().put("state", "ready").put("envelopes", JSONArray())
                "mailbox_receipts" -> JSONObject().put("state", "ready").put("receipts", JSONArray())
                "mailbox_keys" -> JSONObject().put("state", "ready").put("keys", JSONArray()
                    .put(JSONObject().put("binding", Base64.encodeToString(byteArrayOf(6), Base64.NO_WRAP))))
                "mailbox_store" -> {
                    storedAttempts += String(Base64.decode(body.getString("envelope"), Base64.NO_WRAP), Charsets.US_ASCII)
                    JSONObject().put("state", "rejected_or_storage_unavailable")
                }
                else -> error("Unexpected operation: $operation")
            }
        }

        repeat(2) { execution.service(request, 1, true) { error("No inbound ACK expected") } }
        assertEquals(ids.take(4) + ids.take(4), storedAttempts)
        assertTrue(execution.needsContinuation)
    }
}
