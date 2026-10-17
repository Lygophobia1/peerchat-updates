package com.peerchat.android.application

import com.peerchat.android.TransferBridge
import com.peerchat.android.platform.ServerAccounts
import org.json.JSONObject
import java.util.Base64

/** Bounded transport of the native owner's next command. This adapter retains
 * no file, key, job, retry timer, offset, envelope or receipt state. */
internal class DmAttachmentExecution(
    private val native:(Long,Int,ByteArray)->ByteArray?={handle,operation,input->TransferBridge.request(handle,operation,input)}
) {
    fun service(accounts:ServerAccounts,handle:Long):Boolean=service(accounts::directAttachmentCache,handle)

    internal fun service(accounts:(String,JSONObject)->JSONObject,handle:Long):Boolean {
        val work=borrow(handle,accounts,{true},{},{throw it})?:return false
        apply(work,exchange(work));return true
    }

    /** One command borrowed by the existing scheduled owner, not another queue
     * or retry owner. All native access and scope callbacks run under its gate. */
    internal class Borrowed internal constructor(
        internal val handle:Long,internal val operation:String,internal val requestId:String,
        internal val request:()->ByteArray,internal val rejected:(Exception)->ByteArray,
        internal val current:()->Boolean,internal val applied:()->Unit,internal val failed:(Exception)->Unit
    )
    private class Result(val bytes:ByteArray,val failure:Exception?=null)

    internal fun borrow(handle:Long,accounts:(String,JSONObject)->JSONObject,current:()->Boolean,
                        applied:()->Unit,failed:(Exception)->Unit):Borrowed? {
        val bytes=native(handle,12,ByteArray(0))?:return null
        if(bytes.isEmpty())return null
        val command=Command.parse(bytes)
        return Borrowed(handle,command.operation,command.requestId,
            {encodeReply(command,accounts(command.operation,command.fields()))},
            {NativeRecordWriter(30).also{w->
                w.text("PeerChat/DMCacheFailure/v1");w.number(command.generation);w.text(command.requestId);w.text("network_unavailable")
            }.bytes()},current,applied,failed)
    }
    private fun exchange(work:Borrowed):Result {
        val began=System.nanoTime()
        try{return try{Result(work.request())}catch(failure:Exception){Result(work.rejected(failure),failure)}}
        finally{val elapsed=(System.nanoTime()-began)/1_000_000
            if(elapsed>=250)android.util.Log.i("PeerChatNetwork","stage=dm_cache_rpc operation=${work.operation} native_request_id=${work.requestId} elapsed_ms=$elapsed")}
    }
    private fun apply(work:Borrowed,result:Result){
        checkNotNull(native(work.handle,if(result.failure==null)13 else 14,result.bytes)){"Native attachment reply commit unavailable"}
        result.failure?.let{if(expiredServerSession(it))throw it}
        work.applied()
    }
    internal fun serviceOutsideGate(gate:Any,prepare:()->Borrowed?){
        val work=synchronized(gate){prepare()}?:return
        // Only transport/encoding is outside the gate. No native operation,
        // authorization mutation or callback can race local profile commands.
        val result=exchange(work)
        synchronized(gate){
            if(work.current())try{apply(work,result)}catch(failure:Exception){work.failed(failure)}
        }
    }

    private data class Command(val generation:Long,val requestId:String,val operation:String,
        val recipient:Long,val id:String,val lease:String,val offset:Long,val maximum:Long,val upload:Long,
        val descriptor:ByteArray,val audience:ByteArray,val data:ByteArray) {
        fun fields():JSONObject=JSONObject().also{body->
            // Local correlation only: ServerAccounts replaces this with its
            // independently generated actual RPC ID before writing the wire.
            body.put("requestId",requestId)
            when(operation){
                "cache_dm_capability"->Unit
                "cache_dm_register"->{require(data.isNotEmpty());body.put("proof",encoded(data))}
                "cache_dm_prepare"->{require(recipient>0);body.put("recipientAccountId",recipient)}
                "cache_dm_admit"->{require(descriptor.isNotEmpty()&&audience.isNotEmpty());body.put("descriptor",encoded(descriptor));body.put("audience",encoded(audience))}
                "cache_dm_status","cache_dm_context"->body.put("id",objectId())
                "cache_dm_acquire"->{body.put("id",objectId());body.put("upload",upload==1L)}
                "cache_dm_append"->{require(data.isNotEmpty());body.put("lease",leaseId());body.put("offset",offset);body.put("data",encoded(data))}
                "cache_dm_complete"->body.put("lease",leaseId())
                "cache_dm_read"->{require(maximum in 1..65536);body.put("lease",leaseId());body.put("offset",offset);body.put("maximum",maximum)}
                "cache_dm_progress"->{body.put("lease",leaseId());body.put("offset",offset)}
                "cache_dm_release"->{body.put("lease",leaseId());body.put("upload",upload==1L)}
                "cache_dm_publish"->{require(data.isNotEmpty());body.put("id",objectId());body.put("envelope",encoded(data))}
                "cache_dm_verified"->{require(data.isNotEmpty());body.put("id",objectId());body.put("receipt",encoded(data))}
                "cache_dm_receipts"->{val cursor=data.toString(Charsets.US_ASCII);require(cursor.isEmpty()||cursor.matches(Regex("[0-9a-f]{64}")));body.put("id",objectId());body.put("after",cursor)}
                else->error("Unsupported native cache operation")
            }
        }
        private fun objectId()=id.also{require(it.matches(ID))}
        private fun leaseId()=lease.also{require(it.matches(ID))}
        companion object {
            fun parse(bytes:ByteArray):Command {
                require(bytes.size<=100000);val r=NativeRecord(bytes,30)
                require(r.text()=="PeerChat/DMCacheCommand/v1")
                val value=Command(r.number(),r.text(),r.text(),r.number(),r.text(),r.text(),r.number(),r.number(),r.number(),r.bytes(),r.bytes(),r.bytes());r.end()
                require(value.generation>0&&value.requestId.matches(ID)&&value.operation in OPERATIONS&&
                    (value.id.isEmpty()||value.id.matches(ID))&&(value.lease.isEmpty()||value.lease.matches(ID))&&
                    value.upload in 0..1&&value.descriptor.size<=2048&&value.audience.size<=32768&&value.data.size<=65536)
                return value
            }
        }
    }

    companion object {
        private val ID=Regex("[0-9a-f]{32}")
        private val DEVICE=Regex("[0-9a-f]{64}")
        private val OPERATIONS=setOf("cache_dm_capability","cache_dm_register","cache_dm_prepare","cache_dm_admit","cache_dm_status",
            "cache_dm_context","cache_dm_acquire","cache_dm_append","cache_dm_complete","cache_dm_read",
            "cache_dm_progress","cache_dm_release","cache_dm_publish","cache_dm_verified","cache_dm_receipts")
        private fun encoded(bytes:ByteArray)=Base64.getUrlEncoder().withoutPadding().encodeToString(bytes)
        private fun blob(body:JSONObject,key:String,maximum:Int):ByteArray {
            if(!body.has(key))return ByteArray(0)
            val text=body.getString(key);require(text.length<=(maximum*4+2)/3&&text.matches(Regex("[A-Za-z0-9_-]*")))
            return Base64.getUrlDecoder().decode(text).also{require(it.size<=maximum&&encoded(it)==text)}
        }
        private fun number(body:JSONObject,key:String):Long {
            if(!body.has(key))return 0
            val value=body.get(key);require(value is Byte||value is Short||value is Int||value is Long)
            return (value as Number).toLong().also{require(it>=0)}
        }
        private fun flag(body:JSONObject,key:String):Long {
            if(!body.has(key))return 0
            val value=body.get(key);require(value is Boolean);return if(value)1 else 0
        }
        private fun text(body:JSONObject,key:String,maximum:Int):String =
            (if(body.has(key))body.getString(key)else "").also{require(it.length<=maximum)}
        private fun encodeReply(command:Command,body:JSONObject):ByteArray {
            require(body.toString().toByteArray(Charsets.UTF_8).size<=100000)
            val state=body.getString("state").also{require(it=="accepted"||it.matches(Regex("cache_[a-z_]{1,80}")))}
            val receipts=body.optJSONArray("items")
            require(receipts==null||command.operation=="cache_dm_receipts")
            val count=receipts?.length()?:0;require(count<=8)
            return NativeRecordWriter(30).also{w->
                w.text("PeerChat/DMCacheReply/v1");w.number(command.generation);w.text(command.requestId);w.text(state)
                w.number(number(body,"cacheVersion"));w.number(number(body,"dmAttachmentVersion"))
                w.number(number(body,"durableOffset"));w.number(number(body,"ciphertextSize"));w.number(number(body,"expiresUtc"))
                w.number(flag(body,"ciphertextComplete"));w.text(text(body,"lease",32).also{require(it.isEmpty()||it.matches(ID))})
                w.blob(blob(body,"descriptor",2048));w.blob(blob(body,"audience",32768));w.blob(blob(body,"data",65536))
                w.number(flag(body,"newNoticeStored"));w.number(count.toLong())
                repeat(count){i->val row=checkNotNull(receipts).getJSONObject(i)
                    w.number(number(row,"accountId").also{require(it>0)});w.text(text(row,"device",64).also{require(it.matches(DEVICE))})
                    w.number(number(row,"durableOffset"));w.number(flag(row,"plaintextVerified"));w.blob(blob(row,"receipt",4096))
                }
                w.text(text(body,"cursor",64).also{require(it.isEmpty()||it.matches(DEVICE))});w.number(flag(body,"more"))
            }.bytes().also{require(it.size<=100000)}
        }
    }
}
