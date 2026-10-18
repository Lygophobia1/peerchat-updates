package com.peerchat.android.platform

import android.app.Service
import android.content.Intent
import com.peerchat.android.platform.audio.AndroidAudioDevice
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk=[34])
class VoiceServiceTest {
    @Test fun processRestartCannotRecreateMicrophoneConsent(){
        val controller=Robolectric.buildService(PeerChatVoiceService::class.java).create()
        assertEquals(Service.START_NOT_STICKY,controller.get().onStartCommand(null,0,1))
        assertTrue(Shadows.shadowOf(controller.get()).isStoppedBySelf)
        assertEquals("Android audio inactive",AndroidAudioDevice.diagnostics())
        controller.destroy()
    }
    @Test fun forgedStartAndOldNotificationCannotOpenAudio(){
        for(intent in listOf(Intent().setAction("com.peerchat.android.START_EXPLICIT_VOICE").putExtra("voice_consent","bad"),
            Intent().setAction("com.peerchat.android.END_EXPLICIT_VOICE").putExtra("voice_consent","old"),
            Intent().setAction("com.peerchat.android.VOICE_AUDIO_ROUTE").putExtra("speaker",true))){
            val controller=Robolectric.buildService(PeerChatVoiceService::class.java).create()
            assertEquals(Service.START_NOT_STICKY,controller.get().onStartCommand(intent,0,1))
            assertTrue(Shadows.shadowOf(controller.get()).isStoppedBySelf)
            assertEquals("Android audio inactive",AndroidAudioDevice.diagnostics())
            controller.destroy()
        }
    }
}
