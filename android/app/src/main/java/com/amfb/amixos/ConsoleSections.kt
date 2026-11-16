package com.amfb.amixos

import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.GridLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.ToggleButton
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import java.util.concurrent.Executors

/** The console's sections on the phone that are not on the mixer screen: SAMPLE PADS, MASTERING and CONSOLE STATUS. Each talks to
 *  the console's remote service with the remembered pairing; without one they say so. */
abstract class ConsoleSection : AppCompatActivity() {
    protected var client: DaemonClient? = null
    protected val io = Executors.newSingleThreadExecutor(); protected val ui = Handler(Looper.getMainLooper())
    protected val accent = Color.parseColor("#EA580C"); protected val dim = Color.parseColor("#8B98A8"); protected val cyan = Color.parseColor("#22D3EE")
    protected lateinit var root: LinearLayout; protected lateinit var note: TextView
    abstract val titleRes: Int
    abstract fun build()
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty()
        if (host.isNotEmpty()) client = DaemonClient(Endpoint(host, p.getString("port", null)?.toIntOrNull() ?: Prefs.defaultPort(this), p.getString("token", "").orEmpty()))
        root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(titleRes); setTextColor(accent); textSize = 18f; setTypeface(null, Typeface.BOLD) })
        note = TextView(this).apply { setTextColor(dim); textSize = 12f; setPadding(0, 6, 0, 10); text = if (client == null) getString(R.string.eq_offline) else "" }
        root.addView(note)
        build()
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#0F141C")); addView(root) }, null))
    }
    protected fun send(f: (DaemonClient) -> Unit) { val c = client ?: return; io.execute { runCatching { f(c) }.onFailure { e -> ui.post { note.text = e.message ?: e.toString() } } } }
    override fun onDestroy() { io.shutdown(); super.onDestroy() }
    protected fun label(t: String, color: Int = dim, size: Float = 12f) = TextView(this).apply { text = t; setTextColor(color); textSize = size }
}

/** SAMPLE PADS: the console's 100 built-in one-shots, by category; a tap plays it on the console (through its master, also while the
 *  phones run the sound). STOP ALL and the pads' level. */
class SamplePadsActivity : ConsoleSection() {
    override val titleRes = R.string.pads_title
    internal var samples: List<DaemonClient.Sample> = emptyList(); private var cat = ""; private lateinit var grid: GridLayout; private lateinit var cats: LinearLayout
    internal var lastPlayed = 0
    private val colors = mapOf("Drums" to "#EA580C", "Percussion" to "#FBBF24", "Bass" to "#A78BFA", "Leads" to "#22D3EE", "Pads" to "#34D399", "Keys" to "#60A5FA", "FX" to "#F472B6")
    override fun build() {
        cats = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        root.addView(HorizontalScrollView(this).apply { addView(cats); isHorizontalScrollBarEnabled = false })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        row.addView(Button(this).apply { text = getString(R.string.pads_stop); setOnClickListener { send { it.samplesStop() } } })
        val lvl = label(getString(R.string.pads_level, "-6.0")); row.addView(lvl)
        root.addView(row)
        root.addView(SeekBar(this).apply { max = 520; progress = 340   // -40 .. +12 dB in 0.1 dB
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, v: Int, user: Boolean) { val db = (v - 400) / 10.0; lvl.text = getString(R.string.pads_level, String.format("%+.1f", db)); if (user) send { it.samplesGain(db) } }
                override fun onStartTrackingTouch(s: SeekBar) {}; override fun onStopTrackingTouch(s: SeekBar) {} }) })
        grid = GridLayout(this).apply { columnCount = 4 }
        root.addView(grid)
        val c = client ?: return
        io.execute { val l = runCatching { c.samples() }.getOrElse { e -> ui.post { note.text = e.message }; emptyList() }
            ui.post { samples = l; note.text = if (l.isEmpty()) getString(R.string.pads_none) else getString(R.string.pads_help); showCats(); showPads() } }
    }
    private fun showCats() { cats.removeAllViews()
        for (k in listOf("") + samples.map { it.category }.distinct()) cats.addView(Button(this).apply { text = if (k.isEmpty()) "ALL" else k.uppercase(); alpha = if (k == cat) 1f else 0.55f
            setOnClickListener { cat = k; showCats(); showPads() } }) }
    internal fun showPads() { grid.removeAllViews(); val w = (resources.displayMetrics.widthPixels - 48) / 4
        for (s in samples.filter { cat.isEmpty() || it.category == cat }) {
            val col = Color.parseColor(colors[s.category] ?: "#94A3B8")
            grid.addView(TextView(this).apply { text = "${s.id}\n${s.name}"; setTextColor(Color.WHITE); textSize = 11f; gravity = Gravity.CENTER; maxLines = 3
                background = GradientDrawable().apply { cornerRadius = 18f; setColor(Color.argb(70, Color.red(col), Color.green(col), Color.blue(col))); setStroke(3, col) }
                layoutParams = GridLayout.LayoutParams().apply { width = w - 12; height = (w * 0.8).toInt(); setMargins(6, 6, 6, 6) }
                setOnClickListener { v -> lastPlayed = s.id; v.alpha = 0.5f; v.animate().alpha(1f).setDuration(250).start(); if (Prefs.haptics(this@SamplePadsActivity)) v.performHapticFeedback(android.view.HapticFeedbackConstants.KEYBOARD_TAP)
                    send { it.sample(s.id) } } }) }
    }
}

