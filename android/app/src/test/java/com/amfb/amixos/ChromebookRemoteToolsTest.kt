package com.amfb.amixos

import android.graphics.Bitmap
import android.graphics.Canvas
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.SeekBar
import android.widget.TextView
import com.amfb.amixos.remote.ConsoleState
import com.amfb.amixos.remote.DcaState
import com.amfb.amixos.remote.StripState
import com.amfb.amixos.remote.Tone
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.io.FileOutputStream

/** The remote's TOOLS (GROUP FADERS, 360 AUDIO, ADD EFFECTS) and CHROMEBOOK MODE at laptop sizes (ChromeOS feature mocked). */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [34], qualifiers = "w1366dp-h768dp-land-mdpi")
class ChromebookRemoteToolsTest {
    private fun find(v: View, t: String): View? { if (v.tag == t || v.contentDescription == t) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), t)?.let { return it }; return null }
    private fun texts(v: View, out: MutableList<String> = mutableListOf()): List<String> { if (v is TextView) out += v.text.toString(); if (v is ViewGroup) for (i in 0 until v.childCount) texts(v.getChildAt(i), out); return out }
    private val shots = File(File(System.getProperty("amfx.shots2") ?: "build/shots2"), "chromebook").apply { mkdirs() }
    private fun shot(v: View, w: Int, h: Int, name: String) {
        v.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY)); v.layout(0, 0, w, h)
        val b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888); v.draw(Canvas(b)); FileOutputStream(File(shots, "$name.png")).use { b.compress(Bitmap.CompressFormat.PNG, 100, it) }
    }
    private fun chromeOs() = shadowOf(RuntimeEnvironment.getApplication().packageManager).setSystemFeature("org.chromium.arc", true)
    private fun strip(i: Int, n: String) = StripState(i, n, 0.0, 0.0, false, false, 0.0, Tone(0.0, 0.0, 1000.0, 0.0, true), emptyList())
    private fun state(limit: Int) = ConsoleState(listOf(strip(0, "VOX"), strip(1, "GTR"), strip(2, "KEYS")), emptyList(), -6.0, true, -0.5,
        dcas = (1..8).map { DcaState(it, "DCA $it", -3.0, false, 0, if (it == 1) listOf(0, 1) else emptyList(), winVol = it == 2, locked = it > limit) }, dcaLimit = limit)
    @Before fun before() { Nav.standaloneForTests = false }
    @After fun after() { Nav.standaloneForTests = null }

    @Test fun groupFadersFollowTheEditionAndTheWindowsVolumeSwitch() {
        assertEquals(1, RemoteTools.dcaLimitForPlan("BASIC")); assertEquals(4, RemoteTools.dcaLimitForPlan("PRO")); assertEquals(8, RemoteTools.dcaLimitForPlan("Studio")); assertEquals(8, RemoteTools.dcaLimitForPlan(""))
        val s = state(4); assertEquals(4, RemoteTools.dcaLimit(s, "PRO")); assertEquals(1, RemoteTools.dcaLimit(s, "BASIC")); assertEquals(listOf(1, 2, 3, 4), RemoteTools.usable(s, "PRO").map { it.id })
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        val sent = mutableListOf<Triple<Int, Double?, Boolean?>>()
        val p = RemoteTools.dcaPanel(a, s, "PRO") { g, db, m -> sent += Triple(g, db, m) }
        for (g in 1..4) assertNotNull("fader $g", find(p, "dcaPanelFader$g"))
        for (g in 5..8) { assertNull(find(p, "dcaPanelFader$g")); assertNotNull(find(p, "dcaLocked$g")) }
        assertTrue((find(p, "dcaMembers1") as TextView).text.contains("VOX, GTR"))
        assertNotNull(find(p, "dcaWinVol2")); assertNull(find(p, "dcaWinVol1"))
        (find(p, "dcaPanelMute1") as android.widget.ToggleButton).performClick(); assertEquals(Triple(1, null, true), sent.last())
        val f = find(p, "dcaPanelFader3") as SeekBar; shadowOf(f).onSeekBarChangeListener.onProgressChanged(f, 540, true); assertEquals(3, sent.last().first); assertEquals(-6.0, sent.last().second!!, 1e-9)
        shot(a.dcaPage(state(8)).let { android.widget.ScrollView(a).apply { addView(it) } }, 1366, 768, "remote-dca-1366x768")
    }

    @Test fun toolsListTheirPagesAndTheEffectsUseTheInsertsFlow() {
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get()
        val btn = find(a.window.decorView, "remoteTools"); assertNotNull(btn)
        val picked = mutableListOf<RemoteTools.Tool>(); val tools = RemoteTools.toolsPanel(a) { picked += it }
        for (t in RemoteTools.Tool.values()) { find(tools, t.tag)!!.performClick() }; assertEquals(RemoteTools.Tool.values().toList(), picked)
        a.openTool(RemoteTools.Tool.EFFECTS); assertEquals(InsertsActivity::class.java.name, shadowOf(a).nextStartedActivity.component!!.className)
        assertFalse(RemoteTools.spatialExposed(mapOf("strips" to emptyList<Any>())))
        val sp = RemoteTools.spatialPanel(a, false); assertTrue(texts(sp).any { it.contains("read-only") })
        shot(tools, 600, 520, "remote-tools")
    }

    @Test fun onChromeOsTheRemoteSitsInTheDesktopFrame() {
        chromeOs(); val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get(); val root = a.window.decorView
        assertTrue(ChromeFrame.isChromeOs(a)); assertNotNull(find(root, "chromeRail")); assertNull(find(root, "navBar"))
        for (e in listOf("rail_mixer", "rail_sound_card", "rail_network", "rail_settings", "rail_licence")) assertNotNull(e, find(root, e))
        val r = a.resources.configuration; assertEquals(1366, r.screenWidthDp); assertEquals(768, r.screenHeightDp)
        shot(root.rootView, 1366, 768, "remote-chromebook-1366x768")
    }

    @Test @Config(qualifiers = "w1920dp-h1080dp-land-mdpi") fun theMixerFillsA1080pWindow() {
        chromeOs(); Nav.standaloneForTests = true
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get(); val root = a.window.decorView
        assertNotNull(find(root, "chromeRail")); assertNull(find(root, "navBar")); assertEquals(1920, a.resources.configuration.screenWidthDp)
        shot(root.rootView, 1920, 1080, "mixer-chromebook-1920x1080")
    }

    @Test fun arrowKeysMoveTheMasterAsOnTheConsole() {
        fun k(code: Int, meta: Int = 0) = KeyEvent(0, 0, KeyEvent.ACTION_DOWN, code, 0, meta)
        assertEquals(1.0, ChromeFrame.masterStep(k(KeyEvent.KEYCODE_DPAD_UP), false)!!, 1e-9)
        assertEquals(-0.1, ChromeFrame.masterStep(k(KeyEvent.KEYCODE_DPAD_DOWN, KeyEvent.META_SHIFT_ON), false)!!, 1e-9)
        assertNull(ChromeFrame.masterStep(k(KeyEvent.KEYCODE_DPAD_UP), true)); assertNull(ChromeFrame.masterStep(k(KeyEvent.KEYCODE_DPAD_LEFT), false))
        assertNull(ChromeFrame.masterStep(k(KeyEvent.KEYCODE_DPAD_UP, KeyEvent.META_ALT_ON), false))
        assertEquals(-5.0, ChromeFrame.stepMaster(-6.0, 1.0), 1e-9); assertEquals(-59.0, ChromeFrame.stepMaster(-120.0, 1.0), 1e-9); assertEquals(10.0, ChromeFrame.stepMaster(9.5, 1.0), 1e-9); assertEquals(-120.0, ChromeFrame.stepMaster(-59.5, -1.0), 1e-9)
        chromeOs(); Nav.standaloneForTests = true
        val a = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().get()
        val before = Prefs.local(a).getFloat("masterDb", 0f).toDouble()
        assertTrue(a.dispatchKeyEvent(k(KeyEvent.KEYCODE_DPAD_DOWN))); assertEquals(before - 1.0, Prefs.local(a).getFloat("masterDb", 0f).toDouble(), 1e-6)
    }

    @Test fun theMouseWheelMovesAFader() {
        assertEquals(770, ChromeFrame.wheel(750, 1f)); assertEquals(0, ChromeFrame.wheel(5, -3f)); assertEquals(1000, ChromeFrame.wheel(995, 2f))
        val a = Robolectric.buildActivity(RemoteActivity::class.java).setup().get(); var moved = -1
        val f = FaderView(a).apply { progress = 500; onMove = { moved = it } }
        val props = MotionEvent.PointerProperties().apply { id = 0; toolType = MotionEvent.TOOL_TYPE_MOUSE }
        val coords = MotionEvent.PointerCoords().apply { setAxisValue(MotionEvent.AXIS_VSCROLL, 1f) }
        val e = MotionEvent.obtain(0, 0, MotionEvent.ACTION_SCROLL, 1, arrayOf(props), arrayOf(coords), 0, 0, 1f, 1f, 0, 0, android.view.InputDevice.SOURCE_MOUSE, 0)
        assertTrue(f.dispatchGenericMotionEvent(e)); assertEquals(520, f.progress); assertEquals(520, moved)
    }
}
