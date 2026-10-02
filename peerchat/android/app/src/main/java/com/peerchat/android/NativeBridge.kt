package com.peerchat.android

object NativeBridge {
    init { System.loadLibrary("peerchat_jni") }
    external fun create(): Long
    external fun destroy(handle: Long): Int
    external fun info(handle: Long): IntArray?
    external fun exchange(handle: Long, bytes: ByteArray): ByteArray?

    fun prove(): String {
        val bytes = "PeerChat / 日本語 / café / 😀\u0000end".toByteArray(Charsets.UTF_8)
        var version = 0
        repeat(100) {
            val handle = create()
            check(handle != 0L) { "Native instance creation failed" }
            try {
                val state = checkNotNull(info(handle))
                version = state[0]
                check(version == 1 && state[1] == 1)
                check(state.sliceArray(2..4).all { it == 0 })
                check(checkNotNull(exchange(handle, bytes)).contentEquals(bytes))
                check(exchange(handle, byteArrayOf(0xc0.toByte(), 0x80.toByte())) == null)
            } finally { check(destroy(handle) == 0) }
            check(destroy(handle) == 2)
            check(info(handle) == null)
        }
        return "Native foundation: Loaded\nABI version: $version\nJNI UTF-8/lifecycle: Passed (100 cycles)\nIdentity/storage/peer TLS: Not implemented"
    }
}
