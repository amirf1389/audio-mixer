package com.amfb.amixos

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.DashPathEffect
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import com.amfb.amixos.remote.Rta
import java.util.concurrent.Executors
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.roundToInt

/** The RTA EQ graph: the live spectrum, the program EQ's curve (orange) with a dot per octave you drag up and down, and an AUTO EQ
 *  proposal (violet, dashed) until it is applied or cancelled. */
class RtaEqView(ctx: Context) : View(ctx) {
    var gains = DoubleArray(10); var on = true
    var proposal: DoubleArray? = null; set(v) { field = v; invalidate() }
    var spectrum: Rta? = null; set(v) { field = v; invalidate() }
    var onChange: ((Int, Boolean) -> Unit)? = null   // band, finished (the finger lifted)
    private val range = 15.0
    private val grid = Paint().apply { color = 0xFF233040.toInt(); strokeWidth = 2f }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFC2C7CF.toInt(); textSize = 24f; typeface = Typeface.MONOSPACE }
    private val eqLine = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFFFB590.toInt(); strokeWidth = 5f; style = Paint.Style.STROKE }
    private val propLine = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFA78BFA.toInt(); strokeWidth = 4f; style = Paint.Style.STROKE; pathEffect = DashPathEffect(floatArrayOf(18f, 14f), 0f) }
    private val rtaFill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x5522D3EE; style = Paint.Style.FILL }
    private val rtaLine = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xAA22D3EE.toInt(); strokeWidth = 2.5f; style = Paint.Style.STROKE }
    private val dot = Paint(Paint.ANTI_ALIAS_FLAG)
    private fun xOf(f: Double) = (ln(f / 20) / ln(1000.0) * width).toFloat()
    private fun fOf(x: Float) = 20 * 1000.0.pow((x / width).toDouble().coerceIn(0.0, 1.0))
    private fun yOf(db: Double) = (height / 2 - db / range * (height / 2 - 30)).toFloat()
    private fun dbOf(y: Float) = ((height / 2 - y) / (height / 2 - 30) * range).coerceIn(-range, range)
    private fun bands(g: DoubleArray) = g.mapIndexed { i, d -> EqBand(if (kotlin.math.abs(d) > 0.01) "peaking" else "off", RtaEq.ISO[i], d, 1.41) }

    override fun onMeasure(w: Int, h: Int) { val wd = MeasureSpec.getSize(w); setMeasuredDimension(wd, (wd * 0.62).toInt()) }
    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0F15.toInt())
        for (f in RtaEq.ISO) { val x = xOf(f); c.drawLine(x, 0f, x, height.toFloat(), grid); c.drawText(RtaEq.hz(f), x + 4, height - 10f, label) }
        for (db in listOf(-6, 0, 6)) { val y = yOf(db.toDouble()); c.drawLine(0f, y, width.toFloat(), y, grid) }
        spectrum?.let { drawSpectrum(c, it, width.toFloat(), height.toFloat(), ::xOf, rtaFill, rtaLine) }
        fun curve(g: DoubleArray, paint: Paint) { val b = bands(g); val p = Path()
            for (i in 0..width step 3) { val y = yOf(AudioMath.totalDb(b, fOf(i.toFloat())).coerceIn(-range * 1.4, range * 1.4)); if (i == 0) p.moveTo(0f, y) else p.lineTo(i.toFloat(), y) }
            c.drawPath(p, paint) }
        eqLine.alpha = if (on) 255 else 90; curve(gains, eqLine)
        proposal?.let { curve(it, propLine) }
        RtaEq.ISO.forEachIndexed { i, f -> val x = xOf(f); val y = yOf(gains[i])
            dot.color = if (i == dragging) Color.WHITE else 0xFFFFB590.toInt(); dot.alpha = if (on) 255 else 120; c.drawCircle(x, y, if (i == dragging) 24f else 16f, dot) }
    }
    private var dragging = -1
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { var best = -1; var bd = Float.MAX_VALUE
                RtaEq.ISO.forEachIndexed { i, f -> val d = kotlin.math.abs(xOf(f) - e.x); if (d < bd) { bd = d; best = i } }
                if (bd > width / 14f) return false
                dragging = best; parent?.requestDisallowInterceptTouchEvent(true); invalidate(); return true }
            MotionEvent.ACTION_MOVE -> if (dragging >= 0) { gains[dragging] = (dbOf(e.y) * 2).roundToInt() / 2.0; onChange?.invoke(dragging, false); invalidate(); return true }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> if (dragging >= 0) { val d = dragging; dragging = -1; onChange?.invoke(d, true); invalidate(); return true }
        }
        return super.onTouchEvent(e)
    }
}

