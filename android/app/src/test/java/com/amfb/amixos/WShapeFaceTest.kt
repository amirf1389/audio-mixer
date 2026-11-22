package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import android.os.Looper

/** W-SHAPE TUNER on the phone: same parameters as the console, the curve points, A / B in the console's format, and the ear training's no-giveaway rule */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [34])
class WShapeFaceTest {
    private fun defaults() = doubleArrayOf(0.6, 0.55, 0.4, 0.4, 0.4, 0.5, 0.6, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.0, 1.0, 0.0, 0.0, 0.0)

    @Test fun bandsAndPointsMatchTheEngine() {
        assertEquals(90.0, WShape.bandHz(0, 0.5), 1e-9); assertEquals(40.0, WShape.bandHz(0, 0.0), 1e-9); assertEquals(16000.0, WShape.bandHz(4, 1.0), 1e-6)
        for (b in 0 until 5) assertEquals(0.3, WShape.bandKnob(b, WShape.bandHz(b, 0.3)), 1e-9)
        val k = defaults(); val pts = WShape.points(k)
        assertEquals(5, pts.size); assertEquals(0.6 * 0.55 * 9, pts[0].db, 1e-9); assertEquals(-0.6 * 0.4 * 6, pts[1].db, 1e-9)
        val d = WShape.drag(k, 2, 3000.0, 2.0); assertEquals(WShape.bandKnob(2, 3000.0), d[10]!!, 1e-9); assertEquals(2.0 / (0.6 * 6), d[3]!!, 1e-9)
        assertEquals(1000.0, WShape.bandHz(2, WShape.drag(k, 2, 200.0, 0.0)[10]!!), 1e-6)   // a point stays within its band's range
        assertEquals(18, WShape.IDS.size); assertEquals("train", WShape.IDS[WShape.TRAIN]); assertEquals("learn", WShape.IDS[WShape.LEARN])
    }

    @Test fun randomQuestionDoesNotDrawTheBoostUntilAnswered() {
        val k = defaults(); val f = WShape.bandHz(2, k[10]); val before = WShape.curveDb(k, f, true)
        val t = WShape.Trainer(); t.quiz(3); k[WShape.TRAIN] = t.trainValue()
        assertEquals(3, WShape.trainBand(k[WShape.TRAIN])); assertFalse(t.showBoost)
        assertEquals(before, WShape.curveDb(k, f, t.showBoost), 1e-9)          // open question: no giveaway
        assertTrue(t.answer(3)); assertTrue(t.showBoost); assertTrue(WShape.curveDb(k, f, t.showBoost) - before > 3)   // answered: the boost is drawn
        assertFalse(t.answer(1)); assertEquals(1, t.score); assertEquals(1, t.total)   // one answer per question
        t.quiz(5); t.answer(1); assertEquals(1, t.score); assertEquals(2, t.total); assertFalse(t.lastRight)
        val r = WShape.Trainer(); r.raw(); assertTrue(r.showBoost); assertEquals(0.2, r.trainValue(), 1e-9)   // 4 RAW EQ may show it
        repeat(4) { assertTrue(r.rawNext()) }; assertFalse(r.rawNext()); assertEquals(0, r.rawStep)
    }

    @Test fun abUsesTheConsoleFormat() {
        val a = DoubleArray(19) { it / 20.0 }; val b = DoubleArray(19) { 0.5 }
        val s = WShape.encodeAb(a, b, "B"); assertTrue(s.startsWith("A,0.0000,0.0500")); assertTrue(s.contains("|B,0.5000")); assertTrue(s.endsWith("|B"))
        val d = WShape.decodeAb(s); assertEquals("B", d.side); assertEquals(19, d.a!!.size); assertEquals(0.05, d.a!![1], 1e-9); assertEquals(0.5, d.b!![18], 1e-9)
        val e = WShape.decodeAb("A,|B,|A"); assertEquals(null, e.a); assertEquals("A", e.side)
    }

    private fun find(v: View, tag: String): View? { if (v.tag == tag) return v; if (v is ViewGroup) for (i in 0 until v.childCount) find(v.getChildAt(i), tag)?.let { return it }; return null }

    @Test fun theFaceHasEveryTool() {
        val ctx = ApplicationProvider.getApplicationContext<android.content.Context>()
        val sets = LinkedHashMap<String, Double>()
        val params = WShape.IDS.mapIndexed { i, id -> mapOf<String, Any?>("id" to id, "title" to id.uppercase(), "value" to defaults()[i], "default" to defaults()[i], "text" to "") }
        val face = PluginFace.build(ctx, mapOf("kind" to "wshape", "name" to "W-SHAPE TUNER", "bypass" to false, "mix" to 1.0, "params" to params, "uiKey" to "test"), 0, object : PluginFace.Host {
            override fun set(id: String, value: Double) { sets[id] = value }; override fun remove() {}; override fun changed() {} })
        assertNotNull(find(face, "wshape-face"))
        for (t in listOf("ws-a", "ws-b", "ws-compare", "ws-learn", "ws-st0", "ws-st1", "ws-st2", "ws-presets", "ws-raw", "ws-quiz", "ws-stop", "ws-answer1", "ws-answer5")) assertNotNull(t, find(face, t))
        find(face, "ws-st2")!!.performClick(); assertEquals(1.0, sets["stereo"]!!, 0.0)
        find(face, "ws-raw")!!.performClick(); assertEquals(0.2, sets["train"]!!, 1e-9)
        find(face, "ws-stop")!!.performClick(); assertEquals(0.0, sets["train"]!!, 0.0)
        find(face, "ws-quiz")!!.performClick(); assertTrue(sets["train"]!! > 0)
        find(face, "ws-learn")!!.performClick(); assertEquals(1.0, sets["learn"]!!, 0.0)
        find(face, "ws-b")!!.performClick(); find(face, "ws-a")!!.performClick()
        shadowOf(Looper.getMainLooper()).idle()
        assertTrue(ctx.getSharedPreferences("wshape_ui", 0).getString("ab:test", "")!!.endsWith("|A"))
    }
}
