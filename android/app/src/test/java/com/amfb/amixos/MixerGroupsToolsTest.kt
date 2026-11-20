package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

/** THE PHONE MIXER: GROUP FADERS (DCA), the TOOLS section (tone, level meter) and CHROMEBOOK MODE. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class MixerGroupsToolsTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
    private fun open() = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
    @Before fun before() { PlayersBus.clear(); Nav.standaloneForTests = true }
    @After fun after() { PlayersBus.clear(); Nav.standaloneForTests = null }

    @Test fun aGroupFaderAddsItsLevelWithoutMovingTheChannelFaders() {
        assertEquals(-6.0, Dca.effectiveDb(0.0, -6.0), 1e-9)
        assertEquals(-16.0, Dca.effectiveDb(-10.0, -6.0), 1e-9)
        assertEquals(-3.0, Dca.effectiveDb(-3.0, null), 1e-9)                  // not in a group: its own fader
        assertEquals(Dca.FLOOR, Dca.effectiveDb(-60.0, 6.0), 1e-9)              // a channel fully down stays off
        assertEquals(Dca.FLOOR, Dca.effectiveDb(0.0, Dca.FLOOR), 1e-9)          // a group fully down turns its channels off
        assertEquals(Dca.CEILING, Dca.effectiveDb(10.0, 10.0), 1e-9)
        assertEquals(Dca.FLOOR, Dca.effectiveDb(-55.0, -20.0), 1e-9)
    }

    @Test fun groupsAreAssignedInSetupAndKept() {
        val a = open(); val p = Prefs.local(a)
        assertNull(find(a.window.decorView, "dca0"))                            // no group in use: no group fader on the desk
        find(a.window.decorView, "dcaAssign_0")!!.performClick()                // CH 1 → DCA 1
        assertEquals(0, Dca.groupOf(p, 0)); assertNotNull(find(a.window.decorView, "dca0"))
        find(a.window.decorView, "dcaAssign_100")!!.performClick(); find(a.window.decorView, "dcaAssign_100")!!.performClick()   // DECK A → DCA 2
        assertEquals(1, Dca.groupOf(p, 100)); assertEquals(listOf(0), Dca.members(p, 0, a.dcaChannels())); assertNotNull(find(a.window.decorView, "dca1"))
        p.edit().putFloat("db0", -10f).apply(); Dca.setLevel(p, 0, -6.0)
        assertEquals(-16.0, Dca.effectiveDb(p, 0, -10.0), 1e-9); assertEquals(-10f, p.getFloat("db0", 0f))   // the channel's own fader is untouched
        assertFalse(Dca.effectiveMute(p, 0, false))
        find(a.window.decorView, "dcaMute0")!!.performClick()
        assertTrue(Dca.muted(p, 0)); assertTrue(Dca.effectiveMute(p, 0, false)); assertFalse(Dca.effectiveMute(p, 100, false))   // only its own members
        val again = open(); assertNotNull(find(again.window.decorView, "dca0")); assertTrue(Dca.muted(Prefs.local(again), 0))   // kept
        repeat(3) { find(again.window.decorView, "dcaAssign_100")!!.performClick() }   // 2 → 3 → 4 → none
        assertEquals(-1, Dca.groupOf(Prefs.local(again), 100)); assertNull(find(again.window.decorView, "dca1"))
    }

    @Test fun toolsIsItsOwnSectionWithToneAndLevel() {
        val a = open(); val root = a.window.decorView
        assertEquals(View.GONE, find(root, "secTools")!!.visibility)
        find(root, "sec_tools")!!.performClick()
        assertEquals(View.VISIBLE, find(root, "secTools")!!.visibility); assertEquals(View.GONE, find(root, "secSetup")!!.visibility)
        val tools = find(root, "secTools") as ViewGroup
        for (t in listOf("toneToggle", "toneKind_sine", "toneKind_pink", "toneLevel", "lvlMaster", "lvlInput", "lvlSpl")) assertNotNull(t, find(tools, t))
        find(tools, "toneKind_pink")!!.performClick(); assertEquals("pink", Prefs.local(a).getString("toneKind", ""))
        find(tools, "toneToggle")!!.performClick()                                // not started: the tone asks for START, nothing plays
        assertEquals(a.getString(R.string.lm_tone_need_start), (find(tools, "toneStatus") as TextView).text.toString())
        assertTrue((find(tools, "lvlMaster") as TextView).text.contains("—"))     // silent: no reading
        assertEquals(-20.0, MixerTools.dbfs(0.1f), 1e-6); assertEquals(100.0, MixerTools.spl(0.1f, 120)!!, 1e-6); assertNull(MixerTools.spl(0f, 120))
        assertEquals(LocalMixerActivity.DECKS - 1, MixerTools.TONE_DECK)
    }

    @Test fun theToneGeneratorMakesSineAndPink() {
        val g = SignalMath.Generator(48000).apply { kind = "sine"; levelDb = -20.0 }; val b = FloatArray(4800); g.fill(b)
        assertEquals(-20.0, MixerTools.dbfs(b.maxOf { Math.abs(it) }), 0.1)
        g.kind = "pink"; g.fill(b); assertTrue(b.any { it != 0f } && b.all { Math.abs(it) <= 1f })
    }

    @Test fun chromebookModeDecides() {
        assertTrue(ChromeFrame.decide("auto", true, false)); assertTrue(ChromeFrame.decide("auto", false, true)); assertFalse(ChromeFrame.decide("auto", false, false))
        assertTrue(ChromeFrame.decide("on", false, false)); assertFalse(ChromeFrame.decide("off", true, true))
    }

    @Test fun forcedOnTheScreensSitInTheRailFrame() {
        val a = open(); assertNotNull(find(a.window.decorView, "navBar")); assertNull(find(a.window.decorView, "chromeRail"))
        find(a.window.decorView, "cbMode_on")!!.performClick(); assertEquals("on", ChromeFrame.mode(a))
        val b = open(); val root = b.window.decorView
        assertNotNull(find(root, "chromeRail")); assertNull(find(root, "navBar"))
        for (e in listOf("rail_mixer", "rail_sound_card", "rail_network", "rail_settings", "rail_licence")) assertNotNull(e, find(root, e))
        find(root, "rail_settings")!!.performClick(); assertEquals(SettingsActivity::class.java.name, shadowOf(b).nextStartedActivity.component!!.className)
        find(root, "rail_sound_card")!!.performClick(); assertEquals(DevicesActivity::class.java.name, shadowOf(b).nextStartedActivity.component!!.className)
        find(root, "rail_network")!!.performClick(); assertEquals(DevicesActivity::class.java.name, shadowOf(b).nextStartedActivity.component!!.className)   // the mixer app: the phone's networks
        assertNotNull(find(root, "secFaders"))                                   // the mixer itself is unchanged inside the frame
        ChromeFrame.setMode(b, "off"); assertNotNull(find(open().window.decorView, "navBar"))
    }

    @Test fun onChromeOsAutoUsesTheRail() {
        val app = org.robolectric.RuntimeEnvironment.getApplication()
        shadowOf(app.packageManager).setSystemFeature("org.chromium.arc", true)
        assertTrue(ChromeFrame.isChromeOs(app)); assertNotNull(find(open().window.decorView, "chromeRail"))
    }

    @Test @Config(qualifiers = "sw800dp-w1280dp-h800dp-land") fun aLargeWindowOnItsSideUsesTheRail() {
        val a = open(); assertTrue(ChromeFrame.largeLandscape(a)); assertNotNull(find(a.window.decorView, "chromeRail"))
    }
}
