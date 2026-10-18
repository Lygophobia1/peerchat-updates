package com.peerchat.android.platform

import com.peerchat.android.application.FriendIdentity
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class ProtectedFriendIdentityIndexTest {
    private val peer="b".repeat(64)
    private val label="profile-device-server-account-context".toByteArray()
    private val records=object:RecordProtector {
        private val key=SecretKeySpec(ByteArray(32){it.toByte()},"AES")
        override fun protect(plain:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.ENCRYPT_MODE,key)
            cipher.updateAAD(context);return cipher.iv+cipher.doFinal(plain)
        }
        override fun unprotect(blob:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.DECRYPT_MODE,key,GCMParameterSpec(128,blob.copyOfRange(0,12)))
            cipher.updateAAD(context);return cipher.doFinal(blob.copyOfRange(12,blob.size))
        }
    }
    @Test fun renameAndRestartRetainEncryptedNamesButNeverAddTrustOrRoutingIdentities(){
        var disk:ByteArray?=null
        fun open(account:Long=19,aad:ByteArray=label)=ProtectedFriendIdentityIndex(account,aad,{disk},{disk=it.copyOf()},records)
        val friend=FriendIdentity(8,"Other",peer,true,deviceNames=mapOf(peer to "Private phone name","c".repeat(64) to "Label only"))
        open().reconcile(listOf(friend))
        assertFalse(disk!!.toString(Charsets.UTF_8).contains("Private phone name"))
        val restored=open().localHistoryBindings().single()
        assertEquals(friend.deviceNames,restored.deviceNames);assertFalse(restored.online)
        assertEquals(listOf(peer),restored.knownPeerIds)
        assertEquals(friend.deviceNames,open().reconcile(listOf(friend.copy(deviceNames=null))).single().deviceNames)
        open().reconcile(listOf(friend.copy(deviceNames=mapOf(peer to "Renamed phone"))))
        assertEquals("Renamed phone",open().localHistoryBindings().single().deviceNames!![peer])
        assertThrows(Exception::class.java){open(account=20)}
        assertThrows(Exception::class.java){open(aad="different-profile".toByteArray())}
        open().reconcile(emptyList())
        assertEquals("Renamed phone",open().localHistoryBindings().single().deviceNames!![peer])
        val another=open().reconcile(listOf(friend.copy(accountId=9,deviceNames=null))).single()
        assertTrue(another.deviceNames!!.isEmpty())
    }
    @Test fun legacyProtectedIndexWithoutLabelsStillLoadsAndKeepsHistoryMapping(){
        val legacy=JSONObject().put("version",1).put("localAccount",19).put("bindings",JSONArray().put(
            JSONObject().put("account",8).put("username","Other").put("current",peer).put("devices",JSONArray().put(peer))))
        var disk:ByteArray?=records.protect(legacy.toString().toByteArray(),label)
        val index=ProtectedFriendIdentityIndex(19,label,{disk},{disk=it},records)
        assertNull(index.localHistoryBindings().single().deviceNames)
        val refreshed=index.reconcile(listOf(FriendIdentity(8,"Other",peer,true)))
        assertEquals(listOf(peer),refreshed.single().knownPeerIds)
        assertTrue(refreshed.single().deviceNames!!.isEmpty())
    }

    @Test fun persistsOnlySerializedChangesAndRetriesAfterFailedWrite(){
        var disk:ByteArray?=null;var writes=0;var failWrite=false
        val index=ProtectedFriendIdentityIndex(19,label,{disk},{
            writes++
            if(failWrite)throw IllegalStateException("write failed")
            disk=it.copyOf()
        },records)
        val friend=FriendIdentity(8,"Other",peer,true,deviceNames=mapOf(peer to "Phone"))
        index.reconcile(listOf(friend));assertEquals(1,writes)
        index.reconcile(listOf(friend.copy(online=false)));assertEquals(1,writes)
        val reopened=ProtectedFriendIdentityIndex(19,label,{disk},{
            writes++;disk=it.copyOf()
        },records)
        reopened.reconcile(listOf(friend.copy(online=false)));assertEquals(1,writes)
        index.reconcile(listOf(friend.copy(deviceNames=mapOf(peer to "Renamed phone"))))
        assertEquals(2,writes)
        assertEquals("Renamed phone",index.localHistoryBindings().single().deviceNames!![peer])

        val newDevice="c".repeat(64)
        index.reconcile(listOf(friend.copy(peerId=newDevice,knownPeerIds=listOf(peer,newDevice),deviceNames=mapOf(peer to "Renamed phone"))))
        assertEquals(3,writes)
        failWrite=true
        val renamed=friend.copy(username="New name",deviceNames=mapOf(peer to "Renamed phone"))
        assertThrows(IllegalStateException::class.java){index.reconcile(listOf(renamed))}
        assertEquals(4,writes)
        failWrite=false
        index.reconcile(listOf(renamed));assertEquals(5,writes)
        assertEquals("New name",index.localHistoryBindings().single().username)
    }
}
