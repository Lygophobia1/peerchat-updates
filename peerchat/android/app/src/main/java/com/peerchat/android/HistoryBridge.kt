package com.peerchat.android

import android.content.Context
import com.peerchat.android.platform.AndroidStoragePlatform

/** Local display reader only: separate connection/mutex, no session or transfer owner. */
object HistoryBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun bind(handle:Long,platform:AndroidStoragePlatform):Int
    external fun read(handle:Long,peer:ByteArray,before:Long):ByteArray?
}

internal class BackgroundHistoryReader(private val context:Context,private val profile:String) {
    private var bound=0L
    // Serializes only display readers. Never acquired by messages or transfers.
    @Synchronized fun read(handle:Long,peer:String,before:Long):ByteArray {
        if(bound!=handle){
            val platform=AndroidStoragePlatform(context,profile)
            check(HistoryBridge.bind(handle,platform)==0){"Local history reader unavailable"}
            // Native retains/closes the provider with this profile instance.
            bound=handle
        }
        return checkNotNull(HistoryBridge.read(handle,peer.toByteArray(Charsets.US_ASCII),before))
    }
}
