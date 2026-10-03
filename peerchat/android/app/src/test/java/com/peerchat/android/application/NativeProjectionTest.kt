package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class NativeProjectionTest {
    private val a="a".repeat(64)
    private val b="b".repeat(64)
    private val c="c".repeat(64)
    private fun rejected(body: ()->Unit){var rejected=false;try {body()}catch(_: Exception){rejected=true};assertTrue("Malformed local snapshot accepted",rejected)}
    @Test fun currentBindingsRetainHistoricalDeviceWithoutPromotingTrust() {
        val old=FriendIdentity(8,"Jonah",a,false)
        val current=reconcileFriends(listOf(old),listOf(FriendIdentity(8,"Jonah",b,true)))
        assertEquals(listOf(a,b),current.single().knownPeerIds)
        assertEquals(b,current.single().peerId)
        assertEquals(8L,current.single().accountId)
        // This projection contains no verified/trusted flag or approval command.
        assertEquals("Jonah",current.single().username)
    }
    @Test fun duplicateNamesRemainSeparateAccountDeviceBindings() {
        val result=reconcileFriends(emptyList(),listOf(FriendIdentity(8,"Jonah",a,true),FriendIdentity(9,"Jonah",b,false)))
        assertEquals(listOf(8L,9L),result.map {it.accountId})
        assertEquals(listOf(listOf(a),listOf(b)),result.map {it.knownPeerIds})
        rejected {reconcileFriends(emptyList(),listOf(FriendIdentity(8,"Jonah",a,true),FriendIdentity(9,"Other",a,true)))}
    }
    @Test fun malformedAndUnboundedBindingsRejected() {
        rejected {reconcileFriends(emptyList(),listOf(FriendIdentity(0,"Name",a,true)))}
        rejected {reconcileFriends(emptyList(),listOf(FriendIdentity(8,"Name","wrong",true)))}
        rejected {reconcileFriends(emptyList(),listOf(FriendIdentity(8,"Name",a,true),FriendIdentity(8,"Name",b,true)))}
        rejected {reconcileFriends(emptyList(),listOf(FriendIdentity(8,"x".repeat(65),a,true)))}
    }
    @Test fun nativeSessionReadoutKeepsOwnerFlags() {
        val value=NativeRecordWriter(4).apply {number(1);number(0);number(1);number(5000);number(3);text("platform relay failed")}.bytes()
        val state=connectionSnapshot(value)
        assertTrue(state.ready);assertFalse(state.pending);assertTrue(state.retrying)
        assertEquals(5000L,state.retryDelayMs);assertEquals(3L,state.networkRevision)
        rejected {connectionSnapshot(value+byteArrayOf(0))}
        rejected {connectionSnapshot(NativeRecordWriter(4).apply {number(2);number(0);number(0);number(0);number(1);text("none")}.bytes())}
    }
    @Test fun relayCommandsAreBoundedAndAttemptBound() {
        val value=NativeRecordWriter(1).apply {number(1);number(23);text(a);number(5)}.bytes()
        assertEquals(RelayExecution(23,a,5),relayExecutions(value).single())
        rejected {relayExecutions(value.copyOf(value.size-1))}
        rejected {relayExecutions(NativeRecordWriter(1).apply {number(33)}.bytes())}
        rejected {relayExecutions(NativeRecordWriter(1).apply {number(1);number(0);text(a);number(5)}.bytes())}
    }
    @Test fun nativeHistoryKeepsDeviceAttributionAndDelivery() {
        val value=NativeRecordWriter(2).apply {
            number(1);text("f".repeat(32));text(a);text(b);number(100);number(20);number(3);number(2);text("Hello 日本語 😀")
        }.bytes()
        val message=messageSnapshots(value).single()
        assertEquals(a,message.senderDevice);assertEquals(b,message.recipientDevice)
        assertEquals(2L,message.delivery);assertEquals(3L,message.sequence);assertEquals("Hello 日本語 😀",message.text)
        rejected {messageSnapshots(value.copyOf(value.size-1))}
        rejected {messageSnapshots(NativeRecordWriter(2).apply {number(9)}.bytes())}
    }
    @Test fun malformedUtf8AndNegativeLengthsFailClosed() {
        val value=NativeRecordWriter(1).apply {number(1);number(1);text(a);number(1)}.bytes()
        value[28]=0xff.toByte()
        rejected {relayExecutions(value)}
        rejected {NativeRecord(byteArrayOf(80,84,1),1)}
        val number=NativeRecordWriter(1).apply {number(1)}.bytes();number[11]=0xff.toByte()
        rejected {relayExecutions(number)}
    }
    @Test fun snapshotsDoNotRebindIdentityUsingFriendlyNames() {
        val first=reconcileFriends(emptyList(),listOf(FriendIdentity(8,"Display",a,true)))
        val second=reconcileFriends(first,listOf(FriendIdentity(9,"Display",c,true)))
        assertEquals(listOf(c),second.single().knownPeerIds)
        assertEquals(9L,second.single().accountId)
    }
}
