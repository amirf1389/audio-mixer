package com.amfb.amixos

import android.annotation.SuppressLint
import android.app.AlertDialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import java.util.concurrent.Executors
import kotlin.math.ln
import kotlin.math.pow

/** The EQ curve: 20 Hz .. 20 kHz across, ±18 dB up and down, the summed response of the bands and a dot per band.
 *  Drag a dot: across = frequency, up / down = gain (for the bands that have one). Tap a dot to select it. */
class EqCurveView(ctx: Context) : View(ctx) {
    var bands: MutableList<EqBand> = mutableListOf(); var selected = 0
    var onChange: ((Int) -> Unit)? = null; var onSelect: ((Int) -> Unit)? = null
    private val colors = intArrayOf(0xFFFFB590.toInt(), 0xFF22D3EE.toInt(), 0xFFA78BFA.toInt(), 0xFF34D399.toInt(), 0xFFF472B6.toInt(), 0xFFFBBF24.toInt(), 0xFF60A5FA.toInt(), 0xFFF87171.toInt())
    private val grid = Paint().apply { color = 0xFF233040.toInt(); strokeWidth = 2f }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFC2C7CF.toInt(); textSize = 26f; typeface = Typeface.MONOSPACE }
    private val curve = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; strokeWidth = 5f; style = Paint.Style.STROKE }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x33EA580C; style = Paint.Style.FILL }
    private val dot = Paint(Paint.ANTI_ALIAS_FLAG)
    private val range = 18.0
    /** the live RTA behind the curve (null: none) */
    var spectrum: com.amfb.amixos.remote.Rta? = null; set(v) { field = v; invalidate() }
    private val rtaFill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x5522D3EE; style = Paint.Style.FILL }
    private val rtaLine = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xAA22D3EE.toInt(); strokeWidth = 2.5f; style = Paint.Style.STROKE }
    private fun xOf(f: Double) = (ln(f / 20) / ln(1000.0) * width).toFloat()
    private fun fOf(x: Float) = 20 * 1000.0.pow((x / width).toDouble().coerceIn(0.0, 1.0))
    private fun yOf(db: Double) = (height / 2 - db / range * (height / 2 - 30)).toFloat()
    private fun dbOf(y: Float) = ((height / 2 - y) / (height / 2 - 30) * range).coerceIn(-range, range)

    override fun onMeasure(w: Int, h: Int) { val wd = MeasureSpec.getSize(w); setMeasuredDimension(wd, (wd * 0.62).toInt()) }
    override fun onDraw(c: Canvas) {
        c.drawColor(0xFF0B0F15.toInt())
        for (f in listOf(50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0)) { val x = xOf(f); c.drawLine(x, 0f, x, height.toFloat(), grid)
            c.drawText(if (f >= 1000) "${(f / 1000).toInt()}k" else "${f.toInt()}", x + 6, height - 10f, label) }
        for (db in listOf(-12, -6, 0, 6, 12)) { val y = yOf(db.toDouble()); c.drawLine(0f, y, width.toFloat(), y, grid); c.drawText((if (db > 0) "+" else "") + db, 6f, y - 6, label) }
        spectrum?.let { drawSpectrum(c, it, width.toFloat(), height.toFloat(), ::xOf, rtaFill, rtaLine) }
        val p = Path(); val area = Path(); area.moveTo(0f, yOf(0.0))
        for (i in 0..width step 3) { val y = yOf(AudioMath.totalDb(bands, fOf(i.toFloat())).coerceIn(-range * 1.4, range * 1.4)); if (i == 0) p.moveTo(0f, y) else p.lineTo(i.toFloat(), y); area.lineTo(i.toFloat(), y) }
        area.lineTo(width.toFloat(), yOf(0.0)); area.close(); c.drawPath(area, fill); c.drawPath(p, curve)
        bands.forEachIndexed { i, b -> if (b.type == "off") return@forEachIndexed
            val x = xOf(b.freq); val y = if (b.usesGain) yOf(b.gainDb) else yOf(0.0)
            dot.color = colors[i % colors.size]; dot.style = Paint.Style.FILL; c.drawCircle(x, y, if (i == selected) 26f else 18f, dot)
            if (i == selected) { dot.style = Paint.Style.STROKE; dot.strokeWidth = 4f; dot.color = Color.WHITE; c.drawCircle(x, y, 32f, dot) }
            label.color = Color.BLACK; c.drawText("${i + 1}", x - 8, y + 9, label); label.color = 0xFFC2C7CF.toInt() }
    }
    var dragging = -1; private set
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { var best = -1; var bd = 90f * 90f
                bands.forEachIndexed { i, b -> if (b.type == "off") return@forEachIndexed; val dx = xOf(b.freq) - e.x; val dy = (if (b.usesGain) yOf(b.gainDb) else yOf(0.0)) - e.y; val d = dx * dx + dy * dy; if (d < bd) { bd = d; best = i } }
                if (best >= 0) { dragging = best; selected = best; onSelect?.invoke(best); parent?.requestDisallowInterceptTouchEvent(true); invalidate() }
                return best >= 0 }
            MotionEvent.ACTION_MOVE -> if (dragging >= 0) { val b = bands[dragging]; b.freq = fOf(e.x).coerceIn(20.0, 20000.0); if (b.usesGain) b.gainDb = dbOf(e.y); onChange?.invoke(dragging); invalidate(); return true }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { dragging = -1 }
        }
        return super.onTouchEvent(e)
    }
}

