package com.peerchat.android.application

import java.io.ByteArrayOutputStream
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.charset.CodingErrorAction

// Bounded LOCAL JNI snapshot projection, not a Kotlin peer-wire implementation.
internal class NativeRecord(bytes: ByteArray, type: Int) {
    private val input=ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
    init { require(bytes.size in 4..262144 && input.get().toInt()==80 && input.get().toInt()==84 && input.get().toInt()==1 && input.get().toInt()==type) }
    fun number(): Long { require(input.remaining()>=8);return input.long.also { require(it>=0) } }
    fun text(): String {
        val bytes=bytes()
        return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
    }
    fun bytes(maximum:Int=65536): ByteArray {
        require(maximum in 1..262144);val count=number();require(count<=maximum && count<=input.remaining())
        return ByteArray(count.toInt()).also { input.get(it) }
    }
    fun end() { require(!input.hasRemaining()) }
    fun hasRemaining():Boolean=input.hasRemaining()
}
internal class NativeRecordWriter(type: Int) {
    private val output=ByteArrayOutputStream().apply {write(byteArrayOf(80,84,1,type.toByte()))}
    fun raw(value:ByteArray){require(value.size<=262144&&output.size()+value.size<=262144);output.write(value)}
    fun tag(value:Int){require(value in 0..6);output.write(value)}
    fun number(value: Long) {require(value>=0);repeat(8){output.write((value ushr (it*8)).toInt())}}
    fun text(value: String) {val bytes=value.toByteArray(Charsets.UTF_8);require(bytes.size<=65536);number(bytes.size.toLong());output.write(bytes)}
    fun blob(value: ByteArray) {require(value.size<=65536);number(value.size.toLong());output.write(value)}
    fun bytes(): ByteArray = output.toByteArray().also {require(it.size<=262144)}
}
data class ArchiveRelayScope(val account:Long,val issuer:String,val source:String,val reader:String,val sourceGeneration:Long,val readerGeneration:Long,val sourceGrant:String,val readerGrant:String){
    init{require(account>0&&sourceGeneration>0&&readerGeneration>0&&source!=reader&&listOf(issuer,source,reader).all{it.matches(Regex("[0-9a-f]{64}"))}&&listOf(sourceGrant,readerGrant).all{it.matches(Regex("[0-9a-f]{32}"))})}
    fun fields()=org.json.JSONObject().put("purpose","archive_history").put("archiveIssuer",issuer).put("archiveSource",source).put("archiveReader",reader)
        .put("archiveSourceGeneration",sourceGeneration.toString()).put("archiveReaderGeneration",readerGeneration.toString()).put("archiveSourceGrantId",sourceGrant).put("archiveReaderGrantId",readerGrant)
    fun binding():ByteArray=NativeRecordWriter(32).also{w->w.text("PeerChat/ArchiveSession/v1");w.number(1);w.number(1);w.number(account);w.text(issuer);w.text(source);w.text(reader);w.number(sourceGeneration);w.number(readerGeneration);w.text(sourceGrant);w.text(readerGrant)}.bytes()
    companion object{
        fun parse(bytes:ByteArray):ArchiveRelayScope{require(bytes.size<=1024);val r=NativeRecord(bytes,32);require(r.text()=="PeerChat/ArchiveSession/v1"&&r.number()==1L&&r.number()==1L)
            val scope=ArchiveRelayScope(r.number(),r.text(),r.text(),r.text(),r.number(),r.number(),r.text(),r.text());r.end();require(scope.binding().contentEquals(bytes));return scope}
        fun event(event:org.json.JSONObject)=ArchiveRelayScope(event.getLong("accountId"),event.getString("archiveIssuer"),event.getString("archiveSource"),event.getString("archiveReader"),event.getLong("archiveSourceGeneration"),event.getLong("archiveReaderGeneration"),event.getString("archiveSourceGrantId"),event.getString("archiveReaderGrantId"))
    }
}
data class GroupRelayScope(val account:Long,val authenticationGeneration:Long,val group:String,val epoch:Long,val targetAccount:Long,val targetDevice:String){
    init{require(account>0&&authenticationGeneration>0&&epoch>0&&targetAccount>0&&group.matches(Regex("[0-9a-f]{32}"))&&targetDevice.matches(Regex("[0-9a-f]{64}")))}
    fun fields()=org.json.JSONObject().put("purpose","group_key").put("groupId",group).put("rosterGeneration",epoch.toString())
    companion object{fun parse(bytes:ByteArray):GroupRelayScope{
        val r=NativeRecord(bytes,27);require(r.text()=="PeerChat/AndroidGroupRelay/v1")
        val result=GroupRelayScope(r.number(),r.number(),r.text(),r.number(),r.number(),r.text());r.end();return result
    }}
}
data class RelayExecution(val attempt: Long,val peerId: String,val networkRevision: Long,val archive:ArchiveRelayScope?=null,val group:GroupRelayScope?=null)
internal fun incomingArchiveSource(scope:ArchiveRelayScope,account:Long,local:String,remote:String):Boolean =
    scope.account==account&&scope.source==local&&scope.reader==remote
internal fun relayExecutions(bytes: ByteArray): List<RelayExecution> {
    val record=NativeRecord(bytes,1);val count=record.number();require(count<=32)
    val result=List(count.toInt()) {
        val attempt=record.number();val peer=record.text();val network=record.number()
        require(attempt>0 && network>0 && peer.matches(Regex("[0-9a-f]{64}")))
        val binding=record.bytes();val group=if(binding.size>=4&&binding[3].toInt()==27)GroupRelayScope.parse(binding)else null
        val archive=if(binding.isEmpty()||group!=null)null else ArchiveRelayScope.parse(binding)
        require(archive==null||peer==archive.source||peer==archive.reader)
        require(group==null||peer==group.targetDevice)
        RelayExecution(attempt,peer,network,archive,group)
    };record.end();return result
}
