package com.peerchat.android
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeBridgeTest {
    @Test fun lifecycleAndStandardUtf8() {
        assertTrue(NativeBridge.prove().contains("Passed (100 cycles)"))
        assertNull(NativeBridge.info(0))
        assertEquals(2, NativeBridge.destroy(0))
        val handle = NativeBridge.create()
        try {
            val bytes = ByteArray(65536) { 'x'.code.toByte() }
            assertArrayEquals(bytes, NativeBridge.exchange(handle, bytes))
            assertNull(NativeBridge.exchange(handle, ByteArray(65537)))
        } finally { assertEquals(0, NativeBridge.destroy(handle)) }
    }
}
