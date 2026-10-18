package com.peerchat.android.application

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

@org.junit.runner.RunWith(org.robolectric.RobolectricTestRunner::class)
@org.robolectric.annotation.Config(sdk=[34])
class GroupVoiceRoomsTest {
    private val group = "a".repeat(32)
    private val room = "b".repeat(32)
    private val epoch = "c".repeat(32)
    private val nextEpoch = "d".repeat(32)
    private val localDevice = "1".repeat(64)
    private val peerDevice = "2".repeat(64)
    private val localGeneration = "3".repeat(32)
    private val peerGeneration = "4".repeat(32)
    private val route = "5".repeat(32)

    private fun member(account: Long, device: String, generation: String) = JSONObject()
        .put("accountId", account).put("publicIdentity", device)
        .put("username", "member $account").put("mediaGeneration", generation)

    private fun room(members: JSONArray = JSONArray().put(member(7, localDevice, localGeneration))
        .put(member(8, peerDevice, peerGeneration)), id: String = room) = JSONObject()
        .put("roomId", id).put("kind", "voice").put("name", "Voice")
        .put("creatorAccountId", 7).put("creatorDevice", localDevice)
        .put("sharer", JSONObject.NULL).put("members", members)

    private fun snapshot(revision: Long = 1, roster: Long = 2, epoch: String = this.epoch,
                         rooms: JSONArray = JSONArray().put(room())) = JSONObject()
        .put("groupId", group).put("epoch", epoch).put("revision", revision)
        .put("rosterGeneration", roster).put("rooms", rooms)

    private fun snapshotEvent(value: JSONObject) = JSONObject()
        .put("type", "group_room_snapshot").put("groupId", group).put("snapshot", value)

    private fun signal(revision: Long = 1, roster: Long = 2,
                       sourceDevice: String = peerDevice, sourceGeneration: String = peerGeneration,
                       targetDevice: String = localDevice, targetGeneration: String = localGeneration) = JSONObject()
        .put("type", "group_media_signal").put("groupId", group).put("roomId", room)
        .put("epoch", epoch).put("revision", revision).put("rosterGeneration", roster)
        .put("sourceAccountId", 8).put("sourceDevice", sourceDevice).put("sourceGeneration", sourceGeneration)
        .put("targetAccountId", 7).put("targetDevice", targetDevice).put("targetGeneration", targetGeneration)
        .put("routeId", route).put("mediaGeneration", 1).put("kind", "offer")
        .put("data", "sdp").put("mid", "0")

    private fun GroupVoiceRooms.trackAndLoad(value: JSONObject = snapshot()) {
        assertNotNull(request(group, "snapshot") { true })
        event(snapshotEvent(value), 7, localDevice)
    }

    @Test fun snapshotRevisionAndRosterNeverMoveBackwardWithinEpoch() {
        val rooms = GroupVoiceRooms()
        rooms.trackAndLoad()
        rooms.event(snapshotEvent(snapshot(revision = 0, roster = 2)), 7, localDevice)
        assertEquals(1L, rooms.snapshot(group)?.revision)
        rooms.event(snapshotEvent(snapshot(revision = 2, roster = 1)), 7, localDevice)
        assertEquals(1L, rooms.snapshot(group)?.revision)
        rooms.event(snapshotEvent(snapshot(revision = 2, roster = 3)), 7, localDevice)
        assertEquals(2L, rooms.snapshot(group)?.revision)
        assertEquals(3L, rooms.snapshot(group)?.roster)
        rooms.event(snapshotEvent(snapshot(revision = 0, roster = 1, epoch = nextEpoch)), 7, localDevice)
        assertEquals(epoch, rooms.snapshot(group)?.epoch)
        assertEquals(2L, rooms.snapshot(group)?.revision)
        rooms.reset()
        rooms.trackAndLoad(snapshot(revision = 0, roster = 1, epoch = nextEpoch))
        assertEquals(nextEpoch, rooms.snapshot(group)?.epoch)
        assertEquals(0L, rooms.snapshot(group)?.revision)
    }

