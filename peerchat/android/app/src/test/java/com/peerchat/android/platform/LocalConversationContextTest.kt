package com.peerchat.android.platform

import com.peerchat.android.application.FriendIdentity
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
class LocalConversationContextTest {
    private val endpoint=ServerEndpoint("server.example",7443,"f".repeat(64))
    private val records=object:RecordProtector {
        val key=SecretKeySpec(ByteArray(32){it.toByte()},"AES")
        override fun protect(plain:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.ENCRYPT_MODE,key)
            cipher.updateAAD(context);return cipher.iv+cipher.doFinal(plain)
        }
        override fun unprotect(blob:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.DECRYPT_MODE,key,GCMParameterSpec(128,blob.copyOfRange(0,12)))
            cipher.updateAAD(context);return cipher.doFinal(blob.copyOfRange(12,blob.size))
        }
    }
    @Test fun reopenWithoutNetworkRetainsAccountDeviceButNeverClaimsOnline() {
        var disk:ByteArray?=null
        fun open()=ProtectedLocalConversationContextStore({disk},{disk=it.copyOf()},records)
        assertNull(open().load(endpoint))
        val value=LocalConversationContext(19,"Jonah","a".repeat(64),listOf(FriendIdentity(8,"Other","b".repeat(64),true,deviceNames=mapOf("b".repeat(64) to "Office PC"))))
        open().save(endpoint,value)
        repeat(100){val reopened=open().load(endpoint)!!
            assertEquals(19L,reopened.account);assertEquals(value.device,reopened.device)
            assertEquals(8L,reopened.friends.single().accountId);assertFalse(reopened.friends.single().online)
            assertEquals("Office PC",reopened.friends.single().deviceNames!!["b".repeat(64)])}
        assertFalse(disk!!.toString(Charsets.UTF_8).contains("Jonah"))
        assertNull(open().load(endpoint.copy(certificatePin="e".repeat(64))))
        assertNull(open().load(endpoint.copy(host="another.example")))
    }
    @Test fun corruptCacheFailsClosedWithoutDroppingServerPinOrReplacingIdentity() {
        var disk:ByteArray?=null
        fun open()=ProtectedLocalConversationContextStore({disk},{disk=it.copyOf()},records)
        open().save(endpoint,LocalConversationContext(19,"Jonah","a".repeat(64),emptyList()))
        val original=disk!!.copyOf()
        disk=original.copyOf().also{it[it.lastIndex]=(it.last().toInt() xor 1).toByte()}
        assertThrows(Exception::class.java){open().load(endpoint)}
        disk=original.copyOf(10);assertThrows(Exception::class.java){open().load(endpoint)}
        disk=original;assertEquals("a".repeat(64),open().load(endpoint)!!.device)
        assertThrows(Exception::class.java){open().save(endpoint,LocalConversationContext(0,"Other","a".repeat(64),emptyList()))}
    }

    @Test fun saveSkipsPresenceOnlyRefreshesAndRetriesFailedWrites(){
        var disk:ByteArray?=null;var writes=0;var failWrite=false
        val store=ProtectedLocalConversationContextStore({disk},{
            writes++
            if(failWrite)throw IllegalStateException("write failed")
            disk=it.copyOf()
        },records)
        val peer="b".repeat(64)
        val friend=FriendIdentity(8,"Other",peer,true,deviceNames=mapOf(peer to "Phone"))
        val original=LocalConversationContext(19,"Jonah","a".repeat(64),listOf(friend))
        store.save(endpoint,original);assertEquals(1,writes)
        store.save(endpoint,original.copy(friends=listOf(friend.copy(online=false))))
        assertEquals(1,writes)
        val reopened=ProtectedLocalConversationContextStore({disk},{
            writes++;disk=it.copyOf()
        },records)
        assertNotNull(reopened.load(endpoint))
        reopened.save(endpoint,original.copy(friends=listOf(friend.copy(online=false))))
        assertEquals(1,writes)

        val renamed=original.copy(device="c".repeat(64),friends=listOf(friend.copy(deviceNames=mapOf(peer to "Renamed phone"))))
        store.save(endpoint,renamed);assertEquals(2,writes)
        failWrite=true
        val changed=renamed.copy(username="New name")
        assertThrows(IllegalStateException::class.java){store.save(endpoint,changed)}
        assertEquals(3,writes)
        failWrite=false
        store.save(endpoint,changed);assertEquals(4,writes)
        assertEquals("New name",store.load(endpoint)!!.username)
    }
}
