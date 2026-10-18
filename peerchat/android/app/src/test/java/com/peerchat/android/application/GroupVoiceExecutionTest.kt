package com.peerchat.android.application

import com.peerchat.android.platform.MediaTurnCredentials
import org.junit.Assert.*
import org.junit.Test

class GroupVoiceExecutionTest {
    private val binding=GroupNativeBinding("a".repeat(32),"b".repeat(32),"c".repeat(32),"d".repeat(32),9)
    private val source=GroupNativeSource(23,"e".repeat(64),"f".repeat(32))
    private fun response(op:Int,active:Boolean=false,accepted:Boolean=true,domain:String="PeerChat/AndroidGroupVoice/v1"):ByteArray =
        NativeRecordWriter(7).also{w->w.text(domain);w.number(op.toLong());w.number(if(accepted)1 else 0);w.number(1);w.text("");
            w.number(if(active)1 else 0);if(active){w.text(binding.group);w.text(binding.room);w.text(binding.epoch);
                w.text(binding.participation);w.number(binding.mediaGeneration);w.number(4);w.number(3);w.number(0);w.number(0)}
            repeat(7){w.number(0)}}.bytes()
    private fun header(r:NativeRecord,op:Int){assertEquals("PeerChat/AndroidGroupVoice/v1",r.text());assertEquals(op.toLong(),r.number());
        assertEquals(71L,r.number());assertEquals(4L,r.number())}
    private fun tuple(r:NativeRecord,value:GroupNativeBinding){assertEquals(value.group,r.text());assertEquals(value.room,r.text());
        assertEquals(value.epoch,r.text());assertEquals(value.participation,r.text());assertEquals(value.mediaGeneration,r.number())}
    @Test fun startCarriesRoomAndAuthenticatedSourceScope() {
        val credentials=MediaTurnCredentials("turn.invalid",3478,"user","secret",2000000000)
        val execution=GroupVoiceExecution{handle,op,bytes->
            assertEquals(17L,handle);assertEquals(1,op);val r=NativeRecord(bytes,7);header(r,op);tuple(r,binding)
            assertEquals(4L,r.number());assertEquals(3L,r.number());assertEquals(1L,r.number());assertEquals(source.account,r.number());
            assertEquals(source.device,r.text());assertEquals(source.participation,r.text());assertEquals("turn.invalid",r.text());
            assertEquals(3478L,r.number());assertEquals("user",r.text());assertEquals("secret",r.text());
            assertEquals(2000000000L,r.number());assertEquals(2L,r.number());r.end();response(op,active=true)
        }
        val snapshot=execution.start(17,71,4,binding,4,3,listOf(source),credentials,2)
        assertTrue(snapshot.accepted);assertTrue(snapshot.active);assertEquals(binding,snapshot.binding)
    }
    @Test fun everyPostStartOperationPreservesTheCompleteObservedTuple() {
        val signal=GroupNativeSignal(source.key,"1".repeat(32),1,"offer","sdp")
        val credentials=MediaTurnCredentials("turn.invalid",3478,"user","secret",2000000000)
        val seen=mutableListOf<Int>()
        val execution=GroupVoiceExecution{_,op,bytes->
            val r=NativeRecord(bytes,7);header(r,op);tuple(r,binding)
            when(op){2->{r.number();r.number();assertEquals(0L,r.number())}
                3->{r.text();r.number();r.text();r.text();r.number();r.number()}
                4->{assertEquals(signal.sourceKey,r.text());assertEquals(signal.routeId,r.text());assertEquals(1L,r.number());
                    assertEquals("offer",r.text());assertEquals("sdp",r.text());assertEquals("0",r.text())}
                6,7->assertEquals(1L,r.number())}
            r.end();seen.add(op);response(op,active=op!=8)
        }
        execution.reconcile(17,71,4,binding,5,3,emptyList());execution.configure(17,71,4,binding,credentials,3)
        execution.signal(17,71,4,binding,signal);execution.takeSignals(17,71,4,binding)
        execution.mute(17,71,4,binding,true);execution.deafen(17,71,4,binding,true);execution.stop(17,71,4,binding)
        assertEquals(listOf(2,3,4,5,6,7,8),seen)
    }
    @Test fun malformedNativeProjectionAndInvalidSignalsFailClosed() {
        assertThrows(IllegalArgumentException::class.java){GroupNativeSignal("bad","1".repeat(32),1,"offer","sdp")}
        assertThrows(IllegalArgumentException::class.java){GroupNativeSignal(source.key,"1".repeat(32),1,"candidate","x".repeat(4097))}
        val execution=GroupVoiceExecution{_,_,_->response(0,domain="PeerChat/AndroidVoice/v1")}
        assertThrows(IllegalArgumentException::class.java){execution.snapshot(17,71,4)}
    }
}
