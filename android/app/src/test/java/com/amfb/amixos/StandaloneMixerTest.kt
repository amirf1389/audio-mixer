package com.amfb.amixos

import android.content.Intent
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config

/** THE STANDALONE PHONE MIXER: the inputs, two song decks (their cards and their channels on the desk), the master and REC on one screen;
 *  "Open the phone mixer at start" brings it up from the launcher; the deck clock reads right. (No native engine on the JVM: the decks say so.) */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class StandaloneMixerTest {
    private fun texts(v: View, out: MutableList<String> = mutableListOf()): List<String> {
        if (v is TextView) out += v.text.toString(); if (v is ViewGroup) for (i in 0 until v.childCount) texts(v.getChildAt(i), out); return out }

    @Test fun oneScreenHasInputsDecksMasterAndRec() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        val t = texts(a.window.decorView)
        assertEquals("two deck cards and two deck channels", 4, t.count { it == "DECK A" || it == "DECK B" })
        assertTrue(t.contains("MASTER")); assertTrue(t.contains("● REC")); assertTrue(t.count { it == "LOAD" } == 2); assertTrue(t.count { it == "LOOP" } == 2)
        assertTrue("the deck tells why it cannot play here", t.any { it.contains("engine") || it.contains("No song") })
    }

    @Test fun startsInPhoneMixerWhenAsked() {
        val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
        Prefs.remote(ctx).edit().putBoolean("startPhoneMixer", true).putBoolean("bootAnim", false).commit()
        val home = Robolectric.buildActivity(HomeActivity::class.java, Intent(Intent.ACTION_MAIN)).setup().get()
        assertEquals(LocalMixerActivity::class.java.name, shadowOf(home).nextStartedActivity?.component?.className)
        Prefs.remote(ctx).edit().putBoolean("startPhoneMixer", false).commit()
        val home2 = Robolectric.buildActivity(HomeActivity::class.java, Intent(Intent.ACTION_MAIN)).setup().get()
        val next = shadowOf(home2).nextStartedActivity
        assertTrue("off: HOME does not jump to the mixer", next == null || next.component?.className != LocalMixerActivity::class.java.name)
    }

    @Test fun deckClock() { assertEquals("0:00", DeckPlayer.clock(0)); assertEquals("1:05", DeckPlayer.clock(65_000_000)); assertEquals("12:00", DeckPlayer.clock(720_000_000)); assertEquals("0:00", DeckPlayer.clock(-5)) }

    /** DUAL MODE: both mixer screens carry THIS PHONE | CONSOLE; switching goes to the other screen and is remembered for the MIXER tab */
    @Test fun dualModeSwitchesAndRemembers() {
        val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
        val phone = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        assertTrue(texts(phone.window.decorView).containsAll(listOf("THIS PHONE", "CONSOLE (PC)")))
        assertEquals("phone", Prefs.remote(ctx).getString("mixerMode", ""))
        assertEquals(LocalMixerActivity::class.java, Nav.mixerTarget(phone))
        phone.window.decorView.findViewWithTag<View>("mode_console").performClick()
        assertEquals(RemoteActivity::class.java.name, shadowOf(phone).nextStartedActivity?.component?.className)
        assertEquals("console", Prefs.remote(ctx).getString("mixerMode", ""))
        val remote = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        assertTrue(texts(remote.window.decorView).containsAll(listOf("THIS PHONE", "CONSOLE (PC)")))
        assertEquals(RemoteActivity::class.java, Nav.mixerTarget(remote))
        remote.window.decorView.findViewWithTag<View>("mode_phone").performClick()
        assertEquals(LocalMixerActivity::class.java.name, shadowOf(remote).nextStartedActivity?.component?.className)
    }
}
