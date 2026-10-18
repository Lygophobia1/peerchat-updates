package com.peerchat.android.platform.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioDeviceInfo
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Build
import android.os.Handler
import android.os.Looper

/** Foreground-service call owner. prepare follows explicit Start/Answer and a
 * RECORD_AUDIO grant. Activity stop does not own or terminate an active call. */
class AndroidCallAudio(private val context: Context, private val onFocusLost: () -> Unit) : AutoCloseable {
    data class Processing(val echoCancellation: Boolean = false, val noiseSuppression: Boolean = true, val automaticGain: Boolean = true)
    private val manager = context.getSystemService(AudioManager::class.java)
    private var focus: AudioFocusRequest? = null
    private var previousMode = AudioManager.MODE_NORMAL
    private var previousSpeaker = false
    private var active = false

    fun prepare(processing: Processing = Processing()): Boolean {
        if (active) return true
        if (context.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return false
        val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
            .setAcceptsDelayedFocusGain(false)
            .setOnAudioFocusChangeListener({ change ->
                if (change != AudioManager.AUDIOFOCUS_GAIN && active) {
                    close()
                    onFocusLost()
                }
            }, Handler(Looper.getMainLooper())).build()
        if (manager.requestAudioFocus(request) != AudioManager.AUDIOFOCUS_REQUEST_GRANTED) return false
        focus = request
        previousMode = manager.mode
        @Suppress("DEPRECATION")
        previousSpeaker = manager.isSpeakerphoneOn
        active = true
        return try {
            manager.mode = AudioManager.MODE_IN_COMMUNICATION
            if (!AndroidAudioDevice.prepare(context.applicationContext, processing) {
                    close(); onFocusLost()
                }) { close(); false } else true
        } catch (_: RuntimeException) { close(); false }
    }

    /** Android 12+ communication routing includes connected wired/Bluetooth devices.
     * Earlier releases use the OS default communication route and speaker toggle. */
    fun setSpeaker(enabled: Boolean): Boolean {
        if (!active) return false
        return try {
            if (Build.VERSION.SDK_INT >= 31) {
                val type = if (enabled) AudioDeviceInfo.TYPE_BUILTIN_SPEAKER else AudioDeviceInfo.TYPE_BUILTIN_EARPIECE
                val device = manager.availableCommunicationDevices.firstOrNull { it.type == type }
                if (device == null) { manager.clearCommunicationDevice(); !enabled }
                else manager.setCommunicationDevice(device)
            } else {
                @Suppress("DEPRECATION")
                manager.isSpeakerphoneOn = enabled
                true
            }
        } catch (_: RuntimeException) { false }
    }

    override fun close() {
        if (!active) return
        active = false
        AndroidAudioDevice.close()
        runCatching {
            if (Build.VERSION.SDK_INT >= 31) manager.clearCommunicationDevice()
            else {
                @Suppress("DEPRECATION")
                manager.isSpeakerphoneOn = previousSpeaker
            }
            manager.mode = previousMode
        }
        focus?.let { manager.abandonAudioFocusRequest(it) }
        focus = null
    }
}
