package com.amfb.amixos

import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.View
import android.view.ViewGroup
import androidx.test.core.app.ApplicationProvider
import org.junit.After
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
import java.io.File
import java.io.FileOutputStream

/** PLAY: phone media control (MediaSession, after Notification access), the console's STREAMING controls, the new bottom bar; and the
 *  bugs fixed in this round (a damaged stored scene, the inserts page's worker thread). */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class MediaPlayTest {
    @After fun reset() { Nav.standaloneForTests = null }
    private fun find(v: View, d: String): View? { if (v.contentDescription == d) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), d)?.let { return it }; return null }
    private fun now(playing: Boolean, pos: Long = 10_000, at: Long = 1_000, dur: Long = 180_000, pkg: String = "com.spotify.music") = MediaHub.Now(pkg, "Song", "Artist", "", playing, dur, pos, at, 1f, true)

    @Test fun positionAndTimes() {
        assertEquals(15_000, MediaHub.positionNow(now(true), 6_000)); assertEquals(10_000, MediaHub.positionNow(now(false), 6_000))
        assertEquals(180_000, MediaHub.positionNow(now(true), 10_000_000)); assertEquals(0, MediaHub.positionNow(now(true, pos = -50, at = 0), 0))
        assertEquals("0:00", MediaHub.fmt(-1)); assertEquals("3:05", MediaHub.fmt(185_000)); assertEquals("1:01:01", MediaHub.fmt(3_661_000))
        assertEquals(90_000, MediaHub.seekTarget(0.5, 180_000)); assertEquals(180_000, MediaHub.seekTarget(2.0, 180_000)); assertEquals(0, MediaHub.seekTarget(0.5, 0))
    }
    @Test fun picksThePlayingSessionAndNamesTheApps() {
        assertNull(MediaHub.pick(emptyList()))
        assertEquals("deezer.android.app", MediaHub.pick(listOf(now(false), now(true, pkg = "deezer.android.app")))?.pkg)
        assertEquals("com.spotify.music", MediaHub.pick(listOf(now(false), now(false, pkg = "x")))?.pkg)
        assertEquals("TIDAL", MediaHub.appName("com.aspiro.tidal")); assertEquals("Deezer", MediaHub.appName("deezer.android.app")); assertEquals("My Player", MediaHub.appName("org.x", "My Player")); assertEquals("org.x", MediaHub.appName("org.x"))
        assertEquals(listOf("TIDAL", "Spotify", "Deezer"), MediaHub.players.map { it.name })
    }
    @Test fun consoleStreamingPage() {
        val c = MediaHub.parseConsole(mapOf("ok" to true, "console" to true, "now" to mapOf("service" to "TIDAL", "title" to "T", "artist" to "A", "playing" to true)))
        assertEquals(MediaHub.Console(true, "TIDAL", "T", "A", true), c)
        assertFalse(MediaHub.parseConsole(mapOf("ok" to true, "now" to emptyMap<String, Any?>())).shown); assertFalse(MediaHub.parseConsole(emptyMap()).playing)
        assertEquals(setOf("play", "pause", "toggle", "next", "previous"), MediaHub.consoleCmds)
        val d = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint("127.0.0.1", 1, ""))
        assertTrue(runCatching { d.streamingCmd("rm") }.exceptionOrNull() is IllegalArgumentException)   // refused before anything is sent
    }
    @Test fun withoutAccessPlayExplainsAndShowsNoSession() {
        val a = Robolectric.buildActivity(MediaActivity::class.java).setup().get(); val root = a.window.decorView
        assertFalse(MediaHub.allowed(a)); assertTrue(MediaHub.controllers(a).isEmpty())
        assertEquals(View.VISIBLE, find(root, "playAccess")!!.visibility); assertEquals(View.GONE, find(root, "playCard")!!.visibility)
        assertEquals(3, (find(root, "playApps") as ViewGroup).childCount); assertNotNull(find(root, "playWebPlayers"))
        assertTrue((find(root, "playNote") as android.widget.TextView).text.contains("recorded"))
        assertNotNull(find(root, a.getString(R.string.nav_play)))
        val dm = a.resources.displayMetrics; root.measure(View.MeasureSpec.makeMeasureSpec(dm.widthPixels, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(dm.heightPixels, View.MeasureSpec.EXACTLY)); root.layout(0, 0, dm.widthPixels, dm.heightPixels)
        val out = File(System.getProperty("amfx.shots2") ?: "build/shots2").apply { mkdirs() }
        val b = Bitmap.createBitmap(root.width, root.height, Bitmap.Config.ARGB_8888); root.draw(Canvas(b)); FileOutputStream(File(out, "play-page.png")).use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
    @Test fun theMixerAppBarHasFiveTabsAndFollowsFeatures() {
        Nav.standaloneForTests = true; val ctx = ApplicationProvider.getApplicationContext<android.content.Context>(); Features.resetAll(ctx)
        val a = Robolectric.buildActivity(SettingsActivity::class.java).setup().get(); val bar = a.window.decorView.findViewWithTag<ViewGroup>("navBar")
        assertEquals(listOf("MIX", "PLAY", "TOOLS", "DEVICES", "SETTINGS"), (0 until bar.childCount).map { bar.getChildAt(it).contentDescription.toString() })
        for (i in 0 until bar.childCount) assertTrue(bar.getChildAt(i).minimumHeight >= (48 * a.resources.displayMetrics.density).toInt())
        Features.set(ctx, "devices", false)
        val b = Robolectric.buildActivity(SettingsActivity::class.java).setup().get(); val bar2 = b.window.decorView.findViewWithTag<ViewGroup>("navBar")
        assertFalse((0 until bar2.childCount).any { bar2.getChildAt(it).contentDescription == "DEVICES" }); Features.resetAll(ctx)
    }
    @Test fun aDamagedStoredSceneIsSkipped() {
        assertNull(OfflineScenes.read(null)); assertNull(OfflineScenes.read(42)); assertNull(OfflineScenes.read("{broken")); assertNull(OfflineScenes.read("[1,2]"))
        assertEquals(5L, (OfflineScenes.read("{\"t\":5}")?.get("t") as Number).toLong())
    }
    @Test fun theInsertsPageStopsItsWorker() {
        val c = Robolectric.buildActivity(InsertsActivity::class.java).setup(); c.destroy()
        val f = InsertsActivity::class.java.getDeclaredField("io").apply { isAccessible = true }
        assertTrue((f.get(c.get()) as java.util.concurrent.ExecutorService).isShutdown)
    }
}
