package com.peerchat.android.application

import com.peerchat.android.platform.ServerRejected
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import androidx.test.core.app.ApplicationProvider
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.runBlocking

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class OwnDeviceInventoryTest {
    private val local="a".repeat(64)
    private val other="b".repeat(64)
    private val epoch="c".repeat(32)
    private fun response(revision:Long=1,name:String="Phone",revoked:Boolean=false)=JSONObject()
        .put("deviceManagementVersion",1).put("accountId",4).put("serverEpoch",epoch).put("authorityRevision",revision)
        .put("devices",JSONArray().put(JSONObject().put("publicIdentity",local).put("name",name)
            .put("state",if(revoked)"revoked" else "active").put("currentDevice",true))
            .put(JSONObject().put("publicIdentity",other).put("name","Windows").put("state","active").put("currentDevice",false)))
    private fun decode(value:JSONObject=response())=decodeOwnDeviceInventory(value,4,local,epoch)
    @Test fun namesAndRevocationAreDistinctFromIdentityAndHistory(){
        val snapshot=decode(response(name="My 📱",revoked=true))
        assertEquals(local,snapshot.devices.first().peerId)
        assertEquals("My 📱",snapshot.devices.first().name)
        assertTrue(snapshot.devices.first().revoked)
        assertEquals(other,snapshot.devices.last().peerId)
    }
    @Test fun malformedCrossAccountAndCrossEpochSnapshotsFailClosed(){
        for(value in listOf(response().put("accountId",3),response().put("serverEpoch","d".repeat(32)),response().put("deviceManagementVersion",2),response(-1),response(name="unsafe\nname"),response(name="x".repeat(65)))){
            assertThrows(IllegalArgumentException::class.java){decode(value)}
        }
        val duplicate=response();duplicate.getJSONArray("devices").put(1,duplicate.getJSONArray("devices").getJSONObject(0))
        assertThrows(IllegalArgumentException::class.java){decode(duplicate)}
        val wrongCurrent=response();wrongCurrent.getJSONArray("devices").getJSONObject(1).put("currentDevice",true)
        assertThrows(IllegalArgumentException::class.java){decode(wrongCurrent)}
    }
    @Test fun migratedZeroRevisionIsValid(){assertEquals(0,decode(response(0)).revision)}
    @Test fun snapshotCannotUndoNewerHintOrNameOrRevocation(){
        val projection=OwnDeviceInventoryProjection();projection.bind(4,epoch)
        assertTrue(projection.accept(decode()))
        assertTrue(projection.hint(4,epoch,3))
        assertFalse(projection.accept(decode(response(2))))
        assertTrue(projection.accept(decode(response(3,"Revoked phone",true))))
        assertFalse(projection.accept(decode(response(3,"Phone",false))))
        assertFalse(projection.accept(decode(response(4,"Phone",false))))
        assertFalse(projection.accept(decode(response(1))))
        assertEquals("Revoked phone",projection.snapshot!!.devices.first().name)
        assertTrue(projection.snapshot!!.devices.first().revoked)
    }
    @Test fun oldAccountSessionAndEpochCannotPublish(){
        val projection=OwnDeviceInventoryProjection();projection.bind(4,epoch);assertTrue(projection.accept(decode()))
        assertFalse(projection.hint(3,epoch,99));assertFalse(projection.hint(4,"d".repeat(32),99))
        projection.bind(3,epoch);assertNull(projection.snapshot);assertFalse(projection.accept(decode()))
        projection.bind(4,"d".repeat(32));assertFalse(projection.accept(decode()))
        projection.reset();assertFalse(projection.accept(decode()))
    }
    @Test fun displayNamesNeverRewriteKnownDeviceBindings(){
        val old=FriendIdentity(3,"Jonah",local,false,listOf(local,other))
        val current=FriendIdentity(3,"Jonah",other,true,revokedPeerIds=listOf(local))
        val merged=reconcileFriends(listOf(old),listOf(current)).single()
        assertEquals(listOf(local,other),merged.knownPeerIds)
        assertEquals(listOf(local),merged.revokedPeerIds)
        assertEquals(other,merged.peerId)
        assertThrows(IllegalArgumentException::class.java){reconcileFriends(emptyList(),listOf(current.copy(revokedPeerIds=listOf("bad"))))}
    }
    @Test fun deviceRevocationInvalidatesRuntimeWithoutDeletingCredentialOrRetrying(){
        val failure=ServerRejected("device_revoked")
        assertTrue(expiredServerSession(failure))
        assertFalse(isRememberedNetworkFailure(failure))
        assertFalse(isRememberedCredentialRejected(failure))
        val previous=MessagingSnapshot(accountId=4,localPeerId=local,authenticated=true,localHistoryReady=true,ownDevices=decode())
        val offline=localOfflineSnapshot(previous,"Device revoked")
        assertFalse(offline.authenticated);assertTrue(offline.localHistoryReady)
        assertEquals(local,offline.localPeerId);assertNull(offline.ownDevices)
    }
    @Test fun backendRevocationRetainsLocalConversationAndSuppressesAutomaticResume(){
        val backend=MessagingBackend(ApplicationProvider.getApplicationContext())
        backend.setInUse(false)
        assertTrue(runBlocking{backend.awaitQuiescence(5000)})
        try{
            val field=MessagingBackend::class.java.getDeclaredField("mutable").also{it.isAccessible=true}
            @Suppress("UNCHECKED_CAST") val state=field.get(backend) as MutableStateFlow<MessagingSnapshot>
            val message=MessageSnapshot("d".repeat(32),local,other,1,1,1,0,"isolated queued fixture")
            state.value=MessagingSnapshot(4,"Phone",local,true,localHistoryReady=true,
                conversations=listOf(ConversationSnapshot(FriendIdentity(3,"Friend",other,true),ConnectionSnapshot(ready=true),listOf(message))),ownDevices=decode())
            val fail=MessagingBackend::class.java.getDeclaredMethod("handleTickFailure",Exception::class.java).also{it.isAccessible=true}
            fail.invoke(backend,ServerRejected("device_revoked"))
            assertFalse(state.value.authenticated);assertFalse(state.value.serverPresenceLive)
            assertEquals(message,state.value.conversations.single().messages.single())
            assertTrue(state.value.localHistoryReady);assertNull(state.value.ownDevices)
            assertTrue(MessagingBackend::class.java.getDeclaredField("resumeSuppressed").also{it.isAccessible=true}.getBoolean(backend))
            assertNull(MessagingBackend::class.java.getDeclaredField("rememberedRetryEndpoint").also{it.isAccessible=true}.get(backend))
        }finally{backend.close()}
    }
}
