package com.peerchat.android.application

import com.peerchat.android.VoiceBridge
import java.util.UUID

data class VoiceSnapshot(val accepted:Boolean=true,val available:Boolean=false,val diagnostic:String="Voice capability not loaded",
    val active:Boolean=false,val id:String="",val peer:String="",val lifetime:Long=0,val state:Long=0,val direction:Long=0,
    val muted:Boolean=false,val deafened:Boolean=false,val mediaState:Long=0,val mediaGeneration:Long=0,val mediaRoute:Long=0,val controlAvailable:Boolean=false,
    val owner:String="",val platformDiagnostic:String="",val speaker:Boolean=false,
    val group:String="",val room:String="",val epoch:String="",val participation:String="",val mediaDiagnostics:String="") {
    val target:VoiceCallTarget? get()=if(active&&owner.isNotEmpty())VoiceCallTarget(owner,peer,id,lifetime,group,room,epoch,participation)else null
}

/** Local application scope, never serialized into peer signaling or storage. */
data class VoiceCallTarget(val owner:String,val peer:String,val id:String,val lifetime:Long,
    val group:String="",val room:String="",val epoch:String="",val participation:String="")
data class VoiceConsent(val token:String,val peer:String,val id:String,val answer:Boolean,val group:String="")
enum class VoiceControl { Decline, End, Mute, Deafen }

/** Identifies exactly one native call lifetime for a deferred local action. */
internal data class CallTarget(val peer:String,val id:String,val lifetime:Long) {
    init {require(peer.matches(Regex("[0-9a-f]{64}"))&&id.matches(Regex("[0-9a-f]{32}"))&&lifetime>0)}
}

/** Opaque JNI execution only. It does not own call state, signaling or retries.
 * AndroidVoice/v1 is a private, unshipped Java/native record domain; both ends
 * change together. It is not the peer signaling protocol.
 */
internal class VoiceExecution(private val native:(Long,Int,ByteArray)->ByteArray?={h,o,b->VoiceBridge.request(h,o,b)}) {
    private val measuredKeys=setOf("capture_frames","encoded_frames","opus_submitted","send_api_accepted","remote_track_callback_packets",
        "payload_delivered","received_packets","playback_frames","aec_render_blocks","aec_reference_underruns","aec_resets","aec_delay_ms",
        "apm_capture_10ms_calls","apm_processed_20ms_frames","apm_raw_bypass_frames","apm_level_frames","opus_encode_failures",
        "media_send_api_failures","opus_encoded_bytes","media_send_buffer_bytes","receive_insert_lock_drops","receive_authorized_sources",
        "receive_active_sources","receive_rtp_accepted","receive_rtp_rejected","receive_insert_busy","receive_failure_code",
        "receive_mixed_10ms_blocks","receive_busy_or_silence_10ms_blocks","neteq_packets_inserted","neteq_insert_failures",
        "neteq_pulled_10ms_blocks","neteq_pull_failures","neteq_current_delay_ms","neteq_target_delay_ms",
        "neteq_concealment_events","neteq_concealed_samples","neteq_fec_packets_received","neteq_fec_packets_discarded",
        "neteq_secondary_decoded_rate_q14","neteq_packets_discarded","neteq_buffer_flushes","media_anomaly_skipped_contention")
    private val timedKeys=setOf("capture_interval","playback_interval","capture_work","apm_capture","apm_render","opus_encode",
        "receive_pull","rtp_arrival_interval","screen_capture_wait").mapTo(mutableSetOf()){ "perf_$it" }
    private fun command(handle:Long,op:Int,account:Long,generation:Long,write:(NativeRecordWriter)->Unit={}):VoiceSnapshot {
        val w=NativeRecordWriter(7);w.text("PeerChat/AndroidVoice/v1");w.number(op.toLong());w.number(account);w.number(generation);write(w)
        val r=NativeRecord(checkNotNull(native(handle,op,w.bytes())),7);require(r.text()=="PeerChat/AndroidVoice/v1"&&r.number()==op.toLong())
        fun flag():Boolean {val value=r.number();require(value in 0..1);return value==1L}
        val accepted=flag();val available=flag();val diagnostic=r.text();require(diagnostic.length<=160)
        val active=flag();require(!active||available)
        val result=if(active)VoiceSnapshot(accepted,available,diagnostic,true,r.text(),r.text(),r.number(),r.number(),r.number(),
            flag(),flag(),r.number(),r.number(),r.number(),flag()) else VoiceSnapshot(accepted,available,diagnostic)
        require(!result.active||(result.id.matches(Regex("[0-9a-f]{32}"))&&result.peer.matches(Regex("[0-9a-f]{64}"))&&result.lifetime>0&&result.state in 0..8&&result.direction in 0..1&&result.mediaState in 0..6&&result.mediaRoute in 0..3))
        // Optional private tail preserves compatibility with older JNI test fixtures.
        val mediaDiagnostics=if(r.hasRemaining()){
            require(r.text()=="PeerChat/AndroidVoiceMediaDiagnostics/v1")
            r.text().also { details ->
                require(details.toByteArray(Charsets.UTF_8).size<=16384)
                require(details=="Media diagnostics unavailable"||details.split('\n').filter { it.isNotEmpty() }.all {
                    val split=it.indexOf('=');if(split<1)false else {
                        val key=it.substring(0,split);val value=it.substring(split+1)
                        (key in measuredKeys&&value.matches(Regex("-?[0-9]+(?:\\.[0-9]+)?")))||
                            (key in timedKeys&&value.matches(Regex("count:[0-9]+;last_us:[0-9]+;max_us:[0-9]+;mean_us:[0-9]+;deadline_misses:[0-9]+")))
                    }
                })
            }
        }else ""
        r.end();return result.copy(mediaDiagnostics=mediaDiagnostics)
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
