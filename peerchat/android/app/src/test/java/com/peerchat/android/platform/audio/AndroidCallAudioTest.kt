package com.peerchat.android.platform.audio

import android.Manifest
import android.app.Application
import android.media.AudioManager
import androidx.test.core.app.ApplicationProvider
import org.junit.After
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class AndroidCallAudioTest {
    private val app = ApplicationProvider.getApplicationContext<Application>()
    private val manager = app.getSystemService(AudioManager::class.java)
    @After fun cleanup() { AndroidAudioDevice.close() }

    @Test fun noPermissionCannotAcquireCallAudio() {
        shadowOf(app).denyPermissions(Manifest.permission.RECORD_AUDIO)
        val audio = AndroidCallAudio(app) { fail("No focus callback expected") }
        assertFalse(audio.prepare())
        assertFalse(audio.setSpeaker(true))
        assertEquals(AudioManager.MODE_NORMAL, manager.mode)
        assertFalse(AndroidAudioDevice.start(1, -1, -1, false))
        audio.close()
    }

    @Test fun missingNativeAdapterRestoresModeAndKeepsMicrophoneClosed() {
        shadowOf(app).grantPermissions(Manifest.permission.RECORD_AUDIO)
        manager.mode = AudioManager.MODE_NORMAL
        val audio = AndroidCallAudio(app) {}
        assertFalse(audio.prepare()) // JVM has no native media adapter.
        assertEquals(AudioManager.MODE_NORMAL, manager.mode)
        assertFalse(AndroidAudioDevice.start(1, -1, -1, false))
        assertFalse(AndroidAudioDevice.setMuted(1, false))
        assertEquals("Android audio inactive", AndroidAudioDevice.diagnostics())
        audio.close(); audio.close()
    }

    @Test fun focusDeniedNeverPreparesDevice() {
        shadowOf(app).grantPermissions(Manifest.permission.RECORD_AUDIO)
        shadowOf(manager).setNextFocusRequestResponse(AudioManager.AUDIOFOCUS_REQUEST_FAILED)
        val audio = AndroidCallAudio(app) {}
        assertFalse(audio.prepare())
        assertEquals(AudioManager.MODE_NORMAL, manager.mode)
        assertFalse(AndroidAudioDevice.start(1, -1, -1, false))
    }

    @Test fun staleTokensCannotOpenOrUnmuteDeviceAfterClose() {
        AndroidAudioDevice.close()
        for (token in listOf(-1L, 0L, 1L, Long.MAX_VALUE)) {
            assertFalse(AndroidAudioDevice.start(token, -1, -1, false))
            assertFalse(AndroidAudioDevice.setMuted(token, false))
            AndroidAudioDevice.stop(token)
        }
    }
}
