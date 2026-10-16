package com.peerchat.android.application

/** Profile-local preferences, not negotiated state or delivery authority. */
data class TransferSettingsSnapshot(val preferredChunkBytes:Int=65536,val preferredWindow:Int=16,
    val uploadBitsPerSecond:Long=10_000_000,val downloadBitsPerSecond:Long=10_000_000) {
    fun validate(){require(preferredChunkBytes in listOf(32768,65536)&&preferredWindow in listOf(1,4,8,16))
        require(uploadBitsPerSecond in 0..10_000_000_000L&&downloadBitsPerSecond in 0..10_000_000_000L)}
    companion object {
        internal fun decode(bytes:ByteArray):TransferSettingsSnapshot {
            val r=NativeRecord(bytes,30);val chunk=r.number();val window=r.number();require(chunk in listOf(32768L,65536L)&&window in listOf(1L,4L,8L,16L));val value=TransferSettingsSnapshot(chunk.toInt(),window.toInt(),r.number(),r.number());r.end();value.validate();return value
        }
    }
}
