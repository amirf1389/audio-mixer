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
