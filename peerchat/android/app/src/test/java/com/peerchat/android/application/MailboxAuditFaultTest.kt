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

/** Faults in later phases must not hide committed receive or starve outgoing work. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class MailboxAuditFaultTest {
    private class Native : MailboxNative {
        var outgoingIds: List<String> = emptyList()
        var receiptCalls = 0
        var pendingFailure: Exception? = null
        override fun identity(handle: Long) = byteArrayOf(1)
        override fun registration(handle: Long, allowed: Boolean) = byteArrayOf(2)
        override fun registered(handle: Long, binding: ByteArray) = 0
        override fun receive(handle: Long, wire: ByteArray) = NativeRecordWriter(3).also {
            it.number(1)
            it.blob(byteArrayOf(3))
            it.text("sender")
            it.text("message")
        }.bytes()
        override fun receipt(handle: Long, wire: ByteArray): Int { receiptCalls++;return 0 }
        override fun pending(handle: Long) = NativeRecordWriter(4).also { writer ->
            pendingFailure?.let { throw it }
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
        val committed = mutableListOf<String>()
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
            execution.service(request, 1, true,committed={committed+=it}) { callbacks += it }
        })
        assertEquals(listOf("mailbox_register", "mailbox_fetch", "mailbox_ack", "mailbox_receipts"), operations)
        assertEquals(listOf(true), callbacks)
        assertEquals(listOf("message"),committed)
        assertTrue(execution.inboundDiagnostic.contains("receive=acknowledged"))
    }

    @Test fun stableRejectedFirstFourOutgoingCandidatesDoNotStarveFifthAcrossPasses() {
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
        assertEquals(ids, storedAttempts)
        assertTrue(execution.diagnostic.contains("outgoing_retry=1"))
    }

    @Test fun emptyServiceDoesNotRequestConversationReprojection() {
        val execution = MailboxExecution(Native())
        execution.service({ operation, _ ->
            when (operation) {
                "mailbox_register" -> JSONObject().put("state", "accepted")
                "mailbox_fetch" -> JSONObject().put("state", "ready").put("envelopes", JSONArray())
                "mailbox_receipts" -> JSONObject().put("state", "ready").put("receipts", JSONArray())
                else -> error("Unexpected operation: $operation")
            }
        }, 1, true) { error("No inbound ACK expected") }
        assertEquals(false, execution.projectionChanged)
    }

    @Test fun acceptedReceiptRequestsConversationReprojection() {
        val execution = MailboxExecution(Native())
        execution.service({ operation, _ ->
            when (operation) {
                "mailbox_register" -> JSONObject().put("state", "accepted")
                "mailbox_fetch" -> JSONObject().put("state", "ready").put("envelopes", JSONArray())
                "mailbox_receipts" -> JSONObject().put("state", "ready").put("receipts", JSONArray()
                    .put(JSONObject().put("messageId", "a".repeat(32))
                        .put("receipt", Base64.encodeToString(byteArrayOf(4), Base64.NO_WRAP))))
                else -> error("Unexpected operation: $operation")
            }
        }, 1, true) { error("No inbound ACK expected") }
        assertTrue(execution.projectionChanged)
    }

    @Test fun verifiedReceiptRowIsRevalidatedWithoutRepeatedConversationReprojection() {
        val native = Native()
        val execution = MailboxExecution(native)
        val rpc = { operation: String, _: JSONObject ->
            when (operation) {
                "mailbox_register" -> JSONObject().put("state", "accepted")
                "mailbox_fetch" -> JSONObject().put("state", "ready").put("envelopes", JSONArray())
                "mailbox_receipts" -> JSONObject().put("state", "ready").put("receipts", JSONArray()
                    .put(JSONObject().put("messageId", "a".repeat(32))
                        .put("cursor", "row-position")
                        .put("receipt", Base64.encodeToString(byteArrayOf(4), Base64.NO_WRAP))))
                    .put("nextCursor", "row-position")
                else -> error("Unexpected operation: $operation")
            }
        }
        execution.service(rpc, 1, true) { error("No inbound ACK expected") }
        assertTrue(execution.projectionChanged)
        execution.service(rpc, 1, true) { error("No inbound ACK expected") }
        assertEquals(false, execution.projectionChanged)
        assertEquals(2, native.receiptCalls)
    }

    @Test fun receiptProjectionHintSurvivesLaterUnrelatedPendingFailure() {
        val failure = java.io.IOException("controlled pending failure")
        val native = Native().apply { pendingFailure = failure }
        val execution = MailboxExecution(native)
        val rpc = { operation: String, _: JSONObject ->
            when (operation) {
                "mailbox_register" -> JSONObject().put("state", "accepted")
                "mailbox_fetch" -> JSONObject().put("state", "ready").put("envelopes", JSONArray())
                "mailbox_receipts" -> JSONObject().put("state", "ready").put("receipts", JSONArray()
                    .put(JSONObject().put("messageId", "a".repeat(32)).put("cursor", "row-position")
                        .put("receipt", Base64.encodeToString(byteArrayOf(4), Base64.NO_WRAP))))
                    .put("nextCursor", "row-position")
                else -> error("Unexpected operation: $operation")
            }
        }
        assertEquals(failure, assertThrows(java.io.IOException::class.java) {
            execution.service(rpc, 1, true) { error("No inbound ACK expected") }
        })
        assertTrue(execution.projectionChanged)
        assertEquals(setOf("a".repeat(32)), execution.changedMessageIds)
        assertEquals(1, native.receiptCalls)
    }
}
