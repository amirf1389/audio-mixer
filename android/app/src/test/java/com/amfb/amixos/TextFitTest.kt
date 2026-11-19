package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** RESPONSIVE TEXT: the text size follows the screen width, the phone's font setting and the choice in SETTINGS. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class TextFitTest {
    @Test fun theCurveFollowsTheScreen() {
        assertEquals(0.85f, TextFit.screenFactor(280), 1e-4f)                 // a very narrow phone: smaller, never below the floor
        assertEquals(0.92f, TextFit.screenFactor(360), 1e-4f)
        assertEquals(1.0f, TextFit.screenFactor(411), 1e-4f)                  // a usual phone: unchanged
        assertEquals(1.15f, TextFit.screenFactor(600), 1e-4f)                 // a small tablet
        assertEquals(1.3f, TextFit.screenFactor(1280), 1e-4f)                 // a large tablet: never above the ceiling
        var last = 0f; for (w in 240..1400 step 7) { val f = TextFit.screenFactor(w); assertTrue(f >= last); last = f }   // it never steps down as the screen grows
    }
    @Test fun thePhoneSettingCountsInsideSafeLimits() {
        assertEquals(1.3f, TextFit.fontScale(411, 1.3f, TextFit.AUTO), 1e-4f)
        assertEquals(1.5f, TextFit.fontScale(720, 2.0f, TextFit.AUTO), 1e-4f)  // huge system text on a tablet: capped, the layout holds
        assertEquals(0.8f, TextFit.fontScale(320, 0.85f, TextFit.AUTO), 1e-4f)
        assertEquals(0.88f, TextFit.fontScale(720, 1.0f, TextFit.SMALL), 1e-4f) // a fixed choice ignores the screen
        assertEquals(1.0f, TextFit.fontScale(320, 1.0f, TextFit.NORMAL), 1e-4f)
        assertEquals(1.15f, TextFit.fontScale(360, 1.0f, TextFit.LARGE), 1e-4f)
    }
    @Test @Config(qualifiers = "sw320dp-w320dp-h568dp") fun aNarrowPhoneGetsSmallerText() {
        val a = Robolectric.buildActivity(HomeActivity::class.java).setup().get()
        assertEquals(0.85f, a.resources.configuration.fontScale, 1e-3f)
    }
    @Test @Config(qualifiers = "sw720dp-w720dp-h1280dp") fun aTabletGetsLargerTextAndTheChoiceWins() {
        val a = Robolectric.buildActivity(SettingsActivity::class.java).setup().get()
        assertEquals(1.3f, a.resources.configuration.fontScale, 1e-3f)
        Prefs.remote(a).edit().putInt("textSize", TextFit.SMALL).apply()
        val b = Robolectric.buildActivity(SettingsActivity::class.java).setup().get()
        assertEquals(0.88f, b.resources.configuration.fontScale, 1e-3f)
    }
}
