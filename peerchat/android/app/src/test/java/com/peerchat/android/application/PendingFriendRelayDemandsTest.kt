package com.peerchat.android.application

import org.junit.Assert.*
import org.junit.Test

class PendingFriendRelayDemandsTest {
    private val old="a".repeat(64)
    private val current="b".repeat(64)
    @Test fun hintBeforeRefreshWaitsForExactCurrentAccountAndDevice(){
        val hints=PendingFriendRelayDemands();val issued=mutableListOf<String>()
        hints.offer(20,current,100)
        hints.drain(listOf(FriendIdentity(20,"friend",old,true,listOf(old,current))),1,101,issued::add)
        assertTrue(issued.isEmpty()) // Historical binding is not current authority.
        hints.drain(listOf(FriendIdentity(21,"other",current,true)),1,102,issued::add)
        assertTrue(issued.isEmpty())
        hints.drain(listOf(FriendIdentity(20,"friend",current,true)),1,103,issued::add)
        assertEquals(listOf(current),issued)
        hints.drain(listOf(FriendIdentity(20,"friend",current,true)),1,104,issued::add)
        assertEquals(1,issued.size)
    }
    @Test fun unexpiredHintSurvivesTemporaryCanonicalReplacement(){
        val hints=PendingFriendRelayDemands();val issued=mutableListOf<String>()
        val friend=FriendIdentity(20,"friend",current,true)
        hints.offer(20,current,100);hints.drain(listOf(friend),1,101,issued::add)
        hints.drain(listOf(friend.copy(peerId=old)),2,102,issued::add)
        assertEquals(listOf(current),issued)
        hints.drain(listOf(friend),3,103,issued::add)
        assertEquals(listOf(current,current),issued)
        hints.drain(listOf(friend),3,104,issued::add)
        assertEquals(2,issued.size)
    }
    @Test fun revokedExpiredAndOldSessionHintsCannotCreateDemand(){
        val hints=PendingFriendRelayDemands();val issued=mutableListOf<String>()
        val friend=FriendIdentity(20,"friend",current,true)
        hints.offer(20,current,100)
        hints.drain(listOf(friend.copy(revokedPeerIds=listOf(current))),1,101,issued::add)
        assertTrue(issued.isEmpty())
        hints.drain(listOf(friend),1,15100,issued::add)
        assertTrue(issued.isEmpty())
        hints.offer(20,current,16000);hints.clear()
        hints.drain(listOf(friend),1,16001,issued::add)
        assertTrue(issued.isEmpty())
    }
}
