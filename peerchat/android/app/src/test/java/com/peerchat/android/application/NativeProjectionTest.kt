package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class NativeProjectionTest {
    private val a="a".repeat(64)
    private val b="b".repeat(64)
    private val c="c".repeat(64)
    private fun rejected(body: ()->Unit){var rejected=false;try {body()}catch(_: Exception){rejected=true};assertTrue("Malformed local snapshot accepted",rejected)}
    private fun historyWire(rows:List<MessageSnapshot>)=NativeRecordWriter(2).apply{
        number(rows.size.toLong());rows.forEach{m->text(m.id);text(m.senderDevice);text(m.recipientDevice)
            number(m.timestamp);number(m.logicalOrder);number(m.sequence);number(m.delivery);text(m.text)}
    }.bytes()
    @Test fun accountHistorySurvivesCurrentDeviceChangeAndFeedsNotifications() {
        val message=MessageSnapshot("f".repeat(32),b,c,100,1,9,2,"Test fixture")
        val friend=FriendIdentity(8,"Jonah",a,false,listOf(a,b))
        val connection=NativeRecordWriter(4).apply{repeat(5){number(0)};text("none")}.bytes()
        fun project(selected:String)=nativeConversationProjection(listOf(friend.copy(peerId=selected)),
            {assertEquals(selected,it);connection},{peer->historyWire(if(peer==b)listOf(message)else emptyList())}).single()
        val offline=project(a);assertEquals(listOf(message),offline.messages)
        assertEquals(project(b).messages,offline.messages)
        val snapshot=MessagingSnapshot(localPeerId=c,authenticated=true)
        val prior=com.peerchat.android.platform.ConversationPreference(notifiedSequence=8)
        assertEquals(1,planMessageNotification(snapshot,offline,prior,
            com.peerchat.android.platform.NotificationPreview.SenderOnly)!!.count)
        assertNull(planMessageNotification(snapshot,offline,prior.copy(notifiedSequence=9),
            com.peerchat.android.platform.NotificationPreview.SenderOnly))
    }
    @Test fun combinedPaginationUsesGlobalNativeRowsWithoutSkippingAnotherDevice() {
        val friend=FriendIdentity(8,"Jonah",a,false,listOf(a,b))
        val rows=(1L..24L).map{n->MessageSnapshot(n.toString(16).padStart(32,'0'),if(n<=12)a else b,c,
            100,1,n,2,"Test fixture")}
        fun page(before:Long)=accountHistoryPage(friend,before){peer,cursor->
            assertTrue(peer in friend.knownPeerIds)
            historyWire(rows.filter{it.senderDevice==peer&&it.sequence<cursor}.takeLast(8))}
        val first=page(Long.MAX_VALUE);val second=page(first.minOf{it.sequence});val third=page(second.minOf{it.sequence})
        assertEquals(24,(first+second+third).map{it.id}.distinct().size)
        assertEquals((1L..24L).toSet(),(first+second+third).map{it.sequence}.toSet())
        assertTrue(page(third.minOf{it.sequence}).isEmpty())
        assertEquals(first,chronologicalMessages(first.reversed()+first))
        assertEquals(first.sortedBy{it.id},first) // equal timestamps/orders have a stable tie-breaker
    }
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
    @Test fun headlessBootstrapProjectsPeersBeforeDeliveryWithoutPresenter() {
        val friend=FriendIdentity(8,"Jonah",a,true)
        val connection=NativeRecordWriter(4).apply{number(0);number(1);number(0);number(0);number(1);text("none")}.bytes()
        val empty=NativeRecordWriter(2).apply{number(0)}.bytes()
        var reads=0
        val before=nativeConversationProjection(listOf(friend),{assertEquals(a,it);reads++;connection},{assertEquals(a,it);empty})
        assertEquals(1,before.size);assertTrue(before.single().connection.pending);assertTrue(before.single().messages.isEmpty());assertEquals(1,reads)
        val id="f".repeat(32)
        val committed=NativeRecordWriter(2).apply{number(1);text(id);text(a);text(b);number(100);number(1);number(1);number(2);text("test message")}.bytes()
        repeat(3){
            val after=nativeConversationProjection(listOf(friend),{connection},{committed})
            assertEquals(id,after.single().messages.single().id)
            assertEquals(1,after.single().messages.size)
        }
        // Wire-storage regression is exercised separately by the actual JNI/
        // isolated-server proof; these are native snapshot fixtures, not fake ACKs.
    }
    @Test fun oldUnreadHistoryCannotCompleteNewWakeBeforeNativeReceiveAndOutput() {
        assertFalse(nativeWakeOutputComplete(10,20,true,false,30))
        assertFalse(nativeWakeOutputComplete(25,20,true,false,24))
        assertFalse(nativeWakeOutputComplete(25,20,false,false,30))
        assertFalse(nativeWakeOutputComplete(25,20,true,true,30))
        assertTrue(nativeWakeOutputComplete(25,20,true,false,30))
    }
    @Test fun backgroundQuiescenceKeepsNativeHistoryButRetiresEveryLiveStateClaim() {
        val messages=listOf(MessageSnapshot("f".repeat(32),a,b,100,1,1,3,"Test fixture"))
        val live=MessagingSnapshot(19,"Jonah",b,true,"Online",listOf(ConversationSnapshot(
            FriendIdentity(8,"Other",a,true),ConnectionSnapshot(ready=true,pending=true),messages)),serverPresenceLive=true,localHistoryReady=true)
        val offline=localOfflineSnapshot(live,"Offline")
        assertFalse(offline.authenticated);assertFalse(offline.serverPresenceLive)
        assertTrue(offline.localHistoryReady);assertEquals(live.localPeerId,offline.localPeerId)
        assertFalse(offline.conversations.single().connection.ready);assertFalse(offline.conversations.single().friend.online)
        assertSame(messages,offline.conversations.single().messages)
        assertEquals(3L,offline.conversations.single().messages.single().delivery) // Stored is NOT invented Delivered.
    }
}