/** draws an RTA spectrum: 0 dBFS at the top, -90 dBFS at the bottom, a filled outline through the band centres */
fun drawSpectrum(c: Canvas, r: com.amfb.amixos.remote.Rta, w: Float, h: Float, xOf: (Double) -> Float, fill: Paint, line: Paint) {
    if (r.f.isEmpty()) return
    fun y(db: Double) = (h * (-db.coerceIn(-90.0, 0.0) / 90.0)).toFloat()
    val p = Path(); val a = Path(); a.moveTo(xOf(r.f[0]), h)
    r.f.forEachIndexed { i, f -> val x = xOf(f); val yy = y(r.db[i]); if (i == 0) p.moveTo(x, yy) else p.lineTo(x, yy); a.lineTo(x, yy) }
    a.lineTo(xOf(r.f.last()), h); a.close(); c.drawPath(a, fill); c.drawPath(p, line)
    val t = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = line.color; alpha = 200; textSize = 22f; typeface = android.graphics.Typeface.MONOSPACE; textAlign = Paint.Align.RIGHT }
    for (d in listOf(0, -30, -60)) c.drawText("$d dBFS", w - 6, y(d.toDouble()) + 24f, t)
}

/** EQ CONTROLLER for one strip of the console: 8 bands (type, frequency, gain, Q) on a curve you drag, presets, flat, bypass.
 *  The settings are kept on this phone per strip (so the screen opens where you left it) and sent to the console as you move. */
class EqActivity : FitActivity() {
    private val io = Executors.newSingleThreadExecutor(); private val ui = Handler(Looper.getMainLooper())
    private var client: DaemonClient? = null; private var strip = 0
    internal lateinit var curve: EqCurveView
    private lateinit var info: TextView; private lateinit var freqBar: SeekBar; private lateinit var gainBar: SeekBar; private lateinit var qBar: SeekBar; private lateinit var typeBtn: Button
    private var bypass = false; private var loading = false
    private val dirty = HashSet<Int>(); private var flushQueued = false

