package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** 1.27 FADER SOURCES: fader-start / stop, the binding of each strip's source, the permission gates and the INFO header. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class FaderSourcesTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }

    @Test fun faderStartAndStop() {
        assertEquals(FaderSources.Action.START, FaderSources.action(-60.0, -20.0))
        assertEquals(FaderSources.Action.START, FaderSources.action(-120.0, 0.0))
        assertEquals(FaderSources.Action.STOP, FaderSources.action(-3.0, -60.0))
        assertEquals(FaderSources.Action.NONE, FaderSources.action(-10.0, -5.0))
        assertEquals(FaderSources.Action.NONE, FaderSources.action(-60.0, -80.0))
        assertEquals(FaderSources.Action.START, FaderSources.action(Double.NaN, -1.0))
    }
    @Test fun bindingIsKeptPerStrip() {
        val p = androidx.test.core.app.ApplicationProvider.getApplicationContext<android.content.Context>().getSharedPreferences("fs_test", 0)
        p.edit().clear().commit()
        assertEquals(FaderSources.Kind.DECK, FaderSources.bound(p, 101).kind); assertEquals(1, FaderSources.bound(p, 101).id)
        assertEquals(FaderSources.Kind.MIC, FaderSources.bound(p, 2).kind)
        FaderSources.bind(p, 2, FaderSources.Source(FaderSources.Kind.USB, 17, "USB|MIC", 96000, 2))
        val b = FaderSources.bound(p, 2); assertEquals(FaderSources.Kind.USB, b.kind); assertEquals(17, b.id); assertEquals("USB/MIC", b.name); assertEquals(96000, b.rate)
        assertEquals(null, FaderSources.decode("BAD|x")); assertEquals(null, FaderSources.decode("WHAT|1|n|0|0"))
        assertEquals(2, FaderSources.appDeck(102)); assertEquals(-1, FaderSources.appDeck(3))
        assertEquals(listOf(FaderSources.Kind.DECK, FaderSources.Kind.APP), FaderSources.choices(100, emptyList()).map { it.kind })
    }
    @Test fun permissionGates() {
        assertEquals(FaderSources.Gate.OK, FaderSources.gate(FaderSources.Kind.DECK, false, false, 34, true))
        assertEquals(FaderSources.Gate.NEED_MIC, FaderSources.gate(FaderSources.Kind.USB, false, false, 34, true))
        assertEquals(FaderSources.Gate.NEED_MIC, FaderSources.gate(FaderSources.Kind.APP, false, false, 34, true))
        assertEquals(FaderSources.Gate.NEED_CAPTURE, FaderSources.gate(FaderSources.Kind.APP, true, false, 34, true))
        assertEquals(FaderSources.Gate.OK, FaderSources.gate(FaderSources.Kind.APP, true, true, 34, true))
        assertEquals(FaderSources.Gate.UNSUPPORTED, FaderSources.gate(FaderSources.Kind.APP, true, true, 28, true))
        assertEquals(FaderSources.Gate.OFF, FaderSources.gate(FaderSources.Kind.MIC, true, true, 34, false))
        assertEquals(FaderSources.Kind.USB, FaderSources.kindOf(android.media.AudioDeviceInfo.TYPE_USB_HEADSET))
        assertEquals(FaderSources.Kind.BLUETOOTH, FaderSources.kindOf(android.media.AudioDeviceInfo.TYPE_BLUETOOTH_SCO))
    }
    @Test fun infoHeader() {
        val s = FaderSources.Source(FaderSources.Kind.USB, 3, "USB MIC", 48000, 2)
        assertEquals("⎆ USB MIC · 48k/2ch · -12", FaderSources.info(s, null, -12.0))
        assertTrue(FaderSources.info(FaderSources.Source(FaderSources.Kind.DECK, 0, "PLAYER A"), "Song.mp3", -120.0).startsWith("▶ Song.mp3"))
        assertTrue(FaderSources.info(FaderSources.Source(FaderSources.Kind.APP, 0, "OTHER APPS", 44100, 2), null, -3.0).contains("44.1k/2ch"))
    }
    @Test fun theMixerShowsTheHeadersAndAFaderStartsAnInput() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        assertNotNull(find(a.window.decorView, "srcInfo_0")); assertNotNull(find(a.window.decorView, "srcInfo_100"))
        val p = Prefs.local(a); p.edit().putBoolean("mute0", false).commit()
        a.faderStart(0, FaderSources.Action.STOP); assertTrue(p.getBoolean("mute0", false))
        a.faderStart(0, FaderSources.Action.START); assertFalse(p.getBoolean("mute0", true))
        a.faderStart(100, FaderSources.Action.START)   // a deck with nothing loaded: nothing happens, nothing breaks
    }
}
