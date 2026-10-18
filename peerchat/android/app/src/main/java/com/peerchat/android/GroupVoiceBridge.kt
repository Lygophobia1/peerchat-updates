package com.peerchat.android

/** Private profile-scoped group media commands. This is not the server wire. */
object GroupVoiceBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun request(handle:Long,operation:Int,input:ByteArray):ByteArray?
}
