package com.amfb.amixos

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import com.amfb.amixos.StreamLinks.Service

/** PLAYERS and the separate AmF X1 MIXER app: shared links become the services' own embed pages and nothing else; the standalone bar. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class StreamPlayersTest {
    private fun url(s: String) = StreamLinks.parse(s)?.url
    @Test fun linksBecomeOfficialEmbeds() {
        assertEquals("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ", url("https://youtu.be/dQw4w9WgXcQ?si=abc"))
        assertEquals("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ", url("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=10s"))
        assertEquals("https://www.youtube-nocookie.com/embed/dQw4w9WgXcQ?list=PL123", url("https://music.youtube.com/watch?v=dQw4w9WgXcQ&list=PL123"))
        assertEquals("https://www.youtube-nocookie.com/embed/videoseries?list=PL123", url("youtube.com/playlist?list=PL123"))
        assertEquals("https://www.youtube-nocookie.com/embed/abc_DEF-123", url("https://m.youtube.com/shorts/abc_DEF-123"))
        assertEquals("https://open.spotify.com/embed/track/4uLU6hMCjMI75M1A2tKUQC", url("https://open.spotify.com/intl-de/track/4uLU6hMCjMI75M1A2tKUQC?si=x"))
        assertEquals("https://open.spotify.com/embed/playlist/37i9dQZF1DXcBWIGoYBM5M", url("https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M"))
        assertEquals("https://embed.tidal.com/tracks/77646168", url("https://tidal.com/browse/track/77646168"))
        assertEquals("https://embed.tidal.com/albums/77646164", url("https://listen.tidal.com/album/77646164"))
        assertEquals("https://widget.deezer.com/widget/dark/track/3135556", url("https://www.deezer.com/en/track/3135556"))
        assertEquals("https://widget.deezer.com/widget/dark/playlist/908622995", url("https://deezer.com/playlist/908622995?utm=1"))
        assertEquals(Service.SPOTIFY, StreamLinks.parse("https://open.spotify.com/album/1DFixLWuPkv3KT3TnV35m3")!!.service)
    }
    @Test fun anythingElseIsRefused() {
        for (bad in listOf("", "hello", "https://example.com/watch?v=dQw4w9WgXcQ", "https://youtube.com.evil.example/watch?v=a", "https://evil.example/?u=https://youtu.be/abc",
            "https://youtu.be/<script>", "https://open.spotify.com/track/../../x", "https://open.spotify.com/user/abc", "https://tidal.com/browse/artist/1", "javascript:alert(1)",
            "https://user@youtu.be/abc", "https://www.youtube.com/watch?v=a\"onload=\"x", "https://deezer.com/en/track/", "x".repeat(3000))) assertNull(bad.take(60), StreamLinks.parse(bad))
    }
    @Test fun thePlayerPageStaysOnItsOwnService() {
        assertTrue(StreamLinks.allowed(Service.YOUTUBE, "https://www.youtube-nocookie.com/embed/abc")); assertTrue(StreamLinks.allowed(Service.SPOTIFY, "https://open.spotify.com/embed/track/1"))
        assertTrue(StreamLinks.allowed(Service.TIDAL, "https://embed.tidal.com/tracks/1")); assertTrue(StreamLinks.allowed(Service.DEEZER, "https://widget.deezer.com/widget/dark/track/1"))
        assertFalse(StreamLinks.allowed(Service.SPOTIFY, "https://www.youtube.com/watch?v=a")); assertFalse(StreamLinks.allowed(Service.YOUTUBE, "http://www.youtube.com/"))
        assertFalse(StreamLinks.allowed(Service.YOUTUBE, "https://youtube.com.evil.example/")); assertFalse(StreamLinks.allowed(Service.DEEZER, "https://evildeezer.com/")); assertFalse(StreamLinks.allowed(Service.TIDAL, "intent://tidal.com/x"))
        assertFalse(StreamLinks.allowed(Service.TIDAL, "file:///sdcard/a")); assertFalse(StreamLinks.allowed(Service.SPOTIFY, "https://spotify.com@evil.example/"))
    }
    @Test fun theSeparateMixerAppHasItsOwnBar() {
        assertTrue(Nav.isStandalonePackage("com.amfb.amixos.mixer")); assertFalse(Nav.isStandalonePackage("com.amfb.amixos"))
        assertEquals(listOf(Nav.Tab.MIXER, Nav.Tab.PLAYERS, Nav.Tab.TOOLS, Nav.Tab.SETTINGS), Nav.tabs(true))
        assertEquals(listOf(Nav.Tab.HOME, Nav.Tab.MIXER, Nav.Tab.TOOLS, Nav.Tab.SETTINGS), Nav.tabs(false))   // AmF X1 itself is unchanged
    }
    @Test fun thePlayersScreenOpensALinkAndSwitchesService() {
        val a = Robolectric.buildActivity(StreamPlayerActivity::class.java).setup().get()
        fun find(v: android.view.View, d: String): android.view.View? { if (v.contentDescription == d) return v; if (v is android.view.ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), d)?.let { return it }; return null }
        val root = a.window.decorView
        assertEquals(4, (find(root, "playerServices") as android.view.ViewGroup).childCount)
        assertTrue((find(root, "playerNote") as android.widget.TextView).text.contains("recorded"))
        assertNull(a.open("https://example.com/x")); assertTrue((find(root, "playerStatus") as android.widget.TextView).text.contains("not a"))
        assertEquals("https://embed.tidal.com/tracks/77646168", a.open("https://tidal.com/browse/track/77646168"))
        assertEquals("TIDAL", Prefs.local(a).getString("playerService", "")); assertTrue((find(root, "playerStatus") as android.widget.TextView).text.contains("TIDAL"))
        assertEquals("https://tidal.com/browse/track/77646168", Prefs.local(a).getString("playerLink_TIDAL", ""))
    }
}
