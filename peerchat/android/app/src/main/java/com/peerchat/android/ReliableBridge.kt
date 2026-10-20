package com.peerchat.android

/** Native reliable runtime commands only. Protocol/trust/session/delivery state
 * remains native. Inputs/results are copied buffers, never native pointers.
 * Snapshot/event records are local JNI records, not a new network protocol.
 */
object ReliableBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun friends(profile: Long, account: Long, generation: Long, snapshot: ByteArray): Int
    external fun presence(profile: Long, expectedPeer: ByteArray, online: Boolean, generation: Long, network: Long): Int
    external fun network(profile:Long,generation:Long,network:Long):Int
    external fun demand(profile: Long, expectedPeer: ByteArray): Int
    external fun requests(profile: Long): ByteArray?
    external fun open(profile: Long, attempt: Long, incoming: Boolean, expectedPeer: ByteArray): Long
    external fun exchange(profile: Long, peer: Long, operation: Int, input: ByteArray): ByteArray?
    external fun close(profile: Long, peer: Long, recoverable: Boolean): Int
    external fun send(profile: Long, expectedPeer: ByteArray, utf8: ByteArray): ByteArray?
    // Local durable queue only. Does not authorize friendship or any route.
    external fun queueLocal(profile: Long, expectedPeer: ByteArray, utf8: ByteArray): ByteArray?
    external fun history(profile: Long, expectedPeer: ByteArray, beforeSequence: Long): ByteArray?
    external fun message(profile:Long,expectedPeer:ByteArray,messageId:ByteArray):ByteArray?
    external fun state(profile: Long, expectedPeer: ByteArray): ByteArray?
    const val RECEIVE = 0
    const val OUTPUT = 1
    const val STATUS = 2
    const val INITIAL_AUTHENTICATED_FRIEND_PIN = 3
    const val EVENT = 4
    const val DEVICE_VERIFICATION = 5
    const val VERIFY_PRESENTED_DEVICE = 6
}
