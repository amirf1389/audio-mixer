package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import android.widget.SeekBar
import android.widget.TextView
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** THE PHONE MIXER'S DUCKER (VOICE OVER MUSIC): the CH inputs key, the decks are the target; settings kept and clamped; in TOOLS. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class PhoneDuckerTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
    private fun open() = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
    @Before fun before() { PlayersBus.clear(); Nav.standaloneForTests = true }
    @After fun after() { PlayersBus.clear(); Nav.standaloneForTests = null }

    @Test fun theVoicesKeyAndTheDecksAreTheTarget() {
        val r = PhoneDucker.roles(LocalMixerActivity.INPUTS, (0 until LocalMixerActivity.DECKS).map { LocalMixerActivity.deckStrip(it) }, LocalMixerActivity.STRIPS)
        assertArrayEquals(intArrayOf(1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2), r)   // 6 CH inputs, then 4 decks of left / right
        assertArrayEquals(intArrayOf(1, 0, 2, 2, 0), PhoneDucker.roles(1, listOf(2), 5))
    }

    @Test fun defaultsAreTheConsolesAndValuesAreClamped() {
        val d = PhoneDucker.Params()
        assertFalse(d.on); assertEquals(-12.0, d.depthDb, 0.0); assertEquals(20.0, d.attackMs, 0.0); assertEquals(300.0, d.holdMs, 0.0); assertEquals(600.0, d.releaseMs, 0.0); assertEquals(0.0, d.liftDb, 0.0)
        val c = PhoneDucker.Params(true, 10.0, -90.0, 0.0, -5.0, 1.0e6, 12.0).clamped()
        assertEquals(0.0, c.thresholdDb, 0.0); assertEquals(-40.0, c.depthDb, 0.0); assertEquals(1.0, c.attackMs, 0.0); assertEquals(0.0, c.holdMs, 0.0); assertEquals(5000.0, c.releaseMs, 0.0); assertEquals(6.0, c.liftDb, 0.0)
        assertEquals(-20.0, PhoneDucker.get(PhoneDucker.with(d, "depthDb", -20.0), "depthDb"), 0.0)
        assertEquals(0.0, PhoneDucker.reductionDb(null, c), 0.0)   // no engine: nothing ducked
    }

    @Test fun theDuckerIsInToolsAndKept() {
        val a = open(); val root = a.window.decorView
        find(root, "sec_tools")!!.performClick()
        val tools = find(root, "secTools") as ViewGroup
        for (t in listOf("duckToggle", "duck_thresholdDb", "duck_depthDb", "duck_attackMs", "duck_holdMs", "duck_releaseMs", "duck_liftDb", "duckNow")) assertNotNull(t, find(tools, t))
        find(tools, "duckToggle")!!.performClick(); assertTrue(PhoneDucker.load(Prefs.local(a)).on)
        val depth = find(tools, "duck_depthDb") as SeekBar; assertEquals(700, depth.progress)   // -12 of -40 .. 0
        assertTrue((find(tools, "duckNow") as TextView).text.contains("0.0"))   // not started: the music is not down
        PhoneDucker.save(Prefs.local(a), PhoneDucker.load(Prefs.local(a)).copy(depthDb = -18.0))
        val b = open(); find(b.window.decorView, "sec_tools")!!.performClick()
        assertEquals(550, (find(b.window.decorView, "duck_depthDb") as SeekBar).progress)
        assertTrue(PhoneDucker.load(Prefs.local(b)).on)
        find(b.window.decorView, "duckToggle")!!.performClick(); assertFalse(PhoneDucker.load(Prefs.local(b)).on)
    }
}
