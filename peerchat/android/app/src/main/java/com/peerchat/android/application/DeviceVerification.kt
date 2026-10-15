package com.peerchat.android.application

import com.peerchat.android.ReliableBridge
import java.util.Base64

/** Immutable presentation of the native TLS identity, not a trust store. */
data class DeviceVerification(val localAccount:Long,val generation:Long,val attempt:Long,val account:Long,
    val contact:String,val presented:String,val previous:List<String>,val proof:String,val live:Boolean=true)
internal fun deviceVerification(bytes:ByteArray,contact:String):DeviceVerification {
    val r=NativeRecord(bytes,3);require(r.text()=="PeerChat/DeviceVerification/v1")
    val local=r.number();val generation=r.number();val attempt=r.number();val account=r.number();val peer=r.text();val count=r.number()
    require(local>0&&generation>0&&attempt>0&&account>0&&peer.matches(Regex("[0-9a-f]{64}"))&&count in 1..32)
    val previous=List(count.toInt()){r.text().also{require(it.matches(Regex("[0-9a-f]{64}")))}};r.end()
    require(previous.distinct().size==previous.size)
    return DeviceVerification(local,generation,attempt,account,contact,peer,previous,Base64.getEncoder().encodeToString(bytes))
}
internal fun verificationDecision(value:DeviceVerification,accept:Boolean):ByteArray =
    Base64.getDecoder().decode(value.proof).also{require(it.size<=4096)}+byteArrayOf(if(accept)1 else 0)
