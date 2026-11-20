package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import androidx.test.core.app.ApplicationProvider

/** THE REMOTE'S FADERS VIEW: four wide channels beside the master (swipe for the rest), and the switch to the KNOBS view. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class RemoteLayoutTest {
    @Test fun fourChannelsStandBesideTheMaster() {
        assertEquals(4, RemoteActivity.ACROSS); val w = RemoteActivity.stripWidthFor(1233, 4)
        assertTrue(4 * (w + 8) + 252 + 8 + 40 <= 1233)                      // four strips, their margins, the master and the page padding fit
        assertTrue(5 * (w + 8) + 252 + 8 + 40 > 1233)                       // and a fifth does not: a page is exactly four channels
        assertTrue(w > RemoteActivity.stripWidthFor(1233, 5))                // each one wider than when five stood across
        assertEquals(176, RemoteActivity.stripWidthFor(600, 4))              // a narrow screen: never thinner than a finger
        assertTrue(RemoteActivity.stripWidthFor(2400, 4) > RemoteActivity.stripWidthFor(1233, 4))   // a tablet: wider faders, still four across
    }
    @Test fun theViewSwitchIsRemembered() {
        val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
        assertTrue(Prefs.bigFaders(ctx))                                     // FADERS is the view a new phone starts in
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        fun find(v: android.view.View, d: String): android.view.View? { if (v.contentDescription == d) return v; if (v is android.view.ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), d)?.let { return it }; return null }
        val b = find(a.window.decorView, "remoteView") as android.widget.Button
        assertEquals("KNOBS", b.text.toString()); b.performClick()
        assertTrue(!Prefs.bigFaders(ctx)); assertEquals("FADERS", b.text.toString()); b.performClick(); assertTrue(Prefs.bigFaders(ctx))
    }
    @Test fun inTheKnobsViewTheFaderKeepsMoreThanHalfTheStrip() {
        // the KNOBS view with pan, gain and the 3-band EQ: the knobs scroll in their own box, the fader is never squeezed to a sliver
        val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
        Prefs.remote(ctx).edit().putBoolean("bigFaders", false).commit()
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        val s = com.amfb.amixos.remote.ConsoleState(strips = (0 until 4).map { com.amfb.amixos.remote.StripState(it, "CH ${it + 1}", 0.0, 0.0, false, false) }, buses = emptyList(), masterDb = 0.0, limiter = true, ceilingDb = -0.5)
        val m = RemoteActivity::class.java.getDeclaredMethod("buildStrips", com.amfb.amixos.remote.ConsoleState::class.java); m.isAccessible = true; m.invoke(a, s)
        val root = a.window.decorView; root.measure(android.view.View.MeasureSpec.makeMeasureSpec(1233, android.view.View.MeasureSpec.EXACTLY), android.view.View.MeasureSpec.makeMeasureSpec(2673, android.view.View.MeasureSpec.EXACTLY)); root.layout(0, 0, 1233, 2673)
        fun all(v: android.view.View, d: String, out: MutableList<android.view.View> = mutableListOf()): List<android.view.View> { if (v.contentDescription == d || v.tag == d) out += v; if (v is android.view.ViewGroup) for (i in 0 until v.childCount) all(v.getChildAt(i), d, out); return out }
        val f = all(root, "stripFader"); val k = all(root, "knobScroll")
        assertTrue(f.isNotEmpty() && k.isNotEmpty())
        val strip = f[0].parent as android.view.View
        assertTrue("fader ${f[0].height} of strip ${strip.height}", f[0].height > strip.height * 0.40)
        assertTrue("knobs ${k[0].height} of strip ${strip.height}", k[0].height < strip.height * 0.40)
        Prefs.remote(ctx).edit().putBoolean("bigFaders", true).commit()
    }
    /** COMPACT (the AmF X1 app): five channels across upright, eight on its side, keys 40 dp tall, no text cut off, the fader over half the strip */
    @Test fun theAppsStripsAndKeysAreCompact() {
        val d = 3f; val w5 = RemoteActivity.compactStripWidthFor(1233, 5, d)
        assertEquals(5, RemoteActivity.COMPACT_ACROSS); assertEquals(8, RemoteActivity.compactAcrossFor(true))
        assertTrue(5 * (w5 + 8) + 252 + 8 + 40 <= 1233)                     // five strips and the master fit upright
        assertTrue(w5 < RemoteActivity.stripWidthFor(1233, 4))               // narrower than the old four-across
        assertEquals((RemoteActivity.MIN_STRIP_DP * d).toInt(), RemoteActivity.compactStripWidthFor(600, 5, d))   // never thinner than 52 dp
        for (big in listOf(true, false)) {
            val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
            Prefs.remote(ctx).edit().putBoolean("bigFaders", big).commit()
            val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
            val s = com.amfb.amixos.remote.ConsoleState(strips = (0 until 8).map { com.amfb.amixos.remote.StripState(it, "CH ${it + 1}", 0.0, 0.0, false, false) }, buses = emptyList(), masterDb = 0.0, limiter = true, ceilingDb = -0.5)
            val m = RemoteActivity::class.java.getDeclaredMethod("buildStrips", com.amfb.amixos.remote.ConsoleState::class.java); m.isAccessible = true; m.invoke(a, s)
            val root = a.window.decorView; root.measure(android.view.View.MeasureSpec.makeMeasureSpec(1233, android.view.View.MeasureSpec.EXACTLY), android.view.View.MeasureSpec.makeMeasureSpec(2673, android.view.View.MeasureSpec.EXACTLY)); root.layout(0, 0, 1233, 2673)
            fun all(v: android.view.View, t: String, out: MutableList<android.view.View> = mutableListOf()): List<android.view.View> { if (v.contentDescription == t || v.tag == t) out += v; if (v is android.view.ViewGroup) for (i in 0 until v.childCount) all(v.getChildAt(i), t, out); return out }
            val keys = all(root, "stripKey") + all(root, "eqKey") + all(root, "remoteView")
            assertTrue(keys.size >= 16)
            for (k in keys) { k as android.widget.TextView
                assertTrue("key ${k.text} ${k.height} px tall", k.height >= (ConsoleUi.KEY_DP * d).toInt() - 1)
                assertTrue("key ${k.text} cut: ${k.paint.measureText(k.text.toString())} in ${k.width - k.totalPaddingLeft - k.totalPaddingRight}", k.paint.measureText(k.text.toString()) <= k.width - k.totalPaddingLeft - k.totalPaddingRight + 1)
                assertTrue(k.lineCount <= 1) }
            val f = all(root, "stripFader"); val strip = f[0].parent as android.view.View
            if (big) { assertTrue("fader ${f[0].height} of ${strip.height}", f[0].height > strip.height * 0.5); assertEquals(w5, strip.width) }
            else assertTrue(f[0].height > strip.height * 0.40)
        }
        Prefs.remote(ApplicationProvider.getApplicationContext<android.content.Context>()).edit().putBoolean("bigFaders", true).commit()
    }
}
