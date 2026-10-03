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
        val count=number();require(count<=65536 && count<=input.remaining())
        val bytes=ByteArray(count.toInt());input.get(bytes)
        return Charsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes)).toString()
    }
    fun end() { require(!input.hasRemaining()) }
}
internal class NativeRecordWriter(type: Int) {
    private val output=ByteArrayOutputStream().apply {write(byteArrayOf(80,84,1,type.toByte()))}
    fun number(value: Long) {require(value>=0);repeat(8){output.write((value ushr (it*8)).toInt())}}
    fun text(value: String) {val bytes=value.toByteArray(Charsets.UTF_8);require(bytes.size<=65536);number(bytes.size.toLong());output.write(bytes)}
    fun bytes(): ByteArray = output.toByteArray().also {require(it.size<=262144)}
}
data class RelayExecution(val attempt: Long,val peerId: String,val networkRevision: Long)
internal fun relayExecutions(bytes: ByteArray): List<RelayExecution> {
    val record=NativeRecord(bytes,1);val count=record.number();require(count<=32)
    val result=List(count.toInt()) {
        val attempt=record.number();val peer=record.text();val network=record.number()
        require(attempt>0 && network>0 && peer.matches(Regex("[0-9a-f]{64}")))
        RelayExecution(attempt,peer,network)
    };record.end();return result
}
