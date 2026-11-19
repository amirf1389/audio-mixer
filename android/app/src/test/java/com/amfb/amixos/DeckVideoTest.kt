package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** A VIDEO ON A DECK: the picture follows the deck's sound, and the pane is there but hidden until a video is loaded. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-xxhdpi")
class DeckVideoTest {
    @Test fun thePictureFollowsTheSound() {
        assertEquals(DeckVideo.Step.START, DeckVideo.step(true, false, 5000, 0))
        assertEquals(DeckVideo.Step.PAUSE, DeckVideo.step(false, true, 5000, 5000))
        assertEquals(DeckVideo.Step.NONE, DeckVideo.step(true, true, 5000, 5100))     // a little apart: left alone, no stutter
        assertEquals(DeckVideo.Step.SEEK, DeckVideo.step(true, true, 5000, 5400))     // drifted: pulled back in step
        assertEquals(DeckVideo.Step.SEEK, DeckVideo.step(false, false, 9000, 0))      // the deck was moved while paused
        assertEquals(DeckVideo.Step.NONE, DeckVideo.step(false, false, 0, 0))
    }
    @Test fun thePaneKeepsTheVideosShape() {
        assertEquals(1080, DeckVideo.heightFor(1920, 1920, 1080))
        assertEquals(675, DeckVideo.heightFor(1200, 0, 0))                            // not known yet: 16:9
        assertEquals(1600, DeckVideo.heightFor(900, 1080, 1920))                      // an upright phone video
    }
    @Test fun thePaneIsHiddenUntilAVideoIsLoaded() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        fun find(v: android.view.View, t: String): android.view.View? { if (v.tag == t || v.contentDescription == t) return v; if (v is android.view.ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
        val pane = find(a.window.decorView, "localVideo"); assertNotNull(pane); assertEquals(android.view.View.GONE, pane!!.visibility)
        val box = find(a.window.decorView, "deckBox")!!; assertEquals(android.view.View.GONE, box.visibility)            // BIG FADERS: the deck cards start folded away
        find(a.window.decorView, "decksToggle")!!.performClick(); assertEquals(android.view.View.VISIBLE, box.visibility)
        val b = find(a.window.decorView, "deckVideo0"); assertNotNull(b); assertEquals(android.view.View.GONE, b!!.visibility)   // no video on the deck: no VIDEO button
    }
}