/** RTA EQ: the console's master program EQ (10 octave bells) shaped against the live spectrum of the master or any strip. AUTO EQ
 *  measures for 6 seconds and proposes gains towards a target (FLAT, HOUSE, WARM); APPLY sends them, CANCEL drops them. */
class RtaEqActivity : FitActivity() {
    private var client: DaemonClient? = null
    private val io = Executors.newSingleThreadExecutor(); private val ui = Handler(Looper.getMainLooper())
    lateinit var graph: RtaEqView; private lateinit var note: TextView; private lateinit var onBtn: Button; private lateinit var srcBtn: Button
    private var src = -1; private var strips = 0; private var target = RtaEq.Target.FLAT
    private var measuring: RtaEq.Average? = null; private var asked = 0L; private var lastSpectrum = System.currentTimeMillis(); @Volatile private var askedWhy = 0L; private var noteIsWhy = false; @Volatile private var busy = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty(); val token = p.getString("token", "").orEmpty()
        if (host.isNotEmpty()) client = DaemonClient(Endpoint(host, p.getString("port", null)?.toIntOrNull() ?: Prefs.defaultPort(this), token))
        val accent = Color.parseColor("#FFB590"); val dim = Color.parseColor("#C2C7CF")
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.rta_title); setTextColor(accent); textSize = 18f; setTypeface(null, Typeface.BOLD) })
        graph = RtaEqView(this); graph.onChange = { _, done -> if (done) send() }
        root.addView(graph)
        fun row() = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }.also { root.addView(it) }
        val r1 = row()
        srcBtn = Button(this).apply { setOnClickListener { pickSource() } }; r1.addView(srcBtn)
        onBtn = Button(this).apply { setOnClickListener { graph.on = !graph.on; showOn(); graph.invalidate(); send() } }; r1.addView(onBtn)
        r1.addView(Button(this).apply { text = getString(R.string.eq_flat); setOnClickListener { graph.gains.fill(0.0); graph.invalidate(); send() } })
        root.addView(TextView(this).apply { text = getString(R.string.rta_auto); setTextColor(accent); setTypeface(null, Typeface.BOLD); setPadding(0, 20, 0, 4) })
        val r2 = row(); val tbs = mutableListOf<Button>()
        for (t in RtaEq.Target.values()) r2.addView(Button(this).apply { text = t.label; tbs += this; setOnClickListener { target = t; tbs.forEach { b -> b.alpha = if (b === this) 1f else 0.55f }; graph.proposal = null; note.text = "" } })
        tbs.forEachIndexed { i, b -> b.alpha = if (i == 0) 1f else 0.55f }
        val r3 = row()
        r3.addView(Button(this).apply { text = getString(R.string.rta_measure); setOnClickListener { measure() } })
        r3.addView(Button(this).apply { text = getString(R.string.rta_apply); setOnClickListener { graph.proposal?.let { pr -> pr.copyInto(graph.gains); graph.on = true; showOn(); graph.proposal = null; send(); note.text = getString(R.string.rta_applied) } } })
        r3.addView(Button(this).apply { text = getString(R.string.rta_cancel); setOnClickListener { graph.proposal = null; note.text = "" } })
        note = TextView(this).apply { setTextColor(Color.parseColor("#C4B5FD")); typeface = Typeface.MONOSPACE; textSize = 12f; setPadding(0, 8, 0, 8) }
        root.addView(note)
        root.addView(TextView(this).apply { text = getString(if (client == null) R.string.eq_offline else R.string.rta_help); setTextColor(dim); textSize = 12f; setPadding(0, 12, 0, 0) })
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(root) }, null))
        showOn(); showSrc()
        client?.let { c -> io.execute { runCatching { val st = c.state(); val pe = c.programEqNow(); ui.post { strips = st.strips.size; graph.gains = pe.gains.toDoubleArray(); graph.on = pe.on; showOn(); graph.invalidate() } } } }
    }
    private fun showOn() { onBtn.text = getString(if (graph.on) R.string.rta_eq_on else R.string.rta_eq_off) }
    private fun showSrc() { srcBtn.text = if (src < 0) getString(R.string.rta_src_master) else "CH %02d".format(src + 1) }
    private fun pickSource() {
        val names = listOf(getString(R.string.rta_src_master)) + (1..strips).map { "CH %02d".format(it) }
        AlertDialog.Builder(this).setItems(names.toTypedArray()) { _, w -> src = w - 1; showSrc(); asked = 0; graph.spectrum = null }.show()
    }
    private fun send() { val c = client ?: return; val g = graph.gains.toList(); val on = graph.on; io.execute { runCatching { c.programEq(g, on) } } }
    private fun measure() {
        if (client == null || measuring != null) return
        graph.proposal = null; measuring = RtaEq.Average(); note.text = getString(R.string.rta_measuring)
        ui.postDelayed({ val m = measuring; measuring = null
            if (m == null || m.frames < 10) { note.text = getString(R.string.rta_no_sound); return@postDelayed }
            val pr = RtaEq.autoEq(m.result(), target)
            if (pr == null) { note.text = getString(R.string.rta_no_sound); return@postDelayed }
            graph.proposal = pr
            note.text = getString(R.string.rta_proposal, target.label) + "\n" + pr.mapIndexed { i, d -> RtaEq.hz(RtaEq.ISO[i]) + " " + (if (d > 0) "+" else "") + d }.joinToString("  ")
        }, 6000)
    }
    private val tick = object : Runnable { override fun run() { val c = client ?: return
        if (!busy) { busy = true; val want = src; io.execute { val now = System.currentTimeMillis()
            if (now - asked > 8000) { asked = now; runCatching { c.rta(want) } }
            val r = c.rtaNow()
            // NO SPECTRUM: say why instead of an empty graph (the console's sound card is not running, or nothing plays on that source)
            val why = if (r?.db?.isNotEmpty() == true) null else if (now - lastSpectrum > 2500 && now - askedWhy > 3000) { askedWhy = now; if (runCatching { c.status().running }.getOrDefault(false)) getString(R.string.rta_silent) else getString(R.string.rta_engine_off) } else null
            busy = false
            ui.post { val s = r?.takeIf { it.src == src && it.db.isNotEmpty() }; graph.spectrum = s
              if (s != null) { lastSpectrum = System.currentTimeMillis(); measuring?.add(RtaEq.octaves(s.f, s.db)); if (noteIsWhy) { note.text = ""; noteIsWhy = false } }
              else if (why != null && measuring == null && graph.proposal == null) { note.text = why; noteIsWhy = true } } } }
        ui.postDelayed(this, 200) } }
    override fun onResume() { super.onResume(); asked = 0; ui.removeCallbacks(tick); ui.post(tick) }
    override fun onPause() { ui.removeCallbacks(tick); super.onPause() }
    override fun onDestroy() { ui.removeCallbacks(tick); io.shutdown(); super.onDestroy() }
}
