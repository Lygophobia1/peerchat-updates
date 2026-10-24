package com.peerchat.android.application

import com.peerchat.android.GroupAttachmentBridge
import org.json.JSONObject
import org.json.JSONArray

/** Projects bounded native commands onto the existing authenticated cache lane. */
internal class GroupAttachmentExecution(
    private val native:(Long,Int,ByteArray)->ByteArray?={h,o,b->GroupAttachmentBridge.request(h,o,b)}
) {
    private fun call(handle:Long,account:Long,generation:Long,operation:Int,write:(NativeRecordWriter)->Unit):ByteArray {
        val w=NativeRecordWriter(30);w.text("PeerChat/AndroidGroupAttachments/v1");w.number(account);w.number(generation);write(w)
        return checkNotNull(native(handle,operation,w.bytes())){"Group attachment storage or authorization unavailable"}
    }
    fun command(handle:Long,account:Long,generation:Long,action:Int,group:String="",id:String="",path:String=""):String =
        call(handle,account,generation,0){it.number(action.toLong());it.text(group);it.text(id);it.text(path)}.toString(Charsets.UTF_8)

    fun borrow(handle:Long,account:Long,generation:Long,server:(String,JSONObject)->JSONObject,
               current:()->Boolean,applied:()->Unit,failed:(Exception)->Unit,pipeline:com.peerchat.android.platform.CacheRpcPipeline?=null):DmAttachmentExecution.Borrowed? {
        val bytes=call(handle,account,generation,1){}
        if(bytes.isEmpty())return null
        require(bytes.size<=100000)
        val command=JSONObject(bytes.toString(Charsets.UTF_8));if(command.length()==0)return null;val operation=command.getString("type")
        val request=command.getString("requestId");require(request.matches(Regex("[0-9a-f]{32}"))&&command.getLong("generation")==generation)
        command.remove("type");command.remove("generation")
        val encode:(JSONObject)->ByteArray={value->
            require(value.getString("requestId")==request&&value.getString("operation")==operation)
            val w=NativeRecordWriter(30);w.text("PeerChat/AttachmentResponse/v1");w.number(generation);w.text(request)
            writeValue(w,value);w.bytes()
        }
        return DmAttachmentExecution.Borrowed(handle,operation,request,{encode(server(operation,command))},{
            NativeRecordWriter(30).also{w->w.text("PeerChat/AttachmentFailure/v1");w.number(generation);w.text(request);w.text("network_unavailable")}.bytes()
        },current,applied,failed,{success,reply->call(handle,account,generation,if(success)2 else 3){it.raw(reply)};Unit},pipeline=pipeline,fields=command,encode=encode)
    }
    companion object {
        internal fun writeValue(w:NativeRecordWriter,value:Any?,depth:Int=0,nodes:IntArray=intArrayOf(0)) {
            require(depth<=12&&++nodes[0]<=4096)
            when(value){
                null,JSONObject.NULL->w.tag(0)
                is Boolean->w.tag(if(value)2 else 1)
                is Byte,is Short,is Int,is Long->{w.tag(3);w.number((value as Number).toLong())}
                is String->{val bytes=value.toByteArray(Charsets.UTF_8);require(bytes.size<=100000);w.tag(4);w.number(bytes.size.toLong());w.raw(bytes)}
                is JSONArray->{require(value.length()<=512);w.tag(5);w.number(value.length().toLong());repeat(value.length()){writeValue(w,value.get(it),depth+1,nodes)}}
                is JSONObject->{require(value.length()<=64);w.tag(6);w.number(value.length().toLong());value.keys().forEach{key->require(key.toByteArray().size<=128);w.text(key);writeValue(w,value.get(key),depth+1,nodes)}}
                else->error("Invalid attachment response value")
            }
        }
    }
}