    @Test fun resetDropsRoomMembershipPendingRequestsAndOldChannelSignals() {
        val rooms = GroupVoiceRooms()
        rooms.trackAndLoad()
        val join = rooms.request(group, "join_voice", room) { true }!!
        rooms.event(signal(), 7, localDevice)
        rooms.reset()
        assertNull(rooms.snapshot(group))
        assertEquals("unavailable", rooms.result(join))
        assertNull(rooms.request(group, "join_voice", room) { true })
        rooms.event(snapshotEvent(snapshot()), 7, localDevice)
        rooms.event(JSONObject().put("type", "group_room_result").put("groupId", group)
            .put("requestId", join).put("state", "accepted").put("snapshot", snapshot()), 7, localDevice)
        assertNull(rooms.snapshot(group))
        rooms.trackAndLoad(snapshot(revision = 0, epoch = nextEpoch, rooms = JSONArray().put(room(JSONArray()))))
        assertEquals(0, rooms.snapshot(group)?.rooms?.single()?.members?.size)
        assertTrue(rooms.take(group, room, 7, localDevice).isEmpty())
    }

    @Test fun pendingRequestsAreBoundedAndExpire() {
        var clock = 100L
        val rooms = GroupVoiceRooms { clock }
        val ids = (0 until 64).map { index ->
            rooms.request(index.toString(16).padStart(32, '0'), "snapshot") { true }!!
        }
        assertNull(rooms.request(group, "snapshot") { true })
        assertEquals("pending", rooms.result(ids.first()))
        clock += 15_000
        val fresh = rooms.request(group, "snapshot") { true }
        assertNotNull(fresh)
        assertEquals("timeout", rooms.result(ids.first()))
        assertEquals("pending", rooms.result(fresh!!))
    }

    @Test fun repeatedSnapshotRequestSharesOnePendingWireRequest() {
        val rooms = GroupVoiceRooms()
        var sends = 0
        val first = rooms.request(group, "snapshot") { sends++; true }
        val second = rooms.request(group, "snapshot") { sends++; true }
        assertEquals(first, second)
        assertEquals(1, sends)
    }

    @Test fun nativeRosterChangeRetiresRoomStateAndRejectsLateOldRoster() {
        val rooms=GroupVoiceRooms()
        fun authorized(roster:Long)=listOf(GroupConversationSnapshot(group,"d".repeat(32),roster,"Group",true,2))
        assertTrue(rooms.reconcileAuthorized(authorized(2)))
        rooms.trackAndLoad()
        val join=rooms.request(group,"join_voice",room){true}!!
        assertNotNull(rooms.snapshot(group))

        assertTrue(rooms.reconcileAuthorized(authorized(3)))
        assertNull(rooms.snapshot(group))
        assertEquals("unavailable",rooms.result(join))
        val refresh=rooms.request(group,"snapshot"){true}
        assertNotNull(refresh)
        rooms.event(snapshotEvent(snapshot(roster=2)),7,localDevice)
        assertNull(rooms.snapshot(group))
        rooms.event(snapshotEvent(snapshot(roster=3)),7,localDevice)
        assertEquals(3L,rooms.snapshot(group)?.roster)
        assertFalse(rooms.reconcileAuthorized(authorized(3)))
    }

    @Test fun snapshotPollingPrioritizesActiveGroupAndBoundsIdleCadence() {
        var clock=100L
        val rooms=GroupVoiceRooms { clock }
        val other="e".repeat(32)
        rooms.reconcileAuthorized(listOf(
            GroupConversationSnapshot(group,"d".repeat(32),2,"Group",true,2),
            GroupConversationSnapshot(other,"f".repeat(32),2,"Other",true,2)))
        assertEquals(group,rooms.dueSnapshotGroup(group))
        val first=rooms.request(group,"snapshot"){true}!!
        assertEquals(other,rooms.dueSnapshotGroup(group))
        rooms.request(other,"snapshot"){true}
        assertNull(rooms.dueSnapshotGroup(group))
        assertEquals(first,rooms.request(group,"snapshot"){fail("duplicate request");true})
        clock+=15_000
        assertEquals(group,rooms.dueSnapshotGroup(group))
        rooms.request(group,"snapshot"){true}
        assertNull(rooms.dueSnapshotGroup(group))
        clock+=45_000
        assertEquals(group,rooms.dueSnapshotGroup(group))
    }

