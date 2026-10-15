package com.peerchat.android.application

import java.util.Base64

/** Immutable native consent presentation. The native owner validates the exact proof. */
data class MailboxReenrollment(val account:Long,val generation:Long,val oldAccount:Long,val device:String,
    val sourceKey:String,val sourceGeneration:Long,val server:String,val serverFingerprint:String,val proof:String)

internal fun mailboxReenrollment(bytes:ByteArray):MailboxReenrollment {
    require(bytes.size in 4..8192)
    val r=NativeRecord(bytes,3);require(r.text()=="PeerChat/MailboxReenrollment/v1")
    val account=r.number();val generation=r.number();val oldAccount=r.number();val device=r.text()
    val key=r.text();val sourceGeneration=r.number();val server=r.text();val pin=r.text();val nonce=r.text();val binding=r.bytes();r.end()
    require(account>0&&oldAccount>0&&account!=oldAccount&&generation>0&&sourceGeneration>0)
    require(device.matches(Regex("[0-9a-f]{64}"))&&key.matches(Regex("[0-9a-f]{32}"))&&pin.matches(Regex("[0-9a-fA-F]{64}")))
    require(nonce.matches(Regex("[0-9a-f]{32}")))
    require(server.isNotBlank()&&server.length<=512&&server.none{it.isISOControl()}&&binding.isNotEmpty())
    return MailboxReenrollment(account,generation,oldAccount,device,key,sourceGeneration,server,pin,Base64.getEncoder().encodeToString(bytes))
}

internal fun mailboxReenrollmentDecision(value:MailboxReenrollment,accept:Boolean):ByteArray {
    require(value.proof.length<=((8192+2)/3)*4)
    val bytes=Base64.getDecoder().decode(value.proof)
    require(mailboxReenrollment(bytes)==value)
    return bytes+byteArrayOf(if(accept)1 else 0)
}
