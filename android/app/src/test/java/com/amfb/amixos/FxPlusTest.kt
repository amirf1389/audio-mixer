package com.amfb.amixos

import android.content.Context
import android.content.res.Configuration
import android.graphics.Bitmap
import android.graphics.Canvas
import android.os.Looper
import android.view.View
import android.view.ViewGroup
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import java.io.File
import java.io.FileOutputStream

/** FX+ : the view-model's bounds, A / B and persistence, the console ops it maps to, the panel it draws, and a rotation with it open */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class FxPlusTest {
    private val ctx: Context = ApplicationProvider.getApplicationContext()
    private fun prefs(n: String) = ctx.getSharedPreferences(n, Context.MODE_PRIVATE).also { it.edit().clear().commit() }
    private fun all(v: View, out: MutableList<View> = mutableListOf()): List<View> { out += v; if (v is ViewGroup) for (i in 0 until v.childCount) all(v.getChildAt(i), out); return out }

    @Test fun everyValueIsHeldInItsRange() {
        for (i in 0 until FxPlus.N) {
            val s = FxPlus.spec(i)
            if (s == null) { assertTrue("index $i is a switch", FxPlus.isSwitch(i)); assertEquals(1.0, FxPlus.clamp(i, 7.0), 0.0); assertEquals(0.0, FxPlus.clamp(i, Double.NaN), 0.0); continue }
            assertEquals(s.max, FxPlus.clamp(i, 1e12), 0.0); assertEquals(s.min, FxPlus.clamp(i, -1e12), 0.0); assertEquals(s.def, FxPlus.clamp(i, Double.NaN), 0.0)
            assertEquals(s.def, FxPlus.clamp(i, Double.POSITIVE_INFINITY), 0.0)
            assertTrue(s.def in s.min..s.max)
            for (k in listOf(0.0, 0.25, 0.5, 1.0)) { val v = FxPlus.fromKnob(s, k); assertTrue("$i $v", v >= s.min - 0.05 && v <= s.max + 0.05); assertEquals(k, FxPlus.toKnob(s, FxPlus.clamp(i, v)), 0.02) }
        }
        assertEquals(FxPlus.N, FxPlus.decode("garbage", false).size)
        assertEquals(FxPlus.defaults(true).toList(), FxPlus.decode(null, true).toList())
    }

    @Test fun modelKeepsABAndSurvivesANewInstance() {
        val p = prefs("fxp1"); var sent: DoubleArray? = null
        val m = FxPlusModel(p, "t:3", false) { sent = it }
        m.toggle(FxPlus.COMP); m.set(1, -999.0); m.set(2, 4.0); m.set(99, 1.0)
        assertEquals(-60.0, m.get(1), 0.0); assertEquals(1.0, sent!![0], 0.0); assertTrue(m.anyOn())
        m.copyToOther(); m.swapAB(); assertTrue(m.showingB); assertEquals(4.0, m.get(2), 0.0)
        m.set(2, 10.0); m.swapAB(); assertEquals(4.0, m.get(2), 0.0)
        val again = FxPlusModel(p, "t:3", false) {}   // what a rotation or a new panel finds
        assertFalse(again.showingB); assertEquals(4.0, again.get(2), 0.0); again.swapAB(); assertEquals(10.0, again.get(2), 0.0)
        m.reset(FxPlus.COMP); assertEquals(FxPlus.COMP.params[0].def, m.get(1), 0.0)
    }

    @Test fun mapsToTheConsoleOps() {
        val v = FxPlus.defaults(false); v[0] = 1.0; v[1] = -500.0; v[7] = 1.0; v[8] = 3.0; v[16] = 1.0; v[19] = 1e9
        val cmds = FxPlus.daemonCmds(4, v)
        assertEquals(listOf("compressor", "channelLimiter", "gate", "deesser"), cmds.map { it["op"] })
        assertTrue(cmds.all { it["strip"] == 4 })
        assertEquals(-60.0, cmds[0]["thresholdDb"]); assertEquals(true, cmds[0]["on"]); assertEquals(0.0, cmds[1]["ceilingDb"]); assertEquals(16000.0, cmds[3]["freqHz"]); assertEquals(false, cmds[2]["on"])
        val ms = FxPlus.daemonCmds(-1, FxPlus.defaults(true).also { it[0] = 1.0; it[1] = -55.0 })
        assertEquals(listOf("limiter", "mastering"), ms.map { it["op"] }); assertEquals(true, ms[0]["on"]); assertEquals(-40.0, ms[1]["thr"]); assertEquals(true, ms[1]["on"])
        assertFalse(FxPlus.daemonCmds(-1, FxPlus.defaults(true))[1].containsKey("on"))   // the user's mastering is never switched off from here
        val gr = FxPlus.grFromMeters(mapOf("strips" to listOf(mapOf("gr" to -4.5, "gate" to -80.0)), "master" to mapOf("limGr" to -2.0, "compGr" to 5.0)), 0)
        assertEquals(-4.5, gr[0], 0.0); assertEquals(-60.0, gr[2], 0.0)
        val mg = FxPlus.grFromMeters(mapOf("master" to mapOf("limGr" to -2.0, "compGr" to 5.0)), -1); assertEquals(0.0, mg[0], 0.0); assertEquals(-2.0, mg[1], 0.0)
        assertEquals(0.0, FxPlus.grFromMeters(emptyMap(), 7)[0], 0.0)
    }

    @Test fun thePanelDrawsEveryModuleAndTheKnobsMoveTheModel() {
        val m = FxPlusModel(prefs("fxp2"), "t:0", false) {}
        val v = FxPlusPanel.view(ctx, "FX+ · CH 1", m, {})
        val views = all(v)
        for (id in listOf("comp", "limit", "gate", "deess")) assertNotNull(id, views.firstOrNull { it.tag == "fxp-face:$id" })
        assertEquals(16, views.count { (it.tag as? String)?.startsWith("fxp:") == true })
        val knob = views.first { it.tag == "fxp:comp:2" } as RotaryKnob; knob.onChange?.invoke(1f); assertEquals(20.0, m.get(2), 0.0)
        views.first { it.contentDescription == "fxpPower:limit" }.performClick(); assertEquals(1.0, m.get(7), 0.0)
        val master = all(FxPlusPanel.view(ctx, "FX+ · MASTER", FxPlusModel(prefs("fxp3"), "t:-1", true) {}, null))
        assertTrue(master.none { it.tag == "fxp-face:gate" }); assertTrue(master.none { it.contentDescription == "fxpEq" })
        shot(v, "fxplus-channel.png"); shot(FxPlusPanel.view(ctx, "FX+ · MASTER", FxPlusModel(prefs("fxp3"), "t:-1", true) {}, null), "fxplus-master.png")
    }

    @Test fun theLocalMixerOpensFxPlusAndSurvivesARotation() {
        val ctl = Robolectric.buildActivity(LocalMixerActivity::class.java).setup().visible(); val a = ctl.get()
        shadowOf(Looper.getMainLooper()).idle()
        val key = all(a.window.decorView).firstOrNull { it.contentDescription == "fxPlus_0" }; assertNotNull("FX+ under CH 1's fader", key)
        assertNotNull(all(a.window.decorView).firstOrNull { it.contentDescription == "fxPlusMaster" })
        key!!.performClick(); shadowOf(Looper.getMainLooper()).idle()
        val land = Configuration(a.resources.configuration).apply { orientation = Configuration.ORIENTATION_LANDSCAPE }
        ctl.configurationChange(land); shadowOf(Looper.getMainLooper()).idle()
        a.showFxPlus(-1); shadowOf(Looper.getMainLooper()).idle()
        ctl.pause().stop().destroy()
    }

    private fun shot(v: View, name: String) {
        val dir = System.getenv("FXPLUS_SHOTS")?.takeIf { it.isNotBlank() } ?: return
        runCatching { val w = 1080; v.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED))
            val h = maxOf(1, (v as? ViewGroup)?.getChildAt(0)?.measuredHeight ?: v.measuredHeight); v.measure(View.MeasureSpec.makeMeasureSpec(w, View.MeasureSpec.EXACTLY), View.MeasureSpec.makeMeasureSpec(h, View.MeasureSpec.EXACTLY)); v.layout(0, 0, w, h)
            val b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888); v.draw(Canvas(b)); File(dir).mkdirs(); FileOutputStream(File(dir, name)).use { b.compress(Bitmap.CompressFormat.PNG, 100, it) } }
    }
}
