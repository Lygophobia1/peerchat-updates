package com.peerchat.android
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class NativeBridgeTest {
    @Test fun lifecycleAndStandardUtf8() {
        // 1,000 real JNI/native lifetimes on ART, including stale handles.
        repeat(10) { assertTrue(NativeBridge.prove().contains("Passed (100 cycles)")) }
        assertNull(NativeBridge.info(0))
        assertEquals(2, NativeBridge.destroy(0))
        assertNull(NativeBridge.info(-1))
        assertEquals(2, NativeBridge.destroy(-1))
        val handle = NativeBridge.create()
        try {
            val bytes = ByteArray(65536) { 'x'.code.toByte() }
            assertArrayEquals(bytes, NativeBridge.exchange(handle, bytes))
            assertNull(NativeBridge.exchange(handle, ByteArray(65537)))
            assertArrayEquals(byteArrayOf(), NativeBridge.exchange(handle, byteArrayOf()))
            for (invalid in listOf(
                byteArrayOf(0xc0.toByte(), 0x80.toByte()),
                byteArrayOf(0xe2.toByte(), 0x82.toByte()),
                byteArrayOf(0xed.toByte(), 0xa0.toByte(), 0x80.toByte()),
                byteArrayOf(0xf4.toByte(), 0x90.toByte(), 0x80.toByte(), 0x80.toByte())
            )) assertNull(NativeBridge.exchange(handle, invalid))
        } finally { assertEquals(0, NativeBridge.destroy(handle)) }
    }
}
