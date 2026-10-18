package com.peerchat.android.application

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class DeviceDisplayNamesTest {
    private val peer="a".repeat(64)
    @Test fun optionalMalformedNamesCannotRejectFriendshipOrAddAuthority(){
        assertNull(deviceDisplayNames(null));assertNull(deviceDisplayNames("wrong type"))
        assertNull(deviceDisplayNames(JSONObject().also{repeat(65){i->it.put(i.toString(),"name")}}))
        assertEquals(emptyMap<String,String>(),deviceDisplayNames(JSONObject()))
        val rows=JSONObject().put(peer,"Desktop 🖥").put("bad-id","Untrusted")
            .put("b".repeat(64),5).put("c".repeat(64),"Bad\nname")
            .put("d".repeat(64),"Bad\u202ename").put("e".repeat(64),"x".repeat(65))
        assertEquals(mapOf(peer to "Desktop 🖥"),deviceDisplayNames(rows))
        assertFalse(validDeviceDisplayName("\uD800"));assertFalse(validDeviceDisplayName("\uDC00"))
        val friend=FriendIdentity(2,"Friend",peer,true,deviceNames=mapOf("f".repeat(64) to "Display only"))
        assertEquals(listOf(peer),reconcileFriends(emptyList(),listOf(friend)).single().knownPeerIds)
    }
    @Test fun absentNamesRetainSameAccountLabelsAndExplicitEmptyClears(){
        val old=FriendIdentity(2,"Friend",peer,true,deviceNames=mapOf(peer to "Phone"))
        assertEquals(old.deviceNames,reconcileFriends(listOf(old),listOf(old.copy(deviceNames=null))).single().deviceNames)
        assertEquals(emptyMap<String,String>(),reconcileFriends(listOf(old),listOf(old.copy(deviceNames=emptyMap()))).single().deviceNames)
        assertEquals(emptyMap<String,String>(),reconcileFriends(listOf(old),listOf(old.copy(accountId=3,deviceNames=null))).single().deviceNames)
    }
    @Test fun labelsAndPresenceDoNotInvalidateNativeIdentityOrReceiptGeneration(){
        val before=FriendIdentity(2,"Friend",peer,true,deviceNames=mapOf(peer to "Phone"))
        assertTrue(sameFriendAuthority(listOf(before),listOf(before.copy(online=false,deviceNames=mapOf(peer to "Renamed")))))
        assertFalse(sameFriendAuthority(listOf(before),listOf(before.copy(revokedPeerIds=listOf(peer)))))
        assertFalse(sameFriendAuthority(listOf(before),listOf(before.copy(accountId=3))))
        assertFalse(sameFriendAuthority(listOf(before),listOf(before.copy(peerId="b".repeat(64)))))
    }
}