    @Test fun newerAuthenticatedRosterRetiresOldRoomWithoutGrantingNewAuthority() {
        val rooms=GroupVoiceRooms()
        fun authorized(roster:Long)=listOf(GroupConversationSnapshot(group,"d".repeat(32),roster,"Group",true,2))
        rooms.reconcileAuthorized(authorized(2))
        rooms.trackAndLoad()
        val join=rooms.request(group,"join_voice",room){true}!!
        rooms.event(signal(),7,localDevice)

        rooms.event(snapshotEvent(snapshot(roster=3,revision=2)),7,localDevice)
        assertNull(rooms.snapshot(group))
        assertEquals("unavailable",rooms.result(join))
        assertTrue(rooms.take(group,room,7,localDevice).isEmpty())
        assertNull(rooms.request(group,"join_voice",room){fail("old roster joined");true})

        rooms.event(snapshotEvent(snapshot(roster=2,revision=3)),7,localDevice)
        assertFalse(rooms.reconcileAuthorized(authorized(2)))
        assertNull(rooms.snapshot(group))
        assertTrue(rooms.reconcileAuthorized(authorized(3)))
        assertNotNull(rooms.request(group,"snapshot"){true})
        rooms.event(snapshotEvent(snapshot(roster=3,revision=2)),7,localDevice)
        assertEquals(3L,rooms.snapshot(group)?.roster)
    }

    @Test fun oldRoomSnapshotStopsGrantingJoinAuthorityAndResetClearsPollClock() {
        var clock=100L
        val rooms=GroupVoiceRooms { clock }
        val authorized=listOf(GroupConversationSnapshot(group,"d".repeat(32),2,"Group",true,2))
        rooms.reconcileAuthorized(authorized)
        rooms.trackAndLoad()
        assertTrue(rooms.isFresh(group))
        assertNotNull(rooms.request(group,"join_voice",room){true})
        rooms.dueSnapshotGroup(group)
        clock+=45_000
        assertFalse(rooms.isFresh(group))
        assertNull(rooms.snapshot(group))
        assertNull(rooms.request(group,"join_voice",room){fail("stale room joined");true})
        rooms.reset()
        rooms.reconcileAuthorized(authorized)
        assertEquals(group,rooms.dueSnapshotGroup())
        assertNull(rooms.snapshot(group))
    }

    @Test fun futureSignalWaitsForSnapshotAndCannotOutliveItsParticipation() {
        val rooms = GroupVoiceRooms()
        rooms.trackAndLoad()
        rooms.event(signal(revision = 2), 7, localDevice)
        assertTrue(rooms.take(group, room, 7, localDevice).isEmpty())
        rooms.event(snapshotEvent(snapshot(revision = 2)), 7, localDevice)
        assertEquals(1, rooms.take(group, room, 7, localDevice).size)
        assertTrue(rooms.take(group, room, 7, localDevice).isEmpty())

        rooms.event(signal(revision = 2), 7, localDevice)
        rooms.event(snapshotEvent(snapshot(revision = 3, rooms = JSONArray().put(room(JSONArray()
            .put(member(7, localDevice, localGeneration)))))), 7, localDevice)
        assertTrue(rooms.take(group, room, 7, localDevice).isEmpty())
    }

    @Test fun signalsRequireExactTargetSourceAndParticipation() {
        val rooms = GroupVoiceRooms()
        rooms.trackAndLoad()
        rooms.event(signal(targetDevice = peerDevice), 7, localDevice)
        rooms.event(signal(sourceGeneration = "6".repeat(32)), 7, localDevice)
        rooms.event(signal(targetGeneration = "6".repeat(32)), 7, localDevice)
        rooms.event(signal(sourceDevice = localDevice), 7, localDevice)
        assertTrue(rooms.take(group, room, 7, localDevice).isEmpty())
        rooms.event(signal(), 7, localDevice)
        assertEquals(1, rooms.take(group, room, 7, localDevice).size)
        assertThrows(IllegalArgumentException::class.java) {
            rooms.event(signal().put("routeId", "bad"), 7, localDevice)
        }
    }

