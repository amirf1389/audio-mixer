package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** THIS PHONE on HOME (the local mixer's card) and the QUICK CHAINS of the phone mixer. (No native engine on the JVM: the rack is
 *  written to the saved state, which is what the mixer loads.) */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class PhoneCardTest {
    private fun find(v: View, tag: String): View? { if (v.tag == tag) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), tag)?.let { return it }; return null }
    private fun texts(v: View, out: MutableList<String> = mutableListOf()): List<String> {
        if (v is TextView) out += v.text.toString(); if (v is ViewGroup) for (i in 0 until v.childCount) texts(v.getChildAt(i), out); return out }

    @Test fun homeHasTheLocalMixerCard() {
        HomeActivity.bootShown = true
        val a = Robolectric.buildActivity(HomeActivity::class.java).setup().resume().get(); val root = a.window.decorView
        val t = texts(root)
        assertTrue(t.contains("THIS PHONE · LOCAL MIXER")); assertTrue(t.contains("OPEN THE MIXER")); assertTrue(t.contains("QUICK CHAINS FOR CH 1")); assertTrue(t.contains("MORE ON THIS PHONE"))
        val chains = find(root, "phoneChains") as ViewGroup; assertEquals(LocalChains.chains.size, chains.childCount)
        assertEquals(5, (find(root, "phoneMore") as ViewGroup).childCount)
        // a chain from HOME lands in CH 1's saved rack, and the card says which chain is on
        chains.getChildAt(0).performClick()
        val p = Prefs.local(a); assertEquals("clean|rider|deesser|", p.getString(LocalMixerActivity.rackKey(0), "")); assertEquals("voice", LocalChains.current(p, 0))
        assertTrue((find(a.window.decorView, "phoneStatus") as TextView).text.contains("VOICE"))
        // a rack changed by hand is no longer "the chain"
        p.edit().putString(LocalMixerActivity.rackKey(0), "clean|hall||").commit(); assertEquals("", LocalChains.current(p, 0))
        // OPENS AT START is the standalone switch, kept
        assertTrue(!Prefs.startInPhoneMixer(a)); find(a.window.decorView, "phoneAtStart")!!.performClick(); assertTrue(Prefs.startInPhoneMixer(a))
        assertTrue(texts(a.window.decorView).contains("OPENS AT START: ON")); find(a.window.decorView, "phoneAtStart")!!.performClick(); assertTrue(!Prefs.startInPhoneMixer(a))
    }
    @Test fun thePhoneMixerHasQuickChains() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        val row = find(a.window.decorView, "chainRow") as ViewGroup; assertEquals(LocalChains.chains.size, row.childCount)
        a.applyChain(LocalChains.byId("karaoke")!!)
        val p = Prefs.local(a); assertEquals("rider|doubler|pingpong|hall", p.getString(LocalMixerActivity.rackKey(0), ""))
        assertTrue(p.getString(LocalMixerActivity.paramKey(0, 2), "")!!.contains("left=0.1351")); assertEquals("", p.getString(LocalMixerActivity.paramKey(0, 3), ""))
        a.applyChain(LocalChains.byId("clear")!!); assertEquals("|||", p.getString(LocalMixerActivity.rackKey(0), "")); assertEquals(null, p.getString(LocalMixerActivity.paramKey(0, 2), null))
    }
}
