package com.peerchat.android.application

import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class GroupVoiceJoinTest {
    @Test fun staleRevisionRetriesOnceThenAccepts()=runBlocking {
        var retries=0
        val seen=mutableListOf<String>()
        assertEquals("accepted",awaitGroupVoiceJoin("old",{seen+=it;if(it=="old")"stale_room_state" else "accepted"},{retries++;"fresh"}))
        assertEquals(listOf("old","fresh"),seen);assertEquals(1,retries)
    }
    @Test fun repeatedConflictStops()=runBlocking {
        var retries=0
        assertEquals("stale_room_state",awaitGroupVoiceJoin("old",{"stale_room_state"},{retries++;"fresh"}))
        assertEquals(1,retries)
    }
    @Test fun failuresNeverRetry()=runBlocking {
        for(state in listOf("group_not_authorized","unavailable","timeout","room_not_found","accepted"))
            assertEquals(state,awaitGroupVoiceJoin("old",{state},{error("Must not retry $state")}))
    }
    @Test fun changedAuthorizationStopsRetry()=runBlocking {
        try { awaitGroupVoiceJoin("old",{"stale_room_state"},{error("Authorization changed")});fail("Expected rejection") }
        catch(e:IllegalStateException){assertEquals("Authorization changed",e.message)}
    }
}
