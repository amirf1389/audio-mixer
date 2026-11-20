package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

/** THE PLAYERS CHANNEL: a stereo strip on the phone mixer that sets the official player's volume and mute. It is a link, never an audio path. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34], qualifiers = "w411dp-h891dp-port-xxhdpi")
class PlayersStripTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }

    @Test fun theFaderLevelBecomesThePlayersVolume() {
        assertEquals(100, PlayersBus.percentFor(0.0)); assertEquals(100, PlayersBus.percentFor(6.0))      // a player has nothing above its own full level
        assertEquals(50, PlayersBus.percentFor(-6.0)); assertEquals(10, PlayersBus.percentFor(-20.0)); assertEquals(0, PlayersBus.percentFor(-60.0)); assertEquals(0, PlayersBus.percentFor(-120.0))
        val js = PlayersBus.volumeScript(37)
        assertTrue(js.contains("func:'setVolume',args:[37]") && js.contains("postMessage") && js.contains("event:'listening'"))
        assertTrue(PlayersBus.volumeScript(900).contains("args:[100]") && PlayersBus.volumeScript(-5).contains("args:[0]"))
        for (bad in listOf("eval(", "fetch(", "XMLHttpRequest", "captureStream", "MediaRecorder", "AudioContext")) assertFalse(bad, js.contains(bad))   // it only speaks to the player
    }
    @Test fun onlyYouTubeTakesAVolumeFromOutside() {
        assertTrue(PlayersBus.volumeWorks(StreamLinks.Service.YOUTUBE))
        for (s in listOf(StreamLinks.Service.SPOTIFY, StreamLinks.Service.TIDAL, StreamLinks.Service.DEEZER)) assertFalse(PlayersBus.volumeWorks(s))
        val yt = StreamLinks.parse("https://youtu.be/dQw4w9WgXcQ")!!; val sp = StreamLinks.parse("https://open.spotify.com/track/11dFghVXANMlKmJXsNCbNl")!!
        assertEquals(yt.url + "?enablejsapi=1&origin=https://com.amfb.amixos.mixer", StreamPlayerActivity.frameUrl(yt, "com.amfb.amixos.mixer"))
        val list = StreamLinks.parse("https://www.youtube.com/watch?v=dQw4w9WgXcQ&list=PL1234567890")!!
        assertTrue(StreamPlayerActivity.frameUrl(list, "x.y").endsWith("&enablejsapi=1&origin=https://x.y"))
        assertEquals(sp.url, StreamPlayerActivity.frameUrl(sp, "com.amfb.amixos.mixer"))                 // the other services' addresses are left as they are
    }
    @Test fun theMainAppHasNoPlayersTabSoNoPlayersStrip() {
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        assertNull(find(a.window.decorView, "playersStrip"))
    }
    @org.junit.After fun backToTheMainApp() { Nav.standaloneForTests = null; PlayersBus.clear() }
    @org.junit.Before fun fresh() { PlayersBus.clear() }
    @Test fun theMixerAppHasThePlayersStripAndItRemembersItsLevel() {
        Nav.standaloneForTests = true
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get(); val root = a.window.decorView
        val strip = find(root, "playersStrip"); assertNotNull(strip)
        assertEquals("No link yet. Press VIDEO to pick one.", (find(root, "playersNote") as android.widget.TextView).text.toString())
        val mute = find(root, "playersMute") as PadButton; assertFalse(mute.on); mute.performClick(); assertTrue(mute.on && Prefs.local(a).getBoolean("playersMute", false))
        val f = find(root, "playersFader") as FaderView; f.onMove?.invoke((com.amfb.amixos.remote.DaemonClient.dbToPos(-12.0) * 1000).toInt())
        assertEquals(-12.0, Prefs.local(a).getFloat("playersDb", 0f).toDouble(), 0.3)
        f.onMove?.invoke(1000); assertEquals(0.0, Prefs.local(a).getFloat("playersDb", 9f).toDouble(), 1e-6)                    // the top of the fader is the player's full level, not +10
        val b = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        assertTrue((find(b.window.decorView, "playersMute") as PadButton).on)
    }
    @Test fun thePlayersPauseOnlyWhenTheWholeAppLeavesTheScreen() {
        // two screens of the app shown one after the other: started, started, stopped leaves one shown; the last stop is the app leaving
        PlayersBus.screenStarted(); PlayersBus.screenStarted(); PlayersBus.screenStopped(); PlayersBus.screenStopped(); PlayersBus.screenStopped()   // an extra stop does not go below zero
        PlayersBus.screenStarted(); PlayersBus.screenStopped()
        assertFalse(PlayersBus.loaded)
    }
    @Test fun theLinkPlaysInAMiniPlayerAboveTheFaders() {
        Nav.standaloneForTests = true
        val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        Prefs.local(ctx).edit().putString("section", "faders").putString("playerService", "YOUTUBE").putString("playerLink_YOUTUBE", "https://youtu.be/dQw4w9WgXcQ").commit()
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get(); val root = a.window.decorView
        val pane = find(root, "miniPlayer")!!; val sec = find(root, "secFaders") as ViewGroup; val desk = find(root, "desk")!!
        assertEquals(View.VISIBLE, pane.visibility); assertTrue(sec.indexOfChild(pane) < sec.indexOfChild(desk))          // the video sits above the faders, in FADERS
        assertNotNull(find(pane, "playerWeb")); assertTrue(PlayersBus.loaded)
        val h = a.resources.displayMetrics.heightPixels; assertTrue(pane.layoutParams.height in 1..(h / 4))               // at most a quarter of the screen: the faders keep the rest
        (find(root, "miniToggle") as PadButton).performClick(); assertEquals(View.GONE, pane.visibility); assertFalse(PlayersBus.loaded)   // VIDEO hides it and stops it
        (find(root, "miniToggle") as PadButton).performClick(); assertEquals(View.VISIBLE, pane.visibility)
        find(root, "sec_media")!!.performClick(); assertEquals(View.GONE, sec.visibility)                                   // only in FADERS
        Prefs.local(ctx).edit().remove("playerLink_YOUTUBE").commit()
        val b = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        b.window.decorView.let { r -> find(r, "sec_faders")!!.performClick(); assertEquals(View.GONE, find(r, "miniPlayer")!!.visibility) }   // no link: no pane
    }
    @Test fun onlyOnePlayerPlaysAtATime() {
        var closed = 0; val ctx = org.robolectric.RuntimeEnvironment.getApplication()
        val w1 = android.webkit.WebView(ctx); val w2 = android.webkit.WebView(ctx)
        PlayersBus.attach(w1, StreamLinks.Service.YOUTUBE) { closed++ }; PlayersBus.attach(w2, StreamLinks.Service.SPOTIFY) { closed += 10 }
        assertEquals(1, closed); assertEquals(StreamLinks.Service.SPOTIFY, PlayersBus.service)                          // the first was closed for the second
        PlayersBus.detach(w1); assertTrue(PlayersBus.loaded); PlayersBus.detach(w2); assertFalse(PlayersBus.loaded)
    }
}