/** MASTERING: the console's master chain from the phone (ON, LOW, AIR, the compressor's THRESHOLD / RATIO / ATTACK / RELEASE / MAKEUP,
 *  DRIVE, OUTPUT); the console follows at once, and what it has is shown when the screen opens and once a second. */
class MasteringActivity : ConsoleSection() {
    override val titleRes = R.string.mast_title
    data class Knob(val key: String, val label: Int, val lo: Double, val hi: Double, val unit: String, val step: Double)
    private val knobs = listOf(Knob("low", R.string.mast_low, -12.0, 12.0, "dB", 0.5), Knob("air", R.string.mast_air, -12.0, 12.0, "dB", 0.5),
        Knob("thr", R.string.mast_thr, -40.0, 0.0, "dB", 0.5), Knob("ratio", R.string.mast_ratio, 1.0, 20.0, ":1", 0.1), Knob("attack", R.string.mast_attack, 0.1, 200.0, "ms", 0.1),
        Knob("release", R.string.mast_release, 10.0, 2000.0, "ms", 1.0), Knob("makeup", R.string.mast_makeup, 0.0, 18.0, "dB", 0.5), Knob("drive", R.string.mast_drive, 0.0, 1.0, "", 0.01),
        Knob("gain", R.string.mast_gain, -12.0, 12.0, "dB", 0.5))
    internal val bars = HashMap<String, SeekBar>(); private val texts = HashMap<String, TextView>(); internal lateinit var onBtn: ToggleButton
    private lateinit var gr: TextView; @Volatile private var touching = false; private var lastSet = 0L
    override fun build() {
        onBtn = ToggleButton(this).apply { textOn = getString(R.string.mast_on); textOff = getString(R.string.mast_off); setOnCheckedChangeListener { b, on -> if (b.tag == null) send { it.mastering(mapOf("on" to on)) } } }
        root.addView(onBtn)
        gr = label("", cyan, 13f).apply { typeface = Typeface.MONOSPACE }; root.addView(gr)
        for (k in knobs) { val t = label(""); texts[k.key] = t; root.addView(t)
            root.addView(SeekBar(this).apply { max = 1000; bars[k.key] = this
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(s: SeekBar, p: Int, user: Boolean) { val v = valueOf(k, p); show(k, v); if (user) { lastSet = System.currentTimeMillis(); send { it.mastering(mapOf(k.key to v)) } } }
                    override fun onStartTrackingTouch(s: SeekBar) { touching = true }; override fun onStopTrackingTouch(s: SeekBar) { touching = false } }) }) }
        root.addView(label(getString(R.string.mast_help)).apply { setPadding(0, 16, 0, 0) })
        ui.post(poll)
    }
    private fun valueOf(k: Knob, p: Int): Double { val v = k.lo + (k.hi - k.lo) * p / 1000.0; return Math.round(v / k.step) * k.step }
    private fun posOf(k: Knob, v: Double) = ((v - k.lo) / (k.hi - k.lo) * 1000).toInt().coerceIn(0, 1000)
    private fun show(k: Knob, v: Double) { texts[k.key]?.text = getString(k.label) + "   " + (if (k.unit == "") String.format("%.0f %%", v * 100) else String.format(if (k.step < 0.1) "%.2f %s" else "%.1f %s", v, k.unit)) }
    internal fun take(m: Map<String, Any?>) {
        if (touching || System.currentTimeMillis() - lastSet < 1200) return
        if (onBtn.isChecked != (m["on"] == true)) { onBtn.tag = "sync"; onBtn.isChecked = m["on"] == true; onBtn.tag = null }
        for (k in knobs) { val v = (m[k.key] as? Number)?.toDouble() ?: continue; bars[k.key]?.progress = posOf(k, v); show(k, v) }
        gr.text = getString(R.string.mast_gr, String.format("%.1f", (m["reductionDb"] as? Number)?.toDouble() ?: 0.0))
    }
    private val poll = object : Runnable { override fun run() { val c = client ?: return
        io.execute { val m = runCatching { c.masteringNow() }.getOrNull(); if (m != null && m.isNotEmpty()) ui.post { take(m) } }; ui.postDelayed(this, 1000) } }
    override fun onDestroy() { ui.removeCallbacks(poll); super.onDestroy() }
}

