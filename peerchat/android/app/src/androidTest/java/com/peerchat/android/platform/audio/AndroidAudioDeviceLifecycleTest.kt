package com.peerchat.android.platform.audio

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.peerchat.android.MainActivity
import com.peerchat.android.VoiceBridge
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.util.concurrent.atomic.AtomicBoolean

/** Real emulator audio device lifecycle. No PCM is saved or inspected. */
@RunWith(AndroidJUnit4::class)
class AndroidAudioDeviceLifecycleTest {
    private val instrumentation get()=InstrumentationRegistry.getInstrumentation()
    private val app get()=instrumentation.targetContext

    private fun qemuProperty():String = ParcelFileDescriptor.AutoCloseInputStream(
        instrumentation.uiAutomation.executeShellCommand("getprop ro.kernel.qemu")
    ).bufferedReader().use { it.readText().trim() }

    private fun session():Any? = AndroidAudioDevice::class.java.getDeclaredField("session")
        .apply { isAccessible=true }.get(null)

    private fun deviceField(name:String):Any? = session()?.let { current ->
        current.javaClass.getDeclaredField(name).apply { isAccessible=true }.get(current)
    }

    @Test(timeout=30_000) fun mutedPlaybackCaptureTransitionsAndStaleTokenIsolation() {
        // These checks precede the permission grant or device use.
        assumeTrue("isolated voice integration package required",app.packageName=="com.peerchat.android.voiceintegration")
        assumeTrue("Android emulator required",qemuProperty()=="1")
        val manager=app.getSystemService(AudioManager::class.java)
        val originalMode=manager.mode
        val hadPermission=app.checkSelfPermission(Manifest.permission.RECORD_AUDIO)==PackageManager.PERMISSION_GRANTED
        if(!hadPermission)instrumentation.uiAutomation.grantRuntimePermission(app.packageName,Manifest.permission.RECORD_AUDIO)
        var activity:Activity?=null
        var audio:AndroidCallAudio?=null
        val focusLost=AtomicBoolean(false)
        try {
            activity=instrumentation.startActivitySync(Intent(app,MainActivity::class.java)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            instrumentation.waitForIdleSync()
            // Loads the JNI adapter. Unregistered test tokens receive silent
            // playout and discard capture inside native code.
            VoiceBridge
            val callAudio=AndroidCallAudio(app) { focusLost.set(true) }
            audio=callAudio
            assertTrue("Call audio must acquire focus and the native adapter",callAudio.prepare())
            assertEquals(AudioManager.MODE_IN_COMMUNICATION,manager.mode)

            val first=101L
            assertTrue(AndroidAudioDevice.start(first,-1,-1,true))
            assertNull("Muted start must not open a microphone",deviceField("record"))
            val playback=checkNotNull(deviceField("track") as? AudioTrack)
            assertEquals(AudioTrack.PLAYSTATE_PLAYING,playback.playState)
            val deadline=System.nanoTime()+2_000_000_000L
            while(playback.playbackHeadPosition==0&&System.nanoTime()<deadline)Thread.sleep(20)
            assertTrue("AudioTrack must advance while capture is muted",playback.playbackHeadPosition>0)

            assertTrue(AndroidAudioDevice.setMuted(first,false))
            val recorder=checkNotNull(deviceField("record") as? AudioRecord)
            assertEquals(AudioRecord.RECORDSTATE_RECORDING,recorder.recordingState)
            assertTrue(AndroidAudioDevice.setMuted(first,true))
            assertNull("Muting must tear down AudioRecord",deviceField("record"))
            assertEquals(AudioTrack.PLAYSTATE_PLAYING,(deviceField("track") as AudioTrack).playState)

            AndroidAudioDevice.stop(first)
            assertNull(session())
            val successor=102L
            assertTrue(AndroidAudioDevice.start(successor,-1,-1,true))
            AndroidAudioDevice.stop(first)
            assertFalse(AndroidAudioDevice.setMuted(first,false))
            assertEquals(AudioTrack.PLAYSTATE_PLAYING,(deviceField("track") as AudioTrack).playState)
            AndroidAudioDevice.stop(successor)
            assertNull(session())
            assertFalse("Unexpected audio focus loss",focusLost.get())
        } finally {
            audio?.close()
            activity?.let{opened->opened.runOnUiThread{opened.finish()}}
            instrumentation.waitForIdleSync()
            // The runner restores the isolated package's permission after this
            // process exits: revoking here kills Android instrumentation before
            // JUnit can publish its result. Never run against the real package.
            assertEquals("Android audio inactive",AndroidAudioDevice.diagnostics())
            assertEquals(originalMode,manager.mode)
        }
    }
}
