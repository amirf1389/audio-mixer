package com.amfb.amixos

import android.view.WindowManager
import android.widget.Switch
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** HIDE FROM SCREENSHOTS: opt-in (off by default, so streaming apps never capture a black screen); when on, FLAG_SECURE on every screen, applied live. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class CaptureGuardTest {
    private fun secure(a: android.app.Activity) = a.window.attributes.flags and WindowManager.LayoutParams.FLAG_SECURE != 0
    private val screens = listOf(HomeActivity::class.java, SettingsActivity::class.java, ToolsActivity::class.java, CalculatorActivity::class.java)

    @Test fun offByDefault() {
        for (cls in screens) assertFalse("${cls.simpleName} must not be secure by default", secure(Robolectric.buildActivity(cls).setup().get()))
    }
    @Test fun theSwitchAppliesLiveToEveryScreen() {
        val s = Robolectric.buildActivity(SettingsActivity::class.java).setup().get()
        val sw = s.window.decorView.findViewWithTag<Switch>(CaptureGuard.KEY)
        assertFalse(sw.isChecked)
        sw.isChecked = true
        assertTrue(secure(s)); assertTrue(CaptureGuard.enabled(s))
        for (cls in screens) assertTrue("${cls.simpleName} must carry FLAG_SECURE", secure(Robolectric.buildActivity(cls).setup().get()))
        sw.isChecked = false
        assertFalse(secure(s))
    }
}
