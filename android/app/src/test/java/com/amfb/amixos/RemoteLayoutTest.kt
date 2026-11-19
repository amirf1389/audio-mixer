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
}
