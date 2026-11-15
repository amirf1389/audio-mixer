package com.amfb.amixos

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.AudioTrack
import android.media.MediaRecorder
import kotlin.concurrent.thread

/** Microphone -> strip 0 of the engine -> speaker / headphones, in 256-frame blocks on a worker thread. Needs RECORD_AUDIO. */
class AudioLoop(private val engine: Engine, private val sampleRate: Int = 48000, private val frames: Int = 256, private val outDevice: Int = 0, private val inDevice: Int = 0, private val bursts: Int = 2) {
    @Volatile private var running = false
    private var worker: Thread? = null
    @Volatile var lastPeaks: FloatArray = FloatArray(engine.strips + 2)
        private set

    /** "aaudio" (native, low latency) or "java" (AudioRecord / AudioTrack fallback); "" = stopped */
    @Volatile var path: String = ""
        private set
    private var watcher: Thread? = null

    fun start() {
        if (running) return
        running = true
        // the NDK's recommended path first: AAudio in native code. A device change (headphones in / out) disconnects the stream: the watcher reopens it
        if (engine.startNative(sampleRate, true, outDevice, inDevice, bursts) > 0) {
            path = "aaudio"
            watcher = thread(name = "amix-aaudio-watch") {
                while (running) {
                    lastPeaks = engine.peaks()
                    if (engine.nativeState()[1] == 1) { Engine.aaudioStop(); if (running && engine.startNative(sampleRate, true, outDevice, inDevice, bursts) <= 0) { running = false; path = "" } }
                    Thread.sleep(33)
                }
            }
            return
        }
        path = "java"
        worker = thread(name = "amix-audio", priority = Thread.MAX_PRIORITY) {
            val inMin = AudioRecord.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT)
            val outMin = AudioTrack.getMinBufferSize(sampleRate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_FLOAT)
            val rec = try { AudioRecord(MediaRecorder.AudioSource.UNPROCESSED, sampleRate, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_FLOAT, maxOf(inMin, frames * 8)) }
                catch (e: SecurityException) { running = false; return@thread }   // the microphone permission was taken back: nothing to run
            val track = AudioTrack.Builder()
                .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(AudioFormat.Builder().setEncoding(AudioFormat.ENCODING_PCM_FLOAT).setSampleRate(sampleRate).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                .setBufferSizeInBytes(maxOf(outMin, frames * 16)).setPerformanceMode(AudioTrack.PERFORMANCE_MODE_LOW_LATENCY).build()
            val mono = FloatArray(frames)
            val inputs = arrayOfNulls<FloatArray>(engine.strips).also { it[0] = mono }
            try {
                rec.startRecording(); track.play()
                while (running) {
                    val got = rec.read(mono, 0, frames, AudioRecord.READ_BLOCKING)
                    if (got <= 0) continue
                    val stereo = engine.process(inputs, got)
                    track.write(stereo, 0, stereo.size, AudioTrack.WRITE_BLOCKING)
                    lastPeaks = engine.peaks()
                }
            } finally {
                runCatching { rec.stop() }; rec.release(); runCatching { track.stop() }; track.release()
            }
        }
    }

    fun stop() { running = false; if (path == "aaudio") Engine.aaudioStop(); watcher?.join(1000); watcher = null; worker?.join(1000); worker = null; path = "" }
}
