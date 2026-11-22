package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import android.widget.SeekBar
import org.junit.After
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
import kotlin.math.abs
import kotlin.math.pow

/** THE PHONE MIXER'S LOUDNESS MANAGER: the ride converges within the range at the speed limits, never past +10 dB; AUTO ON/OFF closes
 *  after the hold and opens at once; a hand on the fader suspends the ride; the master target converges slowly; it is in TOOLS and kept. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class PhoneLoudnessTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
    private fun open() = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
    @Before fun before() { PlayersBus.clear(); Nav.standaloneForTests = true }
    @After fun after() { PlayersBus.clear(); Nav.standaloneForTests = null }
    /** the peak of a sine whose estimated loudness is lufs */
    private fun peakFor(lufs: Double) = (2 * 10.0.pow((lufs + 0.691) / 10)).pow(0.5)
    private val dt = 0.05

    @Test fun theRideConvergesWithinTheSpeedLimits() {
        val v = PhoneLoudness.Params(); val r = PhoneLoudness.Rider(); var maxUp = 0.0
        repeat((8 / dt).toInt()) { r.feed(peakFor(-24.0), dt); val b = r.rideDb; r.step(dt, 0.0, false, true, false, v); maxUp = maxOf(maxUp, r.rideDb - b) }
        assertEquals(-18.0, r.shortTerm + r.rideDb, 0.2); assertTrue(maxUp / dt <= 3.0 + 1e-9)
        val d = PhoneLoudness.Rider(); var maxDown = 0.0
        repeat((6 / dt).toInt()) { d.feed(peakFor(-12.0), dt); val b = d.rideDb; d.step(dt, 0.0, false, true, false, v); maxDown = maxOf(maxDown, b - d.rideDb) }
        assertEquals(-18.0, d.shortTerm + d.rideDb, 0.2); assertTrue(maxDown / dt <= 6.0 + 1e-9)
    }

    @Test fun neverPastTheRangeOrTenDb() {
        val v = PhoneLoudness.Params(rangeDb = 10.0); val r = PhoneLoudness.Rider()
        repeat((10 / dt).toInt()) { r.feed(peakFor(-45.0), dt); r.step(dt, 0.0, false, true, false, v); assertTrue(r.rideDb <= 10.0 + 1e-9) }
        assertEquals(10.0, r.rideDb, 1e-6)
        repeat(20) { r.feed(peakFor(-45.0), dt); r.step(dt, 6.0, false, true, false, v) }; assertTrue(r.rideDb <= 4.0 + 1e-9)   // fader +6: ride +4 at most
        val q = PhoneLoudness.Rider(); repeat((10 / dt).toInt()) { q.feed(peakFor(-2.0), dt); q.step(dt, 0.0, false, true, false, v.copy(rangeDb = 3.0)) }; assertEquals(-3.0, q.rideDb, 1e-6)
    }

    @Test fun autoOnOffClosesAfterTheHoldAndOpensAtOnce() {
        val v = PhoneLoudness.Params(); val r = PhoneLoudness.Rider()
        repeat(40) { r.feed(peakFor(-30.0), dt); r.step(dt, 0.0, false, false, true, v) }; assertFalse(r.closed)
        repeat((3.9 / dt).toInt()) { r.feed(0.0, dt); r.step(dt, 0.0, false, false, true, v) }; assertFalse(r.closed)
        repeat((1.6 / dt).toInt()) { r.feed(0.0, dt); r.step(dt, 0.0, false, false, true, v) }; assertTrue(r.closed); assertEquals(PhoneLoudness.FLOOR, r.gateDb, 1e-9)
        r.feed(peakFor(-30.0), dt); r.step(dt, 0.0, false, false, true, v); assertFalse(r.closed)   // back within one 50 ms tick
        val m = PhoneLoudness.Rider(); repeat((8 / dt).toInt()) { m.feed(0.0, dt); m.step(dt, 0.0, true, false, true, v) }; assertFalse(m.closed)   // a muted channel is left alone
    }

    @Test fun aHandOnTheFaderSuspendsTheRide() {
        val v = PhoneLoudness.Params(); val r = PhoneLoudness.Rider()
        repeat((6 / dt).toInt()) { r.feed(peakFor(-24.0), dt); r.step(dt, 0.0, false, true, false, v) }; assertTrue(r.rideDb > 5)
        r.touch(); repeat((4.5 / dt).toInt()) { r.feed(peakFor(-24.0), dt); r.step(dt, 2.0, false, true, false, v) }; assertEquals(0.0, r.rideDb, 1e-9)
        repeat((4 / dt).toInt()) { r.feed(peakFor(-24.0), dt); r.step(dt, 2.0, false, true, false, v) }; assertTrue(r.rideDb > 2.5)
    }

    @Test fun theMasterTargetConvergesSlowly() {
        val v = PhoneLoudness.Params(masterOn = true, masterTarget = -16.0); val m = PhoneLoudness.MasterRider(); var maxStep = 0.0
        repeat((12 / dt).toInt()) { val b = m.gainDb; val out = peakFor(-20.0) * 10.0.pow(m.gainDb / 20); m.step(out, dt, v); maxStep = maxOf(maxStep, abs(m.gainDb - b)) }
        assertEquals(4.0, m.gainDb, 0.2); assertTrue(maxStep / dt <= 1.0 + 1e-9)
        repeat(3) { m.step(peakFor(-5.0), dt, v) }; assertTrue(m.gainDb <= 6.0)
        assertEquals(0.0, m.step(1.0, dt, v.copy(masterOn = false)), 0.0)
    }

    @Test fun defaultsAreTheConsolesAndClamped() {
        val d = PhoneLoudness.Params(); assertEquals(-18.0, d.target, 0.0); assertEquals(10.0, d.rangeDb, 0.0); assertEquals(3.0, d.upDbS, 0.0); assertEquals(6.0, d.downDbS, 0.0); assertEquals(-50.0, d.gateDb, 0.0); assertEquals(4.0, d.holdS, 0.0)
        val c = PhoneLoudness.Params(target = 0.0, rangeDb = 50.0, gateDb = -100.0, holdS = 0.0, masterTarget = -2.0).clamped()
        assertEquals(-6.0, c.target, 0.0); assertEquals(20.0, c.rangeDb, 0.0); assertEquals(-80.0, c.gateDb, 0.0); assertEquals(0.1, c.holdS, 0.0); assertEquals(-8.0, c.masterTarget, 0.0)
        assertEquals(-23.0, PhoneLoudness.presets["BROADCAST"]!!, 0.0)
    }

    @Test fun theManagerIsInToolsKeptAndRides() {
        val a = open(); val root = a.window.decorView
        find(root, "sec_tools")!!.performClick()
        val tools = find(root, "secTools") as ViewGroup
        for (t in listOf("loudMaster", "loudPreset_STREAMING", "loudPreset_BROADCAST", "loudAll_ride", "loudAll_gate", "loud_target", "loud_rangeDb", "loud_gateDb", "loud_holdS", "loud_ride_0", "loud_gate_0", "loud_ride_100", "loudNow")) assertNotNull(t, find(tools, t))
        find(tools, "loud_ride_0")!!.performClick(); assertTrue(PhoneLoudness.rideOn(Prefs.local(a), 0))
        find(tools, "loudPreset_BROADCAST")!!.performClick(); assertEquals(-23.0, PhoneLoudness.load(Prefs.local(a)).masterTarget, 0.0)
        find(tools, "loudMaster")!!.performClick(); assertTrue(PhoneLoudness.load(Prefs.local(a)).masterOn)
        assertEquals(647, (find(tools, "loud_target") as SeekBar).progress)   // -18 of -40 .. -6
        // the ride from the input peaks: CH 1 at about -24 rides up toward -18
        val peaks = FloatArray(LocalMixerActivity.STRIPS) { 0f }; peaks[0] = peakFor(-24.0).toFloat()
        repeat(160) { a.loudTick(0.05, peaks, 0.0) }
        assertTrue((a.lmOffset[0] ?: 0.0) > 4.0)
        find(tools, "loudAll_gate")!!.performClick(); assertTrue(a.dcaChannels().all { PhoneLoudness.gateOn(Prefs.local(a), it) })
        val b = open(); find(b.window.decorView, "sec_tools")!!.performClick()
        assertTrue(PhoneLoudness.rideOn(Prefs.local(b), 0)); assertTrue(PhoneLoudness.load(Prefs.local(b)).masterOn)
        find(b.window.decorView, "loudMaster")!!.performClick(); assertFalse(PhoneLoudness.load(Prefs.local(b)).masterOn)
    }
}
