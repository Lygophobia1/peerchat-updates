package com.peerchat.android

/** Scoped identity-directory commands. Native validates the record and owns all signing and trust. */
object IdentityDirectoryBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun request(handle:Long,input:ByteArray):ByteArray?
}
