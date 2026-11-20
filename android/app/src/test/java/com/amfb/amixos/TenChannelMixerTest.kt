package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.core.app.ApplicationProvider
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

/** THE AmF X1 MIXER app at ten channels: CH 1 .. 6 and DECK A .. D, each with fader, MUTE, 3-band EQ and FX; the preset pack on every
 *  channel (tone presets on the EQ, effect presets in the rack); groups, multitrack and old saves. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class TenChannelMixerTest {
    private val ctx get() = ApplicationProvider.getApplicationContext<android.content.Context>()
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
    private fun faders(v: View, out: MutableList<FaderView> = mutableListOf()): List<FaderView> { if (v is FaderView) out += v; if (v is ViewGroup) for (i in 0 until v.childCount) faders(v.getChildAt(i), out); return out }
    private fun open() = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
    @Before fun before() { PlayersBus.clear(); Nav.standaloneForTests = true }
    @After fun after() { PlayersBus.clear(); Nav.standaloneForTests = null }

    @Test fun tenChannelStripsEachWithFaderMuteEqAndFx() {
        val a = open(); val desk = find(a.window.decorView, "localChannels") as ViewGroup
        assertEquals(10, LocalMixerActivity.CHANNELS)
        assertTrue("10 channel faders + the master", faders(desk).size >= 10)
        for (ch in (0 until 6) + (100 until 104)) assertNotNull("EQ on channel $ch", find(desk, "eq_$ch"))
        val labels = mutableListOf<String>(); fun texts(v: View) { if (v is TextView) labels += v.text.toString(); if (v is ViewGroup) for (i in 0 until v.childCount) texts(v.getChildAt(i)) }; texts(desk)
        assertTrue("a MUTE on every channel", labels.count { it == "MUTE" } >= 10); assertTrue("an FX key on every channel", labels.count { it.startsWith("FX") } >= 10)
        assertEquals(10, a.dcaChannels().size)                                       // the four groups take any of the ten
    }

    @Test fun enginePlanAndMultitrack() {
        assertEquals(14, LocalMixerActivity.STRIPS); assertTrue(LocalMixerActivity.STRIPS <= 16)   // the multitrack ring holds 16
        assertEquals(listOf(6, 8, 10, 12), (0 until 4).map { LocalMixerActivity.deckStrip(it) })
        assertEquals(listOf("ch1", "ch2", "ch3", "ch4", "ch5", "ch6", "deckA", "deckB", "deckC", "deckD"), LocalMixerActivity.trackPlan(6).map { it.second })
        assertEquals(listOf("ch1", "deckA", "deckB", "deckC", "deckD"), LocalMixerActivity.trackPlan(1).map { it.second })
        assertArrayEquals(intArrayOf(12, 13), LocalMixerActivity.trackPlan(1).last().first)
        assertEquals(LocalMixerActivity.DECKS - 1, MixerTools.TONE_DECK); assertEquals(listOf(12, 13), LocalMixerActivity.chStrips(100 + MixerTools.TONE_DECK))
        assertEquals(listOf(1, 2, 6), LocalMixerActivity.INPUT_MODES)
    }

    @Test fun oldSavesMoveToTheNewDeckStrips() {
        val p = Prefs.local(ctx); p.edit().clear()
            .putString("rack2", "hall|||").putString("fx_2_0", "mix=0.5000").putString("rack4", "delay|||").putString("rack8", "drive|||").putString("fx_8_0", "drive=0.9000").putString("chain8", "voice")
            .putString("rack0", "clean|||").putFloat("deckDb1", -6f).commit()
        LocalMixerActivity.migrate(p)
        assertEquals("hall|||", p.getString("rack6", "")); assertEquals("mix=0.5000", p.getString("fx_6_0", ""))   // deck A: 2 -> 6
        assertEquals("delay|||", p.getString("rack8", ""))                                                         // deck B: 4 -> 8 (where deck D was)
        assertEquals("drive|||", p.getString("rack12", "")); assertEquals("drive=0.9000", p.getString("fx_12_0", "")); assertEquals("voice", p.getString("chain12", ""))
        assertFalse(p.contains("rack2")); assertFalse(p.contains("rack4")); assertFalse(p.contains("fx_8_0"))       // CH 3 and CH 5 start empty
        assertEquals("clean|||", p.getString("rack0", "")); assertEquals(-6f, p.getFloat("deckDb1", 0f))          // inputs and deck levels as they were
        assertEquals(2, p.getInt("layout", 0))
        LocalMixerActivity.migrate(p); assertEquals("delay|||", p.getString("rack8", ""))                          // once only
        val a = open(); assertNotNull(find(a.window.decorView, "eq_100"))                                          // and the mixer opens on them
    }

    @Test fun threeBandEqAndTonePresetsOnEveryChannel() {
        val flat = ChannelEq.bands(0.0, 0.0, 0.0); assertTrue(ChannelEq.flat(flat))
        val b = ChannelEq.bands(6.0, -3.0, 4.0)
        assertEquals(6.0, b[0], 1e-9); assertEquals(6.0, b[2], 1e-9); assertEquals(1.5, b[3], 1e-9); assertEquals(-3.0, b[5], 1e-9); assertEquals(0.5, b[7], 1e-9); assertEquals(4.0, b[9], 1e-9)
        assertEquals(12.0, ChannelEq.bands(12.0, 0.0, 0.0, DoubleArray(10) { 6.0 })[0], 1e-9)                  // never past +-12 dB
        assertEquals(0.75, ChannelEq.knob(6.0), 1e-9)
        val pack = PresetPack.builtIn(ctx)!!; assertTrue(pack.tone.size >= 25)
        assertTrue("every tone preset carries its filters", pack.tone.all { it.kind == "chain" && it.name == "FLAT" || it.bands.isNotEmpty() || it.hpHz > 0 })
        val vocal = pack.tone.first { it.name == "VOCAL" }; val vb = ChannelEq.toneBands(vocal.bands, vocal.hpHz, vocal.lpHz)
        assertEquals(-2.5, vb[3], 1e-9); assertEquals(2.5, vb[7], 1e-9); assertEquals(2.0, vb[9], 1e-9); assertTrue(vb[0] < 0)   // 250 bell down, 3.2 k up, 10 k shelf, the high pass
        val a = open(); val p = Prefs.local(a)
        for (ch in (0 until 6) + (100 until 104)) {                                                                // the same tone on any of the ten
            a.applyTone(ch, vocal); assertEquals("VOCAL", ChannelEq.toneName(p, ch)); assertArrayEquals(ChannelEq.bands(0.0, 0.0, 0.0, vb), ChannelEq.of(p, ch), 1e-2)
            a.setEq(ch, 3.0, 0.0, -3.0); assertEquals(3.0, ChannelEq.low(p, ch), 1e-6); assertEquals("VOCAL", ChannelEq.toneName(p, ch)) }
        assertEquals("EQ ●", (find(a.window.decorView, "eq_3") as TextView).text.toString())
        a.applyTone(3, null); ChannelEq.reset(p, 3); assertTrue(ChannelEq.flat(ChannelEq.of(p, 3))); assertEquals("VOCAL", ChannelEq.toneName(p, 4))
    }

    @Test fun effectPresetsGoIntoEachChannelsRack() {
        val a = open(); val p = Prefs.local(a); val pack = PresetPack.builtIn(ctx)!!
        val verb = pack.effects.first { it.kind == "reverb" }; val dly = pack.effects.first { it.kind == "delay" }
        for (s in (0 until 6) + (0 until 4).map { LocalMixerActivity.deckStrip(it) }) {
            assertEquals(0, a.applyEffectPreset(s, verb)); assertEquals(1, a.applyEffectPreset(s, dly))
            assertEquals("reverb|delay||", p.getString(LocalMixerActivity.rackKey(s), ""))
            verb.values.forEach { (id, _) -> assertTrue("$id kept on strip $s", p.getString(LocalMixerActivity.paramKey(s, 0), "")!!.contains("$id=")) } }
        val s = LocalMixerActivity.deckStrip(3); a.applyEffectPreset(s, verb); a.applyEffectPreset(s, verb)
        assertEquals(3, a.applyEffectPreset(s, dly)); assertEquals("reverb|delay|reverb|delay", p.getString(LocalMixerActivity.rackKey(s), ""))   // full: the last slot
        assertEquals(4, Engine.MAX_INSERTS); assertEquals(Engine.MAX_INSERTS, Engine.EQ_SLOT)                     // the EQ never takes a rack slot
    }

    @Test fun inputModeSixFeedsCh1To6() {
        val a = open(); find(a.window.decorView, "inMode_6")!!.performClick()
        assertEquals(6, Prefs.local(ctx).getInt("inChannels", 0))
        val b = open(); assertTrue((find(b.window.decorView, "inMode_6") as PadButton).on)
    }
}
