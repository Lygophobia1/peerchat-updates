package com.peerchat.android

import android.os.SystemClock
import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.application.FriendIdentity
import com.peerchat.android.application.MessagingBackend
import com.peerchat.android.platform.LocalConversationContext
import com.peerchat.android.platform.LocalConversationContextStore
import com.peerchat.android.platform.PeerIdentityFormat
import com.peerchat.android.platform.ProfileSecurity
import com.peerchat.android.platform.ServerEndpoint
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File
import java.util.UUID
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit

/** Offline measurement only: a protected local index and native history, with no
 * server login, friendship authority, transport, or production profile. */
@RunWith(AndroidJUnit4::class)
class NativeStartupReadinessTest {
    private val context get()=InstrumentationRegistry.getInstrumentation().targetContext
    private val endpoint=ServerEndpoint("startup-fixture.invalid",7443,"f".repeat(64))
    private val historyPeer="b".repeat(64)

    @Test fun protectedOfflineStartupWith96Contacts()=measure(96)
    @Test fun protectedOfflineStartupWith128Contacts()=measure(128)

    private fun measure(count:Int) { runBlocking {
        val profile="test_startup_"+UUID.randomUUID().toString().replace("-","")
        val backends=mutableListOf<MessagingBackend>()
        try {
            val localId=NativeProfile(context,profile).use {native->
                assertEquals(0,ProfileBridge.storageTest(native.handle,0))
                PeerIdentityFormat.peerId(checkNotNull(ProfileBridge.publicKey(native.handle)))
            }
            val friends=List(count){index->
                val peer=if(index==0)historyPeer else (index+1).toString(16).padStart(64,'0')
                FriendIdentity(2000L+index,"Contact $index",peer,false)
            }
            LocalConversationContextStore(context,profile).save(endpoint,
                LocalConversationContext(1000,"Fixture",localId,friends))

            val cold=MessagingBackend(context,profile).also{backends+=it}
            val coldStarted=SystemClock.elapsedRealtimeNanos()
            assertTrue(cold.restoreLocalHistory(endpoint))
            val coldMs=(SystemClock.elapsedRealtimeNanos()-coldStarted)/1_000_000
            assertOffline(cold,count,localId)
            stop(cold)
            backends.remove(cold)

            val warm=MessagingBackend(context,profile).also{backends+=it}
            val warmStarted=SystemClock.elapsedRealtimeNanos()
            assertTrue(warm.restoreLocalHistory(endpoint))
            val warmMs=(SystemClock.elapsedRealtimeNanos()-warmStarted)/1_000_000
            assertOffline(warm,count,localId)
            val openStarted=SystemClock.elapsedRealtimeNanos()
            val history=warm.older(historyPeer,Long.MAX_VALUE)
            val selectedMs=(SystemClock.elapsedRealtimeNanos()-openStarted)/1_000_000
            assertEquals(1,history.size)
            assertEquals("c".repeat(32),history.single().id)
            assertOffline(warm,count,localId)
            Log.i("PeerChatStartupFixture",
                "contacts=$count cold_restore_ms=$coldMs warm_restore_ms=$warmMs selected_open_ms=$selectedMs authenticated=false network=false")
        } finally {
            var stopFailure:Throwable?=null
            for(backend in backends.asReversed())try {stop(backend)}catch(failure:Throwable){stopFailure=failure}
            if(stopFailure==null)clean(profile)
            else throw AssertionError("Backend did not close; test profile retained",stopFailure)
        }
    }}

    private fun assertOffline(backend:MessagingBackend,count:Int,localId:String) {
        val snapshot=backend.state.value
        assertTrue(snapshot.localHistoryReady)
        assertEquals(localId,snapshot.localPeerId)
        assertEquals(count,snapshot.conversations.size)
        assertFalse(snapshot.authenticated)
        assertFalse(snapshot.serverPresenceLive)
        assertFalse(snapshot.voice.available)
        assertTrue(snapshot.voiceRooms.isEmpty())
        assertTrue(snapshot.conversations.all{!it.friend.online&&!it.connection.ready&&
            !it.connection.pending&&!it.connection.retrying})
    }

    private fun stop(backend:MessagingBackend) {
        val field=MessagingBackend::class.java.getDeclaredField("executor").apply{isAccessible=true}
        val executor=field.get(backend) as ScheduledExecutorService
        backend.close()
        assertTrue("Backend close must finish before test profile cleanup",
            executor.awaitTermination(30,TimeUnit.SECONDS))
    }

    private fun clean(profile:String) {
        check(profile.startsWith("test_startup_"))
        val state=ProfileSecurity(context,profile)
        val store=state.store()
        store.deleteEntry(state.identityAlias)
        store.deleteEntry(state.storageAlias)
        val nativeDirectory=File(context.noBackupFilesDir,
            "profiles/"+state.id.joinToString(""){"%02x".format(it)})
        check(!nativeDirectory.exists()||nativeDirectory.deleteRecursively())
        check(!state.directory.exists()||state.directory.deleteRecursively())
    }
}
