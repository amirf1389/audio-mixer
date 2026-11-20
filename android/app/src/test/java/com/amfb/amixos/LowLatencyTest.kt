package com.amfb.amixos

import android.content.Context
import android.content.pm.PackageManager
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

/** LOW LATENCY SOUND DETECTION on the phone: the features and the native buffer read from Android, the AAudio figures in plain lines,
 *  and advice that matches what the phone offers. */
@RunWith(RobolectricTestRunner::class)
class LowLatencyTest {
    private fun hal(low: Boolean = true, pro: Boolean = true, opened: Boolean = true, lowMode: Boolean = true, excl: Boolean = true, burst: Int = 96, rate: Int = 48000, nativeRate: Int = 48000) =
        AudioHal("m", "b", "M", "d", "", "", "15", 35, nativeRate, 192, low, pro, true, true, true, opened, lowMode, excl, burst, rate, burst * 4, true, lowMode, excl, burst, rate)

    @Test fun readsFeaturesAndNativeBufferFromAndroid() {
        val c = ApplicationProvider.getApplicationContext<Context>()
        shadowOf(c.packageManager).setSystemFeature(PackageManager.FEATURE_AUDIO_LOW_LATENCY, true)
        shadowOf(c.packageManager).setSystemFeature(PackageManager.FEATURE_AUDIO_PRO, false)
        val read = AudioHal.read(c, probe = false)   // the AudioManager properties are whatever this Android build reports (no setter here)
        assertTrue(read.lowLatency); assertFalse(read.proAudio)
        val h = read.copy(nativeRate = 48000, nativeFrames = 240)
        assertEquals(5.0, h.nativeBufferMs, 1e-9)
        val lines = h.latencyLines("")
        assertTrue(lines[0], lines[0].contains("FEATURE_AUDIO_LOW_LATENCY: yes") && lines[0].contains("FEATURE_AUDIO_PRO: no"))
        assertTrue(lines[1], lines[1].contains("240 frames") && lines[1].contains("5.0 ms"))
        assertTrue(lines.any { it.contains("probe could not open") })   // no probe: said honestly, not invented
        assertTrue(h.latencyAdvice("").any { it.contains("FEATURE_AUDIO_PRO") })
    }

    @Test fun mmapLowLatencyPhoneGetsNoWarnings() {
        val h = hal(burst = 96)
        val lines = h.latencyLines("aaudio")
        assertTrue(lines.any { it.contains("AAudio (native)") }); assertTrue(lines.any { it.contains("LOW_LATENCY") && it.contains("MMAP exclusive") })
        assertTrue(lines.any { it.contains("Burst: 96 frames (2.0 ms)") })
        val a = h.latencyAdvice("aaudio")
        assertEquals(1, a.size); assertTrue(a[0].contains("Bluetooth headphones add 100-200 ms"))
    }

    @Test fun weakPhoneGetsPlainAdvice() {
        val h = hal(low = false, pro = false, lowMode = false, excl = false, burst = 960, rate = 44100, nativeRate = 48000)
        val a = h.latencyAdvice("java")
        assertTrue(a[0].contains("Java path"))
        assertTrue(a.any { it.contains("does not declare FEATURE_AUDIO_LOW_LATENCY") })
        assertTrue(a.any { it.contains("did not grant LOW_LATENCY") })
        assertTrue(a.any { it.contains("Long burst") })
        assertTrue(a.any { it.contains("resampling") })
        assertTrue(h.latencyLines("java").any { it.contains("NONE") && it.contains("shared") })
    }
}
