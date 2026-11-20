package com.amfb.amixos

import android.content.Context
import android.content.pm.PackageManager
import android.media.AudioManager
import android.os.Build

/** AUDIO HAL REPORT: what this phone's audio hardware layer offers, read only through Android's public APIs (AudioManager properties,
 *  PackageManager features, Build, and a short AAudio probe that opens and closes a stream without playing anything). No hidden API,
 *  no root, no extra permission. Used by the Devices screen, the brand profile and the AmF X1 SDK. */
data class AudioHal(
    val manufacturer: String, val brand: String, val model: String, val device: String, val socMaker: String, val socModel: String, val android: String, val sdk: Int,
    val nativeRate: Int, val nativeFrames: Int,                                  // the HAL's own output rate and buffer: run at these for the least work
    val lowLatency: Boolean, val proAudio: Boolean, val midi: Boolean, val usbHost: Boolean, val unprocessedMic: Boolean,
    val outOpened: Boolean, val outLowLatency: Boolean, val outExclusive: Boolean, val outBurst: Int, val outRate: Int, val outCapacity: Int,
    val inOpened: Boolean, val inLowLatency: Boolean, val inExclusive: Boolean, val inBurst: Int, val inRate: Int,
) {
    /** the output burst in milliseconds (the HAL's own period) */
    val burstMs: Double get() = if (outRate > 0 && outBurst > 0) outBurst * 1000.0 / outRate else 0.0
    /** one line per finding, for screens and reports */
    fun lines(): List<String> = listOf(
        "Device: $manufacturer $model ($device) · Android $android (API $sdk)" + (if (socModel.isNotEmpty()) " · SoC $socMaker $socModel" else ""),
        "Native output: $nativeRate Hz, $nativeFrames frames per buffer",
        "Low-latency audio: " + yes(lowLatency) + " · Pro audio: " + yes(proAudio) + " · MIDI: " + yes(midi) + " · USB host (USB interfaces): " + yes(usbHost),
        "Unprocessed microphone: " + yes(unprocessedMic),
        "AAudio output: " + if (!outOpened) "could not open" else (if (outLowLatency) "LOW LATENCY" else "normal") + " · " + (if (outExclusive) "EXCLUSIVE (MMAP)" else "shared") + " · burst $outBurst frames (" + String.format("%.1f", burstMs) + " ms) at $outRate Hz",
        "AAudio input: " + if (!inOpened) "could not open (microphone permission?)" else (if (inLowLatency) "LOW LATENCY" else "normal") + " · " + (if (inExclusive) "EXCLUSIVE (MMAP)" else "shared") + " · burst $inBurst frames at $inRate Hz")
    private fun yes(b: Boolean) = if (b) "yes" else "no"

    /** LOW LATENCY: the native buffer in milliseconds (PROPERTY_OUTPUT_FRAMES_PER_BUFFER at PROPERTY_OUTPUT_SAMPLE_RATE) */
    val nativeBufferMs: Double get() = if (nativeRate > 0 && nativeFrames > 0) nativeFrames * 1000.0 / nativeRate else 0.0
    /** LOW LATENCY: what this phone offers and what the mixer got. The MMAP / exclusive and performance-mode figures come from the short
     *  AAudio probe (a stream opened the same way as the mixer's own); the live mixer itself only reports which path it runs on
     *  ("aaudio" or "java"; "" = stopped). */
    fun latencyLines(loopPath: String): List<String> = listOf(
        "FEATURE_AUDIO_LOW_LATENCY: " + yes(lowLatency) + " · FEATURE_AUDIO_PRO: " + yes(proAudio),
        "Native sample rate $nativeRate Hz · $nativeFrames frames per buffer (" + String.format("%.1f", nativeBufferMs) + " ms)",
        "Mixer path: " + when (loopPath) { "aaudio" -> "AAudio (native)"; "java" -> "AudioTrack / AudioRecord (Java fallback)"; else -> "stopped" },
        "AAudio performance mode: " + (if (!outOpened) "probe could not open" else if (outLowLatency) "LOW_LATENCY" else "NONE (the system refused low latency)") +
            " · " + (if (outExclusive) "MMAP exclusive" else "shared (legacy path)") + " (from the probe stream)",
        "Burst: $outBurst frames (" + String.format("%.1f", burstMs) + " ms)")
    /** plain advice, the biggest gain first */
    fun latencyAdvice(loopPath: String): List<String> {
        val a = mutableListOf<String>()
        if (loopPath == "java") a += "The mixer fell back to the Java path (AudioTrack / AudioRecord): it adds latency. Restart the mixer; if it stays, this phone's AAudio is unavailable."
        if (!lowLatency) a += "This phone does not declare FEATURE_AUDIO_LOW_LATENCY: expect 40 ms or more. Wired headphones still help; a USB audio interface may go lower."
        else if (!proAudio) a += "Low latency is declared but not FEATURE_AUDIO_PRO: fine for playback and light monitoring, not guaranteed under 20 ms round trip."
        if (outOpened && !outLowLatency) a += "AAudio did not grant LOW_LATENCY: close other audio apps and turn off sound effects / equalizers in the phone's settings."
        if (outOpened && outLowLatency && !outExclusive) a += "The stream is shared, not MMAP exclusive: this phone adds a mixer stage. Nothing to change in the app."
        if (burstMs > 10) a += "Long burst (" + String.format("%.1f", burstMs) + " ms): the phone's audio period is slow; raise the buffer only if you hear clicks."
        if (nativeRate > 0 && outRate > 0 && nativeRate != outRate) a += "The mixer runs at $outRate Hz but the phone's native rate is $nativeRate Hz: resampling adds latency."
        a += "Bluetooth headphones add 100-200 ms: use wired headphones or a USB interface to monitor yourself live."
        return a
    }

    companion object {
        fun read(c: Context, probe: Boolean = true): AudioHal {
            val am = c.getSystemService(Context.AUDIO_SERVICE) as AudioManager; val pm = c.packageManager
            val p = IntArray(16); if (probe) runCatching { Engine.aaudioProbe(p) }
            val socMaker = if (Build.VERSION.SDK_INT >= 31) Build.SOC_MANUFACTURER else ""; val socModel = if (Build.VERSION.SDK_INT >= 31) Build.SOC_MODEL else ""
            return AudioHal(Build.MANUFACTURER ?: "", Build.BRAND ?: "", Build.MODEL ?: "", Build.DEVICE ?: "", socMaker.takeIf { it != Build.UNKNOWN } ?: "", socModel.takeIf { it != Build.UNKNOWN } ?: "",
                Build.VERSION.RELEASE ?: "", Build.VERSION.SDK_INT,
                am.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)?.toIntOrNull() ?: 0, am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)?.toIntOrNull() ?: 0,
                pm.hasSystemFeature(PackageManager.FEATURE_AUDIO_LOW_LATENCY), pm.hasSystemFeature(PackageManager.FEATURE_AUDIO_PRO), pm.hasSystemFeature(PackageManager.FEATURE_MIDI),
                pm.hasSystemFeature(PackageManager.FEATURE_USB_HOST), am.getProperty(AudioManager.PROPERTY_SUPPORT_AUDIO_SOURCE_UNPROCESSED) == "true",
                p[0] == 1, p[1] == 12, p[2] == 0, p[3], p[4], p[6], p[8] == 1, p[9] == 12, p[10] == 0, p[11], p[12])
        }
    }
}
