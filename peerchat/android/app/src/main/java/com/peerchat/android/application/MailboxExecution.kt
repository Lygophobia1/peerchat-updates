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
internal interface MailboxNative {
    fun identity(handle:Long):ByteArray?
    fun registration(handle:Long,allowed:Boolean):ByteArray?
    fun registered(handle:Long,binding:ByteArray):Int
    fun receive(handle:Long,wire:ByteArray):ByteArray?
    fun receipt(handle:Long,wire:ByteArray):Int
    fun pending(handle:Long):ByteArray?
    fun prepareDevices(handle:Long,id:ByteArray,bindings:ByteArray,allowed:Boolean):ByteArray?
    fun prepare(handle:Long,id:ByteArray,binding:ByteArray,allowed:Boolean):ByteArray?
    fun stored(handle:Long,wire:ByteArray):Int
}
private object JniMailboxNative:MailboxNative {
    override fun identity(handle:Long)=ProfileBridge.publicKey(handle)
    override fun registration(handle:Long,allowed:Boolean)=MailboxBridge.registration(handle,allowed)
    override fun registered(handle:Long,binding:ByteArray)=MailboxBridge.registered(handle,binding)
    override fun receive(handle:Long,wire:ByteArray)=MailboxBridge.receive(handle,wire)
    override fun receipt(handle:Long,wire:ByteArray)=MailboxBridge.receipt(handle,wire)
    override fun pending(handle:Long)=MailboxBridge.pending(handle)
    override fun prepareDevices(handle:Long,id:ByteArray,bindings:ByteArray,allowed:Boolean)=MailboxBridge.prepareDevices(handle,id,bindings,allowed)
    override fun prepare(handle:Long,id:ByteArray,binding:ByteArray,allowed:Boolean)=MailboxBridge.prepare(handle,id,binding,allowed)
    override fun stored(handle:Long,wire:ByteArray)=MailboxBridge.stored(handle,wire)
}
internal class MailboxExecution(private val native:MailboxNative=JniMailboxNative) {
    private var registeredBinding: ByteArray? = null
    private var receiptCursor = ""
    private var phase="not_started"
    var diagnostic="Not serviced"
        private set
    var inboundDiagnostic="receive=not_serviced"
        private set
    var needsContinuation=false
        private set
    private fun encoded(value: ByteArray) = Base64.encodeToString(value, Base64.NO_WRAP)
    private fun decoded(value: String, maximum: Int): ByteArray {
        require(value.length <= ((maximum + 2) / 3) * 4)
        return Base64.decode(value, Base64.NO_WRAP).also { require(it.size in 1..maximum) }
    }
    private fun request(accounts: (String,JSONObject)->JSONObject, operation: String, body: JSONObject): JSONObject {
        phase=operation
        val reply=accounts(operation, body)
        // Server responses are untrusted; only known state codes are retained.
        diagnostic="stage=$phase state="+safeMailboxState(reply.optString("state"))
        return reply
    }
    fun reset() { registeredBinding = null; receiptCursor = ""; needsContinuation=false;diagnostic="Not serviced";inboundDiagnostic="receive=not_serviced" }
    // Completion callback is emitted only after native durable receipt creation
    // AND a real authenticated server acknowledgement. Duplicate native receives
    // still drain their ACK but do not invent another message/notification.
    fun service(accounts: ServerAccounts, handle: Long, storageAllowed: Boolean,
                acknowledged: (Boolean) -> Unit) = service(accounts::mailbox,handle,storageAllowed,acknowledged)
    internal fun service(accounts: (String,JSONObject)->JSONObject, handle: Long, storageAllowed: Boolean,
                acknowledged: (Boolean) -> Unit) {
        try { serviceAttempt(accounts,handle,storageAllowed,acknowledged) }
        catch(failure:Exception){
            diagnostic="stage=$phase result=${failure.javaClass.simpleName} $diagnostic".take(320)
            throw failure // Preserve legitimate session invalidation handling.
        }
    }
    private fun serviceAttempt(accounts: (String,JSONObject)->JSONObject, handle: Long, storageAllowed: Boolean,
                               acknowledged: (Boolean) -> Unit) {
        phase="native_identity";diagnostic="stage=$phase"
        needsContinuation=true
        val identity = checkNotNull(native.identity(handle))
        val signing = JSONObject().put("signingPublicKey", encoded(identity))
        if (registeredBinding == null) {
            phase="native_recipient_registration";diagnostic="stage=$phase"
            var binding = native.registration(handle, storageAllowed)
            repeat(2) {
                if (registeredBinding == null && binding != null) {
                    val reply = request(accounts, "mailbox_register", JSONObject(signing.toString()).put("binding", encoded(binding)))
                    check(reply.getString("state") == "accepted") { "Mailbox registration rejected" }
                    phase="native_registration_accepted";diagnostic="stage=$phase"
                    check(native.registered(handle, checkNotNull(binding)) == 0)
                    phase="native_registration_reconcile";diagnostic="stage=$phase"
                    val current = native.registration(handle, storageAllowed)
                    if(current==null){binding=null;return@repeat}
                    if (current.contentEquals(binding)) registeredBinding = binding else binding = current
                }
            }
            check(registeredBinding!=null||binding==null)
        }
        // Local recipient provisioning is required for receiving, not sealing
        // to a verified remote key or reconciling signed delivery receipts.
        // Only a native null result is isolated. Server/authentication failures
        // and nonzero native acceptance still propagate and fail closed.
        val receiving=registeredBinding!=null
        inboundDiagnostic=if(receiving)"receive=registered" else "receive=blocked_native_registration"
        var accepted=0;var pendingValidation=0;var imported=0
        var fetchedCount=0
        if(receiving){
            val fetched = request(accounts, "mailbox_fetch", JSONObject())
            check(fetched.getString("state") == "ready")
            val envelopes = fetched.getJSONArray("envelopes"); require(envelopes.length() <= 64)
            // Small batches preserve the existing wake deadline; remaining envelopes
            // remain durably server-owned until a later servicing pass.
            fetchedCount=envelopes.length()
            inboundDiagnostic="receive=fetched fetched=$fetchedCount imported=0 acknowledged=0 pending_native_validation=0"
            for(index in 0 until envelopes.length()) {
                if(accepted>=4)break
                val wire = decoded(envelopes.getJSONObject(index).getString("envelope"), 70000)
                phase="native_envelope_validation";diagnostic="stage=$phase"
                val result = native.receive(handle, wire)
                if(result==null){
                    pendingValidation++
                    inboundDiagnostic="receive=pending_native_validation fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
                    continue // Trust, key, decrypt or storage failure: never ACK or invent trust.
                }
                val record = NativeRecord(result, 3)
                val inserted = record.number().also { require(it in 0..1) } == 1L
                val receipt = record.bytes(); record.text(); record.text(); record.end()
                if(inserted)imported++
                inboundDiagnostic="receive=durable_import_receipt_ready fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
                val reply = request(accounts, "mailbox_ack", JSONObject(signing.toString()).put("receipt", encoded(receipt)))
                check(reply.getString("state") == "acknowledged")
                acknowledged(inserted)
                accepted++
                inboundDiagnostic="receive=acknowledged fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
            }
            android.util.Log.i("PeerChatWake","stage=mailbox_receive_batch fetched=${envelopes.length()} acknowledged=$accepted pending_validation=$pendingValidation")
        }
        val reconciled = request(accounts, "mailbox_receipts", JSONObject().put("afterMessageId", receiptCursor))
        check(reconciled.getString("state") == "ready")
        val receipts = reconciled.getJSONArray("receipts"); require(receipts.length() <= 64)
        repeat(receipts.length()) { index ->
            val item = receipts.getJSONObject(index)
            val id = item.getString("messageId"); require(id.matches(Regex("[0-9a-f]{32}")))
            phase="native_receipt_validation";diagnostic="stage=$phase"
            check(native.receipt(handle, decoded(item.getString("receipt"), 2048)) == 0)
            receiptCursor = id
        }
        // Sweep periodically from the beginning: a late receipt may sort before
        // the last cursor. Native verification/dedupe makes reconciliation safe.
        if (receipts.length() < 64) receiptCursor = ""
        needsContinuation=(accepted>=4&&fetchedCount>accepted)||receipts.length()==64
        if (!storageAllowed) {diagnostic="stage=complete registered=$receiving outgoing_storage_allowed=false acknowledged=$accepted pending_native_validation=$pendingValidation"+
            if(receiving)"" else " receive_blocked=native_recipient_registration";return}
        phase="native_pending_uploads";diagnostic="stage=$phase"
        val pending = NativeRecord(checkNotNull(native.pending(handle)), 4)
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
            val prepared = NativeRecord(checkNotNull(native.prepareDevices(handle, id.toByteArray(Charsets.US_ASCII), batch.toByteArray(), true)), 4)
            val devices = prepared.number().also { require(it <= 32) }
            val bindings = List(devices.toInt()) { prepared.bytes() }; prepared.end()
            needsContinuation=needsContinuation||devices>4
            for (binding in bindings.take(4)) {
                phase="native_envelope_prepare";diagnostic="stage=$phase"
                val wire = native.prepare(handle, id.toByteArray(Charsets.US_ASCII),
                    binding, true) ?: continue
                if (wire.isEmpty()) continue // Live route or already Delivered.
                val reply = request(accounts, "mailbox_store", JSONObject(signing.toString()).put("envelope", encoded(wire)))
                if (reply.getString("state") == "stored") check(native.stored(handle, wire) == 0)
            }
        }
        diagnostic="stage=complete registered=$receiving acknowledged=$accepted pending_native_validation=$pendingValidation continuation=$needsContinuation"+
            if(receiving)"" else " receive_blocked=native_recipient_registration"
    }
}

internal fun safeMailboxState(state:String):String=if(state in setOf("accepted","ready","stored","acknowledged",
    "not_authorized","invalid_timestamp","key_conflict_or_revoked","invalid_generation","unknown_key",
    "revoked","recipient_unavailable","recipient_disabled","quota_exceeded",
    "rejected_or_storage_unavailable","malformed_request","invalid_key_or_signature"))state else "unrecognized_or_missing"
