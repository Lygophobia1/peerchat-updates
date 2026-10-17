package com.peerchat.android.application

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class DmAttachmentExecutionTest {
    private val id="a".repeat(32)
    private fun command(operation:String="cache_dm_append",data:ByteArray=byteArrayOf(-1,-2),upload:Long=1)=NativeRecordWriter(30).also{w->
        w.text("PeerChat/DMCacheCommand/v1");w.number(7);w.text(id);w.text(operation);w.number(8);w.text(id);w.text("b".repeat(32));w.number(32768);w.number(65536);w.number(upload)
        w.blob(ByteArray(0));w.blob(ByteArray(0));w.blob(data)
    }.bytes()
    @Test fun opaqueOwnerKeepsExactGenerationAndCanonicalCiphertext(){
        val calls=mutableListOf<Int>();var reply=ByteArray(0)
        val execution=DmAttachmentExecution{_,op,input->calls.add(op);if(op==12)command()else input.also{reply=it}}
        assertTrue(execution.service({op,body->
            assertEquals("cache_dm_append",op);assertEquals("__4",body.getString("data"));assertEquals(32768,body.getLong("offset"))
            assertFalse(body.has("descriptor"));assertFalse(body.has("recipientAccountId"))
            JSONObject().put("state","accepted").put("cacheVersion",2).put("durableOffset",32770)
        },1))
        assertEquals(listOf(12,13),calls)
        val r=NativeRecord(reply,30);assertEquals("PeerChat/DMCacheReply/v1",r.text());assertEquals(7,r.number());assertEquals(id,r.text());assertEquals("accepted",r.text())
        assertEquals(2,r.number());assertEquals(0,r.number());assertEquals(32770,r.number())
    }
    @Test fun networkFailureUsesFixedNativeFailureAndNoAdapterRetry(){
        var failure=ByteArray(0);val calls=mutableListOf<Int>()
        val execution=DmAttachmentExecution{_,op,input->calls.add(op);if(op==12)command()else input.also{failure=it}}
        execution.service({_,_->throw java.io.IOException("private URL and path")},1)
        assertEquals(listOf(12,14),calls)
        val r=NativeRecord(failure,30);assertEquals("PeerChat/DMCacheFailure/v1",r.text());assertEquals(7,r.number());assertEquals(id,r.text());assertEquals("network_unavailable",r.text());r.end()
    }
    @Test fun malformedBase64OrOversizedReceiptPageCannotCommitReply(){
        for(response in listOf(JSONObject().put("state","accepted").put("data","AA=="),
            JSONObject().put("state","accepted").put("items",org.json.JSONArray().also{a->repeat(9){a.put(JSONObject())}}))){
            val calls=mutableListOf<Int>();val execution=DmAttachmentExecution{_,op,input->calls.add(op);if(op==12)command("cache_dm_read")else input}
            execution.service({_,_->response},1);assertEquals(listOf(12,14),calls)
        }
    }
    @Test fun invalidNativeOperationCannotReachNetwork(){
        var network=false;val execution=DmAttachmentExecution{_,_,_->command("cache_admit")}
        try{execution.service({_,_->network=true;JSONObject()},1);fail()}catch(_:IllegalArgumentException){}
        assertFalse(network)
    }
    @Test fun absentNativeWorkDoesNotCallTransport(){
        val execution=DmAttachmentExecution{_,_,_->ByteArray(0)}
        assertFalse(execution.service({_,_->error("No native command")},1))
    }
    @Test fun explicitUploadFlagIsIndependentOfReadMaximum(){
        val execution=DmAttachmentExecution{_,op,input->if(op==12)command("cache_dm_acquire",upload=0)else input}
        execution.service({_,body->assertFalse(body.getBoolean("upload"));JSONObject().put("state","accepted")},1)
    }
}
