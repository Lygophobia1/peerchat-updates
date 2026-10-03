package com.peerchat.android.platform

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
class RememberedLoginStoreTest {
    private val records=object:RecordProtector {
        private val key=SecretKeySpec(ByteArray(32){it.toByte()},"AES") // JVM fixture only.
        override fun protect(plain:ByteArray,context:ByteArray):ByteArray {
            val c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.ENCRYPT_MODE,key);c.updateAAD(context);return c.iv+c.doFinal(plain)
        }
        override fun unprotect(blob:ByteArray,context:ByteArray):ByteArray {
            val c=Cipher.getInstance("AES/GCM/NoPadding");c.init(Cipher.DECRYPT_MODE,key,GCMParameterSpec(128,blob.copyOfRange(0,12)));c.updateAAD(context);return c.doFinal(blob.copyOfRange(12,blob.size))
        }
    }
    @Test fun credentialReopensRevocationPersistsAndClearCannotResurrect() {
        var disk:ByteArray?=null
        fun open()=ProtectedRememberedLoginStore({disk},{disk=it.copyOf()},{records})
        assertNull(open().load())
        val value=RememberedLogin(ServerEndpoint("example.test",7443,"a".repeat(64)),8,"b".repeat(64),"c".repeat(43))
        open().save(value)
        repeat(100){val loaded=open().load()!!;assertEquals(8,loaded.accountId);assertEquals(value.credential,loaded.credential);assertEquals(value.endpoint,loaded.endpoint);assertFalse(loaded.revocationPending)}
        assertFalse(disk!!.toString(Charsets.UTF_8).contains(value.credential))
        assertFalse(value.toString().contains(value.credential))
        open().save(value.pendingRevocation());assertTrue(open().load()!!.revocationPending)
        open().clear();assertNull(open().load());assertNull(open().load())
    }
    @Test fun tamperTruncationWrongContextAndUnpinnedServerFailClosed() {
        var disk:ByteArray?=null
        fun open()=ProtectedRememberedLoginStore({disk},{disk=it.copyOf()},{records})
        val value=RememberedLogin(ServerEndpoint("example.test",7443,"a".repeat(64)),8,"b".repeat(64),"c".repeat(43))
        open().save(value);val original=disk!!.copyOf()
        disk=original.copyOf().also{it[it.lastIndex]=(it.last().toInt() xor 1).toByte()}
        assertThrows(Exception::class.java){open().load()}
        disk=original.copyOf(12);assertThrows(Exception::class.java){open().load()}
        disk=records.protect("{}".toByteArray(),"server-configuration-v1".toByteArray())
        assertThrows(Exception::class.java){open().load()}
        disk=original
        assertThrows(IllegalArgumentException::class.java){open().save(RememberedLogin(ServerEndpoint("example.test",7443),8,value.device,value.credential))}
        assertEquals(value.credential,open().load()!!.credential)
    }
}
