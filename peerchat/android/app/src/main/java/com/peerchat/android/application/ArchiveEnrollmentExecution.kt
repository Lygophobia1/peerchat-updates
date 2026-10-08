package com.peerchat.android.application

import org.json.JSONObject

// Public metadata execution on the existing MessagingBackend worker/gate.
// Native Enrollment alone verifies identity, signatures, consent and permissions.
internal class ArchiveEnrollmentExecution(
    private val native: (Int,ByteArray)->ByteArray,
    private val rpc: (String,JSONObject)->JSONObject
) {
    private var revision=0L
    private fun command(operation:Int,fields:(NativeRecordWriter)->Unit = {}):ByteArray =
        NativeRecordWriter(31).also {it.text("PeerChat/ArchiveApplication/v1");it.number(1);it.number(operation.toLong());fields(it)}.bytes()
    private fun invalidate(){native(9,command(9))}
    private fun proof(value:String,max:Int):ByteArray {
        require(value.length<=((max+2)/3)*4)
        return java.util.Base64.getDecoder().decode(value).also {require(it.size in 1..max)}
    }
    private fun request(operation:String,fields:JSONObject=JSONObject()):JSONObject =
        rpc(operation,fields).also {require(it.getBoolean("ok")&&it.getLong("archiveVersion")==1L)}
    private fun refreshState(account:Long,generation:Long,device:String){
        val reply=request("archive_authorization_state")
        require(reply.getLong("accountId")==account)
        val epoch=reply.getString("serverEpoch");require(epoch.isNotEmpty()&&epoch.length<=128)
        val devices=reply.getJSONArray("devices");val grants=reply.getJSONArray("grants")
        require(devices.length()<=32&&grants.length()<=64&&revision<Long.MAX_VALUE)
        revision=maxOf(revision+1,System.nanoTime())
        native(2,command(2){w->
            w.number(account);w.number(generation);w.number(revision);w.text(device);w.text(epoch)
            w.number(devices.length().toLong())
            repeat(devices.length()){w.blob(proof(devices.getJSONObject(it).getString("descriptor"),1024))}
            w.number(grants.length().toLong())
            repeat(grants.length()){
                val row=grants.getJSONObject(it);w.blob(proof(row.getString("grant"),2048))
                require(row.get("currentlyEligible") is Boolean);w.number(if(row.getBoolean("currentlyEligible"))1 else 0)
            }
        })
    }
    fun refresh(account:Long,generation:Long,device:String):ByteArray {
        try{
            invalidate();require(account>0&&generation>0&&device.matches(Regex("[0-9a-f]{64}")))
            val descriptor=native(1,command(1));require(descriptor.size in 1..1024)
            require(request("archive_descriptor_register",JSONObject().put("proof",java.util.Base64.getEncoder().encodeToString(descriptor))).getString("state")=="accepted")
            refreshState(account,generation,device)
            return native(3,command(3))
        }catch(error:Exception){invalidate();throw error}
    }
    fun publishPending(account:Long,generation:Long,device:String):Int {
        try{
            refresh(account,generation,device)
            val record=NativeRecord(native(8,command(8)),31)
            require(record.text()=="PeerChat/ArchiveApplication/v1"&&record.number()==1L&&record.number()==8L)
            val count=record.number();require(count<=32)
            val proofs=List(count.toInt()){record.bytes().also {require(it.size in 1..2048)}};record.end()
            var sent=0
            proofs.take(4).forEach {
                require(request("archive_grant_register",JSONObject().put("proof",java.util.Base64.getEncoder().encodeToString(it))).getString("state")=="accepted");sent++
            }
            refreshState(account,generation,device);return sent
        }catch(error:Exception){invalidate();throw error}
    }
    fun consent(account:Long,generation:Long,action:Int,device:String,capabilities:Long,consent:Boolean):ByteArray {
        require(account>0&&generation>0&&action in 4..7&&device.matches(Regex("[0-9a-f]{64}"))&&capabilities in 0..3)
        return native(10,command(10){w->w.number(account);w.number(generation);w.number(action.toLong());w.text(device);if(action==5)w.number(capabilities);w.number(if(consent)1 else 0)})
    }
}