/** CONSOLE STATUS: is the console's sound running, on what (backend, rate, buffer), how hard it works (DSP load, worst block, dropouts),
 *  how long it has run, how many phones and browsers are linked, recording. Refreshed every second. */
class ConsoleStatusActivity : ConsoleSection() {
    override val titleRes = R.string.cstat_title
    internal lateinit var body: TextView
    override fun build() {
        body = TextView(this).apply { setTextColor(Color.WHITE); typeface = Typeface.MONOSPACE; textSize = 14f; setLineSpacing(8f, 1f) }
        root.addView(body); root.addView(label(getString(R.string.cstat_help)).apply { setPadding(0, 16, 0, 0) })
        ui.post(poll)
    }
    internal fun render(s: com.amfb.amixos.remote.Status): String {
        val up = s.uptimeS.toLong(); val bufMs = if (s.rate > 0 && s.frames > 0 && s.blocks > 0) 1000.0 * s.frames / s.blocks / s.rate else 0.0
        return listOf(
            getString(R.string.cstat_console) + "  " + s.computer.ifBlank { s.name } + "  " + s.version,
            getString(R.string.cstat_sound) + "  " + (if (s.running) "ON · " + s.backend.uppercase() else getString(R.string.cstat_off)),
            getString(R.string.cstat_rate) + "  " + (if (s.rate > 0) String.format("%.0f Hz · %.1f ms", s.rate, bufMs) else "—"),
            getString(R.string.cstat_load) + "  " + String.format("%.1f %%  (worst block %.0f µs)", s.loadPct, s.maxBlockUs),
            getString(R.string.cstat_xruns) + "  " + s.xruns,
            getString(R.string.cstat_up) + "  " + String.format("%d:%02d:%02d", up / 3600, up / 60 % 60, up % 60),
            getString(R.string.cstat_remotes) + "  " + s.remotes,
            getString(R.string.cstat_rec) + "  " + (if (s.recording) "●" else "—"),
            getString(R.string.cstat_plan) + "  " + s.plan.ifBlank { "—" } + " · " + s.strips + " CH").joinToString("\n")
    }
    private val poll = object : Runnable { override fun run() { val c = client ?: return
        io.execute { val s = runCatching { c.status() }; ui.post { s.onSuccess { body.text = render(it); note.text = "" }.onFailure { e -> note.text = e.message } } }; ui.postDelayed(this, 1000) } }
    override fun onDestroy() { ui.removeCallbacks(poll); super.onDestroy() }
}
