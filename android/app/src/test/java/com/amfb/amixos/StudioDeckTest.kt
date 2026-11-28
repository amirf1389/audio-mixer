package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** 1.27 STUDIO section of the phone mixer: the five strips, the send / return / insert mapping, the address bar and the status line. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class StudioDeckTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }

    @Test fun mapping() {
        assertEquals(listOf("MIC 1", "MIC 2", "BROWSER", "PLAYER", "MASTER"), StudioMap.strips.map { it.label })
        assertTrue(StudioMap.strips.all { it.inserts.size == 3 })
        assertEquals(listOf("isoeq", "multiband", "subsynth"), StudioMap.strips.last().inserts)
        assertEquals(listOf(5, 6, 7, 8), (0..3).map { StudioMap.sendSlot(it) }); assertEquals(8, StudioMap.sendSlot(9))
        assertTrue(StudioMap.sendSlot(3) < 9)   // inside AMIX_MAX_INSERTS (sdk/c/include/amix.h)
        assertEquals(0.25, StudioMap.wet(0.5, 0.5), 1e-9); assertEquals(1.0, StudioMap.wet(3.0, 2.0), 1e-9); assertEquals(0.0, StudioMap.wet(-1.0, 1.0), 1e-9)
        assertEquals(0.5, StudioMap.amount(StudioMap.knob(0.5)), 1e-6); assertEquals(0.0, StudioMap.amount(-1f), 1e-9)
        assertTrue(StudioMap.isDuck("duck")); assertEquals(listOf("delay", "reverb", "pitch", "chorus"), StudioMap.SENDS.map { it.first })
        assertEquals(0.0, StudioMap.pageVolume(-60.0), 0.0); assertEquals(1.0, StudioMap.pageVolume(3.0), 0.0); assertEquals(0.5, StudioMap.pageVolume(-6.0206), 1e-3)
    }
    @Test fun addressBar() {
        assertEquals("https://example.org/a", StudioMap.target("example.org/a", ""))
        assertEquals("http://x.test", StudioMap.target("http://x.test", ""))
        assertNull(StudioMap.target("two words", ""))   // no search page set yet: the user is asked for one
        assertEquals("https://s.test/?q=two+words", StudioMap.target("two words", "https://s.test/?q="))
        assertNull(StudioMap.target("   ", "https://s.test/?q=")); assertTrue(StudioMap.allowed("https://a.test")); assertTrue(!StudioMap.allowed("intent://x")); assertTrue(!StudioMap.allowed("file:///sdcard"))
    }
    @Test fun dragHandleAndStatus() {
        assertEquals(0.0, StudioMap.panelShare(0.1, -100f, 1000), 0.0)       // dragged under 8 %: folded
        assertEquals(0.6, StudioMap.panelShare(0.4, 900f, 1000), 0.0)
        assertEquals(0.5, StudioMap.panelShare(0.4, 100f, 1000), 1e-6)
        assertEquals("48.0 kHz · 2 IN · USB · ≈5.3 ms · 1 XRUN · CPU 12%", StudioMap.status(48000, 2, true, 192, 64, 1, 12.0))
    }
    @Test fun theSectionShowsFiveStrips() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get(); val root = a.window.decorView
        find(root, "sec_studio")!!.performClick()
        val sec = find(root, "secStudio")!!; assertEquals(View.VISIBLE, sec.visibility)
        for (id in listOf("mic1", "mic2", "browser", "player", "master")) assertNotNull(id, find(root, "studio_$id"))
        assertNotNull(find(root, "knob_mic1_reverb")); assertNotNull(find(root, "knob_master_chorus")); assertNull(find(root, "knob_browser_reverb"))
        assertNotNull(find(root, "insert_master_2")); assertNotNull(find(root, "studioStatus")); assertNotNull(find(root, "studioAddress"))
        find(root, "sec_faders")!!.performClick(); assertEquals(View.GONE, sec.visibility)
    }
    @Test fun theSlimBarLeavesTheFadersTheHeight() {   // 1.27: the bottom bar is about 52 dp upright (was about 80), the web panel starts folded
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().visible().get(); val root = a.window.decorView; val dm = a.resources.displayMetrics
        root.measure(View.MeasureSpec.makeMeasureSpec(dm.widthPixels, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(dm.heightPixels, View.MeasureSpec.EXACTLY)); root.layout(0, 0, dm.widthPixels, dm.heightPixels)
        val bar = root.findViewWithTag<View>("navBar"); assertTrue("bar ${bar.height}", bar.height <= (60 * dm.density).toInt() && bar.height >= (48 * dm.density).toInt())
        find(root, "sec_studio")!!.performClick(); assertEquals(View.GONE, find(root, "studioWeb")!!.visibility)
    }
}
