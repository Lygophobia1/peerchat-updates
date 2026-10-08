package com.peerchat.android.application

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[28])
class ArchiveEnrollmentExecutionTest {
    // Transport/serialization fixture only. Signature/trust validation stays in
    // the native enrollment tests and packaged Windows C ABI regression.
    private class Fixture {
        val device="a".repeat(64)
        val descriptor=byteArrayOf(1,2,3)
        var pending= mutableListOf<ByteArray>()
        val operations= mutableListOf<Int>()
        var revision=0L
        var live=false
        var account=8L
        var failure=false
        var grantCount=0
        var eligible:Any=true
        fun response()=JSONObject().put("ok",true).put("archiveVersion",1)
        fun native(operation:Int,bytes:ByteArray):ByteArray {
            operations+=operation
            val record=NativeRecord(bytes,31)
            require(record.text()=="PeerChat/ArchiveApplication/v1"&&record.number()==1L&&record.number()==operation.toLong())
            when(operation){
                1->{record.end();return descriptor}
                2->{
                    require(record.number()==8L&&record.number()==3L)
                    val next=record.number();require(next>revision);revision=next
                    require(record.text()==device&&record.text()=="fixture-epoch")
                    val devices=record.number();repeat(devices.toInt()){record.bytes()}
                    val grants=record.number();repeat(grants.toInt()){record.bytes();require(record.number()<=1)}
                    live=true
                }
                3->{require(live)}
                8->{
                    record.end()
                    return NativeRecordWriter(31).also {w->
                        w.text("PeerChat/ArchiveApplication/v1");w.number(1);w.number(8);w.number(pending.size.toLong());pending.forEach(w::blob)
                    }.bytes()
                }
                9->{live=false}
                else->error("Consent mutation must never be automatic")
            }
            record.end()
            return NativeRecordWriter(31).also {it.text("PeerChat/ArchiveApplication/v1");it.number(1);it.number(operation.toLong())}.bytes()
        }
        fun rpc(operation:String,fields:JSONObject):JSONObject=when(operation){
            "archive_descriptor_register"->response().put("state","accepted").also {
                assertEquals(java.util.Base64.getEncoder().encodeToString(descriptor),fields.getString("proof"))
            }
            "archive_grant_register"->{
                if(failure)throw java.io.IOException("Ambiguous test publication")
                grantCount++;response().put("state","accepted")
            }
            "archive_authorization_state"->response().put("accountId",account).put("serverEpoch","fixture-epoch")
                .put("devices",JSONArray().put(JSONObject().put("descriptor",java.util.Base64.getEncoder().encodeToString(descriptor))))
                .put("grants",JSONArray().put(JSONObject().put("grant","AQID").put("currentlyEligible",eligible)))
            else->error("Unexpected RPC")
        }
        fun execution()=ArchiveEnrollmentExecution(::native,::rpc)
    }
    @Test fun refreshUsesCurrentContextAndNeverApprovesOrGrants(){
        val f=Fixture();f.execution().refresh(8,3,f.device)
        assertTrue(f.live);assertFalse(f.operations.any {it in 4..7});assertEquals(0,f.grantCount)
    }
    @Test fun publicationIsBoundedAndUsesOnlyDurableNativeProofs(){
        val f=Fixture();repeat(6){f.pending+=byteArrayOf(it.toByte(),2)}
        assertEquals(4,f.execution().publishPending(8,3,f.device));assertEquals(4,f.grantCount);assertTrue(f.live)
    }
    @Test fun ambiguousPublicationInvalidatesButDoesNotErasePending(){
        val f=Fixture();f.pending+=byteArrayOf(1);f.failure=true;val execution=f.execution()
        try{execution.publishPending(8,3,f.device);fail()}catch(_:java.io.IOException){}
        assertFalse(f.live);assertEquals(1,f.pending.size);assertEquals(0,f.grantCount)
        f.failure=false;assertEquals(1,execution.publishPending(8,3,f.device))
    }
    @Test fun wrongAccountAndInvalidEligibilityFailClosed(){
        val f=Fixture();val execution=f.execution();f.account=9
        try{execution.refresh(8,3,f.device);fail()}catch(_:IllegalArgumentException){}
        assertFalse(f.live);f.account=8;f.eligible="true"
        try{execution.refresh(8,3,f.device);fail()}catch(_:IllegalArgumentException){}
        assertFalse(f.live)
    }
    @Test fun oversizedProofRejectedBeforeNativeRefresh(){
        val f=Fixture();val execution=ArchiveEnrollmentExecution(f::native){_,_->f.response().put("state","accepted").put("accountId",8).put("serverEpoch","fixture-epoch")
            .put("devices",JSONArray().put(JSONObject().put("descriptor","A".repeat(2000)))).put("grants",JSONArray())}
        try{execution.refresh(8,3,f.device);fail()}catch(_:IllegalArgumentException){}
        assertFalse(f.live);assertFalse(f.operations.contains(2))
    }
}
