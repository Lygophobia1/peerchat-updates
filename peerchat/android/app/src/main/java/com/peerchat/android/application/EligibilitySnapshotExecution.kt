package com.peerchat.android.application

import android.util.Base64
import com.peerchat.android.MailboxBridge
import com.peerchat.android.platform.ServerAccounts
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.DataOutputStream

/** Only called by the existing authenticated backend owner. A TLS snapshot is
 * eligibility evidence, not trust: native still requires an existing identity
 * pin and verifies any signed HPKE binding. This collector owns no messages,
 * timer, transport or database. Sends never wait for its network request. */
internal class EligibilitySnapshotExecution(
    private val wallNow:()->Long=System::currentTimeMillis,
    private val install:(Long,ByteArray)->Int={handle,wire->MailboxBridge.eligibility(handle,wire)}
) {
    private var nextAt = 0L
    private var offset = 0
    private var context = ""
    private val refreshAfter=linkedMapOf<Long,Long>()
    private data class Deferred(val wire:ByteArray,val issued:Long,val fresh:Long,val expires:Long)
    // Authenticated response bytes only, NOT eligibility authority. Native must
    // still validate the original snapshot after the local clock reaches issuance.
    private val deferred=linkedMapOf<Long,Deferred>()
    @Volatile var diagnostic = "eligibility=waiting_for_authenticated_snapshot"
        private set

    fun reset() { nextAt=0; offset=0; context=""; refreshAfter.clear();deferred.clear();diagnostic="eligibility=waiting_for_authenticated_snapshot" }

    fun refresh(accounts:ServerAccounts,handle:Long,account:Long,generation:Long,device:String,
                recipients:List<Long>,now:Long):Boolean = refreshWith(handle,account,generation,device,recipients,now){recipient->
        accounts.mailbox("mailbox_eligibility_snapshot",JSONObject().put("recipientAccountId",recipient.toString()))
    }

    internal fun refreshWith(handle:Long,account:Long,generation:Long,device:String,
                recipients:List<Long>,now:Long,fetch:(Long)->JSONObject):Boolean {
        val owner="$account/$generation/$device"
        if(owner!=context){reset();context=owner}
        val targets=recipients.distinct().sorted()
        refreshAfter.keys.retainAll(targets.toSet())
        deferred.keys.retainAll(targets.toSet())
        var installed=false
        val retry=deferred.entries.iterator()
        while(retry.hasNext()) {
            val (recipient,value)=retry.next();val utc=wallNow()
            if(now>=value.expires||utc>=value.fresh){
                retry.remove();refreshAfter[recipient]=now+15_000
                diagnostic="eligibility=unavailable_primary_delivery_independent stage=deferred_snapshot reason=expired"
            }else if(utc>=value.issued){
                retry.remove()
                if(install(handle,value.wire)==0){
                    refreshAfter[recipient]=now+refreshDelay(value.fresh,utc);installed=true
                    diagnostic="eligibility=authenticated_snapshot_cached"
                }else {
                    refreshAfter[recipient]=now+15_000
                    diagnostic="eligibility=unavailable_primary_delivery_independent stage=native_verified_eligibility reason=native_scope_freshness_or_binding_rejected"
                }
            }
        }
        if(now<nextAt)return installed
        nextAt=now+60_000
        if(targets.isEmpty())return false
        val selected=(targets.drop(offset%targets.size)+targets.take(offset%targets.size)).filter{it !in deferred&&(refreshAfter[it]?:0)<=now}.take(4)
        offset=(offset+selected.size)%targets.size
        for(recipient in selected) {
            var stage="authenticated_rpc"
            var reason="transport_or_rpc_unavailable"
            try {
                val reply=fetch(recipient)
                stage="snapshot_response";reason=safeState(reply.optString("state"))
                check(reply.optString("state")=="ready")
                stage="snapshot_scope_and_format";reason="response_scope_or_format_rejected"
                val wire=encode(reply,account,generation,device,recipient)
                val utc=wallNow();val issued=reply.getLong("issuedUtcMs");val fresh=reply.getLong("freshUntilUtcMs")
                if(issued>utc&&issued-utc<=60_000&&deferred.size<32){
                    deferred[recipient]=Deferred(wire,issued,fresh,now+minOf(120_000L,fresh-utc))
                    refreshAfter[recipient]=now+1000
                    diagnostic="eligibility=waiting_for_snapshot_issuance stage=deferred_snapshot clock_ahead_ms=${issued-utc}"
                    continue
                }
                stage="native_verified_eligibility";reason="native_scope_freshness_or_binding_rejected"
                check(install(handle,wire)==0)
                diagnostic="eligibility=authenticated_snapshot_cached"
                refreshAfter[recipient]=now+refreshDelay(fresh,utc);installed=true
            } catch(failure:Exception) {
                if(failure is ScopedBackendWork.Superseded)throw failure
                if(expiredServerSession(failure))throw failure
                diagnostic="eligibility=unavailable_primary_delivery_independent stage=$stage reason=$reason exception=${failure.javaClass.simpleName}"
                refreshAfter[recipient]=now+if(reason=="snapshot_quota_exceeded")60_000 else 15_000
            }
        }
        nextAt=targets.minOf{refreshAfter[it]?:now+1000}.coerceAtLeast(now+1000)
        return installed
    }

    companion object {
        internal fun refreshDelay(freshUntilUtcMs:Long,nowUtcMs:Long):Long=
            (freshUntilUtcMs-nowUtcMs-15_000).coerceIn(1_000,60_000)
        internal fun safeState(value:String):String=when(value){
            "ready","rejected_or_storage_unavailable","malformed_request","invalid_key_or_signature","eligibility_too_large","snapshot_quota_exceeded","unsupported_request"->value
            else->"snapshot_not_ready"
        }
        internal fun encode(reply:JSONObject,account:Long,generation:Long,device:String,recipient:Long):ByteArray {
            fun number(key:String):Long {
                val value=reply.get(key);require(value is Number)
                return value.toLong().also {require(it>0&&value.toDouble()==it.toDouble())}
            }
            require(account>0&&generation>0&&recipient>0&&recipient!=account)
            require(device.matches(Regex("[0-9a-f]{64}")))
            require(reply.getString("state")=="ready"&&number("snapshotVersion")==1L)
            require(number("senderAccountId")==account&&reply.getString("senderDevice")==device&&number("recipientAccountId")==recipient)
            val id=reply.getString("snapshotId");require(id.matches(Regex("[0-9a-f]{32}")))
            val sequence=number("snapshotSequence");val issued=number("issuedUtcMs");val fresh=number("freshUntilUtcMs")
            require(fresh>issued&&fresh-issued<=120_000)
            val rows=reply.getJSONArray("candidates");require(rows.length()<=32)
            val output=ByteArrayOutputStream();val writer=DataOutputStream(output)
            fun text(value:String) {val bytes=value.toByteArray(Charsets.UTF_8);writer.writeInt(bytes.size);writer.write(bytes)}
            writer.write("PeerChat/mailbox-eligibility-snapshot/v1\u0000".toByteArray(Charsets.US_ASCII))
            writer.writeLong(account);writer.writeLong(generation);text(device);writer.writeLong(recipient)
            writer.writeInt(1);text(id);writer.writeLong(sequence);writer.writeLong(issued);writer.writeLong(fresh)
            writer.writeByte(rows.length());val seen=hashSetOf<String>()
            repeat(rows.length()) { index ->
                val row=rows.getJSONObject(index);val target=row.getString("device")
                require(target.matches(Regex("[0-9a-f]{64}"))&&seen.add(target))
                val online=row.get("online");require(online is Boolean)
                val binding=row.getString("binding");require(binding.length<=5464)
                val bytes=if(binding.isEmpty())byteArrayOf() else Base64.decode(binding,Base64.NO_WRAP)
                require(bytes.size<=4096&&(online||bytes.isNotEmpty()))
                text(target);writer.writeByte(if(online)1 else 0);writer.writeInt(bytes.size);writer.write(bytes)
            }
            writer.flush();return output.toByteArray().also {require(it.size<=70_000)}
        }
    }
}
