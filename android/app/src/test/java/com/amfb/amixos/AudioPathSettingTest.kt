package com.amfb.amixos

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** AUDIO PATH in the phone mixer's SETUP: stored in Prefs (AUTO by default), a key per choice, and the LOW LATENCY report names the path. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class AudioPathSettingTest {
    private val ctx: Context = ApplicationProvider.getApplicationContext()

    @Test fun theSettingIsStored() {
        assertEquals(AudioPathChoice.AUTO, Prefs.localAudioPath(ctx))
        Prefs.setLocalAudioPath(ctx, AudioPathChoice.OPENSL); assertEquals(AudioPathChoice.OPENSL, Prefs.localAudioPath(ctx))
        Prefs.setLocalAudioPath(ctx, AudioPathChoice.JAVA); assertEquals(AudioPathChoice.JAVA, Prefs.localAudioPath(ctx))
    }
    @Test fun setupHasAKeyPerPath() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        fun find(v: android.view.View, t: String): android.view.View? { if (v.contentDescription == t) return v; if (v is android.view.ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
        for (c in AudioPathChoice.entries) assertNotNull(c.name, find(a.window.decorView, "audioPath_" + c.name))
        find(a.window.decorView, "audioPath_OPENSL")!!.performClick()
        assertEquals(AudioPathChoice.OPENSL, Prefs.localAudioPath(ctx))
    }
    @Test fun theLowLatencyReportShowsThePath() {
        val h = AudioHal("G", "g", "P", "p", "", "", "14", 34, 48000, 192, true, false, false, true, true, true, true, false, 96, 48000, 1920, true, true, false, 96, 48000)
        assertTrue(h.latencyLines("opensl").any { it == "Mixer path: OpenSL ES (native)" })
        assertTrue(h.latencyLines("aaudio").any { it == "Mixer path: AAudio (native)" })
        assertTrue(h.latencyAdvice("opensl").any { it.contains("OpenSL ES") })
    }
}
