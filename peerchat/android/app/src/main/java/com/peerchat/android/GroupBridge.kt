package com.peerchat.android

/** Local command adapter only; native group/storage owners remain authoritative. */
object GroupBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun request(handle:Long,operation:Int,input:ByteArray):ByteArray?
}