    @Test fun signalQueueIsBoundedAndExpires() {
        var clock = 100L
        val rooms = GroupVoiceRooms { clock }
        rooms.trackAndLoad()
        repeat(140) { rooms.event(signal(), 7, localDevice) }
        assertEquals(128, rooms.take(group, room, 7, localDevice).size)
        rooms.event(signal(), 7, localDevice)
        clock += 10_000
        assertTrue(rooms.take(group, room, 7, localDevice).isEmpty())
    }

    @Test fun duplicateRoomsAndDevicesAreRejected() {
        assertThrows(IllegalArgumentException::class.java) {
            GroupVoiceRooms.parse(snapshot(rooms = JSONArray().put(room()).put(room())))
        }
        assertThrows(IllegalArgumentException::class.java) {
            GroupVoiceRooms.parse(snapshot(rooms = JSONArray().put(room(JSONArray()
                .put(member(7, localDevice, localGeneration))
                .put(member(8, localDevice, peerGeneration))))))
        }
    }

    @Test fun unauthorizedResponseRevokesProjectionAndQueuedSignals() {
        val rooms = GroupVoiceRooms()
        rooms.trackAndLoad()
        rooms.event(signal(), 7, localDevice)
        val join = rooms.request(group, "join_voice", room) { true }!!
        rooms.event(JSONObject().put("type", "group_room_result").put("groupId", group)
            .put("requestId", join).put("operation", "join_voice")
            .put("state", "group_not_authorized"), 7, localDevice)
        assertEquals("group_not_authorized", rooms.result(join))
        assertNull(rooms.snapshot(group))
        assertTrue(rooms.take(group, room, 7, localDevice).isEmpty())
        assertNull(rooms.request(group, "join_voice", room) { true })
    }

    @Test fun outboundSignalUsesOnlyCurrentMemberGenerations() {
        val rooms = GroupVoiceRooms()
        rooms.trackAndLoad()
        val output = mutableListOf<JSONObject>()
        val target = GroupVoiceMember(8, peerDevice, peerGeneration, "member 8").key
        val id = rooms.send(group, room, 7, localDevice, target, route, 4, "candidate", "ice", "0") {
            output += it; true
        }
        assertNotNull(id)
        assertEquals("pending", rooms.result(id!!))
        val wire = output.single()
        assertEquals("group_media", wire.getString("type"))
        assertEquals(localGeneration, wire.getString("sourceGeneration"))
        assertEquals(peerGeneration, wire.getString("targetGeneration"))
        assertEquals(8L, wire.getLong("targetAccountId"))
        assertNull(rooms.send(group, room, 7, localDevice, "8:$peerDevice:${"6".repeat(32)}",
            route, 4, "offer", "sdp", "0") { fail("stale target sent"); true })
        assertNull(rooms.send(group, room, 9, localDevice, target, route, 4, "offer", "sdp", "0") {
            fail("nonmember sent"); true
        })
    }

    @Test fun payloadLimitIsUtf8BytesAndRetirementRemovesPendingAuthority() {
        val rooms=GroupVoiceRooms()
        rooms.trackAndLoad()
        val target=GroupVoiceMember(8,peerDevice,peerGeneration,"member 8").key
        assertThrows(IllegalArgumentException::class.java) {
            rooms.send(group,room,7,localDevice,target,route,1,"candidate","é".repeat(2049),"0") {
                fail("oversized UTF-8 payload sent");true
            }
        }
        val pending=rooms.request(group,"join_voice",room){true}!!
        rooms.event(signal(),7,localDevice)
        rooms.retain(emptySet())
        assertEquals("unavailable",rooms.result(pending))
        assertTrue(rooms.snapshots().isEmpty())
        assertTrue(rooms.take(group,room,7,localDevice).isEmpty())
        assertNull(rooms.request(group,"join_voice",room){fail("retired room joined");true})
    }
}
