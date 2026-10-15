package com.peerchat.android.platform.audio

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import android.media.audiofx.AcousticEchoCanceler
import android.media.audiofx.AudioEffect
import android.media.audiofx.AutomaticGainControl
import android.media.audiofx.NoiseSuppressor
import android.os.Handler
import android.os.Looper
import android.os.Process
import androidx.annotation.Keep

/** PCM device adapter only. No signaling, codec, transport, or call authority. */
@Keep
object AndroidAudioDevice {
    private const val RATE = 16000
    private const val SAMPLES = 320
    private var context: Context? = null
    private var processing = AndroidCallAudio.Processing()
    private var onFailure: (() -> Unit)? = null
    private var session: Session? = null
    private val main = Handler(Looper.getMainLooper())

    @JvmStatic private external fun install(): Boolean
    @JvmStatic private external fun capture(token: Long, pcm: ShortArray)
    @JvmStatic private external fun playout(token: Long, pcm: ShortArray)
    @JvmStatic private external fun rendered(token: Long, pcm: ShortArray, delayMs: Int)
    @JvmStatic private external fun failed(token: Long)

    @Synchronized
    internal fun prepare(app: Context, options: AndroidCallAudio.Processing, failure: () -> Unit): Boolean {
        if (context != null) return false // one foreground call/device owner
        if (app.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return false
        val installed = try { install() } catch (_: UnsatisfiedLinkError) { false }
        if (!installed) return false
        context = app; processing = options; onFailure = failure
        return true
    }

    @Synchronized
    internal fun close() {
        context = null; onFailure = null
        session?.close(); session = null
    }

    @JvmStatic @Synchronized
    fun start(token: Long, inputId: Int, outputId: Int, muted: Boolean): Boolean {
        val app = context ?: return false
        if (token <= 0 || session != null || app.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return false
        val next = Session(token, app, inputId, outputId, processing)
        return try { next.start(muted); session = next; true }
        catch (_: RuntimeException) { next.close(); false }
    }

    @JvmStatic @Synchronized
    fun stop(token: Long) {
        if (session?.token == token) { session?.close(); session = null }
    }

    @JvmStatic @Synchronized
    fun setMuted(token: Long, muted: Boolean): Boolean {
        val current = session?.takeIf { it.token == token } ?: return false
        return try { current.mute(muted); true }
        catch (_: RuntimeException) { current.fail(); false }
    }

    @Synchronized
    fun diagnostics(): String = session?.diagnostics ?: "Android audio inactive"

    private class Session(val token: Long, val app: Context, val inputId: Int, val outputId: Int,
                          val options: AndroidCallAudio.Processing) : AutoCloseable {
        @Volatile private var running = false
        @Volatile private var capturing = false
        private var record: AudioRecord? = null
        private var track: AudioTrack? = null
        private var captureThread: Thread? = null
        private var outputThread: Thread? = null
        private val effects = mutableListOf<AudioEffect>()
        @Volatile var diagnostics = "Android session effects unavailable; WebRTC APM not loaded"
            private set

        fun start(muted: Boolean) {
            val min = AudioTrack.getMinBufferSize(RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT)
            check(min > 0)
            val output = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_VOICE_COMMUNICATION)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SPEECH).build())
                .setAudioFormat(AudioFormat.Builder().setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(maxOf(min, SAMPLES * 2 * 3)).setTransferMode(AudioTrack.MODE_STREAM).build()
            track = output
            check(output.state == AudioTrack.STATE_INITIALIZED)
            if (outputId >= 0) {
                val device = app.getSystemService(AudioManager::class.java).getDevices(AudioManager.GET_DEVICES_OUTPUTS).firstOrNull { it.id == outputId }
                check(device != null && output.setPreferredDevice(device))
            }
            running = true
            // Open capture before starting playback for duplex Bluetooth routes.
            if (!muted) openCapture()
            output.play()
            outputThread = Thread({
                Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
                val pcm = ShortArray(SAMPLES)
                var submitted = 0L
                try {
                    while (running) {
                        playout(token, pcm)
                        var offset = 0
                        while (running && offset < SAMPLES) {
                            val count = output.write(pcm, offset, SAMPLES - offset, AudioTrack.WRITE_BLOCKING)
                            if (count <= 0) error("Android playout stopped")
                            offset += count
                        }
                        if (running && offset == SAMPLES) {
                            submitted += SAMPLES
                            val played = output.playbackHeadPosition.toLong() and 0xffffffffL
                            rendered(token, pcm, ((submitted - played).coerceIn(0, 32000) / 16).toInt())
                        }
                    }
                } catch (_: RuntimeException) { if (running) fail() }
            }, "PeerChat audio output").also { it.start() }
        }

        private fun openCapture() {
            val min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
            check(min > 0)
            if (app.checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                throw SecurityException("Microphone permission is required for capture")
            }
            val input = AudioRecord.Builder().setAudioSource(MediaRecorder.AudioSource.VOICE_COMMUNICATION)
                .setAudioFormat(AudioFormat.Builder().setSampleRate(RATE).setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT).build())
                .setBufferSizeInBytes(maxOf(min, SAMPLES * 2 * 3)).build()
            record = input
            try {
            check(input.state == AudioRecord.STATE_INITIALIZED)
            if (inputId >= 0) {
                val device = app.getSystemService(AudioManager::class.java).getDevices(AudioManager.GET_DEVICES_INPUTS).firstOrNull { it.id == inputId }
                check(device != null && input.setPreferredDevice(device))
            }
            fun effect(create: () -> AudioEffect?, enabled: Boolean): Boolean = try {
                val instance = create()
                if (instance == null) false else { effects += instance; instance.enabled = enabled; enabled && instance.enabled }
            } catch (_: RuntimeException) { false }
            val aec = effect({ if (AcousticEchoCanceler.isAvailable()) AcousticEchoCanceler.create(input.audioSessionId) else null }, options.echoCancellation)
            val ns = effect({ if (NoiseSuppressor.isAvailable()) NoiseSuppressor.create(input.audioSessionId) else null }, options.noiseSuppression)
            val agc = effect({ if (AutomaticGainControl.isAvailable()) AutomaticGainControl.create(input.audioSessionId) else null }, options.automaticGain)
            diagnostics = "Android platform processing: AEC=$aec NS=$ns AGC=$agc; strength presets unsupported; WebRTC APM not loaded"
            capturing = true
            input.startRecording()
            check(input.recordingState == AudioRecord.RECORDSTATE_RECORDING)
            captureThread = Thread({
                Process.setThreadPriority(Process.THREAD_PRIORITY_AUDIO)
                val pcm = ShortArray(SAMPLES)
                try {
                    while (running && capturing) {
                        var offset = 0
                        while (running && capturing && offset < SAMPLES) {
                            val count = input.read(pcm, offset, SAMPLES - offset, AudioRecord.READ_BLOCKING)
                            if (count <= 0) error("Android capture stopped")
                            offset += count
                        }
                        if (running && capturing && offset == SAMPLES) capture(token, pcm)
                    }
                } catch (_: RuntimeException) { if (running && capturing) fail() }
            }, "PeerChat audio input").also { it.start() }
            } catch (failure: RuntimeException) {
                // Revocation and initialization failures also occur on unmute.
                // Release the partially opened device before reporting failure.
                runCatching { closeCapture() }
                throw failure
            }
        }

        private fun closeCapture() {
            capturing = false
            runCatching { record?.stop() }
            try {
                captureThread?.let { if (it !== Thread.currentThread()) it.join(1000) }
            } finally {
                runCatching { record?.release() }; record = null
                effects.forEach { runCatching { it.release() } }; effects.clear()
            }
            // Never start a second recorder if an OEM read has not terminated.
            check(captureThread?.isAlive != true)
            captureThread = null
        }

        fun mute(value: Boolean) {
            if (value) closeCapture() else if (running && !capturing) openCapture()
        }

        fun fail() {
            running = false; capturing = false
            failed(token)
            main.post {
                synchronized(AndroidAudioDevice) {
                    if (session?.token == token) {
                        val callback = onFailure
                        AndroidAudioDevice.close()
                        callback?.invoke()
                    }
                }
            }
        }

        override fun close() {
            running = false
            runCatching { closeCapture() }
            runCatching { track?.pause(); track?.flush(); track?.stop() }
            outputThread?.let { if (it !== Thread.currentThread()) it.join(1000) }
            runCatching { track?.release() }; track = null
            outputThread = null
        }
    }
}
