package com.peerchat.android.application

import com.peerchat.android.TransferBridge
import com.peerchat.android.platform.ServerAccounts
import com.peerchat.android.platform.CacheRpcPipeline
import android.util.Log
import org.json.JSONObject
import java.util.Base64

/** Bounded transport of the native owner's next command. This adapter retains
 * no file, key, job, retry timer, offset, envelope or receipt state. */
private const val CACHE_TRACE_TAG="PeerChatCacheTrace"
private fun cacheTrace(message:()->String) {
    if(Log.isLoggable(CACHE_TRACE_TAG,Log.DEBUG))Log.d(CACHE_TRACE_TAG,message())
}

internal class DmAttachmentExecution(
    private val native:(Long,Int,ByteArray)->ByteArray?={handle,operation,input->TransferBridge.request(handle,operation,input)}
) {
    // Read only under the backend gate. Only accepted enrollment replies that
    // were applied to the current native owner advance this progress marker.
    internal var readinessProgress=0L
        private set
    fun service(accounts:ServerAccounts,handle:Long):Boolean=service(accounts::directAttachmentCache,handle)

    internal fun service(accounts:(String,JSONObject)->JSONObject,handle:Long):Boolean {
        val work=borrow(handle,accounts,{true},{},{throw it})?:return false
        if(work.localOnly)return work.current()
        apply(work,exchange(work));return true
    }

    /** One command borrowed by the existing scheduled owner, not another queue
     * or retry owner. All native access and scope callbacks run under its gate. */
    internal class Borrowed internal constructor(
        internal val handle:Long,internal val operation:String,internal val requestId:String,
        internal val request:()->ByteArray,internal val rejected:(Exception)->ByteArray,
        internal val current:()->Boolean,internal val applied:()->Unit,internal val failed:(Exception)->Unit,
        internal val reply:((Boolean,ByteArray)->Unit)?=null,
        internal val pipeline:CacheRpcPipeline?=null,internal val fields:JSONObject?=null,
        internal val encode:((JSONObject)->ByteArray)?=null,
        internal val localOnly:Boolean=false,
        internal val completionReady:()->Boolean={false},internal val completionApplied:()->Unit={},internal val readinessApplied:()->Unit={}
    )
    private class Result(val bytes:ByteArray,val failure:Exception?=null)

    internal fun borrow(handle:Long,accounts:(String,JSONObject)->JSONObject,current:()->Boolean,
                        applied:()->Unit,failed:(Exception)->Unit,pipeline:CacheRpcPipeline?=null,completionApplied:()->Unit={}):Borrowed? {
        val bytes=native(handle,12,ByteArray(0))?:return null
        if(bytes.isEmpty())return null
        val marker=NativeRecord(bytes,30)
        if(marker.text()=="PeerChat/DMCacheLocalWork/v1"){
            require(marker.number()>0);marker.end()
            // op12 already performed a bounded local step. Continue through the
            // existing burst budget without transport, native replies or ACKs.
            return Borrowed(handle,"local_preparation","",{error("Local work has no request")},
                {error("Local work has no network failure")},current,{},failed,localOnly=true)
        }
        val command=Command.parse(bytes)
        val fields=command.fields()
        var completionReady=false
        var enrollmentAccepted=false
        fun encode(body:JSONObject):ByteArray {
            val wire=encodeReply(command,body)
            enrollmentAccepted=command.operation in setOf("cache_dm_capability","cache_dm_register")&&body.optString("state")=="accepted"
            completionReady=(command.operation=="cache_dm_complete"||command.operation=="cache_dm_status")&&
                body.optString("state")=="accepted"&&body.optBoolean("ciphertextComplete",false)
            return wire
        }
        return Borrowed(handle,command.operation,command.requestId,
            {encode(accounts(command.operation,fields))},
            {NativeRecordWriter(30).also{w->
                w.text("PeerChat/DMCacheFailure/v1");w.number(command.generation);w.text(command.requestId);w.text("network_unavailable")
            }.bytes()},current,applied,failed,pipeline=pipeline,fields=fields,encode={encode(it)},completionReady={completionReady},completionApplied=completionApplied,readinessApplied={if(enrollmentAccepted)readinessProgress++})
    }
    private fun exchange(work:Borrowed):Result {
        check(!work.localOnly)
        val began=System.nanoTime()
        try{return try{Result(work.request())}catch(failure:Exception){Result(work.rejected(failure),failure)}}
        finally{val elapsed=(System.nanoTime()-began)/1_000_000
            if(elapsed>=250)android.util.Log.i("PeerChatNetwork","stage=dm_cache_rpc operation=${work.operation} native_request_id=${work.requestId} elapsed_ms=$elapsed")}
    }
    private fun apply(work:Borrowed,result:Result){
        check(!work.localOnly)
        if(work.reply!=null)work.reply.invoke(result.failure==null,result.bytes)
        else checkNotNull(native(work.handle,if(result.failure==null)13 else 14,result.bytes)){"Native attachment reply commit unavailable"}
        result.failure?.let{if(expiredServerSession(it))throw it}
        work.applied()
        if(result.failure==null)work.readinessApplied()
        if(result.failure==null&&work.completionReady())work.completionApplied()
    }
    internal fun serviceOutsideGate(gate:Any,prepare:()->Borrowed?):Boolean {
        val began=System.nanoTime();var entered=began
        val work=synchronized(gate){entered=System.nanoTime();prepare()?.takeIf{it.current()}}?:return false
        return serviceBorrowedOutsideGate(gate,work,began,entered,System.nanoTime())
    }
    // Borrow already established the captured scope under the owner gate. Do
    // not reacquire it merely to start transport; reply application still
    // rechecks that exact scope under the same gate.
    internal fun serviceBorrowedOutsideGate(gate:Any,work:Borrowed,
        began:Long=System.nanoTime(),entered:Long=began,prepared:Long=entered):Boolean {
        if(work.localOnly)return synchronized(gate){work.current()}
        val result=exchange(work)
        val exchanged=System.nanoTime();var applying=exchanged
        var current=false
        try{return synchronized(gate){
            applying=System.nanoTime();current=work.current()
            if(!current)false
            else try{apply(work,result);result.failure==null}catch(failure:Exception){work.failed(failure);false}
        }}finally{
            val ended=System.nanoTime()
            val trace="stage=cache_command operation=${work.operation} borrow_wait_ms=${(entered-began)/1_000_000} prepare_ms=${(prepared-entered)/1_000_000} rpc_ms=${(exchanged-prepared)/1_000_000} apply_wait_ms=${(applying-exchanged)/1_000_000} apply_ms=${(ended-applying)/1_000_000} scope_current=$current"
            if(ended-began>=250_000_000||!current)android.util.Log.i("PeerChatNetwork",trace)
            else cacheTrace{trace}
        }
    }

    // Only successful direct-cache append replies can use this atomic owner
    // operation. Errors, group callbacks and incompatible windows keep op13/14.
    private fun applyAppendWindow(gate:Any,window:List<Borrowed>,results:List<Result>):Int? {
        if(window.size !in 2..8||window.any{it.operation!="cache_dm_append"||it.reply!=null||it.handle!=window.first().handle}||
            results.any{it.failure!=null||it.bytes.size>8192}||results.sumOf{it.bytes.size}>69000)return null
        return synchronized(gate){
            if(window.any{!it.current()})return@synchronized null
            try{
                val wire=NativeRecordWriter(30).also{w->w.text("PeerChat/DMCacheReplyWindow/v1");w.number(window.size.toLong());results.forEach{w.blob(it.bytes)}}.bytes()
                val response=checkNotNull(native(window.first().handle,19,wire)){"Native attachment window commit unavailable"}
                val record=NativeRecord(response,30);require(record.text()=="PeerChat/DMCacheWindowResult/v1");val applied=record.number();require(applied in 0L..1L);record.end()
                if(applied==0L)return@synchronized null
                window.forEach{it.applied()};window.size
            }catch(failure:Exception){window.forEach{it.failed(failure)};0}
        }
    }

    // The native v2 owner grants credit; this adapter only transports its
    // already-ready requests, preserving their order and bounded memory.
    private fun serviceWindowOutsideGate(gate:Any,remaining:Int,prepare:()->Borrowed?):Int {
        fun timedBorrow(block:()->Borrowed?):Borrowed? {
            val began=System.nanoTime();var entered=began
            val work=synchronized(gate){entered=System.nanoTime();block()?.takeIf{it.current()}}
            val ended=System.nanoTime()
            val trace="stage=cache_borrow operation=${work?.operation?:"none"} wait_ms=${(entered-began)/1_000_000} native_ms=${(ended-entered)/1_000_000}"
            if(ended-began>=250_000_000)Log.i("PeerChatNetwork",trace) else cacheTrace{trace}
            return work
        }
        val first=timedBorrow(prepare)?:return 0
        val pipe=first.pipeline
        fun append(work:Borrowed)=work.operation=="cache_dm_append"||work.operation=="cache_append"
        if(pipe==null||!append(first))return if(serviceBorrowedOutsideGate(gate,first))1 else 0
        val window=mutableListOf(first);var trailing:Borrowed?=null
        for(i in 1 until minOf(com.peerchat.android.platform.tls.BoundedRpc.MAX_WINDOW,remaining)){
            val next=timedBorrow{if(first.current())prepare() else null}?:break
            if(next.pipeline===pipe&&append(next))window.add(next)else {trailing=next;break}
        }
        val began=System.nanoTime()
        val results=try{
            val replies=pipe.exchange(window.map{it.operation to checkNotNull(it.fields)})
            require(replies.size==window.size)
            replies.mapIndexed{i,value->Result(checkNotNull(window[i].encode).invoke(value))}
        }catch(failure:Exception){window.map{Result(it.rejected(failure),failure)}}
        val exchanged=System.nanoTime()
        var completed=applyAppendWindow(gate,window,results)?:run {
            var count=0
            window.zip(results).forEach{(work,result)->
                synchronized(gate){if(work.current())try{apply(work,result);if(result.failure==null)count++}catch(failure:Exception){work.failed(failure)}}
            }
            count
        }
        cacheTrace{"stage=cache_window chunks=${window.size} completed=$completed network_ms=${(exchanged-began)/1_000_000} apply_ms=${(System.nanoTime()-exchanged)/1_000_000} elapsed_ms=${(System.nanoTime()-began)/1_000_000}"}
        // A non-append command borrowed while filling credit must not be lost.
        trailing?.let{if(synchronized(gate){it.current()}&&serviceBorrowedOutsideGate(gate,it))completed++}
        return completed
    }

    // Drain already-ready work on the existing single lane. Each borrow/apply
    // rechecks scope; network never holds the gate. Yield after 16 work units
    // (RPCs or bounded native preparation steps) or 100 ms,
    // and stop immediately on empty work, stale scope or failure (no retry loop).
    internal fun serviceBurstOutsideGate(gate:Any,clock:()->Long={System.nanoTime()},prepare:()->Borrowed?):Int {
        val began=clock();var completed=0
        while(completed<16){
            val count=serviceWindowOutsideGate(gate,16-completed,prepare);if(count==0)break
            completed+=count
            if(clock()-began>=100_000_000)break
        }
        return completed
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
                "cache_dm_prepare"->{require(recipient>0);body.put("recipientAccountId",recipient);if(audience.isNotEmpty())body.put("audience",encoded(audience))}
                "cache_dm_admit"->{require(descriptor.isNotEmpty()&&audience.isNotEmpty());body.put("descriptor",encoded(descriptor));body.put("audience",encoded(audience))}
                "cache_dm_status","cache_dm_context"->body.put("id",objectId())
                "cache_dm_acquire"->{body.put("id",objectId());body.put("upload",upload==1L)}
                "cache_dm_append"->{require(data.isNotEmpty());body.put("lease",leaseId());body.put("offset",offset);body.put("data",encoded(data))}
                "cache_dm_complete"->body.put("lease",leaseId()).put("bounded",true)
                "cache_dm_read"->{require(maximum in 1..65536);body.put("lease",leaseId());body.put("offset",offset);body.put("maximum",maximum)}
                "cache_dm_progress"->{body.put("lease",leaseId());body.put("offset",offset)}
                "cache_dm_release"->{require(data.isEmpty()||(upload==1L&&data.size==1&&data[0]==1.toByte()));body.put("lease",leaseId());body.put("upload",upload==1L);if(data.isNotEmpty())body.put("abandonUpload",true)}
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
                w.text("PeerChat/AttachmentBodyVersion/v1");w.number((if(body.has("thumbnailBodyVersion"))number(body,"thumbnailBodyVersion")else 1L).also{require(it in 1..2)})
            }.bytes().also{require(it.size<=100000)}
        }
    }
}
