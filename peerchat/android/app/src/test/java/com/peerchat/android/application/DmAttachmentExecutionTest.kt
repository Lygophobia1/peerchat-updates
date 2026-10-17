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
    @Test fun delayedCacheResponseDoesNotHoldLocalSendGate(){
        val gate=Any();val entered=java.util.concurrent.CountDownLatch(1);val release=java.util.concurrent.CountDownLatch(1)
        val committed=java.util.concurrent.CountDownLatch(1);val pool=java.util.concurrent.Executors.newFixedThreadPool(2)
        val execution=DmAttachmentExecution{_,op,input->if(op==12)command()else input}
        val started=System.nanoTime();val waitMillis=java.util.concurrent.atomic.AtomicLong(-1)
        try{
            val cache=pool.submit {execution.serviceOutsideGate(gate){
                execution.borrow(1,{_,_->
                    assertFalse(Thread.holdsLock(gate));entered.countDown();check(release.await(5,java.util.concurrent.TimeUnit.SECONDS));JSONObject().put("state","accepted")
                },{assertTrue(Thread.holdsLock(gate));true},{assertTrue(Thread.holdsLock(gate))},{throw it})
            }}
            assertTrue(entered.await(5,java.util.concurrent.TimeUnit.SECONDS))
            val waiting=System.nanoTime()
            pool.submit {synchronized(gate){waitMillis.set((System.nanoTime()-waiting)/1_000_000);committed.countDown()}}
            assertTrue("Local durable admission waited behind a cache response",committed.await(250,java.util.concurrent.TimeUnit.MILLISECONDS))
            assertEquals("Cache response is still held while local admission succeeds",1L,release.count)
            release.countDown();cache.get(5,java.util.concurrent.TimeUnit.SECONDS)
        }finally{
            release.countDown();pool.shutdown();pool.awaitTermination(5,java.util.concurrent.TimeUnit.SECONDS)
            println("modeled_coordinator_local_admission wait_ms=${waitMillis.get()} total_ms="+(System.nanoTime()-started)/1_000_000+" native_commit_not_measured=true")
        }
    }
    @Test fun staleSuccessOrExpiredSessionCannotApplyOrInvalidateReplacementScope(){
        for(failure in listOf(false,true))for(change in listOf("profile","account","session","generation","revocation","restart","stop")){
            val gate=Any();var current=true;var applied=0;var invalidated=0;val nativeCalls=mutableListOf<Int>()
            val execution=DmAttachmentExecution{_,op,input->nativeCalls.add(op);if(op==12)command()else input}
            execution.serviceOutsideGate(gate){execution.borrow(1,{_,_->
                synchronized(gate){current=false} // Captured scope replaced while transport was borrowed.
                if(failure)throw com.peerchat.android.platform.ServerRejected("session_invalid")
                JSONObject().put("state","accepted")
            },{current},{applied++},{invalidated++})}
            assertEquals("$change/$failure",listOf(12),nativeCalls)
            assertEquals(0,applied);assertEquals(0,invalidated)
        }
    }
    @Test fun currentExpiredSessionAppliesFailureBeforeLockedInvalidation(){
        val gate=Any();val calls=mutableListOf<Int>();var invalidated=false
        val execution=DmAttachmentExecution{_,op,input->calls.add(op);if(op==12)command()else input}
        execution.serviceOutsideGate(gate){execution.borrow(1,{_,_->throw com.peerchat.android.platform.ServerRejected("session_invalid")},
            {true},{fail("Expired session cannot complete")},{failure->
                assertTrue(Thread.holdsLock(gate));assertTrue(expiredServerSession(failure));assertEquals(listOf(12,14),calls);invalidated=true
            })}
        assertTrue(invalidated)
    }
    @Test fun canceledNativeRequestIsNotRetriedOrTurnedIntoAnotherOffer(){
        val gate=Any();val calls=mutableListOf<Int>();var canceled=false;var failures=0
        val execution=DmAttachmentExecution{_,op,_->calls.add(op);if(op==12)command()else {assertTrue(canceled);null}}
        execution.serviceOutsideGate(gate){execution.borrow(1,{_,_->synchronized(gate){canceled=true};JSONObject().put("state","accepted")},
            {true},{fail("Canceled native request must not complete")},{assertTrue(Thread.holdsLock(gate));failures++})}
        assertEquals(listOf(12,13),calls);assertEquals(1,failures)
    }
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
