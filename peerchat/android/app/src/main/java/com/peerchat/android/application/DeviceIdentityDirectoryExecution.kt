package com.peerchat.android.application

import org.json.JSONObject
import java.util.Base64

internal data class DeviceIdentityEndorsement(val proof:ByteArray,val active:Boolean)
internal data class DeviceIdentitySnapshot(val account:Long,val serverFingerprint:String,val revision:Long,
    val authorityProof:ByteArray,val authorityActive:Boolean,val endorsements:List<DeviceIdentityEndorsement>,
    val possessionProofs:List<ByteArray>)

/** Parses only bounded public directory evidence; native verifies all signatures and trust. */
internal object DeviceIdentityDirectoryExecution {
    private val fingerprint=Regex("[0-9a-f]{64}")
    fun proof(value:String,max:Int):ByteArray {
        require(value.length<=((max+2)/3)*4)
        return Base64.getDecoder().decode(value).also{require(it.size in 1..max)}
    }
    fun snapshot(body:JSONObject,requestedAccount:Long,pin:String,ownAccount:Long):DeviceIdentitySnapshot {
        require(body.getInt("deviceIdentityVersion")==1&&requestedAccount>0&&pin.matches(fingerprint))
        require(body.getString("accountId")==requestedAccount.toString()&&body.getString("serverFingerprint")==pin)
        val revision=body.getLong("revision");require(revision>=0)
        val authority=body.getString("authorityProof").let{if(it.isEmpty())byteArrayOf() else proof(it,2048)}
        require(body.get("authorityActive") is Boolean)
        val active=body.getBoolean("authorityActive")
        require(!active||authority.isNotEmpty())
        val rows=body.getJSONArray("endorsements");require(rows.length()<=32)
        val endorsements=List(rows.length()){i->
            val row=rows.getJSONObject(i);require(row.get("active") is Boolean)
            DeviceIdentityEndorsement(proof(row.getString("proof"),2048),row.getBoolean("active"))
        }
        val possessions=body.getJSONArray("possessionProofs")
        require(possessions.length()<=32&& (requestedAccount==ownAccount||possessions.length()==0))
        return DeviceIdentitySnapshot(requestedAccount,pin,revision,authority,active,endorsements,
            List(possessions.length()){proof(possessions.getString(it),1024)})
    }
    fun command(operation:Int,account:Long,generation:Long,pin:String,started:Long,
        fields:(NativeRecordWriter)->Unit={}):ByteArray {
        require(operation in 0..3&&account>0&&generation>0&&pin.matches(fingerprint)&&started>0)
        return NativeRecordWriter(30).also {w->
            w.text("PeerChat/IdentityDirectoryCommand/v1");w.number(operation.toLong());w.number(account)
            w.number(generation);w.text(pin);w.number(started);fields(w)
        }.bytes()
    }
    fun apply(native:(ByteArray)->ByteArray,snapshot:DeviceIdentitySnapshot,localAccount:Long,generation:Long,
        started:Long,confirmedRoot:String="") {
        require(snapshot.authorityProof.isNotEmpty()) // Unconfigured account conveys no trust evidence.
        require(confirmedRoot.isEmpty()||confirmedRoot.matches(fingerprint))
        val reply=native(command(3,localAccount,generation,snapshot.serverFingerprint,started){w->
            w.number(snapshot.account);w.number(snapshot.revision);w.blob(snapshot.authorityProof)
            w.number(if(snapshot.authorityActive)1 else 0);w.number(snapshot.endorsements.size.toLong())
            snapshot.endorsements.forEach{w.blob(it.proof);w.number(if(it.active)1 else 0)}
            w.text(confirmedRoot)
        })
        check(reply.contentEquals(byteArrayOf(1))){"Native directory rejected the snapshot"}
    }
    fun applyIfVerified(native:(ByteArray)->ByteArray,snapshot:DeviceIdentitySnapshot,localAccount:Long,
        generation:Long,started:Long):Boolean = try {
        apply(native,snapshot,localAccount,generation,started);true
    }catch(_:IllegalArgumentException){false}catch(_:IllegalStateException){false}
    fun challenge(body:JSONObject,account:Long,pin:String):ByteArray {
        require(body.getInt("deviceIdentityVersion")==1&&body.getString("accountId")==account.toString()&&
            body.getString("serverFingerprint")==pin)
        return proof(body.getString("payload"),2048)
    }
    fun encoded(proof:ByteArray):String=Base64.getEncoder().encodeToString(proof)
}
