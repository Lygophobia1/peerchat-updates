package com.peerchat.android

// Narrow native application boundary. Kotlin transports opaque bytes/results;
// native KeyManager/Storage/DeliveryManager own custody, identity, IDs and ACKs.
object MailboxBridge {
    init { System.loadLibrary("peerchat_messaging") }
    external fun configureEnrollment(handle:Long,account:Long,generation:Long,server:ByteArray,pin:ByteArray):Int
    external fun reenrollmentOffer(handle:Long):ByteArray?
    external fun reenrollmentDecision(handle:Long,proof:ByteArray):Int
    external fun reenrollmentError(handle:Long):ByteArray?
    external fun hasScopedEnrollment(handle:Long):Int
    external fun registration(handle: Long, storageAllowed: Boolean): ByteArray?
    external fun registrationDiagnostic(handle:Long):ByteArray?
    external fun registered(handle: Long, binding: ByteArray): Int
    external fun pending(handle: Long): ByteArray?
    external fun prepare(handle: Long, messageId: ByteArray, binding: ByteArray, storageAllowed: Boolean): ByteArray?
    external fun prepareDevices(handle: Long, messageId: ByteArray, bindings: ByteArray, storageAllowed: Boolean): ByteArray?
    external fun stored(handle: Long, envelope: ByteArray): Int
    external fun receive(handle: Long, envelope: ByteArray): ByteArray?
    external fun receipt(handle: Long, receipt: ByteArray): Int
}
