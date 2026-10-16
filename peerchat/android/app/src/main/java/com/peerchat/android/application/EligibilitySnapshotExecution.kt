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
internal class EligibilitySnapshotExecution {
    private var nextAt = 0L
    private var offset = 0
    private var context = ""
    private val refreshAfter=linkedMapOf<Long,Long>()
    var diagnostic = "eligibility=waiting_for_authenticated_snapshot"
        private set

    fun reset() { nextAt=0; offset=0; context=""; refreshAfter.clear();diagnostic="eligibility=waiting_for_authenticated_snapshot" }

    fun refresh(accounts:ServerAccounts,handle:Long,account:Long,generation:Long,device:String,
                recipients:List<Long>,now:Long):Boolean {
        val owner="$account/$generation/$device"
        if(owner!=context){reset();context=owner}
        if(now<nextAt)return false
        nextAt=now+60_000
        val targets=recipients.distinct().sorted()
        refreshAfter.keys.retainAll(targets.toSet())
        if(targets.isEmpty())return false
        val selected=(targets.drop(offset%targets.size)+targets.take(offset%targets.size)).filter{(refreshAfter[it]?:0)<=now}.take(4)
        offset=(offset+selected.size)%targets.size
        var installed=false
        for(recipient in selected) {
            try {
                val reply=accounts.mailbox("mailbox_eligibility_snapshot",JSONObject().put("recipientAccountId",recipient.toString()))
                val wire=encode(reply,account,generation,device,recipient)
                check(MailboxBridge.eligibility(handle,wire)==0)
                diagnostic="eligibility=authenticated_snapshot_cached"
                refreshAfter[recipient]=now+60_000;installed=true
            } catch(failure:Exception) {
                if(expiredServerSession(failure))throw failure
                diagnostic="eligibility=unavailable_primary_delivery_independent reason=${failure.javaClass.simpleName}"
                refreshAfter[recipient]=now+15_000
            }
        }
        nextAt=targets.minOf{refreshAfter[it]?:now+1000}.coerceAtLeast(now+1000)
        return installed
    }

    companion object {
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
