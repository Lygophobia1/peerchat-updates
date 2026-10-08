package com.peerchat.android

/** Bounded profile-worker commands only. Native Enrollment owns consent and
 * signed grants; this bridge owns no trust, history, socket, or retry policy. */
object ArchiveBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun request(handle: Long, operation: Int, input: ByteArray): ByteArray?
}
