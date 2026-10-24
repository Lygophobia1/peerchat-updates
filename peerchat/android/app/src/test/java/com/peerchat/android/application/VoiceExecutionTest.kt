package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

/** Local record projection tests with an explicit fake JNI port; no audio claim. */
class VoiceExecutionTest {
    private val id="1".repeat(32)
    private val peer="a".repeat(64)
    private val target=CallTarget(peer,id,23)
    private fun response(op:Int=0,accepted:Long=1,available:Long=0,active:Long=0,diagnostic:String="Pinned media unavailable",
                         domain:String="PeerChat/AndroidVoice/v1",state:Long=5,direction:Long=0,lifetime:Long=1,muted:Long=0,
                         mediaState:Long=2,mediaRoute:Long=3,call:String=id,device:String=peer):ByteArray {
        val w=NativeRecordWriter(7);w.text(domain);w.number(op.toLong());w.number(accepted);w.number(available);w.text(diagnostic);w.number(active)
        if(active==1L){w.text(call);w.text(device);w.number(lifetime);w.number(state);w.number(direction);w.number(muted);w.number(0);w.number(mediaState);w.number(1);w.number(mediaRoute);w.number(1)}
        return w.bytes()
    }
    private fun parse(bytes:ByteArray)=VoiceExecution{_,_,_->bytes}.snapshot(17,71,4)
    @Test fun optionalMeasuredDiagnosticsTailIsBoundedAndLegacyRecordsStillDecode(){
        assertEquals("",parse(response()).mediaDiagnostics)
        fun withTail(marker:String,details:String):ByteArray {
            val base=response();val tail=NativeRecordWriter(7).apply{text(marker);text(details)}.bytes()
            return base+tail.copyOfRange(4,tail.size)
        }
        val marker="PeerChat/AndroidVoiceMediaDiagnostics/v1"
        val measured="neteq_packets_inserted=31\nneteq_current_delay_ms=40\nperf_receive_pull=count:3;last_us:12;max_us:17;mean_us:14;deadline_misses:0\n"
        assertEquals(measured,parse(withTail(marker,measured)).mediaDiagnostics)
        assertEquals("Media diagnostics unavailable",parse(withTail(marker,"Media diagnostics unavailable")).mediaDiagnostics)
        for(details in listOf("selected_ice_pair=10.0.0.1", "latest_media_error=password", "neteq_packets_inserted=x",
            "neteq_packets_inserted=1\nsecret=turn-password", "x".repeat(16385)))
            assertThrows(IllegalArgumentException::class.java){parse(withTail(marker,details))}
        assertThrows(IllegalArgumentException::class.java){parse(withTail("wrong",measured))}
    }
    @Test fun nativeNoiseDiagnosticsCannotTurnSuccessfulCallStartIntoPreparationFailure(){
        val details="automatic_noise_level=1\nrnnoise_vad=0.003\nnoise_gate_open=0\nnoise_gate_background=1\nnoise_gate_gain=0.02\nnoise_gate_target=0.02\nnoise_gate_attenuation_db=-33.98\nnoise_floor_dbfs=-48.5\nrnnoise_input_dbfs=-35.5\nrnnoise_output_dbfs=-62.3\nrnnoise_dry_mix=0.05\nnoise_control_outgoing_dbfs=-72.1\n"
        val execution=VoiceExecution{_,op,_->
            val base=response(op=op,available=1,active=1)
            val tail=NativeRecordWriter(7).apply{text("PeerChat/AndroidVoiceMediaDiagnostics/v1");text(details)}.bytes()
            base+tail.copyOfRange(4,tail.size)
        }
        val started=execution.start(17,71,4,peer,id)
        assertTrue(started.accepted);assertTrue(started.active)
        assertEquals(details,started.mediaDiagnostics)
        assertEquals(details,execution.snapshot(17,71,4).mediaDiagnostics)
    }
    @Test fun unavailableSnapshotNeverInventsAnActiveCall(){
        val result=parse(response());assertFalse(result.available);assertFalse(result.active);assertTrue(result.accepted)
        assertEquals("Pinned media unavailable",result.diagnostic);assertEquals("",result.id);assertEquals(0L,result.mediaGeneration)
    }
    @Test fun startUsesExactHandleAccountGenerationAndLeavesUnavailableRejected(){
        var calls=0
        val execution=VoiceExecution{h,op,input->
            ++calls;assertEquals(17L,h);assertEquals(1,op)
            val r=NativeRecord(input,7);assertEquals("PeerChat/AndroidVoice/v1",r.text());assertEquals(1L,r.number())
            assertEquals(71L,r.number());assertEquals(4L,r.number());assertEquals(peer,r.text());assertTrue(r.text().matches(Regex("[0-9a-f]{32}")));r.end()
            response(op=1,accepted=0)
        }
        val result=execution.start(17,71,4,peer)
        assertEquals(1,calls);assertFalse(result.accepted);assertFalse(result.available);assertFalse(result.active)
    }
    @Test fun startPreservesCallerProvidedCallIntent(){
        val execution=VoiceExecution{_,op,input->
            val r=NativeRecord(input,7);r.text();r.number();r.number();r.number()
            assertEquals(peer,r.text());assertEquals(id,r.text());r.end();response(op=op,accepted=0)
        }
        assertFalse(execution.start(17,71,4,peer,id).accepted)
    }
    @Test fun actionOperationsPreserveCurrentNativeScopeAndCompleteCallTarget(){
        val seen=mutableListOf<Int>()
        val execution=VoiceExecution{h,op,input->
            assertEquals(9L,h);val r=NativeRecord(input,7);r.text();assertEquals(op.toLong(),r.number());assertEquals(8L,r.number());assertEquals(12L,r.number())
            if(op in 2..6){assertEquals(peer,r.text());assertEquals(id,r.text());assertEquals(23L,r.number())}
            if(op in 5..6)assertEquals(1L,r.number());r.end();seen.add(op);response(op=op,accepted=0)
        }
        execution.snapshot(9,8,12);execution.answer(9,8,12,target);execution.decline(9,8,12,target);execution.end(9,8,12,target)
        execution.mute(9,8,12,target,true);execution.deafen(9,8,12,target,true)
        assertEquals(listOf(0,2,3,4,5,6),seen)
    }
    @Test fun staleCallTargetsRemainExactAndNativeRejectionIsNotRetried(){
        // Fake JNI port checks transport scope only; native enforcement is
        // covered by the JNI gate. Each stale component must reach it unchanged.
        val stale=listOf(target.copy(peer="b".repeat(64)),target.copy(id="2".repeat(32)),target.copy(lifetime=22))
        for(expected in stale)for(op in 2..6){
            var calls=0
            val execution=VoiceExecution{_,operation,input->
                ++calls;assertEquals(op,operation);val r=NativeRecord(input,7);r.text();r.number();r.number();r.number()
                assertEquals(expected.peer,r.text());assertEquals(expected.id,r.text());assertEquals(expected.lifetime,r.number())
                if(op in 5..6)assertEquals(0L,r.number());r.end();null
            }
            assertThrows(IllegalStateException::class.java){when(op){
                2->execution.answer(9,8,12,expected)
                3->execution.decline(9,8,12,expected)
                4->execution.end(9,8,12,expected)
                5->execution.mute(9,8,12,expected,false)
                else->execution.deafen(9,8,12,expected,false)
            }}
            assertEquals(1,calls)
        }
    }
    @Test fun malformedCallTargetsAndStartIntentsAreRejectedBeforeNative(){
        for(bad in listOf("", "bad", "A".repeat(64), "a".repeat(63),"a".repeat(65)))
            assertThrows(IllegalArgumentException::class.java){CallTarget(bad,id,23)}
        for(bad in listOf("", "bad", "A".repeat(32), "1".repeat(31),"1".repeat(33)))
            assertThrows(IllegalArgumentException::class.java){CallTarget(peer,bad,23)}
        for(bad in listOf(0L,-1L,Long.MIN_VALUE))
            assertThrows(IllegalArgumentException::class.java){CallTarget(peer,id,bad)}
        var calls=0;val execution=VoiceExecution{_,_,_->++calls;response()}
        assertThrows(IllegalArgumentException::class.java){execution.start(17,71,4,"bad",id)}
        assertThrows(IllegalArgumentException::class.java){execution.start(17,71,4,peer,"bad")}
        assertEquals(0,calls)
    }
    @Test fun authenticatedIceUsesSameAccountGenerationAndExactFields(){
        var called=false
        val execution=VoiceExecution{_,op,input->
            called=true;assertEquals(7,op);val r=NativeRecord(input,7);r.text();r.number();assertEquals(71L,r.number());assertEquals(4L,r.number())
            assertEquals("turn.invalid",r.text());assertEquals(3478L,r.number());assertEquals("synthetic-user",r.text());assertEquals("synthetic-password",r.text())
            assertEquals(2000000000L,r.number());assertEquals(19L,r.number());r.end();response(op=7,accepted=0)
        }
        val result=execution.configureIce(17,71,4,"turn.invalid",3478,"synthetic-user","synthetic-password",2000000000,19)
        assertTrue(called);assertFalse(result.accepted);assertFalse(result.available)
    }
    @Test fun activeProjectionPreservesNativeCallAndMediaEnums(){
        val result=parse(response(available=1,active=1,muted=1))
        assertTrue(result.available);assertTrue(result.active);assertEquals(id,result.id);assertEquals(peer,result.peer)
        assertEquals(1L,result.lifetime);assertEquals(5L,result.state);assertEquals(0L,result.direction);assertTrue(result.muted)
        assertEquals(2L,result.mediaState);assertEquals(1L,result.mediaGeneration);assertEquals(3L,result.mediaRoute);assertTrue(result.controlAvailable)
    }
    @Test fun malformedHeadersFlagsAndUnboundedFieldsAreRejected(){
        val bad=listOf(response(domain="other"),response(op=1),response(accepted=2),response(available=2),response(active=2),
            response(available=0,active=1),response(diagnostic="x".repeat(161)),response(available=1,active=1,state=9),
            response(available=1,active=1,direction=2),response(available=1,active=1,muted=2),response(available=1,active=1,lifetime=0),
            response(available=1,active=1,mediaState=7),response(available=1,active=1,mediaRoute=4),response(available=1,active=1,call="bad"),
            response(available=1,active=1,device="bad"),response()+byteArrayOf(0),response().copyOf(12),ByteArray(262145))
        bad.forEach{bytes->assertThrows(IllegalArgumentException::class.java){parse(bytes)}}
        assertThrows(IllegalStateException::class.java){VoiceExecution{_,_,_->null}.snapshot(17,71,4)}
    }
    @Test fun oversizedInputNeverReachesNativePort(){
        var called=false;val execution=VoiceExecution{_,_,_->called=true;response()}
        assertThrows(IllegalArgumentException::class.java){execution.start(17,71,4,"a".repeat(65537))};assertFalse(called)
    }
}
