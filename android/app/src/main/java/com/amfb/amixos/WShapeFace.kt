package com.amfb.amixos

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/** W-SHAPE TUNER on the phone: the same controls as the console's editor, on the same engine parameters (fx_more.cpp kW):
 *  the curve with five draggable points (x = the band's frequency within its range, y = its gain), AUTO W (LEARN) with progress,
 *  A / B slots and COMPARE (kept in the console's "ui=" format), the ear training (4 RAW EQ and 4 RANDOM EQ with a score; the hidden boost is not
 *  drawn until the question is answered), STEREO / MID / SIDE and the nine factory presets. */
object WShape {
    /** the engine's parameter ids in order (index = the console's parameter number) */
    val IDS = listOf("amount", "bass", "lowmid", "presence", "upper", "air", "smooth", "output", "fbass", "flowmid", "fpresence", "fupper", "fair", "loudcomp", "gainmatch", "stereo", "train", "learn")
    const val TRAIN = 16; const val LEARN = 17; const val STEREO = 15; const val LEARN_SECONDS = 8.0
    val NAMES = listOf("BASS", "LOW-MID", "PRESENCE", "UPPER", "AIR")
    private val RANGE = arrayOf(doubleArrayOf(40.0, 90.0, 200.0), doubleArrayOf(200.0, 350.0, 800.0), doubleArrayOf(1000.0, 1800.0, 4000.0), doubleArrayOf(3000.0, 5000.0, 8000.0), doubleArrayOf(6000.0, 10000.0, 16000.0))
    /** each band's gain knob and the most it does (dB, a dip is negative) */
    private val GAIN = arrayOf(intArrayOf(1, 9), intArrayOf(2, -6), intArrayOf(3, 6), intArrayOf(4, -6), intArrayOf(5, 9))
    fun range(b: Int) = RANGE[b]
    fun bandHz(b: Int, v: Double): Double { val r = RANGE[b]; val x = v.coerceIn(0.0, 1.0); return if (x < 0.5) r[1] * (r[1] / r[0]).pow(2 * x - 1) else r[1] * (r[2] / r[1]).pow(2 * x - 1) }
    fun bandKnob(b: Int, hz: Double): Double { val r = RANGE[b]; val f = hz.coerceIn(r[0], r[2]); return if (f < r[1]) 0.5 + 0.5 * ln(f / r[1]) / ln(r[1] / r[0]) else 0.5 + 0.5 * ln(f / r[1]) / ln(r[2] / r[1]) }
    data class Point(val band: Int, val hz: Double, val db: Double, val gainIndex: Int, val maxDb: Int)
    fun points(k: DoubleArray): List<Point> = (0 until 5).map { b -> Point(b, bandHz(b, k[8 + b]), k[0] * k[GAIN[b][0]] * GAIN[b][1], GAIN[b][0], GAIN[b][1]) }
    /** dragging a band's point: its FREQ knob and its gain knob (index to value) */
    fun drag(k: DoubleArray, b: Int, hz: Double, db: Double): Map<Int, Double> { val a = max(0.05, k[0]); val g = GAIN[b]
        return mapOf(8 + b to bandKnob(b, hz), g[0] to (db / (a * g[1])).coerceIn(0.0, 1.0)) }
    fun trainBand(v: Double) = ((v.coerceIn(0.0, 1.0)) * 5 + 0.5).toInt()
    fun stereoOf(v: Double) = if (v < 0.25) 0 else if (v < 0.75) 1 else 2

