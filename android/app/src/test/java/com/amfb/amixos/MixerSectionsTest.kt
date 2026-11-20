package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** THE PHONE MIXER'S SECTIONS: FADERS, MEDIA and SETUP, one at a time, upright and on its side. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class MixerSectionsTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
    private fun faders(v: View, out: MutableList<FaderView> = mutableListOf()): List<FaderView> { if (v is FaderView && v.isShown) out += v; if (v is ViewGroup) for (i in 0 until v.childCount) faders(v.getChildAt(i), out); return out }
    private fun shown(a: LocalMixerActivity) = listOf("secFaders", "secMedia", "secSetup").filter { find(a.window.decorView, it)!!.visibility == View.VISIBLE }

    @Test fun theFadersHaveTheScreenAndMediaAndSetupAreTheirOwnSections() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get(); val root = a.window.decorView
        assertEquals(listOf("secFaders"), shown(a))                                              // it opens on the faders
        val media = find(root, "secMedia") as ViewGroup; val setup = find(root, "secSetup") as ViewGroup; val desk = find(root, "secFaders") as ViewGroup
        assertNotNull(find(media, "deckBox")); assertNotNull(find(media, "localVideo")); assertNull(find(desk, "deckBox"))     // decks and picture: MEDIA only
        assertNotNull(find(setup, "chainRow")); assertNull(find(desk, "chainRow"))                                           // input, output, chains: SETUP only
        find(root, "sec_media")!!.performClick(); assertEquals(listOf("secMedia"), shown(a))
        find(root, "sec_setup")!!.performClick(); assertEquals(listOf("secSetup"), shown(a))
        val again = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get(); assertEquals(listOf("secSetup"), shown(again))   // the section is remembered
        find(again.window.decorView, "sec_faders")!!.performClick(); assertEquals(listOf("secFaders"), shown(again))
        org.robolectric.shadows.ShadowLooper.idleMainLooper(); again.window.decorView.let { it.measure(View.MeasureSpec.makeMeasureSpec(it.width, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(it.height, View.MeasureSpec.EXACTLY)); it.layout(0, 0, it.width, it.height) }
        // upright: the faders are taller than half the screen (the decks and the setup no longer stand above them)
        val f = faders(again.window.decorView); assertTrue(f.isNotEmpty())
        assertTrue("fader ${f[0].height} of ${again.resources.displayMetrics.heightPixels}", f[0].height > again.resources.displayMetrics.heightPixels * 0.5)
    }

    @Test @Config(qualifiers = "w891dp-h411dp-land-xxhdpi") fun onItsSideSixChannelsStandAcrossAndTheFadersUseTheHeight() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get(); val root = a.window.decorView
        assertEquals(6, RemoteActivity.acrossFor(true)); assertEquals(4, RemoteActivity.acrossFor(false))
        assertEquals(listOf("secFaders"), shown(a))
        val w = a.resources.displayMetrics.widthPixels; val h = a.resources.displayMetrics.heightPixels; assertTrue(w > h)
        val strip = RemoteActivity.stripWidthFor(w - 88, 6)
        assertTrue(strip >= RemoteActivity.stripWidthFor(1233, 4))                               // on its side each fader is at least as wide as upright
        assertTrue(6 * (strip + 8) + 340 + 8 + 40 <= w)                                          // six strips and the wider master fit the screen
        val f = faders(root); assertTrue(f.size >= 6)
        assertTrue("fader ${f[0].height} of $h", f[0].height > h * 0.50)                         // and takes more than half of the short height
        val bar = find(root, "sectionBar")!!; val start = find(root, "sec_faders")!!
        assertTrue(bar.isShown && start.isShown)
        find(root, "sec_media")!!.performClick(); assertEquals(listOf("secMedia"), shown(a))
    }

    @Test fun turningThePhoneKeepsTheMixerAndLaysItOutAgain() {
        val c = Robolectric.buildActivity(LocalMixerActivity::class.java).setup(); val a = c.get()
        find(a.window.decorView, "sec_media")!!.performClick()
        org.robolectric.RuntimeEnvironment.setQualifiers("+land"); c.configurationChange()
        assertTrue(a === c.get() && !a.isFinishing)                                              // the same screen: nothing was restarted
        assertEquals(android.content.res.Configuration.ORIENTATION_LANDSCAPE, a.resources.configuration.orientation)
        assertEquals(listOf("secMedia"), shown(a))                                               // still on the section it was on
    }

    @Test @Config(qualifiers = "w891dp-h411dp-land-xxhdpi") fun theRemoteOnItsSideDropsTheHintAndShowsSixAcross() {
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        assertEquals(View.GONE, find(a.window.decorView, "remoteHint")!!.visibility)
    }
    @Test fun theRemoteUprightKeepsTheHint() {
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        assertEquals(View.VISIBLE, find(a.window.decorView, "remoteHint")!!.visibility)
    }
}
