package com.peerchat.android.application

import com.peerchat.android.MailboxBridge
import com.peerchat.android.ProfileBridge
import com.peerchat.android.platform.ServerAccounts
import com.peerchat.android.platform.ServerRejected
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
    fun storeFailure(handle:Long,retry:ByteArray):Int=-1
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
    override fun storeFailure(handle:Long,retry:ByteArray)=MailboxBridge.storeFailure(handle,retry)
}
private fun MailboxNative.scoped(scope:ScopedBackendWork):MailboxNative {
    val target=this
    return object:MailboxNative {
        override fun identity(handle:Long)=scope.local{target.identity(handle)}
        override fun registration(handle:Long,allowed:Boolean)=scope.local{target.registration(handle,allowed)}
        override fun registered(handle:Long,binding:ByteArray)=scope.local{target.registered(handle,binding)}
        override fun receive(handle:Long,wire:ByteArray)=scope.local{target.receive(handle,wire)}
        override fun receipt(handle:Long,wire:ByteArray)=scope.local{target.receipt(handle,wire)}
        override fun pending(handle:Long)=scope.local{target.pending(handle)}
        override fun prepareDevices(handle:Long,id:ByteArray,bindings:ByteArray,allowed:Boolean)=scope.local{target.prepareDevices(handle,id,bindings,allowed)}
        override fun prepare(handle:Long,id:ByteArray,binding:ByteArray,allowed:Boolean)=scope.local{target.prepare(handle,id,binding,allowed)}
        override fun stored(handle:Long,wire:ByteArray)=scope.local{target.stored(handle,wire)}
        override fun storeFailure(handle:Long,retry:ByteArray)=scope.local{target.storeFailure(handle,retry)}
    }
}
internal class MailboxExecution(private val native:MailboxNative=JniMailboxNative) {
    private var registeredBinding: ByteArray? = null
    private var receiptCursor = ""
    private var fetchCursor = ""
    private var legacyReceiptCursor = false
    private val projectedReceiptRows=linkedSetOf<String>()
    private val outgoingRetry = linkedMapOf<String,Pair<Long,Int>>()
    private val deviceRetry = linkedMapOf<String,Pair<Long,Int>>()
    var nextRetryAt=0L
        private set
    private var outgoingOffset = 0
    private val outgoingDeviceOffset=linkedMapOf<String,Int>()
    private var phase="not_started"
    var diagnostic="Not serviced"
        private set
    var inboundDiagnostic="receive=not_serviced"
        private set
    var needsContinuation=false
        private set
    var projectionChanged=false
        private set
    val changedMessageIds=linkedSetOf<String>()
    private data class Received(val inserted:Boolean,val receipt:ByteArray,val sender:String,val messageId:String)
    private fun encoded(value: ByteArray) = Base64.encodeToString(value, Base64.NO_WRAP)
    private fun decoded(value: String, maximum: Int): ByteArray {
        require(value.length <= ((maximum + 2) / 3) * 4)
        return Base64.decode(value, Base64.NO_WRAP).also { require(it.size in 1..maximum) }
    }
    private fun opaqueCursor(value:String):String {require(value.length<=512);return value}
    private fun request(accounts: (String,JSONObject)->JSONObject, operation: String, body: JSONObject): JSONObject {
        phase=operation
        val reply=accounts(operation, body)
        // Server responses are untrusted; only known state codes are retained.
        diagnostic="stage=$phase state="+safeMailboxState(reply.optString("state"))
        return reply
    }
    fun reset() { registeredBinding = null; receiptCursor = "";fetchCursor="";legacyReceiptCursor=false;projectedReceiptRows.clear();outgoingRetry.clear();deviceRetry.clear();nextRetryAt=0;outgoingDeviceOffset.clear();outgoingOffset=0;needsContinuation=false;projectionChanged=false;changedMessageIds.clear();diagnostic="Not serviced";inboundDiagnostic="receive=not_serviced" }
    // Completion callback is emitted only after native durable receipt creation
    // AND a real authenticated server acknowledgement. Duplicate native receives
    // still drain their ACK but do not invent another message/notification.
    fun service(accounts: ServerAccounts, handle: Long, storageAllowed: Boolean,
                committed: (String) -> Unit = {}, acknowledged: (Boolean) -> Unit) =
        service(accounts::mailbox,handle,storageAllowed,committed,acknowledged)
    internal fun service(accounts: (String,JSONObject)->JSONObject, handle: Long, storageAllowed: Boolean,
                committed: (String) -> Unit = {}, acknowledged: (Boolean) -> Unit) {
        serviceUsing(native,accounts,handle,storageAllowed,{id,_->committed(id)},acknowledged)
    }
    internal fun serviceScoped(scope:ScopedBackendWork,accounts:(String,JSONObject)->JSONObject,handle:Long,storageAllowed:Boolean,
                               committed:(String,String)->Unit={_,_->},acknowledged:(Boolean)->Unit){
        serviceUsing(native.scoped(scope),{op,body->scope.network{accounts(op,body)}},handle,storageAllowed,
            {id,sender->scope.local{committed(id,sender)}},{inserted->scope.local{acknowledged(inserted)}})
    }
    private fun serviceUsing(native:MailboxNative,accounts:(String,JSONObject)->JSONObject,handle:Long,storageAllowed:Boolean,
                             committed:(String,String)->Unit,acknowledged:(Boolean)->Unit){
        try { serviceAttempt(native,accounts,handle,storageAllowed,committed,acknowledged) }
        catch(failure:Exception){
            diagnostic="stage=$phase result=${failure.javaClass.simpleName} $diagnostic".take(320)
            throw failure // Preserve legitimate session invalidation handling.
        }
    }
    private fun serviceAttempt(native:MailboxNative,accounts: (String,JSONObject)->JSONObject, handle: Long, storageAllowed: Boolean,
                               committed: (String,String) -> Unit, acknowledged: (Boolean) -> Unit) {
        phase="native_identity";diagnostic="stage=$phase"
        nextRetryAt=0
        needsContinuation=true
        projectionChanged=false
        changedMessageIds.clear()
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
        var processedCount=0
        if(receiving){
            val fetched = request(accounts, "mailbox_fetch", JSONObject().put("cursor",fetchCursor))
            check(fetched.getString("state") == "ready")
            val envelopes = fetched.getJSONArray("envelopes"); require(envelopes.length() <= 64)
            // Small batches preserve the existing wake deadline; remaining envelopes
            // remain durably server-owned until a later servicing pass.
            fetchedCount=envelopes.length()
            inboundDiagnostic="receive=fetched fetched=$fetchedCount imported=0 acknowledged=0 pending_native_validation=0"
            for(index in 0 until envelopes.length()) {
                if(accepted>=4)break
                val item=envelopes.getJSONObject(index)
                val rowCursor=opaqueCursor(item.optString("cursor"))
                phase="native_envelope_validation";diagnostic="stage=$phase"
                val result=try {
                    native.receive(handle,decoded(item.getString("envelope"),70000))
                }catch(failure:Exception){
                    if(failure is ScopedBackendWork.Superseded||expiredServerSession(failure))throw failure
                    pendingValidation++
                    processedCount++
                    if(rowCursor.isNotEmpty())fetchCursor=rowCursor
                    inboundDiagnostic="receive=pending_native_validation fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
                    continue // Malformed/undecryptable row stays server-owned; a safe cursor allows later rows.
                }
                if(result==null){
                    pendingValidation++
                    inboundDiagnostic="receive=pending_native_validation fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
                    processedCount++
                    if(rowCursor.isNotEmpty())fetchCursor=rowCursor
                    continue // Trust, key, decrypt or storage failure: never ACK or invent trust.
                }
                val parsed=try {
                    val record=NativeRecord(result,3)
                    val inserted=record.number().also{require(it in 0..1)}==1L
                    val receipt=record.bytes();val sender=record.text();val messageId=record.text();record.end()
                    Received(inserted,receipt,sender,messageId)
                }catch(failure:Exception){
                    pendingValidation++
                    processedCount++
                    if(rowCursor.isNotEmpty())fetchCursor=rowCursor
                    inboundDiagnostic="receive=pending_native_validation fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
                    continue // No valid receipt available; never ACK this row.
                }
                val (inserted,receipt,sender,messageId)=parsed
                if(inserted){
                    imported++
                    committed(messageId,sender) // Native history is durable even if ACK or later work fails.
                }
                inboundDiagnostic="receive=durable_import_receipt_ready fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
                val reply = request(accounts, "mailbox_ack", JSONObject(signing.toString()).put("receipt", encoded(receipt)))
                check(reply.getString("state") == "acknowledged")
                acknowledged(inserted)
                accepted++
                processedCount++
                if(rowCursor.isNotEmpty())fetchCursor=rowCursor
                inboundDiagnostic="receive=acknowledged fetched=$fetchedCount imported=$imported acknowledged=$accepted pending_native_validation=$pendingValidation"
            }
            // The authenticated server cursor is opaque. Older servers omit it;
            // in that case the next ordinary pass retries the first page.
            if(envelopes.length()==0)fetchCursor=""
            else if(processedCount==envelopes.length())fetchCursor=opaqueCursor(fetched.optString("nextCursor"))
            android.util.Log.i("PeerChatWake","stage=mailbox_receive_batch fetched=${envelopes.length()} acknowledged=$accepted pending_validation=$pendingValidation")
        }
        val receiptRequest=if(legacyReceiptCursor)JSONObject().put("afterMessageId",receiptCursor)
            else JSONObject().put("cursor",receiptCursor)
        val reconciled = request(accounts, "mailbox_receipts", receiptRequest)
        check(reconciled.getString("state") == "ready")
        val receipts = reconciled.getJSONArray("receipts"); require(receipts.length() <= 64)
        var receiptValidationFailures=0
        repeat(receipts.length()) { index ->
            val item = receipts.getJSONObject(index)
            val rowCursor=opaqueCursor(item.optString("cursor"))
            phase="native_receipt_validation";diagnostic="stage=$phase"
            try {
                val id=item.getString("messageId");require(id.matches(Regex("[0-9a-f]{32}")))
                check(native.receipt(handle, decoded(item.getString("receipt"), 2048)) == 0)
                // Native validates every sweep. This bounded display hint avoids
                // rereading unchanged history for an already verified server row.
                if(rowCursor.isEmpty()||projectedReceiptRows.add(rowCursor)){projectionChanged=true;changedMessageIds.add(id)}
                while(projectedReceiptRows.size>4096)projectedReceiptRows.remove(projectedReceiptRows.first())
            }catch(failure:Exception){
                if(failure is ScopedBackendWork.Superseded||expiredServerSession(failure))throw failure
                receiptValidationFailures++ // Unverified row remains on server and is retried next sweep.
            }
            if(rowCursor.isNotEmpty())receiptCursor=rowCursor
        }
        // Sweep periodically from the beginning: a late receipt may sort before
        // the last cursor. Native verification/dedupe makes reconciliation safe.
        legacyReceiptCursor=!reconciled.has("nextCursor")
        receiptCursor=if(receipts.length()==0)"" else if(legacyReceiptCursor){
            if(receipts.length()==64)receipts.getJSONObject(receipts.length()-1).getString("messageId") else ""
        }else opaqueCursor(reconciled.optString("nextCursor"))
        needsContinuation=fetchCursor.isNotEmpty()||receiptCursor.isNotEmpty()
        if (!storageAllowed) {diagnostic="stage=complete registered=$receiving outgoing_storage_allowed=false acknowledged=$accepted pending_native_validation=$pendingValidation receipt_pending_validation=$receiptValidationFailures"+
            if(receiving)"" else " receive_blocked=native_recipient_registration";return}
        phase="native_pending_uploads";diagnostic="stage=$phase"
        val pending = NativeRecord(checkNotNull(native.pending(handle)), 4)
        val count = pending.number(); require(count <= 64)
        val candidates = List(count.toInt()) { Triple(pending.text(), pending.text(), pending.number()) }
        pending.end()
        val now=System.nanoTime()/1_000_000
        val eligible=candidates.filter{(id,_,_)->(outgoingRetry[id]?.first?:0L)<=now}
        val selected=if(eligible.isEmpty())emptyList() else List(minOf(4,eligible.size)){eligible[(outgoingOffset+it)%eligible.size]}
        if(eligible.isNotEmpty())outgoingOffset=(outgoingOffset+selected.size)%eligible.size
        needsContinuation=needsContinuation||eligible.size>selected.size
        var failed=0
        var failureReason="none"
        for ((id, _, account) in selected) try {
            val keysReply = request(accounts, "mailbox_keys", JSONObject().put("recipientAccountId", account))
            if(keysReply.getString("state")!="ready")throw MailboxItemRefused(safeMailboxState(keysReply.optString("state")))
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
            val offset=outgoingDeviceOffset[id]?:0
            val selectedBindings=if(bindings.isEmpty())emptyList() else List(minOf(4,bindings.size)){bindings[(offset+it)%bindings.size]}
            if(bindings.isNotEmpty())outgoingDeviceOffset[id]=(offset+selectedBindings.size)%bindings.size
            while(outgoingDeviceOffset.size>128)outgoingDeviceOffset.remove(outgoingDeviceOffset.keys.first())
            for (binding in selectedBindings) {
                val deviceKey=id+":"+encoded(java.security.MessageDigest.getInstance("SHA-256").digest(binding))
                if((deviceRetry[deviceKey]?.first?:0L)>now)continue
                try {
                phase="native_envelope_prepare";diagnostic="stage=$phase"
                val wire = native.prepare(handle, id.toByteArray(Charsets.US_ASCII),
                    binding, true) ?: continue
                if (wire.isEmpty()) continue // Live route or already Delivered.
                val reply = request(accounts, "mailbox_store", JSONObject(signing.toString()).put("envelope", encoded(wire)))
                if (reply.getString("state") == "stored") {check(native.stored(handle, wire) == 0);projectionChanged=true;changedMessageIds.add(id);deviceRetry.remove(deviceKey)}
                else {
                    val reason=safeMailboxState(reply.optString("state"))
                    val permanent=reason in setOf("not_authorized","invalid_key_or_signature","recipient_key_retired","recipient_key_unavailable","expired_or_invalid_timestamp","conflicting_message")
                    val retry=ByteArrayOutputStream()
                    DataOutputStream(retry).use{ stream ->
                        stream.write("PeerChat/mailbox-store-retry/v1\u0000".toByteArray(Charsets.US_ASCII))
                        val attempts=((deviceRetry[deviceKey]?.second?:0)+1).coerceAtMost(8)
                        stream.writeLong(System.currentTimeMillis()+if(permanent)900_000L else (30_000L shl (attempts-1)).coerceAtMost(900_000L))
                        val text=reason.toByteArray(Charsets.US_ASCII);stream.writeInt(text.size);stream.write(text)
                        stream.writeInt(wire.size);stream.write(wire)
                    }
                    try{native.storeFailure(handle,retry.toByteArray())}catch(_:Exception){/* Supplemental metadata cannot discard the attempt. */}
                    throw MailboxItemRefused(reason)
                }
                } catch(failure:Exception) {
                    if(failure is ScopedBackendWork.Superseded||expiredServerSession(failure))throw failure
                    failed++
                    failureReason=if(failure is MailboxItemRefused)failure.state else "device_attempt_unavailable"
                    val attempts=((deviceRetry[deviceKey]?.second?:0)+1).coerceAtMost(8)
                    val permanent=failureReason in setOf("not_authorized","invalid_key_or_signature","recipient_key_retired","recipient_key_unavailable","expired_or_invalid_timestamp","conflicting_message")
                    deviceRetry[deviceKey]=now+(if(permanent)900_000L else (30_000L shl (attempts-1)).coerceAtMost(900_000L)) to attempts
                    while(deviceRetry.size>256)deviceRetry.remove(deviceRetry.keys.first())
                }
            }
            val deferred=bindings.map{id+":"+encoded(java.security.MessageDigest.getInstance("SHA-256").digest(it))}
            if(deferred.isNotEmpty()&&deferred.all{(deviceRetry[it]?.first?:0L)>now})outgoingRetry[id]=deferred.minOf{checkNotNull(deviceRetry[it]).first} to 0
            else outgoingRetry.remove(id)
        }catch(failure:Exception){
            if(failure is ScopedBackendWork.Superseded||failure is ServerRejected&&com.peerchat.android.application.expiredServerSession(failure))throw failure
            failed++
            failureReason=when(failure){
                is MailboxItemRefused->failure.state
                is ServerRejected->safeMailboxState(failure.code)
                is java.io.IOException->"network_unavailable"
                else->"native_or_validation_unavailable"
            }
            val old=outgoingRetry[id]?.second?:0
            val attempts=(old+1).coerceAtMost(8)
            val terminal=failure is MailboxItemRefused&&failure.state in setOf("not_authorized","revoked","recipient_disabled","malformed_request","invalid_key_or_signature")
            val delay=if(terminal)900000L else (30000L shl (attempts-1)).coerceAtMost(900000L)
            outgoingRetry[id]=now+delay to attempts
            while(outgoingRetry.size>128)outgoingRetry.remove(outgoingRetry.keys.first())
        }
        val pendingIds=candidates.map{it.first}.toSet()
        deviceRetry.keys.removeAll{it.substringBefore(':') !in pendingIds}
        nextRetryAt=(outgoingRetry.values+deviceRetry.values).map{it.first}.filter{it>now}.minOrNull()?:0L
        diagnostic="stage=complete registered=$receiving acknowledged=$accepted pending_native_validation=$pendingValidation receipt_pending_validation=$receiptValidationFailures outgoing_retry=$failed retry_reason=$failureReason continuation=$needsContinuation"+
            if(receiving)"" else " receive_blocked=native_recipient_registration"
    }
}

private class MailboxItemRefused(val state:String):Exception()

internal fun safeMailboxState(state:String):String=if(state in setOf("accepted","ready","stored","acknowledged",
    "not_authorized","invalid_timestamp","key_conflict_or_revoked","invalid_generation","unknown_key",
    "revoked","recipient_unavailable","recipient_disabled","quota_exceeded",
    "rejected_or_storage_unavailable","malformed_request","invalid_key_or_signature",
    "recipient_key_retired","recipient_key_unavailable","expired_or_invalid_timestamp","conflicting_message"))state else "unrecognized_or_missing"
