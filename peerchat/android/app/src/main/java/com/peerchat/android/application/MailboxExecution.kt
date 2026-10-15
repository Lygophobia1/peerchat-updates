package com.peerchat.android.application

import com.peerchat.android.MailboxBridge
import com.peerchat.android.ProfileBridge
import com.peerchat.android.platform.ServerAccounts
import org.json.JSONObject
import android.util.Base64
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream

/** Bounded application execution of opaque native commands. No sockets, timer,
 * outbox, ACK authority, key custody or reconnect policy are owned here. The
 * existing MessagingBackend calls this under its profile gate/operation budget. */
internal class MailboxExecution {
    private var registeredBinding: ByteArray? = null
    private var receiptCursor = ""
    private var phase="not_started"
    var diagnostic="Not serviced"
        private set
    var needsContinuation=false
        private set
    private fun encoded(value: ByteArray) = Base64.encodeToString(value, Base64.NO_WRAP)
    private fun decoded(value: String, maximum: Int): ByteArray {
        require(value.length <= ((maximum + 2) / 3) * 4)
        return Base64.decode(value, Base64.NO_WRAP).also { require(it.size in 1..maximum) }
    }
    private fun request(accounts: ServerAccounts, operation: String, body: JSONObject): JSONObject {
        phase=operation
        val reply=accounts.mailbox(operation, body)
        // Server responses are untrusted; only known state codes are retained.
        diagnostic="stage=$phase state="+safeMailboxState(reply.optString("state"))
        return reply
    }
    fun reset() { registeredBinding = null; receiptCursor = ""; needsContinuation=false;diagnostic="Not serviced" }
    // Completion callback is emitted only after native durable receipt creation
    // AND a real authenticated server acknowledgement. Duplicate native receives
    // still drain their ACK but do not invent another message/notification.
    fun service(accounts: ServerAccounts, handle: Long, storageAllowed: Boolean,
                acknowledged: (Boolean) -> Unit) {
        try { serviceAttempt(accounts,handle,storageAllowed,acknowledged) }
        catch(failure:Exception){
            diagnostic="stage=$phase result=${failure.javaClass.simpleName} $diagnostic".take(320)
            throw failure // Preserve legitimate session invalidation handling.
        }
    }
    private fun serviceAttempt(accounts: ServerAccounts, handle: Long, storageAllowed: Boolean,
                               acknowledged: (Boolean) -> Unit) {
        phase="native_identity";diagnostic="stage=$phase"
        needsContinuation=true
        val identity = checkNotNull(ProfileBridge.publicKey(handle))
        val signing = JSONObject().put("signingPublicKey", encoded(identity))
        if (registeredBinding == null) {
            phase="native_recipient_registration";diagnostic="stage=$phase"
            var binding = checkNotNull(MailboxBridge.registration(handle, storageAllowed))
            repeat(2) {
                if (registeredBinding == null) {
                    val reply = request(accounts, "mailbox_register", JSONObject(signing.toString()).put("binding", encoded(binding)))
                    check(reply.getString("state") == "accepted") { "Mailbox registration rejected" }
                    phase="native_registration_accepted";diagnostic="stage=$phase"
                    check(MailboxBridge.registered(handle, binding) == 0)
                    phase="native_registration_reconcile";diagnostic="stage=$phase"
                    val current = checkNotNull(MailboxBridge.registration(handle, storageAllowed))
                    if (current.contentEquals(binding)) registeredBinding = binding else binding = current
                }
            }
            check(registeredBinding != null)
        }
        val fetched = request(accounts, "mailbox_fetch", JSONObject())
        check(fetched.getString("state") == "ready")
        val envelopes = fetched.getJSONArray("envelopes"); require(envelopes.length() <= 64)
        // Small batches preserve the existing wake deadline; remaining envelopes
        // remain durably server-owned until a later servicing pass.
        var accepted=0;var awaitingVerification=0
        for(index in 0 until envelopes.length()) {
            if(accepted>=4)break
            val wire = decoded(envelopes.getJSONObject(index).getString("envelope"), 70000)
            phase="native_envelope_validation";diagnostic="stage=$phase"
            val result = MailboxBridge.receive(handle, wire)
            if(result==null){awaitingVerification++;continue} // Unverified/changed/invalid: never ACK, never open a peer to obtain trust.
            val record = NativeRecord(result, 3)
            val inserted = record.number().also { require(it in 0..1) } == 1L
            val receipt = record.bytes(); record.text(); record.text(); record.end()
            val reply = request(accounts, "mailbox_ack", JSONObject(signing.toString()).put("receipt", encoded(receipt)))
            check(reply.getString("state") == "acknowledged")
            acknowledged(inserted)
            accepted++
        }
        android.util.Log.i("PeerChatWake","stage=mailbox_receive_batch fetched=${envelopes.length()} acknowledged=$accepted pending_validation=$awaitingVerification")
        val reconciled = request(accounts, "mailbox_receipts", JSONObject().put("afterMessageId", receiptCursor))
        check(reconciled.getString("state") == "ready")
        val receipts = reconciled.getJSONArray("receipts"); require(receipts.length() <= 64)
        repeat(receipts.length()) { index ->
            val item = receipts.getJSONObject(index)
            val id = item.getString("messageId"); require(id.matches(Regex("[0-9a-f]{32}")))
            phase="native_receipt_validation";diagnostic="stage=$phase"
            check(MailboxBridge.receipt(handle, decoded(item.getString("receipt"), 2048)) == 0)
            receiptCursor = id
        }
        // Sweep periodically from the beginning: a late receipt may sort before
        // the last cursor. Native verification/dedupe makes reconciliation safe.
        if (receipts.length() < 64) receiptCursor = ""
        needsContinuation=(accepted>=4&&envelopes.length()>accepted)||receipts.length()==64
        if (!storageAllowed) return
        phase="native_pending_uploads";diagnostic="stage=$phase"
        val pending = NativeRecord(checkNotNull(MailboxBridge.pending(handle)), 4)
        val count = pending.number(); require(count <= 64)
        val candidates = List(count.toInt()) { Triple(pending.text(), pending.text(), pending.number()) }
        pending.end()
        needsContinuation=needsContinuation||count>4
        for ((id, _, account) in candidates.take(4)) {
            val keysReply = request(accounts, "mailbox_keys", JSONObject().put("recipientAccountId", account))
            check(keysReply.getString("state") == "ready")
            val keys = keysReply.getJSONArray("keys"); require(keys.length() <= 32)
            val batch = ByteArrayOutputStream()
            DataOutputStream(batch).use { stream ->
                stream.write("PeerChat/mailbox-recipient-batch/v1\u0000".toByteArray(Charsets.US_ASCII)); stream.writeShort(keys.length())
                repeat(keys.length()) { index -> val binding = decoded(keys.getJSONObject(index).getString("binding"), 4096); stream.writeInt(binding.size); stream.write(binding) }
            }
            phase="native_recipient_resolution";diagnostic="stage=$phase"
            val prepared = NativeRecord(checkNotNull(MailboxBridge.prepareDevices(handle, id.toByteArray(Charsets.US_ASCII), batch.toByteArray(), true)), 4)
            val devices = prepared.number().also { require(it <= 32) }
            val bindings = List(devices.toInt()) { prepared.bytes() }; prepared.end()
            needsContinuation=needsContinuation||devices>4
            for (binding in bindings.take(4)) {
                phase="native_envelope_prepare";diagnostic="stage=$phase"
                val wire = MailboxBridge.prepare(handle, id.toByteArray(Charsets.US_ASCII),
                    binding, true) ?: continue
                if (wire.isEmpty()) continue // Live route or already Delivered.
                val reply = request(accounts, "mailbox_store", JSONObject(signing.toString()).put("envelope", encoded(wire)))
                if (reply.getString("state") == "stored") check(MailboxBridge.stored(handle, wire) == 0)
            }
        }
        diagnostic="stage=complete registered=true acknowledged=$accepted awaiting_verification=$awaitingVerification continuation=$needsContinuation"
    }
}

internal fun safeMailboxState(state:String):String=if(state in setOf("accepted","ready","stored","acknowledged",
    "not_authorized","invalid_timestamp","key_conflict_or_revoked","invalid_generation","unknown_key",
    "revoked","recipient_unavailable","recipient_disabled","quota_exceeded"))state else "unrecognized_or_missing"