    companion object {
        fun defaults() = mutableListOf(EqBand("highpass", 40.0, 0.0, 0.71), EqBand("lowshelf", 100.0, 0.0, 0.71), EqBand("peaking", 250.0, 0.0, 1.0), EqBand("peaking", 1000.0, 0.0, 1.0),
            EqBand("peaking", 3000.0, 0.0, 1.0), EqBand("highshelf", 10000.0, 0.0, 0.71), EqBand("off", 6000.0, 0.0, 4.0), EqBand("lowpass", 18000.0, 0.0, 0.71).also { it.type = "off" })
        val PRESETS = linkedMapOf(
            "FLAT" to defaults(),
            "VOCAL" to mutableListOf(EqBand("highpass", 90.0, 0.0, 0.71), EqBand("lowshelf", 150.0, -2.0, 0.71), EqBand("peaking", 300.0, -2.5, 1.2), EqBand("peaking", 1000.0, 0.0, 1.0),
                EqBand("peaking", 3500.0, 3.0, 1.0), EqBand("highshelf", 10000.0, 2.5, 0.71), EqBand("peaking", 7000.0, -2.0, 4.0), EqBand("off", 18000.0, 0.0, 0.71)),
            "SPEECH / PODCAST" to mutableListOf(EqBand("highpass", 80.0, 0.0, 0.71), EqBand("lowshelf", 120.0, 1.5, 0.71), EqBand("peaking", 400.0, -3.0, 1.0), EqBand("peaking", 1000.0, 0.0, 1.0),
                EqBand("peaking", 2500.0, 2.0, 1.0), EqBand("highshelf", 8000.0, 1.0, 0.71), EqBand("off", 6000.0, 0.0, 4.0), EqBand("lowpass", 15000.0, 0.0, 0.71)),
            "KICK" to mutableListOf(EqBand("highpass", 30.0, 0.0, 0.71), EqBand("peaking", 60.0, 4.0, 1.2), EqBand("peaking", 350.0, -5.0, 1.5), EqBand("peaking", 1000.0, 0.0, 1.0),
                EqBand("peaking", 4000.0, 3.0, 1.5), EqBand("highshelf", 10000.0, -2.0, 0.71), EqBand("off", 6000.0, 0.0, 4.0), EqBand("off", 18000.0, 0.0, 0.71)),
            "ACOUSTIC GUITAR" to mutableListOf(EqBand("highpass", 80.0, 0.0, 0.71), EqBand("lowshelf", 120.0, -2.0, 0.71), EqBand("peaking", 200.0, -2.0, 1.0), EqBand("peaking", 1000.0, 0.0, 1.0),
                EqBand("peaking", 5000.0, 2.0, 1.0), EqBand("highshelf", 12000.0, 2.0, 0.71), EqBand("off", 6000.0, 0.0, 4.0), EqBand("off", 18000.0, 0.0, 0.71)),
            "LOUDNESS (LOW VOLUME)" to mutableListOf(EqBand("off", 40.0, 0.0, 0.71), EqBand("lowshelf", 100.0, 6.0, 0.71), EqBand("peaking", 250.0, 0.0, 1.0), EqBand("peaking", 1000.0, 0.0, 1.0),
                EqBand("peaking", 3000.0, 0.0, 1.0), EqBand("highshelf", 10000.0, 4.0, 0.71), EqBand("off", 6000.0, 0.0, 4.0), EqBand("off", 18000.0, 0.0, 0.71)),
            "TELEPHONE" to mutableListOf(EqBand("highpass", 400.0, 0.0, 1.0), EqBand("off", 100.0, 0.0, 0.71), EqBand("peaking", 1500.0, 6.0, 1.0), EqBand("off", 1000.0, 0.0, 1.0),
                EqBand("off", 3000.0, 0.0, 1.0), EqBand("off", 10000.0, 0.0, 0.71), EqBand("off", 6000.0, 0.0, 4.0), EqBand("lowpass", 3400.0, 0.0, 1.0)))
        fun encode(b: List<EqBand>) = b.joinToString(";") { "${it.type},${it.freq},${it.gainDb},${it.q}" }
        fun decode(s: String?): MutableList<EqBand>? = runCatching { s!!.split(";").map { val f = it.split(","); EqBand(f[0], f[1].toDouble(), f[2].toDouble(), f[3].toDouble()) }.toMutableList().takeIf { it.size == 8 } }.getOrNull()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        strip = intent.getIntExtra("strip", 0)
        val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty(); val token = p.getString("token", "").orEmpty()
        if (host.isNotEmpty()) client = DaemonClient(Endpoint(host, p.getString("port", null)?.toIntOrNull() ?: Prefs.defaultPort(this), token))
        val accent = Color.parseColor("#FFB590"); val dim = Color.parseColor("#C2C7CF")
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.eq_title, strip + 1) + "  " + (intent.getStringExtra("name") ?: ""); setTextColor(accent); textSize = 18f; setTypeface(null, Typeface.BOLD) })
        info = TextView(this).apply { setTextColor(Color.parseColor("#7FD8E8")); typeface = Typeface.MONOSPACE; textSize = 13f; setPadding(0, 8, 0, 8) }
        curve = EqCurveView(this); curve.bands = decode(Prefs.local(this).getString("eq$strip", null)) ?: defaults()
        curve.onChange = { i -> showBand(); send(i) }; curve.onSelect = { showBand() }
        root.addView(curve); root.addView(info)
        val tabs = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for (i in 0 until 8) tabs.addView(Button(this).apply { text = "${i + 1}"; minWidth = 0; minimumWidth = 0; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); setOnClickListener { curve.selected = i; curve.invalidate(); showBand() } })
        root.addView(tabs)
        typeBtn = Button(this).apply { setOnClickListener { AlertDialog.Builder(this@EqActivity).setItems(AudioMath.EQ_TYPES.map { it.uppercase() }.toTypedArray()) { _, w -> band().type = AudioMath.EQ_TYPES[w]; curve.invalidate(); showBand(); send(curve.selected) }.show() } }
        root.addView(typeBtn)
        fun bar(label: String, max: Int, f: (Int) -> Unit): SeekBar { root.addView(TextView(this).apply { text = label; setTextColor(dim) })
            return SeekBar(this).apply { this.max = max; setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, v: Int, user: Boolean) { if (user && !loading) { f(v); curve.invalidate(); showBand(); send(curve.selected) } }
                override fun onStartTrackingTouch(s: SeekBar) {}; override fun onStopTrackingTouch(s: SeekBar) {} }) }.also { root.addView(it) } }
        freqBar = bar(getString(R.string.eq_freq), 1000) { band().freq = 20 * 1000.0.pow(it / 1000.0) }
        gainBar = bar(getString(R.string.eq_gain), 360) { band().gainDb = (it - 180) / 10.0 }
        qBar = bar(getString(R.string.eq_q), 1000) { band().q = 0.1 * 300.0.pow(it / 1000.0) }
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(Button(this).apply { text = getString(R.string.eq_presets); setOnClickListener { val names = PRESETS.keys.toTypedArray()
            AlertDialog.Builder(this@EqActivity).setTitle(R.string.eq_presets).setItems(names) { _, w -> curve.bands = PRESETS[names[w]]!!.map { it.copy() }.toMutableList(); curve.invalidate(); showBand(); sendAll() }.show() } })
        row.addView(Button(this).apply { text = getString(R.string.eq_flat); setOnClickListener { curve.bands.forEach { it.gainDb = 0.0 }; curve.invalidate(); showBand(); sendAll() } })
        row.addView(Button(this).apply { text = getString(R.string.eq_bypass); setOnClickListener { bypass = !bypass; text = getString(if (bypass) R.string.eq_bypassed else R.string.eq_bypass); sendAll() } })
        if (client != null) row.addView(Button(this).apply { text = getString(R.string.eq_rta_on); setOnClickListener { rtaOn = !rtaOn; text = getString(if (rtaOn) R.string.eq_rta_on else R.string.eq_rta_off)
            if (rtaOn) { rtaAsked = 0; ui.removeCallbacks(rtaTick); ui.post(rtaTick) } else curve.spectrum = null } })
        root.addView(row)
        root.addView(TextView(this).apply { text = getString(if (client == null) R.string.eq_offline else R.string.eq_help); setTextColor(dim); textSize = 12f; setPadding(0, 12, 0, 0) })
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(root) }, null))
        showBand()
    }
    private fun band() = curve.bands[curve.selected]
    private fun showBand() {
        val b = band(); val i = curve.selected
        info.text = "BAND ${i + 1}  " + b.type.uppercase() + "  " + (if (b.freq >= 1000) String.format("%.2f kHz", b.freq / 1000) else String.format("%.0f Hz", b.freq)) +
            (if (b.usesGain) String.format("  %+.1f dB", b.gainDb) else "") + String.format("  Q %.2f", b.q)
        typeBtn.text = getString(R.string.eq_type, b.type.uppercase())
        loading = true; freqBar.progress = (ln(b.freq / 20) / ln(1000.0) * 1000).toInt(); gainBar.progress = (b.gainDb * 10 + 180).toInt(); gainBar.isEnabled = b.usesGain
        qBar.progress = (ln(b.q / 0.1) / ln(300.0) * 1000).toInt(); loading = false
        Prefs.local(this).edit().putString("eq$strip", encode(curve.bands)).apply()
    }
    /** Changes are sent at most 25 times a second; only the bands that moved since the last send go out. */
    private fun send(i: Int) { synchronized(dirty) { dirty.add(i) }; if (flushQueued) return; flushQueued = true
        ui.postDelayed({ flushQueued = false; val c = client ?: return@postDelayed; val list = synchronized(dirty) { dirty.toList().also { dirty.clear() } }
            val snap = list.map { it to curve.bands[it].copy() }
            io.execute { for ((k, b) in snap) runCatching { c.eq(strip, k, if (bypass) "off" else b.type, b.freq, b.q, b.gainDb) } } }, 40) }
    private fun sendAll() { for (i in 0 until 8) send(i) }
    // ---- THE CONSOLE'S EQ: the bands as the console (or another remote) has them, on open and once a second while no finger is on the curve
    private var eqAsked = 0L; @Volatile private var eqBusy = false
    private fun takeConsoleEq(st: com.amfb.amixos.remote.StripState) {
        val names = listOf("off", "peaking", "lowshelf", "highshelf", "lowpass", "highpass", "notch"); var changed = false
        st.eq.take(curve.bands.size).forEachIndexed { k, e -> if (e.size < 4 || e[0].toInt() == 0 || e[0].toInt() >= names.size) return@forEachIndexed
            val b = curve.bands[k]; val t = names[e[0].toInt()]
            if (b.type != t || Math.abs(b.freq - e[1]) > 0.5 || Math.abs(b.q - e[2]) > 0.005 || Math.abs(b.gainDb - e[3]) > 0.05) { b.type = t; b.freq = e[1]; b.q = e[2]; b.gainDb = e[3]; changed = true } }
        if (changed) { curve.invalidate(); showBand() }
    }
    private fun followConsoleEq(cl: DaemonClient) { val now = System.currentTimeMillis(); if (eqBusy || now - eqAsked < 1000 || curve.dragging >= 0 || flushQueued) return
        eqAsked = now; eqBusy = true; io.execute { val st = runCatching { cl.state().strips.getOrNull(strip) }.getOrNull(); eqBusy = false; if (st != null) ui.post { if (!flushQueued) takeConsoleEq(st) } } }
    // ---- the live RTA of this strip, behind the curve: asked for every 8 s, read 5 times a second while the screen is on top
    private var rtaOn = true; private var rtaAsked = 0L; @Volatile private var rtaBusy = false
    private val rtaTick = object : Runnable { override fun run() { val cl = client ?: return
        followConsoleEq(cl)
        // RTA off: no spectrum, but the console's EQ is still followed (once a second)
        if (!rtaOn) { curve.spectrum = null; ui.postDelayed(this, 1000); return }
        // one request at a time: a slow network skips frames instead of queueing them
        if (!rtaBusy) { rtaBusy = true; io.execute { val now = System.currentTimeMillis()
            if (now - rtaAsked > 8000) { rtaAsked = now; runCatching { cl.rta(strip) } }
            val r = cl.rtaNow(); rtaBusy = false; ui.post { if (rtaOn) curve.spectrum = r?.takeIf { it.src == strip } } } }
        ui.postDelayed(this, 200) } }
    override fun onResume() { super.onResume(); rtaAsked = 0; ui.removeCallbacks(rtaTick); ui.post(rtaTick) }
    override fun onPause() { ui.removeCallbacks(rtaTick); super.onPause() }
    override fun onDestroy() { ui.removeCallbacks(rtaTick); io.shutdown(); super.onDestroy() }
}
