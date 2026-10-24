package com.peerchat.android

/** JNI adapter to the shared authenticated group attachment owner. */
object GroupAttachmentBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun request(handle:Long,operation:Int,input:ByteArray):ByteArray?
}
