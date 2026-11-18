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

/** SAMPLE PADS: the console's 1000 CUSTOM PADS (20 banks of 50), its 1000 SOUND EFFECTS (20 families of 50) and the 100 BUILT-IN one-shots.
 *  A tap plays on the console (through its master, also while the phones run the sound); a long press on a pad edits it (name, level, colour,
 *  an effect or a built-in sample, empty, reset); a long press on an effect puts it on the pad being edited. The pads are the console's own
 *  file: a change made on the computer shows here within two seconds and a change made here shows on the computer. Audio files are
 *  chosen on the computer. STOP ALL and the pads' level. */
class SamplePadsActivity : ConsoleSection() {
    override val titleRes = R.string.pads_title
    internal var samples: List<DaemonClient.Sample> = emptyList(); internal var pads: List<DaemonClient.Pad> = emptyList(); internal var effects: List<DaemonClient.Sfx> = emptyList()
    internal var view = "PADS"; internal var bank = 0; internal var family = ""; internal var selPad = 1; internal var version = -1
    private var cat = ""; private lateinit var grid: GridLayout; private lateinit var tabs: LinearLayout; private lateinit var sub: LinearLayout
    internal var lastPlayed = 0; internal var lastPad = 0; internal var lastSfx = 0
    private val colors = mapOf("Drums" to "#EA580C", "Percussion" to "#FBBF24", "Bass" to "#A78BFA", "Leads" to "#22D3EE", "Pads" to "#34D399", "Keys" to "#60A5FA", "FX" to "#F472B6")
    private val padColors = listOf("#F59E0B", "#22D3EE", "#34D399", "#A78BFA", "#F472B6", "#FB7185", "#38BDF8", "#94A3B8")
    private val poll = object : Runnable { override fun run() { checkPads(); ui.postDelayed(this, 2000) } }
    override fun build() {
        tabs = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }; root.addView(HorizontalScrollView(this).apply { addView(tabs); isHorizontalScrollBarEnabled = false })
        sub = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }; root.addView(HorizontalScrollView(this).apply { addView(sub); isHorizontalScrollBarEnabled = false })
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
        showTabs()
        val c = client ?: return
        io.execute { val l = runCatching { c.samples() }.getOrElse { e -> ui.post { note.text = e.message }; emptyList() }; val fx = runCatching { c.sfx() }.getOrElse { emptyList() }
            val pg = runCatching { c.pads(1, 50) }.getOrNull()
            ui.post { samples = l; effects = fx; if (family.isEmpty()) family = fx.firstOrNull()?.family.orEmpty(); if (pg != null) { pads = pg.pads; version = pg.version }
                note.text = if (l.isEmpty()) getString(R.string.pads_none) else getString(R.string.pads_help); showTabs() } }
    }
    override fun onResume() { super.onResume(); ui.postDelayed(poll, 2000) }
    override fun onPause() { ui.removeCallbacks(poll); super.onPause() }
    /** the pads changed on the computer (or another phone): the bank on screen is read again */
    private fun checkPads() { val c = client ?: return; if (view != "PADS") return; val b = bank
        io.execute { val pg = runCatching { c.pads(b * 50 + 1, b * 50 + 50) }.getOrNull() ?: return@execute; ui.post { if (bank == b && pg.version != version) { pads = pg.pads; version = pg.version; showGrid() } } } }
    internal fun loadBank(b: Int) { bank = b; val c = client ?: return
        io.execute { val pg = runCatching { c.pads(b * 50 + 1, b * 50 + 50) }.getOrElse { e -> ui.post { note.text = e.message }; null } ?: return@execute; ui.post { if (bank == b) { pads = pg.pads; version = pg.version; showTabs() } } } }
    private fun chip(t: String, on: Boolean, click: () -> Unit) = Button(this).apply { text = t; alpha = if (on) 1f else 0.55f; isAllCaps = true; setOnClickListener { click() } }
    internal fun showTabs() {
        tabs.removeAllViews()
        for ((k, r) in listOf("PADS" to R.string.pads_tab_pads, "EFFECTS" to R.string.pads_tab_fx, "BUILT-IN" to R.string.pads_tab_builtin)) tabs.addView(chip(getString(r), view == k) { view = k; if (k == "PADS") loadBank(bank); showTabs() })
        sub.removeAllViews()
        when (view) {
            "PADS" -> for (b in 0 until 20) sub.addView(chip("${b * 50 + 1}-${b * 50 + 50}", b == bank) { loadBank(b) })
            "EFFECTS" -> for (f in effects.map { it.family }.distinct()) sub.addView(chip(f, f == family) { family = f; showTabs() })
            else -> for (k in listOf("") + samples.map { it.category }.distinct()) sub.addView(chip(if (k.isEmpty()) "ALL" else k, k == cat) { cat = k; showTabs() })
        }
        showGrid()
    }
    private fun tile(top: String, bottom: String, colHex: String, faint: Boolean, w: Int) = TextView(this).apply {
        text = "$top\n$bottom"; setTextColor(if (faint) dim else Color.WHITE); textSize = 11f; gravity = Gravity.CENTER; maxLines = 3
        val col = Color.parseColor(colHex); background = GradientDrawable().apply { cornerRadius = 18f; setColor(Color.argb(if (faint) 20 else 70, Color.red(col), Color.green(col), Color.blue(col))); setStroke(3, if (faint) Color.argb(80, Color.red(col), Color.green(col), Color.blue(col)) else col) }
        layoutParams = GridLayout.LayoutParams().apply { width = w - 12; height = (w * 0.8).toInt(); setMargins(6, 6, 6, 6) } }
    private fun flash(v: android.view.View) { v.alpha = 0.5f; v.animate().alpha(1f).setDuration(250).start(); if (Prefs.haptics(this)) v.performHapticFeedback(android.view.HapticFeedbackConstants.KEYBOARD_TAP) }
    internal fun showGrid() { grid.removeAllViews(); val w = (resources.displayMetrics.widthPixels - 48) / 4
        when (view) {
            "PADS" -> for (p in pads) grid.addView(tile("${p.n}${if (p.custom) " ✎" else ""}", if (p.kind == "empty") getString(R.string.pads_empty) else p.name, padColors.getOrElse(p.color) { "#94A3B8" }, p.kind == "empty", w).apply {
                if (p.n == selPad) (background as GradientDrawable).setStroke(6, Color.WHITE)
                setOnClickListener { v -> lastPad = p.n; selPad = p.n; flash(v); if (p.kind != "empty") send { it.playPad(p.n) } else note.text = getString(R.string.pads_empty_help) }
                setOnLongClickListener { selPad = p.n; editPad(p); true } })
            "EFFECTS" -> for (e in effects.filter { it.family == family }) grid.addView(tile("#${e.id}", e.name, "#22D3EE", false, w).apply {
                setOnClickListener { v -> lastSfx = e.id; flash(v); send { it.playSfx(e.id) } }
                setOnLongClickListener { putOnPad("sfx:${e.id}", e.name); true } })
            else -> for (s in samples.filter { cat.isEmpty() || it.category == cat }) grid.addView(tile("${s.id}", s.name, colors[s.category] ?: "#94A3B8", false, w).apply {
                setOnClickListener { v -> lastPlayed = s.id; flash(v); send { it.sample(s.id) } }
                setOnLongClickListener { putOnPad("sample:${s.id}", s.name); true } })
        }
    }
    /** a long press on an effect or a built-in sample: it goes on the pad being edited */
    internal fun putOnPad(source: String, name: String) { val n = selPad
        send { it.setPad(n, source = source); ui.post { note.text = getString(R.string.pads_put, name, n) } } }
    /** EDIT a pad: name, level, colour; EMPTY, RESET; an effect is put on it from EFFECTS (long press) */
    private fun editPad(p: DaemonClient.Pad) {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(40, 20, 40, 0) }
        val name = android.widget.EditText(this).apply { setText(p.name); setSingleLine(); filters = arrayOf(android.text.InputFilter.LengthFilter(48)) }; box.addView(name)
        if (p.kind == "file") box.addView(label(getString(R.string.pads_file_note)))
        val gl = label(getString(R.string.pads_gain, String.format("%+.1f", p.gainDb))); box.addView(gl)
        val gain = SeekBar(this).apply { max = 104; progress = ((p.gainDb + 40) * 2).toInt().coerceIn(0, 104)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener { override fun onProgressChanged(s: SeekBar, v: Int, u: Boolean) { gl.text = getString(R.string.pads_gain, String.format("%+.1f", v / 2.0 - 40)) }
                override fun onStartTrackingTouch(s: SeekBar) {}; override fun onStopTrackingTouch(s: SeekBar) {} }) }; box.addView(gain)
        var color = p.color; val sw = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun paint() { for (i in 0 until sw.childCount) (sw.getChildAt(i).background as GradientDrawable).setStroke(if (i == color) 6 else 0, Color.WHITE) }
        padColors.forEachIndexed { i, h -> sw.addView(android.view.View(this).apply { background = GradientDrawable().apply { cornerRadius = 8f; setColor(Color.parseColor(h)) }
            layoutParams = LinearLayout.LayoutParams(64, 48).apply { setMargins(4, 8, 4, 8) }; setOnClickListener { color = i; paint() } }) }; paint(); box.addView(sw)
        box.addView(label(getString(R.string.pads_edit_help)))
        android.app.AlertDialog.Builder(this).setTitle(getString(R.string.pads_edit_title, p.n)).setView(box)
            .setPositiveButton(R.string.pads_save) { _, _ -> val nm = name.text.toString().trim(); val g = gain.progress / 2.0 - 40; send { it.setPad(p.n, name = nm, gainDb = g, color = color); ui.post { loadBank(bank) } } }
            .setNeutralButton(R.string.pads_reset) { _, _ -> send { it.resetPad(p.n); ui.post { loadBank(bank) } } }
            .setNegativeButton(R.string.pads_clear) { _, _ -> send { it.setPad(p.n, source = ""); ui.post { loadBank(bank) } } }.show()
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
        root.addView(body)
        // ENGINE CONTROL: stop / start the console's sound, and reopen the same sound card at another rate or buffer
        val row1 = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }; val row2 = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row1.addView(Button(this).apply { text = getString(R.string.eng_stop); setOnClickListener { android.app.AlertDialog.Builder(this@ConsoleStatusActivity).setMessage(R.string.eng_stop_ask)
            .setPositiveButton(R.string.eng_stop) { _, _ -> send { it.stop() } }.setNegativeButton(android.R.string.cancel, null).show() } })
        row1.addView(Button(this).apply { text = getString(R.string.eng_start); setOnClickListener { reopen(null, null) } })
        row2.addView(Button(this).apply { text = getString(R.string.eng_rate); setOnClickListener { val rates = listOf(44100, 48000, 88200, 96000)
            android.app.AlertDialog.Builder(this@ConsoleStatusActivity).setItems(rates.map { "$it Hz" }.toTypedArray()) { _, w -> reopen(rates[w].toDouble(), null) }.show() } })
        row2.addView(Button(this).apply { text = getString(R.string.eng_buffer); setOnClickListener { val bufs = listOf(64, 128, 256, 512, 1024)
            android.app.AlertDialog.Builder(this@ConsoleStatusActivity).setItems(bufs.map { "$it" }.toTypedArray()) { _, w -> reopen(null, bufs[w]) }.show() } })
        root.addView(label(getString(R.string.eng_title), accent, 13f).apply { setPadding(0, 18, 0, 4) }); root.addView(row1); root.addView(row2)
        root.addView(label(getString(R.string.cstat_help)).apply { setPadding(0, 16, 0, 0) })
        ui.post(poll)
    }
    internal var last: com.amfb.amixos.remote.Status? = null
    /** reopen the console's sound on the same device (as the console opened it), at [rate] / [frames] when given */
    internal fun reopen(rate: Double?, frames: Int?) { val s = last; send { c -> val b = s?.backend?.ifBlank { null } ?: "auto"
        c.open(b, s?.inDevice.orEmpty(), s?.outDevice.orEmpty(), rate ?: s?.rate?.takeIf { it > 0 } ?: 48000.0, frames ?: s?.bufferFrames?.takeIf { it > 0 } ?: 256, s?.inChannels ?: 2, s?.outChannels ?: 2) } }
    internal fun render(s: com.amfb.amixos.remote.Status): String { last = s
        val up = s.uptimeS.toLong(); val bufMs = if (s.rate > 0 && s.frames > 0 && s.blocks > 0) 1000.0 * s.frames / s.blocks / s.rate else 0.0
        return listOf(
            getString(R.string.cstat_console) + "  " + s.computer.ifBlank { s.name } + "  " + s.version,
            getString(R.string.cstat_sound) + "  " + (if (s.running) "ON · " + s.backend.uppercase() else getString(R.string.cstat_off)),
            getString(R.string.cstat_rate) + "  " + (if (s.rate > 0) String.format("%.0f Hz · %d frames · %.1f ms", s.rate, s.bufferFrames, bufMs) else "—"),
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

/** a row of level bars (peak, with the RMS inside), one per channel, then the master L / R; 0 dB at the top, -60 at the bottom */
class LevelsView(ctx: Context) : View(ctx) {
    var levels: List<Pair<Double, Double>> = emptyList(); set(v) { field = v; invalidate() }   // (peak, rms) linear, channels then master L, R
    var clips: List<Boolean> = emptyList()
    private val p = android.graphics.Paint()
    override fun onMeasure(w: Int, h: Int) { setMeasuredDimension(MeasureSpec.getSize(w), 360) }
    private fun pos(lin: Double): Float { if (lin <= 1e-6) return 0f; val db = 20 * kotlin.math.log10(lin); return ((db + 60) / 60).coerceIn(0.0, 1.0).toFloat() }
    override fun onDraw(c: android.graphics.Canvas) {
        c.drawColor(Color.parseColor("#0B0F15")); val n = levels.size; if (n == 0) return
        val bw = width.toFloat() / n; val h = height - 24f
        p.color = Color.parseColor("#233040"); for (db in listOf(-6, -12, -24, -48)) { val y = h * (-db / 60f); c.drawLine(0f, y, width.toFloat(), y, p) }
        levels.forEachIndexed { i, (pk, rms) -> val x = i * bw + bw * 0.15f; val w = bw * 0.7f; val master = i >= n - 2
            p.color = Color.parseColor("#1A2230"); c.drawRect(x, 0f, x + w, h, p)
            val ypk = h * (1 - pos(pk)); val yr = h * (1 - pos(rms))
            p.color = if (pos(pk) > 0.95f) Color.parseColor("#EF4444") else if (pos(pk) > 0.8f) Color.parseColor("#FBBF24") else if (master) Color.parseColor("#EA580C") else Color.parseColor("#22D3EE")
            c.drawRect(x, ypk, x + w, h, p); p.color = Color.argb(140, 255, 255, 255); c.drawRect(x + w * 0.3f, yr, x + w * 0.7f, h, p)
            if (clips.getOrElse(i) { false }) { p.color = Color.parseColor("#EF4444"); c.drawRect(x, 0f, x + w, 6f, p) }
            p.color = Color.parseColor("#8B98A8"); p.textSize = 18f; p.textAlign = android.graphics.Paint.Align.CENTER
            c.drawText(if (i == n - 2) "L" else if (i == n - 1) "R" else "${i + 1}", x + w / 2, height - 4f, p) }
    }
}

/** METERS & LOUDNESS: every channel's level and the master, the master's loudness (LUFS momentary / short-term / integrated, range,
 *  true peak), the phase correlation and dropouts; five times a second. */
class MetersActivity : ConsoleSection() {
    override val titleRes = R.string.meters_title
    internal lateinit var levels: LevelsView; internal lateinit var loud: TextView
    override fun build() {
        levels = LevelsView(this); root.addView(levels)
        loud = TextView(this).apply { setTextColor(Color.WHITE); typeface = Typeface.MONOSPACE; textSize = 14f; setPadding(0, 16, 0, 0); setLineSpacing(6f, 1f) }; root.addView(loud)
        root.addView(label(getString(R.string.meters_help)).apply { setPadding(0, 16, 0, 0) })
        ui.post(poll)
    }
    internal fun show(m: com.amfb.amixos.remote.Meters) {
        val n = Prefs.stripCount(this).coerceAtMost(m.strips.size)
        levels.clips = m.strips.take(n).map { it.clip } + listOf(m.masterClip, m.masterClip)
        levels.levels = m.strips.take(n).map { it.pk to it.rms } + listOf(m.masterL to m.masterRms, m.masterR to m.masterRms)
        fun db(v: Double) = if (v <= -119) "-inf" else String.format("%.1f", v)
        val c = m.correlation; val silent = m.loudness.m <= -119 && maxOf(m.masterL, m.masterR) <= 1e-6
        loud.text = listOf(getString(R.string.meters_lufs, db(m.loudness.m), db(m.loudness.s), db(m.loudness.i)),
            getString(R.string.meters_range, String.format("%.1f", m.loudness.lra), db(m.loudness.tp)),
            if (silent) getString(R.string.meters_phase, "—", getString(R.string.meters_phase_silent))   // no sound: nothing to judge
            else getString(R.string.meters_phase, String.format("%+.2f", c), getString(if (c < 0) R.string.meters_phase_bad else if (c < 0.3) R.string.meters_phase_wide else R.string.meters_phase_ok)),
            getString(R.string.meters_xruns, m.xruns)).joinToString("\n")
    }
    @Volatile private var busy = false
    private val poll = object : Runnable { override fun run() { val cl = client ?: return
        if (!busy) { busy = true; io.execute { val m = runCatching { cl.meters() }; busy = false; ui.post { m.onSuccess { show(it); note.text = "" }.onFailure { e -> note.text = e.message } } } }
        ui.postDelayed(this, 200) } }
    override fun onDestroy() { ui.removeCallbacks(poll); super.onDestroy() }
}

/** SOUND SCORE: the console's score of the master, live, with the six parts and what to do about each (SoundScore.kt, the console's rules).
 *  The tone part reads the master's RTA, which this screen keeps running while it is open. */
class SoundScoreActivity : ConsoleSection() {
    override val titleRes = R.string.score_title
    internal lateinit var big: TextView; internal lateinit var parts: LinearLayout; internal var last: SoundScore.Score? = null
    private var target = -16.0; private var asked = 0L; @Volatile private var busy = false
    override fun build() {
        big = TextView(this).apply { textSize = 56f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER; setTextColor(dim); text = "—" }; root.addView(big)
        parts = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; root.addView(parts)
        root.addView(label(getString(R.string.score_help)).apply { setPadding(0, 16, 0, 0) })
        client?.let { c -> io.execute { runCatching { c.loudnessTarget() }.onSuccess { target = it } } }
        ui.post(poll)
    }
    internal fun show(s: SoundScore.Score) {
        last = s
        if (!s.valid) { big.text = "—"; big.setTextColor(dim); parts.removeAllViews(); parts.addView(label(getString(R.string.score_silent))); return }
        val col = Color.parseColor(if (s.total >= 85) "#34D399" else if (s.total >= 70) "#22D3EE" else if (s.total >= 50) "#FBBF24" else "#EF4444")
        big.text = "${s.total.toInt()}  ${s.grade}"; big.setTextColor(col); parts.removeAllViews()
        for (p in s.parts) { parts.addView(label("${p.name}   ${p.points.toInt()} / ${p.max.toInt()}", Color.WHITE, 14f).apply { setPadding(0, 12, 0, 0) })
            parts.addView(android.widget.ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 100; progress = (100 * p.points / p.max).toInt() })
            if (p.points < p.max) parts.addView(label("→ " + p.tip, Color.parseColor("#FBBF24"), 12f)) }
    }
    private val poll = object : Runnable { override fun run() { val cl = client ?: return
        if (!busy) { busy = true; io.execute { val now = System.currentTimeMillis(); if (now - asked > 8000) { asked = now; runCatching { cl.rta(-1) } }
            val m = runCatching { cl.meters() }.getOrNull(); val r = cl.rtaNow(); busy = false
            if (m != null) { fun db(lin: Double) = if (lin <= 1e-6) -120.0 else 20 * kotlin.math.log10(lin)
                val inp = SoundScore.Input(shortTerm = m.loudness.s, target = target, truePeak = m.loudness.tp, peakDb = db(maxOf(m.masterL, m.masterR)), rmsDb = db(m.masterRms),
                    correlation = m.correlation, freqs = r?.f.orEmpty(), spectrum = r?.db.orEmpty(), clipStrips = m.strips.count { it.clip }, xruns = m.xruns, masterClip = m.masterClip)
                val s = SoundScore.score(inp); ui.post { show(s) } } } }
        ui.postDelayed(this, 500) } }
    override fun onDestroy() { ui.removeCallbacks(poll); client?.let { c -> Thread { runCatching { c.rta(null) } }.start() }; super.onDestroy() }
}

/** ROUTING: INPUTS (which device input feeds each channel, or none) and OUTPUTS (what each device output carries: master, monitor, a bus).
 *  Kept by the console's service and followed by the console at once. Tap a row to change it. */
class RoutingActivity : ConsoleSection() {
    override val titleRes = R.string.routing_title
    internal var routing: com.amfb.amixos.remote.Routing? = null; private var tab = 0; internal lateinit var list: LinearLayout
    override fun build() {
        val tabs = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf(R.string.routing_inputs, R.string.routing_outputs).forEachIndexed { k, r -> tabs.addView(Button(this).apply { text = getString(r); setOnClickListener { tab = k; render() } }) }
        root.addView(tabs); list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; root.addView(list)
        root.addView(label(getString(R.string.routing_help)).apply { setPadding(0, 16, 0, 0) })
        load()
    }
    private fun load() { val c = client ?: return; io.execute { val r = runCatching { c.routing() }; ui.post { r.onSuccess { routing = it; render() }.onFailure { e -> note.text = e.message } } } }
    private fun srcName(id: Int) = routing?.sources?.firstOrNull { it.first == id }?.second ?: "#$id"
    internal fun render() { val r = routing ?: return; list.removeAllViews()
        if (tab == 0) {
            note.text = getString(R.string.routing_device_in, r.deviceIn)
            r.inputs.take(Prefs.stripCount(this)).forEachIndexed { s, inp -> list.addView(Button(this).apply {
                text = String.format("CH %02d   ←   %s", s + 1, if (inp < 0) getString(R.string.routing_none) else "IN ${inp + 1}" + if (inp >= r.deviceIn) "  (" + getString(R.string.routing_unavailable) + ")" else "")
                isAllCaps = false; gravity = Gravity.START or Gravity.CENTER_VERTICAL
                setOnClickListener { val n = maxOf(r.deviceIn, 2, inp + 1); val names = listOf(getString(R.string.routing_none)) + (1..n).map { "IN $it" }
                    android.app.AlertDialog.Builder(this@RoutingActivity).setTitle(String.format("CH %02d", s + 1)).setItems(names.toTypedArray()) { _, w ->
                        val v = w - 1; send { it.inRoute(s, v) }; routing = r.copy(inputs = r.inputs.toMutableList().also { l -> l[s] = v }); render() }.show() } }) }
        } else {
            note.text = getString(R.string.routing_device_out, r.deviceOut)
            r.outputs.take(maxOf(r.deviceOut, 8)).forEachIndexed { o, src -> list.addView(Button(this).apply {
                text = String.format("OUT %02d   →   %s", o + 1, srcName(src)) + if (o >= r.deviceOut) "  (" + getString(R.string.routing_unavailable) + ")" else ""
                isAllCaps = false; gravity = Gravity.START or Gravity.CENTER_VERTICAL
                setOnClickListener { val names = r.sources.map { it.second }
                    android.app.AlertDialog.Builder(this@RoutingActivity).setTitle(String.format("OUT %02d", o + 1)).setItems(names.toTypedArray()) { _, w ->
                        val v = r.sources[w].first; send { it.outRoute(o, v) }; routing = r.copy(outputs = r.outputs.toMutableList().also { l -> l[o] = v }); render() }.show() } }) }
        }
    }
}
