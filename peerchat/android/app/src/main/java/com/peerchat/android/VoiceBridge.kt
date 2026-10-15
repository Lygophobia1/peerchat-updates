package com.peerchat.android

/** Local commands; native CallManager and actual media capability own truth. */
object VoiceBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun request(handle:Long,operation:Int,input:ByteArray):ByteArray?
}
