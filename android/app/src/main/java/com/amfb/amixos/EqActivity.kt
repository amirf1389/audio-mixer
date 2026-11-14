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
    private val colors = intArrayOf(0xFFEA580C.toInt(), 0xFF22D3EE.toInt(), 0xFFA78BFA.toInt(), 0xFF34D399.toInt(), 0xFFF472B6.toInt(), 0xFFFBBF24.toInt(), 0xFF60A5FA.toInt(), 0xFFF87171.toInt())
    private val grid = Paint().apply { color = 0xFF233040.toInt(); strokeWidth = 2f }
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF8B98A8.toInt(); textSize = 26f; typeface = Typeface.MONOSPACE }
    private val curve = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.WHITE; strokeWidth = 5f; style = Paint.Style.STROKE }
    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0x33EA580C; style = Paint.Style.FILL }
    private val dot = Paint(Paint.ANTI_ALIAS_FLAG)
    private val range = 18.0
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
        val p = Path(); val area = Path(); area.moveTo(0f, yOf(0.0))
        for (i in 0..width step 3) { val y = yOf(AudioMath.totalDb(bands, fOf(i.toFloat())).coerceIn(-range * 1.4, range * 1.4)); if (i == 0) p.moveTo(0f, y) else p.lineTo(i.toFloat(), y); area.lineTo(i.toFloat(), y) }
        area.lineTo(width.toFloat(), yOf(0.0)); area.close(); c.drawPath(area, fill); c.drawPath(p, curve)
        bands.forEachIndexed { i, b -> if (b.type == "off") return@forEachIndexed
            val x = xOf(b.freq); val y = if (b.usesGain) yOf(b.gainDb) else yOf(0.0)
            dot.color = colors[i % colors.size]; dot.style = Paint.Style.FILL; c.drawCircle(x, y, if (i == selected) 26f else 18f, dot)
            if (i == selected) { dot.style = Paint.Style.STROKE; dot.strokeWidth = 4f; dot.color = Color.WHITE; c.drawCircle(x, y, 32f, dot) }
            label.color = Color.BLACK; c.drawText("${i + 1}", x - 8, y + 9, label); label.color = 0xFF8B98A8.toInt() }
    }
    private var dragging = -1
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

/** EQ CONTROLLER for one strip of the console: 8 bands (type, frequency, gain, Q) on a curve you drag, presets, flat, bypass.
 *  The settings are kept on this phone per strip (so the screen opens where you left it) and sent to the console as you move. */
class EqActivity : AppCompatActivity() {
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
        val accent = Color.parseColor("#EA580C"); val dim = Color.parseColor("#8B98A8")
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.eq_title, strip + 1) + "  " + (intent.getStringExtra("name") ?: ""); setTextColor(accent); textSize = 18f; setTypeface(null, Typeface.BOLD) })
        info = TextView(this).apply { setTextColor(Color.parseColor("#00F2FF")); typeface = Typeface.MONOSPACE; textSize = 13f; setPadding(0, 8, 0, 8) }
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
        root.addView(row)
        root.addView(TextView(this).apply { text = getString(if (client == null) R.string.eq_offline else R.string.eq_help); setTextColor(dim); textSize = 12f; setPadding(0, 12, 0, 0) })
        setContentView(ScrollView(this).apply { setBackgroundColor(Color.parseColor("#0F141C")); addView(root) })
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
    override fun onDestroy() { io.shutdown(); super.onDestroy() }
}
