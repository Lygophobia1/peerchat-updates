package com.peerchat.android

import android.content.Context
import com.peerchat.android.platform.AndroidStoragePlatform

// Application/platform binding only. Protocol, history/outbox, sessions and
// delivery remain native. No Kotlin messaging/reconnect implementation.
object ProfileBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun bind(handle: Long, platform: AndroidStoragePlatform): Int
    external fun publicKey(handle: Long): ByteArray?
    external fun sign(handle: Long, payload: ByteArray): ByteArray?
    // Export exists only in the debug build and rejects non-test profiles.
    external fun storageTest(handle: Long, mode: Int): Int
    external fun attachmentTest(handle: Long): Int
}
class NativeProfile(context: Context, profile: String = "default") : AutoCloseable {
    val handle = NativeBridge.create()
    private var platform: AndroidStoragePlatform? = null
    private var closed = false
    internal fun recipientDiagnostics():String=platform?.recipientDiagnostic ?: "stage=profile_closed"
    init {
        check(handle != 0L) { "Native instance unavailable" }
        try {
            platform = AndroidStoragePlatform(context, profile)
            check(ProfileBridge.bind(handle, checkNotNull(platform)) == 0) { "Native profile provider binding failed" }
        } catch (failure: Exception) { NativeBridge.destroy(handle); platform?.close(); throw failure }
    }
    override fun close() {
        // The native provider context closes the facade after its last admitted
        // operation. Closing it here could race a call holding the native instance.
        if (!closed) { NativeBridge.destroy(handle); platform = null; closed = true }
    }
}
