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
class ServerConfigurationStoreTest {
    // Test-only JVM AES key; production uses the unchanged Keystore PCR1 provider.
    private val records=object:RecordProtector {
        val key=SecretKeySpec(ByteArray(32){it.toByte()},"AES")
        override fun protect(plain:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding");cipher.init(Cipher.ENCRYPT_MODE,key)
            cipher.updateAAD(context);return cipher.iv+cipher.doFinal(plain)
        }
        override fun unprotect(blob:ByteArray,context:ByteArray):ByteArray {
            val cipher=Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.DECRYPT_MODE,key,GCMParameterSpec(128,blob.copyOfRange(0,12)))
            cipher.updateAAD(context);return cipher.doFinal(blob.copyOfRange(12,blob.size))
        }
    }
    @Test fun protectedConfigurationReopensAndTamperingNeverDropsPin() {
        var disk:ByteArray?=null
        fun open()=ProtectedServerConfigurationStore({disk},{disk=it.copyOf()},{records})
        assertNull(open().load())
        val expected=ServerEndpoint("server.example",8443,"a".repeat(64))
        open().save(expected);assertEquals(expected,open().load())
        assertFalse(disk!!.toString(Charsets.UTF_8).contains("server.example"))
        val original=disk!!.copyOf()
        disk=original.copyOf().also {it[it.lastIndex]=(it.last().toInt() xor 1).toByte()}
        assertThrows(Exception::class.java){open().load()}
        disk=original.copyOf(12);assertThrows(Exception::class.java){open().load()}
        disk=original;assertEquals(expected,open().load())
        assertThrows(IllegalArgumentException::class.java){open().save(ServerEndpoint("server.example",8443,"trust-all"))}
        assertEquals(expected,open().load())
    }
}
