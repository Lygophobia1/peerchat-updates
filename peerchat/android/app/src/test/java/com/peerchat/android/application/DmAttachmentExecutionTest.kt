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
    @Test fun readinessProgressRequiresAcceptedCurrentNativeEnrollmentReply(){
        for(op in listOf("cache_dm_capability","cache_dm_status"))
        for(state in listOf("accepted","cache_unavailable"))
        for(current in listOf(false,true))
        for(committable in listOf(false,true)){
            val execution=DmAttachmentExecution{_,operation,input->if(operation==12)command(op)else if(committable)input else null}
            execution.serviceOutsideGate(Any()){
                execution.borrow(1,{_,_->JSONObject().put("state",state)},{current},{},{})
            }
            assertEquals(if(op=="cache_dm_capability"&&state=="accepted"&&current&&committable)1L else 0L,execution.readinessProgress)
        }
    }
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
    @Test fun alreadyBorrowedRpcStartsWhileGateIsBusyAndReplyStillChecksScopeUnderGate(){
        for(stale in listOf(false,true)){
            val gate=Any();val held=java.util.concurrent.CountDownLatch(1);val release=java.util.concurrent.CountDownLatch(1)
            val rpc=java.util.concurrent.CountDownLatch(1);val pool=java.util.concurrent.Executors.newFixedThreadPool(2)
            var current=true;var applied=0;val calls=java.util.Collections.synchronizedList(mutableListOf<Int>())
            val execution=DmAttachmentExecution{_,op,input->calls.add(op);if(op==12)command("cache_dm_status")else {assertTrue(Thread.holdsLock(gate));input}}
            val borrowed=synchronized(gate){checkNotNull(execution.borrow(1,{_,_->
                assertFalse(Thread.holdsLock(gate));rpc.countDown();JSONObject().put("state","accepted")
            },{assertTrue(Thread.holdsLock(gate));current},{assertTrue(Thread.holdsLock(gate));applied++},{throw it}))}
            try{
                val holder=pool.submit{synchronized(gate){held.countDown();check(release.await(5,java.util.concurrent.TimeUnit.SECONDS));if(stale)current=false}}
                assertTrue(held.await(5,java.util.concurrent.TimeUnit.SECONDS))
                val transfer=pool.submit<Boolean>{execution.serviceBorrowedOutsideGate(gate,borrowed)}
                assertTrue("Ready cache RPC waited for a second borrow gate",rpc.await(5,java.util.concurrent.TimeUnit.SECONDS))
                assertFalse("Reply applied while another owner held the gate",transfer.isDone)
                assertEquals(listOf(12),calls.toList())
                release.countDown();holder.get(5,java.util.concurrent.TimeUnit.SECONDS)
                assertEquals(!stale,transfer.get(5,java.util.concurrent.TimeUnit.SECONDS))
                assertEquals(if(stale)0 else 1,applied);assertEquals(if(stale)listOf(12)else listOf(12,13),calls.toList())
            }finally{release.countDown();pool.shutdownNow();pool.awaitTermination(5,java.util.concurrent.TimeUnit.SECONDS)}
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
    @Test fun cacheReplySurvivesUiHandoffButNotLossOfTransferPermission() {
        for(revoke in listOf(false,true)) {
            val gate=Any();val lifetime=TransferWorkLifetime();lifetime.update(true)
            val captured=lifetime.revision;var commits=0
            val execution=DmAttachmentExecution{_,op,input->if(op==12)command()else input.also{commits++}}
            execution.serviceOutsideGate(gate){execution.borrow(1,{_,_->
                lifetime.update(!revoke);lifetime.update(true)
                JSONObject().put("state","accepted").put("durableOffset",32770)
            },{lifetime.revision==captured},{},{throw it})}
            assertEquals(if(revoke)0 else 1,commits)
        }
    }
    @Test fun staleScopeAtInitialBorrowCannotDispatchRpc(){
        val gate=Any();var dispatched=false;var committed=false
        val execution=DmAttachmentExecution{_,op,input->if(op==12)command("cache_dm_status")else input.also{committed=true}}
        assertEquals(0,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->dispatched=true;JSONObject()},
            {assertTrue(Thread.holdsLock(gate));false},{},{throw it})})
        assertFalse(dispatched);assertFalse(committed)
    }
    private fun localWork(generation:Long=7)=NativeRecordWriter(30).also{it.text("PeerChat/DMCacheLocalWork/v1");it.number(generation)}.bytes()
    @Test fun localPreparationContinuesToRealRequestWithoutNetworkOrFakeReplies(){
        val gate=Any();var steps=0;var requests=0;var applied=0;val calls=mutableListOf<Int>()
        val execution=DmAttachmentExecution{_,op,input->
            assertTrue(Thread.holdsLock(gate));calls.add(op)
            if(op==12)when(steps++){0,1,2->localWork();3->command("cache_dm_status");else->ByteArray(0)}else input
        }
        val completed=execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->
            assertFalse(Thread.holdsLock(gate));requests++;JSONObject().put("state","accepted")
        },{assertTrue(Thread.holdsLock(gate));true},{applied++},{throw it})}
        assertEquals(4,completed);assertEquals(1,requests);assertEquals(1,applied)
        assertEquals(listOf(12,12,12,12,13,12),calls)
    }
    @Test fun localPreparationUsesExistingCountAndTimeBudgets(){
        for(timed in listOf(false,true)){
            val gate=Any();var clock=0L;var steps=0
            val execution=DmAttachmentExecution{_,op,_->assertEquals(12,op);assertTrue(Thread.holdsLock(gate));steps++;if(timed)clock+=10_000_000;localWork()}
            val count=execution.serviceBurstOutsideGate(gate,{clock}){execution.borrow(1,{_,_->error("Local preparation reached transport")},
                {assertTrue(Thread.holdsLock(gate));true},{error("Local preparation fabricated an applied reply")},{throw it})}
            assertEquals(if(timed)10 else 16,count);assertEquals(count,steps)
        }
    }
    @Test fun localPreparationStopsOnEmptyWorkAndStaleScope(){
        val gate=Any();var current=true;var steps=0
        val execution=DmAttachmentExecution{_,op,_->assertEquals(12,op);steps++;current=false;localWork()}
        assertEquals(0,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Stale local work reached transport")},
            {assertTrue(Thread.holdsLock(gate));current},{error("Local work fabricated a reply")},{throw it})})
        assertEquals(1,steps)
        current=true
        val local=DmAttachmentExecution{_,op,_->assertEquals(12,op);localWork()}
        val borrowed=synchronized(gate){checkNotNull(local.borrow(1,{_,_->error("Local work reached transport")},
            {assertTrue(Thread.holdsLock(gate));current},{error("Local work fabricated a reply")},{throw it}))}
        current=false;assertFalse(local.serviceBorrowedOutsideGate(gate,borrowed))
    }
    @Test fun localPreparationBorrowedAfterAppendIsKeptWithoutSendingOrAckingIt(){
        val gate=Any();var next=0;var replies=0;var applied=0;var batches=0
        val execution=DmAttachmentExecution{_,op,input->assertTrue(Thread.holdsLock(gate));if(op==12)when(next++){0->command();1->localWork();else->ByteArray(0)}else input.also{assertEquals(13,op);replies++}}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{requests->assertFalse(Thread.holdsLock(gate));batches++;assertEquals(listOf("cache_dm_append"),requests.map{it.first});requests.map{JSONObject().put("state","accepted")}}
        assertEquals(2,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Unexpected serial request")},
            {assertTrue(Thread.holdsLock(gate));true},{applied++},{throw it},pipe)})
        assertEquals(1,batches);assertEquals(1,replies);assertEquals(1,applied)
    }
    @Test fun directServiceRecognizesLocalWorkWithoutTransportOrReply(){
        val calls=mutableListOf<Int>()
        val execution=DmAttachmentExecution{_,op,_->calls.add(op);check(op==12);localWork()}
        assertTrue(execution.service({_,_->error("Local work reached transport")},1))
        assertEquals(listOf(12),calls)
    }
    @Test fun malformedLocalMarkersNeverReachNetworkOrNativeReply(){
        for(bytes in listOf(localWork(0),localWork()+byteArrayOf(1))){
            val execution=DmAttachmentExecution{_,op,_->assertEquals(12,op);bytes}
            try{execution.service({_,_->error("Malformed local work reached transport")},1);fail("Malformed local marker accepted")}catch(_:IllegalArgumentException){}
        }
    }
    @Test fun readyBurstIsBoundedAndDoesNotWaitForAnotherOwnerTick(){
        val gate=Any();var remaining=40;var applied=0
        val execution=DmAttachmentExecution{_,op,input->if(op==12){if(remaining-->0)command() else ByteArray(0)}else input}
        fun prepare()=execution.borrow(1,{_,_->assertFalse(Thread.holdsLock(gate));JSONObject().put("state","accepted")},{true},{applied++},{throw it})
        assertEquals(16,execution.serviceBurstOutsideGate(gate,{0}){prepare()})
        assertEquals(16,applied)
        assertEquals(16,execution.serviceBurstOutsideGate(gate,{0}){prepare()})
        assertEquals(8,execution.serviceBurstOutsideGate(gate,{0}){prepare()})
        assertEquals(40,applied)
    }
    @Test fun burstYieldsAfterSlowRpcAndStopsOnNetworkFailure(){
        val gate=Any();var clock=0L;var requests=0
        val execution=DmAttachmentExecution{_,op,input->if(op==12)command()else input}
        assertEquals(1,execution.serviceBurstOutsideGate(gate,{clock}){execution.borrow(1,{_,_->requests++;clock+=125_000_000;JSONObject().put("state","accepted")},{true},{},{throw it})})
        assertEquals(1,requests)
        assertEquals(0,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->requests++;throw java.io.IOException("offline")},{true},{},{throw it})})
        assertEquals(2,requests)
    }
    @Test fun eightChunkWindowKeepsNativeReplyOrderAndTransportOutsideGate(){
        val gate=Any();var remaining=8;val commits=mutableListOf<Long>();var batches=0
        val execution=DmAttachmentExecution{_,op,input->if(op==12){if(remaining-->0)command()else ByteArray(0)}else {
            assertEquals(19,op);assertTrue(Thread.holdsLock(gate));val window=NativeRecord(input,30);assertEquals("PeerChat/DMCacheReplyWindow/v1",window.text());assertEquals(8L,window.number())
            repeat(8){val r=NativeRecord(window.bytes(),30);r.text();r.number();r.text();r.text();r.number();r.number();commits.add(r.number())};window.end();windowResult(true)
        }}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{requests->
            assertFalse(Thread.holdsLock(gate));assertEquals(8,requests.size);batches++
            requests.indices.map{JSONObject().put("state","accepted").put("durableOffset",(it+1)*65536L)}
        }
        assertEquals(8,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Serial append")},{true},{},{throw it},pipe)})
        assertEquals(1,batches);assertEquals((1L..8L).map{it*65536L},commits)
    }
    @Test fun interruptedWindowFailsEveryBorrowedRequestWithoutInventingAcknowledgments(){
        val gate=Any();var remaining=8;val operations=mutableListOf<Int>()
        val execution=DmAttachmentExecution{_,op,input->if(op==12){if(remaining-->0)command()else ByteArray(0)}else input.also{operations.add(op)}}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{throw java.io.IOException("connection ended after partial durable writes")}
        assertEquals(0,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Serial append")},{true},{},{throw it},pipe)})
        assertEquals(List(8){14},operations)
    }
    @Test fun sessionRevokedDuringWindowCannotCommitReplies(){
        val gate=Any();var remaining=8;var current=true;var commits=0
        val execution=DmAttachmentExecution{_,op,input->if(op==12){if(remaining-->0)command()else ByteArray(0)}else input.also{commits++}}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{requests->current=false;requests.map{JSONObject().put("state","accepted")}}
        assertEquals(0,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Serial append")},{current},{},{throw it},pipe)})
        assertEquals(0,commits)
    }
    @Test fun controlBorrowedAfterAppendIsNotLostOrIncludedInWindow(){
        val gate=Any();var next=0;var controls=0;var commits=0
        val execution=DmAttachmentExecution{_,op,input->if(op==12){when(next++){0->command();1->command("cache_dm_complete");else->ByteArray(0)}}else input.also{commits++}}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{requests->assertEquals(listOf("cache_dm_append"),requests.map{it.first});requests.map{JSONObject().put("state","accepted")}}
        assertEquals(2,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{op,_->assertEquals("cache_dm_complete",op);controls++;JSONObject().put("state","accepted")},{true},{},{throw it},pipe)})
        assertEquals(1,controls);assertEquals(2,commits)
    }
    private fun windowResult(applied:Boolean)=NativeRecordWriter(30).also{it.text("PeerChat/DMCacheWindowResult/v1");it.number(if(applied)1 else 0)}.bytes()
    @Test fun unsupportedNativeWindowFallsBackWithoutLosingAnyReply(){
        val gate=Any();var remaining=8;var windows=0;var singles=0;var applied=0
        val execution=DmAttachmentExecution{_,op,input->when(op){12->if(remaining-->0)command()else ByteArray(0);19->{assertTrue(Thread.holdsLock(gate));windows++;windowResult(false)};else->{assertEquals(13,op);singles++;input}}}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{requests->requests.map{JSONObject().put("state","accepted")}}
        assertEquals(8,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Serial request")},{true},{applied++},{throw it},pipe)})
        assertEquals(1,windows);assertEquals(8,singles);assertEquals(8,applied)
    }
    @Test fun failedNativeWindowNeverExposesPartialProgressOrReplaysSingleReplies(){
        val gate=Any();var remaining=8;var applied=0;var failed=0;val commits=mutableListOf<Int>()
        val execution=DmAttachmentExecution{_,op,_->if(op==12){if(remaining-->0)command()else ByteArray(0)}else{assertTrue(Thread.holdsLock(gate));commits.add(op);null}}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{requests->requests.map{JSONObject().put("state","accepted")}}
        assertEquals(0,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Serial request")},{true},{applied++},{assertTrue(Thread.holdsLock(gate));failed++},pipe)})
        assertEquals(listOf(19),commits);assertEquals(0,applied);assertEquals(8,failed)
    }
    @Test fun windowProgressCallbacksRunOnlyAfterNativeCommitReturnsUnderOwnerGate(){
        val gate=Any();var remaining=8;var committed=false;var applied=0
        val execution=DmAttachmentExecution{_,op,_->if(op==12){if(remaining-->0)command()else ByteArray(0)}else{assertEquals(19,op);assertTrue(Thread.holdsLock(gate));assertEquals(0,applied);committed=true;windowResult(true)}}
        val pipe=com.peerchat.android.platform.CacheRpcPipeline{requests->assertFalse(Thread.holdsLock(gate));requests.map{JSONObject().put("state","accepted")}}
        assertEquals(8,execution.serviceBurstOutsideGate(gate,{0}){execution.borrow(1,{_,_->error("Serial request")},{true},{assertTrue(Thread.holdsLock(gate));assertTrue(committed);applied++},{throw it},pipe)})
        assertEquals(8,applied)
    }
    @Test fun onlyAcceptedNativeCommittedCompletionRefreshesProjectionImmediately(){
        for(operation in listOf("cache_dm_complete","cache_dm_status","cache_dm_append","cache_dm_receipts"))for(accepted in listOf(false,true))for(complete in listOf(false,true)){
            val gate=Any();val cadence=OwnerProjectionCadence();val now=100L;cadence.published(now,false,true)
            var commits=0;var completions=0
            val execution=DmAttachmentExecution{_,op,input->assertTrue(Thread.holdsLock(gate));if(op==12)command(operation,data=if(operation=="cache_dm_receipts")ByteArray(0)else byteArrayOf(-1,-2))else input.also{commits++}}
            val work=synchronized(gate){checkNotNull(execution.borrow(1,{_,_->assertFalse(Thread.holdsLock(gate));JSONObject().put("state",if(accepted)"accepted"else"cache_upload_incomplete").put("ciphertextComplete",complete)},
                {assertTrue(Thread.holdsLock(gate));true},{cadence.remainingDirty(now)},{throw it},completionApplied={assertTrue(Thread.holdsLock(gate));assertEquals(1,commits);completions++;cadence.dirty()}))}
            execution.serviceBorrowedOutsideGate(gate,work)
            val expected=accepted&&complete&&operation in listOf("cache_dm_complete","cache_dm_status")
            assertEquals("$operation/$accepted/$complete",if(expected)1 else 0,completions);assertEquals(expected,cadence.due(now))
        }
    }
    @Test fun staleOrFailedCompletionNeverRefreshesReplacementProjection(){
        for(stale in listOf(false,true)){
            val gate=Any();var current=true;var completions=0;var failures=0
            val execution=DmAttachmentExecution{_,op,_->if(op==12)command("cache_dm_complete")else null}
            val work=synchronized(gate){checkNotNull(execution.borrow(1,{_,_->if(stale)current=false;JSONObject().put("state","accepted").put("ciphertextComplete",true)},
                {current},{},{failures++},completionApplied={completions++}))}
            execution.serviceBorrowedOutsideGate(gate,work);assertEquals(0,completions);assertEquals(if(stale)0 else 1,failures)
        }
    }
    private val id="a".repeat(32)
    private fun command(operation:String="cache_dm_append",data:ByteArray=byteArrayOf(-1,-2),upload:Long=1,audience:ByteArray=ByteArray(0))=NativeRecordWriter(30).also{w->
        w.text("PeerChat/DMCacheCommand/v1");w.number(7);w.text(id);w.text(operation);w.number(8);w.text(id);w.text("b".repeat(32));w.number(32768);w.number(65536);w.number(upload)
        w.blob(ByteArray(0));w.blob(audience);w.blob(data)
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
    @Test fun preparationRetainsNativeSelectedAudience(){
        val selected=byteArrayOf(1,2,3)
        val execution=DmAttachmentExecution{_,op,input->if(op==12)command("cache_dm_prepare",audience=selected)else input}
        assertTrue(execution.service({_,body->
            assertArrayEquals(selected,java.util.Base64.getUrlDecoder().decode(body.getString("audience")))
            JSONObject().put("state","accepted")
        },1))
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
