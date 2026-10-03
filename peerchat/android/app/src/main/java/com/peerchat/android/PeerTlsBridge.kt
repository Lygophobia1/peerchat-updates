package com.peerchat.android

/** Byte-feed boundary only. Native code owns lifetime and profile association.
 * No sockets, framing, retries, relationship promotion or delivery in Kotlin.
 */
object PeerTlsBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun open(profile: Long, incoming: Boolean, expectedPeer: ByteArray): Long
    external fun exchange(profile: Long, session: Long, operation: Int, input: ByteArray): ByteArray?
    external fun close(profile: Long, session: Long): Int
    const val RECEIVE = 0
    const val SEND = 1
    const val OUTPUT = 2
    const val PLAINTEXT = 3
    const val REMOTE_PUBLIC_KEY = 4
    const val STATUS = 5 // [ready, closed]; no friend/group trust promotion.
    const val SHUTDOWN = 6
}
