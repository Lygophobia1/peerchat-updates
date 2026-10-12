package com.peerchat.android

/** Copied local commands into the profile's sole native TransferManager. */
object TransferBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun request(profile:Long,operation:Int,input:ByteArray):ByteArray?
}