    // ---- the curve: the same filters the engine runs (RBJ biquads at 48 kHz), GAIN MATCH and OUTPUT; the trainer's boost only when shown
    private fun biq(t: String, f0: Double, q: Double, g: Double): DoubleArray { val fs = 48000.0; val A = 10.0.pow(g / 40); val w = 2 * PI * min(f0, fs * 0.45) / fs; val c = cos(w); val sn = sin(w); val al = sn / (2 * max(q, 0.1))
        val b0: Double; val b1: Double; val b2: Double; val a0: Double; val a1: Double; val a2: Double
        if (t == "peak") { b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A }
        else { val r = 2 * sqrt(A) * al; val s = if (t == "ls") 1 else -1
            b0 = A * ((A + 1) - s * (A - 1) * c + r); b1 = 2 * s * A * ((A - 1) - s * (A + 1) * c); b2 = A * ((A + 1) - s * (A - 1) * c - r); a0 = (A + 1) + s * (A - 1) * c + r; a1 = -2 * s * ((A - 1) + s * (A + 1) * c); a2 = (A + 1) + s * (A - 1) * c - r }
        return doubleArrayOf(b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0) }
    private fun mag(k: DoubleArray, f: Double): Double { val w = 2 * PI * f / 48000; val cr = cos(w); val ci = -sin(w); val c2r = cos(2 * w); val c2i = -sin(2 * w)
        val nr = k[0] + k[1] * cr + k[2] * c2r; val ni = k[1] * ci + k[2] * c2i; val dr = 1 + k[3] * cr + k[4] * c2r; val di = k[3] * ci + k[4] * c2i; return 10 * log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-12) }
    /** the response in dB at f; showTrain = false leaves out the ear trainer's boost (a 4 RANDOM EQ question that is still open) */
    fun curveDb(k: DoubleArray, f: Double, showTrain: Boolean): Double {
        val a = k[0]; val q = 1.4 - k[6] * 0.9; fun F(b: Int) = bandHz(b, k[8 + b])
        val ks = mutableListOf(biq("ls", F(0), 0.6, a * k[1] * 9), biq("peak", F(1), q, -a * k[2] * 6), biq("peak", F(2), q, a * k[3] * 6), biq("peak", F(3), q * 1.2, -a * k[4] * 6), biq("hs", F(4), 0.6, a * k[5] * 9))
        var sum = 0.0; var n = 0; var x = 40.0; while (x <= 16000) { var d = 0.0; for (c in ks) d += mag(c, x); sum += 10.0.pow(d / 10); n++; x *= 2.0.pow(1.0 / 3) }
        val comp = (if (k[14] >= 0.5) -10 * log10(sum / n) else 0.0) + (k[7] - 0.5) * 24
        val tr = if (showTrain) trainBand(k[TRAIN]) else 0
        if (tr > 0) ks.add(biq(if (tr == 1) "ls" else if (tr == 5) "hs" else "peak", F(tr - 1), if (tr == 1 || tr == 5) 0.6 else 1.0, 6.0))
        var d = comp; for (c in ks) d += mag(c, f); return d }

    /** the ear training. 4 RAW EQ: every band boosted in turn; 4 RANDOM EQ: a hidden band is boosted and named, scored */
    class Trainer {
        var rawStep = 0; private set
        var quizBand = 0; private set
        var revealed = false; private set
        var score = 0; private set
        var total = 0; private set
        var lastRight = false; private set
        var lastBand = 0; private set
        /** the curve may show the boost: not while a 4 RANDOM EQ question is open */
        val showBoost get() = !(quizBand > 0 && !revealed)
        fun trainValue() = (if (rawStep > 0) rawStep else quizBand) / 5.0
        fun raw() { quizBand = 0; revealed = false; rawStep = 1 }
        /** the next band of 4 RAW EQ; false when the round is over */
        fun rawNext(): Boolean { if (rawStep >= 5) { stop(); return false }; rawStep++; return true }
        fun quiz(pick: Int = 0, rnd: java.util.Random = java.util.Random()) { rawStep = 0; revealed = false; quizBand = if (pick in 1..5) pick else 1 + rnd.nextInt(5) }
        fun answer(b: Int): Boolean { if (quizBand <= 0 || revealed) return false; total++; lastBand = quizBand; lastRight = b == quizBand; if (lastRight) score++; revealed = true; return true }
        fun stop() { rawStep = 0; quizBand = 0; revealed = false }
    }

    // ---- A / B: the console's format, "A,v0,..,v18|B,...|A" (the 18 controls, then MIX)
    fun encodeAb(a: DoubleArray?, b: DoubleArray?, side: String): String { fun e(x: DoubleArray?) = x?.joinToString(",") { String.format(java.util.Locale.US, "%.4f", it) } ?: ""
        return "A," + e(a) + "|B," + e(b) + "|" + (if (side == "B") "B" else "A") }
    data class Ab(val a: DoubleArray?, val b: DoubleArray?, val side: String)
    fun decodeAb(s: String?): Ab { val p = s.orEmpty().split("|"); fun d(t: String?) = t.orEmpty().split(",").drop(1).filter { it.isNotEmpty() }.mapNotNull { it.toDoubleOrNull() }.toDoubleArray().takeIf { it.isNotEmpty() }
        return Ab(d(p.getOrNull(0)), d(p.getOrNull(1)), if (p.getOrNull(2) == "B") "B" else "A") }

    /** the W-SHAPE section of a face: on top of the knobs; params are the effect's {id, value} in engine order */
    fun section(c: Context, params: List<Map<String, Any?>>, mix: Double, st: PluginFace.Style, host: PluginFace.Host, uiKey: String): View? {
        if (params.size < IDS.size) return null
        val ids = params.map { it["id"].toString() }
        val k = DoubleArray(IDS.size) { (params[it]["value"] as? Number)?.toDouble() ?: 0.0 }
        var mixV = mix
        val prefs = c.getSharedPreferences("wshape_ui", Context.MODE_PRIVATE)
        val ui = Handler(Looper.getMainLooper())
        val tr = Trainer()
        val box = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; setPadding(0, 12, 0, 0); tag = "wshape-face" }
        val curve = CurveView(c, st, k, tr) { b, hz, db -> drag(k, b, hz, db).forEach { (i, v) -> k[i] = v; host.set(ids[i], v) } }
        box.addView(curve, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 360))
        val info = TextView(c).apply { setTextColor(st.accent); textSize = 11f; typeface = Typeface.MONOSPACE; setPadding(4, 6, 4, 6); tag = "ws-info" }
        fun setK(i: Int, v: Double) { k[i] = v; host.set(ids[i], v) }
        fun row() = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 6, 0, 0) }
        fun key(parent: LinearLayout, t: String, tagName: String, f: () -> Unit) = TextView(c).apply { text = t; tag = tagName; setTextColor(st.ink); textSize = 11f; typeface = Typeface.DEFAULT_BOLD; gravity = Gravity.CENTER; minHeight = 110; setPadding(14, 0, 14, 0); maxLines = 1
            background = GradientDrawable().apply { cornerRadius = 18f; setStroke(2, Color.argb(130, Color.red(st.ink), Color.green(st.ink), Color.blue(st.ink))) }
            setOnClickListener { f() }; parent.addView(this, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(4, 0, 4, 0) }) }
        // A / B and COMPARE
        var ab = decodeAb(prefs.getString(uiKey, null)); var side = ab.side; var stA = ab.a; var stB = ab.b
        fun snap() = k.copyOf() + doubleArrayOf(mixV)
        fun restore(s: DoubleArray?) { if (s == null) return; for (i in IDS.indices) if (i < s.size && i != TRAIN && i != LEARN) setK(i, s[i]); if (s.size > IDS.size) { mixV = s[IDS.size]; host.set("mix", mixV) }; curve.invalidate() }
        fun save() { prefs.edit().putString(uiKey, encodeAb(if (side == "A") snap() else stA, if (side == "B") snap() else stB, side)).apply() }
        lateinit var refresh: () -> Unit
        fun pick(s: String) { if (s == side) return; if (side == "A") stA = snap() else stB = snap(); side = s; restore(if (s == "A") stA else stB); save(); refresh() }
        val r1 = row(); val kA = key(r1, "A", "ws-a") { pick("A") }; val kB = key(r1, "B", "ws-b") { pick("B") }
        key(r1, c.getString(R.string.ws_compare), "ws-compare") { pick(if (side == "A") "B" else "A") }
        // AUTO W (LEARN): the engine listens 8 s and sets the five gains, then puts LEARN back to 0
        var learnT = -1.0
        val kL = key(r1, c.getString(R.string.ws_learn), "ws-learn") { if (learnT >= 0) return@key; setK(LEARN, 1.0); learnT = 0.0
            val tick = object : Runnable { override fun run() { learnT += 0.25; if (learnT >= LEARN_SECONDS + 0.5) { learnT = -1.0; k[LEARN] = 0.0; host.changed(); return }; refresh(); ui.postDelayed(this, 250) } }; ui.postDelayed(tick, 250); refresh() }
        box.addView(r1)
        // STEREO / MID / SIDE
        val r2 = row(); val modes = listOf("STEREO", "MID", "SIDE").mapIndexed { i, t -> key(r2, t, "ws-st$i") { setK(STEREO, i * 0.5); refresh() } }
        key(r2, c.getString(R.string.ws_presets), "ws-presets") { val pk = PresetPack.current(c)?.effectsOf("wshape").orEmpty(); if (pk.isEmpty()) return@key
            android.app.AlertDialog.Builder(c).setTitle(R.string.fx_presets).setItems(pk.map { it.name }.toTypedArray()) { _, w -> pk[w].values.forEach { (id, v) -> val i = IDS.indexOf(id); if (i >= 0) setK(i, v) else if (id == "mix") host.set("mix", v) }; curve.invalidate(); host.changed() }.show() }
        box.addView(r2)
        // EAR TRAINING
        val r3 = row()
        val rawTick = object : Runnable { override fun run() { if (tr.rawStep == 0) return; if (tr.rawNext()) { setK(TRAIN, tr.trainValue()); ui.postDelayed(this, 2500) } else setK(TRAIN, 0.0); refresh() } }
        val kRaw = key(r3, "4 RAW EQ", "ws-raw") { if (tr.rawStep > 0) { tr.stop(); setK(TRAIN, 0.0) } else { tr.raw(); setK(TRAIN, tr.trainValue()); ui.postDelayed(rawTick, 2500) }; refresh() }
        val kQuiz = key(r3, "4 RANDOM EQ", "ws-quiz") { if (tr.quizBand > 0) { tr.stop(); setK(TRAIN, 0.0) } else { tr.quiz(); setK(TRAIN, tr.trainValue()) }; refresh() }
        key(r3, c.getString(R.string.ws_stop), "ws-stop") { tr.stop(); ui.removeCallbacks(rawTick); setK(TRAIN, 0.0); refresh() }
        box.addView(r3)
        val r4 = row()
        NAMES.forEachIndexed { i, n -> key(r4, n, "ws-answer${i + 1}") { if (tr.answer(i + 1)) { refresh(); ui.postDelayed({ if (tr.quizBand > 0) { tr.quiz(); setK(TRAIN, tr.trainValue()); refresh() } }, 1800) } }.apply { textSize = 9f } }
        box.addView(r4); box.addView(info)
        refresh = {
            kA.alpha = if (side == "A") 1f else 0.5f; kB.alpha = if (side == "B") 1f else 0.5f
            modes.forEachIndexed { i, v -> v.alpha = if (stereoOf(k[STEREO]) == i) 1f else 0.5f }
            kRaw.alpha = if (tr.rawStep > 0) 1f else 0.6f; kQuiz.alpha = if (tr.quizBand > 0) 1f else 0.6f
            kL.text = if (learnT >= 0) c.getString(R.string.ws_listening, (100 * learnT / LEARN_SECONDS).roundToInt().coerceAtMost(100)) else c.getString(R.string.ws_learn)
            r4.visibility = if (tr.quizBand > 0) View.VISIBLE else View.GONE
            info.text = when {
                tr.rawStep > 0 -> c.getString(R.string.ws_now_boosted, NAMES[tr.rawStep - 1])
                tr.quizBand > 0 && tr.revealed -> (if (tr.lastRight) c.getString(R.string.ws_right, NAMES[tr.lastBand - 1]) else c.getString(R.string.ws_it_was, NAMES[tr.lastBand - 1])) + "   " + c.getString(R.string.ws_score, tr.score, tr.total)
                tr.quizBand > 0 -> c.getString(R.string.ws_which) + "   " + c.getString(R.string.ws_score, tr.score, tr.total)
                else -> c.getString(R.string.ws_pick)
            }
            curve.invalidate()
        }
        refresh()
        box.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener { override fun onViewAttachedToWindow(v: View) {}
            override fun onViewDetachedFromWindow(v: View) { ui.removeCallbacksAndMessages(null); if (k[TRAIN] > 0) host.set(ids[TRAIN], 0.0); save() } })   // no test tone left on when the face goes
        return box
    }

    /** the W curve and its five points; drag a point: left / right its frequency (within its band), up / down its gain */
    class CurveView(c: Context, private val st: PluginFace.Style, private val k: DoubleArray, private val tr: Trainer, private val onDrag: (Int, Double, Double) -> Unit) : View(c) {
        private val p = Paint(Paint.ANTI_ALIAS_FLAG); private var held = -1
        private fun x(f: Double) = (ln(f / 20.0) / ln(20000.0 / 20.0) * width).toFloat()
        private fun f(x: Float) = 20.0 * (20000.0 / 20.0).pow((x / max(1, width)).toDouble())
        private fun y(db: Double) = (height / 2f - db / 15.0 * (height / 2f - 10)).toFloat()
        private fun db(y: Float) = (height / 2f - y) / (height / 2f - 10) * 15.0
        override fun onDraw(cv: Canvas) {
            p.style = Paint.Style.FILL; p.color = Color.parseColor("#0B0D12"); cv.drawRoundRect(0f, 0f, width.toFloat(), height.toFloat(), 18f, 18f, p)
            p.style = Paint.Style.STROKE; p.strokeWidth = 1f; p.color = Color.argb(50, 255, 255, 255); cv.drawLine(0f, height / 2f, width.toFloat(), height / 2f, p)
            for (g in listOf(100.0, 1000.0, 10000.0)) cv.drawLine(x(g), 0f, x(g), height.toFloat(), p)
            val path = Path(); var i = 0; while (i <= 120) { val fr = 20.0 * 1000.0.pow(i / 120.0); val yy = y(curveDb(k, fr, tr.showBoost)); if (i == 0) path.moveTo(x(fr), yy) else path.lineTo(x(fr), yy); i++ }
            p.color = Color.parseColor("#E8F4FF"); p.strokeWidth = 4f; cv.drawPath(path, p)
            p.style = Paint.Style.FILL
            for (pt in points(k)) { p.color = if (pt.band == held) Color.WHITE else st.accent; cv.drawCircle(x(pt.hz), y(pt.db), 16f, p) }
        }
        override fun onTouchEvent(e: MotionEvent): Boolean {
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> { held = points(k).minByOrNull { abs(x(it.hz) - e.x) + abs(y(it.db) - e.y) }?.band ?: -1; parent?.requestDisallowInterceptTouchEvent(true) }
                MotionEvent.ACTION_MOVE -> if (held >= 0) { onDrag(held, f(e.x), db(e.y)); invalidate() }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { held = -1; parent?.requestDisallowInterceptTouchEvent(false); performClick(); invalidate() }
            }
            return true
        }
        override fun performClick(): Boolean { super.performClick(); return true }
    }
}
