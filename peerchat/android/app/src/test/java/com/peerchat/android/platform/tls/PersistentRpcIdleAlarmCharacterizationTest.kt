package com.peerchat.android.platform.tls

import org.junit.Assert.*
import org.junit.Test
import java.net.SocketException

/** Models the persistent RPC channel's independent 10-second socket alarm. */
class PersistentRpcIdleAlarmCharacterizationTest {
    private class Channel(private val now:()->Long):AutoCloseable {
        var deadline=now()+10_000_000_000L
        var closed=false
        fun beginRequest() {
            if(now()>=deadline){closed=true;throw SocketException("Prior idle alarm closed channel")}
            // ServerChannel.readTimeout(10000) cancels the old alarm and arms a
            // fresh hard operation timeout when a request actually begins.
            deadline=now()+10_000_000_000L
        }
        override fun close(){closed=true}
    }

    @Test fun idleAlarmCanCloseBetweenReuseCheckAndRequestRearm() {
        var now=0L;var opens=0
        val owner=BoundedRpc({opens++;Channel{now}},{!it.closed},{now})
        try {
            owner.run("same-session") { it.beginRequest() }
            assertEquals(1,opens)
            now=9_999_999_999L // Still below BoundedRpc's 10-second idle limit.
            assertThrows(SocketException::class.java) {
                owner.run("same-session") { channel ->
                    now+=2 // Old socket alarm wins just before readTimeout re-arms.
                    channel.beginRequest()
                }
            }
            assertEquals(1,opens)
            owner.run("same-session") { it.beginRequest() }
            assertEquals("A failed channel is retired and the next request reopens",2,opens)
        } finally { owner.close() }
    }
}
