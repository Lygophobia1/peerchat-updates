package com.peerchat.android.application

import com.peerchat.android.VoiceBridge
import java.util.UUID

data class VoiceSnapshot(val accepted:Boolean=true,val available:Boolean=false,val diagnostic:String="Voice capability not loaded",
    val active:Boolean=false,val id:String="",val peer:String="",val lifetime:Long=0,val state:Long=0,val direction:Long=0,
    val muted:Boolean=false,val deafened:Boolean=false,val mediaState:Long=0,val mediaGeneration:Long=0,val mediaRoute:Long=0,val controlAvailable:Boolean=false)

/** Identifies exactly one native call lifetime for a deferred local action. */
internal data class CallTarget(val peer:String,val id:String,val lifetime:Long) {
    init {require(peer.matches(Regex("[0-9a-f]{64}"))&&id.matches(Regex("[0-9a-f]{32}"))&&lifetime>0)}
}

/** Opaque JNI execution only. It does not own call state, signaling or retries.
 * AndroidVoice/v1 is a private, unshipped Java/native record domain; both ends
 * change together. It is not the peer signaling protocol.
 */
internal class VoiceExecution(private val native:(Long,Int,ByteArray)->ByteArray?={h,o,b->VoiceBridge.request(h,o,b)}) {
    private fun command(handle:Long,op:Int,account:Long,generation:Long,write:(NativeRecordWriter)->Unit={}):VoiceSnapshot {
        val w=NativeRecordWriter(7);w.text("PeerChat/AndroidVoice/v1");w.number(op.toLong());w.number(account);w.number(generation);write(w)
        val r=NativeRecord(checkNotNull(native(handle,op,w.bytes())),7);require(r.text()=="PeerChat/AndroidVoice/v1"&&r.number()==op.toLong())
        fun flag():Boolean {val value=r.number();require(value in 0..1);return value==1L}
        val accepted=flag();val available=flag();val diagnostic=r.text();require(diagnostic.length<=160)
        val active=flag();require(!active||available)
        val result=if(active)VoiceSnapshot(accepted,available,diagnostic,true,r.text(),r.text(),r.number(),r.number(),r.number(),
            flag(),flag(),r.number(),r.number(),r.number(),flag()) else VoiceSnapshot(accepted,available,diagnostic)
        require(!result.active||(result.id.matches(Regex("[0-9a-f]{32}"))&&result.peer.matches(Regex("[0-9a-f]{64}"))&&result.lifetime>0&&result.state in 0..8&&result.direction in 0..1&&result.mediaState in 0..6&&result.mediaRoute in 0..3))
        r.end();return result
    }
    private fun targetCommand(handle:Long,op:Int,account:Long,generation:Long,target:CallTarget,write:(NativeRecordWriter)->Unit={})=
        command(handle,op,account,generation){it.text(target.peer);it.text(target.id);it.number(target.lifetime);write(it)}
    fun snapshot(handle:Long,account:Long,generation:Long)=command(handle,0,account,generation)
    fun start(handle:Long,account:Long,generation:Long,peer:String,id:String=UUID.randomUUID().toString().replace("-","")):VoiceSnapshot {
        require(peer.matches(Regex("[0-9a-f]{64}"))&&id.matches(Regex("[0-9a-f]{32}")))
        return command(handle,1,account,generation){it.text(peer);it.text(id)}
    }
    fun answer(handle:Long,account:Long,generation:Long,target:CallTarget)=targetCommand(handle,2,account,generation,target)
    fun decline(handle:Long,account:Long,generation:Long,target:CallTarget)=targetCommand(handle,3,account,generation,target)
    fun end(handle:Long,account:Long,generation:Long,target:CallTarget)=targetCommand(handle,4,account,generation,target)
    fun mute(handle:Long,account:Long,generation:Long,target:CallTarget,value:Boolean)=targetCommand(handle,5,account,generation,target){it.number(if(value)1 else 0)}
    fun deafen(handle:Long,account:Long,generation:Long,target:CallTarget,value:Boolean)=targetCommand(handle,6,account,generation,target){it.number(if(value)1 else 0)}
    // Called only with the existing authenticated server's live TURN response.
    fun configureIce(handle:Long,account:Long,generation:Long,host:String,port:Int,username:String,password:String,expiresUtc:Long,credentialGeneration:Long)=
        command(handle,7,account,generation){it.text(host);it.number(port.toLong());it.text(username);it.text(password);it.number(expiresUtc);it.number(credentialGeneration)}
}
