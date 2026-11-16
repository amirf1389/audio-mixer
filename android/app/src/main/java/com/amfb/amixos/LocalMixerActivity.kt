package com.amfb.amixos

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.media.AudioDeviceInfo
import android.media.AudioManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.MiniJson

/** THIS PHONE: the AmF X1 mixer running on the phone itself.
 *  INPUT: any input Android offers (built-in mic, headset, USB interface ...), MONO on CH 1 or STEREO on CH 1-2 (inputs 1 and 2 of a USB
 *  interface, or a stereo mic). OUTPUT: speaker, headphones, USB, Bluetooth ... Each channel: fader, pan, mute and an INSERT RACK of
 *  AmF X1 effects (the console's rack effects; desktop VST / VST3 plug-ins are Windows / macOS / Linux binaries and cannot load on Android);
 *  MASTER: fader, limiter and its own rack. What is chosen is kept for next time. Rate and buffer come from the phone's audio HAL. */
class LocalMixerActivity : AppCompatActivity() {
    companion object {
        const val STRIPS = 2
        /** the rack of a strip (-1 = master) as kept in prefs: one entry per slot, "kind" or "" */
        fun rackKey(strip: Int) = if (strip < 0) "rackMaster" else "rack$strip"
        fun paramKey(strip: Int, slot: Int) = "fx_${if (strip < 0) "m" else strip.toString()}_$slot"
    }
    private val hal by lazy { AudioHal.read(this) }
    private val rate by lazy { hal.nativeRate.takeIf { it in 8000..192000 } ?: 48000 }
    private val frames by lazy { hal.nativeFrames.takeIf { it in 32..2048 } ?: 256 }
    private val bursts by lazy { BrandProfile.of(hal).bursts }
    /** the native engine; null when it cannot load (the JVM tests): the screen is still drawn */
    private val engine: Engine? by lazy { runCatching { Engine(STRIPS, rate.toDouble()) }.getOrNull()?.also { applyAll(it) } }
    private var loop: AudioLoop? = null
    private val ui = Handler(Looper.getMainLooper())
    private val accent by lazy { getColor(R.color.accent) }; private val cyan by lazy { getColor(R.color.cyan) }; private val dim by lazy { getColor(R.color.dim) }
    private lateinit var body: LinearLayout; private lateinit var status: TextView; private lateinit var startBtn: Button
    private val meters = HashMap<String, ProgressBar>(); private val fxBtns = HashMap<Int, Button>()
    private fun prefs() = Prefs.local(this)
    private fun inChannels() = prefs().getInt("inChannels", 1).coerceIn(1, 2)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        runCatching { Engine.setDirs(filesDir.absolutePath, cacheDir.absolutePath, cacheDir.absolutePath) }
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#0F141C")); addView(body) }, null))
        render()
        ui.post(object : Runnable { override fun run() { showMeters(); ui.postDelayed(this, 50) } })
    }

    /** builds the whole screen from the saved choices (again after a change of input channels or devices) */
    fun render() {
        body.removeAllViews(); meters.clear(); fxBtns.clear()
        body.addView(TextView(this).apply { text = getString(R.string.home_phone); setTextColor(accent); textSize = 20f; setTypeface(null, Typeface.BOLD) })
        status = TextView(this).apply { setTextColor(dim); textSize = 12f; typeface = Typeface.MONOSPACE; setPadding(0, 4, 0, 12) }; body.addView(status); showStatus()
        // ---- INPUT / OUTPUT
        val io = card(); body.addView(io)
        io.addView(label(getString(R.string.lm_input)))
        io.addView(Button(this).apply { text = deviceName(Prefs.localInDevice(this@LocalMixerActivity), false); isAllCaps = false; setOnClickListener { pickDevice(false) } })
        val chRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf(1 to getString(R.string.lm_mono), 2 to getString(R.string.lm_stereo)).forEach { (n, t) ->
            chRow.addView(Button(this).apply { text = t; setTextColor(if (inChannels() == n) accent else Color.WHITE); setOnClickListener { setInChannels(n) } }) }
        io.addView(chRow)
        io.addView(label(getString(R.string.lm_output)))
        io.addView(Button(this).apply { text = deviceName(Prefs.localOutDevice(this@LocalMixerActivity), true); isAllCaps = false; setOnClickListener { pickDevice(true) } })
        startBtn = Button(this).apply { text = getString(if (loop == null) R.string.start else R.string.stop); textSize = 16f; setOnClickListener { if (loop == null) startAudio() else stopAudio() } }
        io.addView(startBtn)
        // ---- CHANNELS and MASTER
        for (s in 0 until inChannels()) body.addView(stripCard(s))
        body.addView(stripCard(-1))
        body.addView(TextView(this).apply { text = getString(R.string.lm_note); setTextColor(dim); textSize = 12f; setPadding(0, 16, 0, 0) })
    }
    private fun card() = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(28, 22, 28, 22)
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 8, 0, 8) } }
    private fun label(t: String) = TextView(this).apply { text = t; setTextColor(cyan); textSize = 11f; letterSpacing = 0.15f; typeface = Typeface.MONOSPACE; setPadding(0, 10, 0, 2) }
    private fun bar() = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000; progressDrawable.setTint(Color.parseColor("#34D399"))
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 22) }

    /** one channel (strip 0, 1) or the master (-1): fader, pan / limiter, mute, the insert rack, a meter */
    private fun stripCard(s: Int): View {
        val c = card(); val p = prefs(); val master = s < 0
        val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        head.addView(TextView(this).apply { text = if (master) getString(R.string.lm_master) else getString(R.string.lm_channel, s + 1, if (inChannels() == 2) (if (s == 0) "L" else "R") else ""); setTextColor(if (master) accent else Color.WHITE); textSize = 15f; setTypeface(null, Typeface.BOLD)
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
        head.addView(Button(this).apply { text = fxLabel(s); setOnClickListener { showRack(s) } }.also { fxBtns[s] = it })
        c.addView(head)
        if (!master) { val mute = p.getBoolean("mute$s", false)
            c.addView(Button(this).apply { text = getString(R.string.lm_mute); setTextColor(if (mute) Color.parseColor("#EF4444") else Color.WHITE)
                setOnClickListener { val on = !p.getBoolean("mute$s", false); p.edit().putBoolean("mute$s", on).apply(); engine?.setMute(s, on); setTextColor(if (on) Color.parseColor("#EF4444") else Color.WHITE) } }) }
        val key = if (master) "masterDb" else "db$s"; val dbText = TextView(this).apply { setTextColor(Color.WHITE); typeface = Typeface.MONOSPACE }
        fun showDb(db: Double) { dbText.text = getString(R.string.lm_level, if (db <= -60) "-∞" else String.format("%+.1f dB", db)) }
        showDb(p.getFloat(key, 0f).toDouble()); c.addView(dbText)
        c.addView(SeekBar(this).apply { max = 700; progress = ((p.getFloat(key, 0f) + 60) * 10).toInt()
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(b: SeekBar, v: Int, u: Boolean) { if (!u) return; val db = -60.0 + v / 10.0; p.edit().putFloat(key, db.toFloat()).apply(); showDb(db); if (master) engine?.setMaster(db) else engine?.setGain(s, db) }
                override fun onStartTrackingTouch(b: SeekBar) {}; override fun onStopTrackingTouch(b: SeekBar) {} }) })
        if (master) {
            val lim = p.getBoolean("limiter", true)
            c.addView(Button(this).apply { text = getString(R.string.lm_limiter); setTextColor(if (lim) Color.parseColor("#34D399") else Color.WHITE)
                setOnClickListener { val on = !p.getBoolean("limiter", true); p.edit().putBoolean("limiter", on).apply(); engine?.setLimiter(on, -0.5, 100.0); setTextColor(if (on) Color.parseColor("#34D399") else Color.WHITE) } })
            c.addView(label("L")); c.addView(bar().also { meters["L"] = it }); c.addView(label("R")); c.addView(bar().also { meters["R"] = it })
        } else {
            val panText = TextView(this).apply { setTextColor(dim); typeface = Typeface.MONOSPACE }
            fun showPan(v: Float) { panText.text = getString(R.string.lm_pan, if (Math.abs(v) < 0.01f) "C" else if (v < 0) "L" + (-v * 100).toInt() else "R" + (v * 100).toInt()) }
            val defPan = if (inChannels() == 2) (if (s == 0) -1f else 1f) else 0f
            showPan(p.getFloat("pan$s", defPan)); c.addView(panText)
            c.addView(SeekBar(this).apply { max = 200; progress = ((p.getFloat("pan$s", defPan) + 1) * 100).toInt()
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(b: SeekBar, v: Int, u: Boolean) { if (!u) return; val pan = (v - 100) / 100f; p.edit().putFloat("pan$s", pan).apply(); showPan(pan); engine?.setPan(s, pan.toDouble()) }
                    override fun onStartTrackingTouch(b: SeekBar) {}; override fun onStopTrackingTouch(b: SeekBar) {} }) })
            c.addView(label(getString(R.string.lm_in_level))); c.addView(bar().also { meters["s$s"] = it })
        }
        return c
    }

    // ---- INSERT RACK
    private fun rack(s: Int): MutableList<String> { val l = prefs().getString(rackKey(s), "").orEmpty().split('|').toMutableList(); while (l.size < Engine.MAX_INSERTS) l.add(""); return l.take(Engine.MAX_INSERTS).toMutableList() }
    private fun saveRack(s: Int, l: List<String>) = prefs().edit().putString(rackKey(s), l.joinToString("|")).apply()
    private fun fxLabel(s: Int): String { val n = rack(s).count { it.isNotEmpty() }; return if (n == 0) getString(R.string.lm_fx_add) else getString(R.string.lm_fx_n, n) }
    private val catalog: List<Pair<String, String>> by lazy {
        @Suppress("UNCHECKED_CAST") val l = runCatching { MiniJson.parse(Engine.fxCatalogJson()) as List<Map<String, Any?>> }.getOrDefault(emptyList())
        l.map { it["kind"].toString() to it["name"].toString() } }
    private fun showRack(s: Int) {
        val dlg = AlertDialog.Builder(this).setTitle(if (s < 0) getString(R.string.lm_rack_master) else getString(R.string.lm_rack_ch, s + 1)).setPositiveButton(android.R.string.ok, null).create()
        val holder = ScrollView(this)
        fun refresh() { holder.removeAllViews(); holder.addView(rackPanel(s, catalog, { slot -> engine?.insertJson(s, slot) ?: "null" }, { refresh() })); fxBtns[s]?.text = fxLabel(s) }
        refresh(); dlg.setView(holder); dlg.setOnDismissListener { fxBtns[s]?.text = fxLabel(s) }; dlg.show()
    }
    /** the rack of one strip: four slots, each empty (+ ADD) or an effect with BYPASS, REMOVE, MIX and its knobs. [slotJson] gives what the
     *  engine reports for a slot (a test passes made-up JSON); [changed] redraws. */
    fun rackPanel(s: Int, cat: List<Pair<String, String>>, slotJson: (Int) -> String, changed: () -> Unit): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 12, 24, 12); setBackgroundColor(Color.parseColor("#141B26")) }
        val names = rack(s)
        for (slot in 0 until Engine.MAX_INSERTS) {
            val c = card(); box.addView(c)
            val kind = names[slot]
            if (kind.isEmpty()) {
                c.addView(Button(this).apply { text = getString(R.string.lm_slot_add, slot + 1); setOnClickListener {
                    if (cat.isEmpty()) return@setOnClickListener
                    AlertDialog.Builder(this@LocalMixerActivity).setTitle(R.string.lm_pick_fx).setItems(cat.map { it.second }.toTypedArray()) { _, w ->
                        val k = cat[w].first; if (engine?.insert(s, slot, k) == 0 || engine == null) { val l = rack(s); l[slot] = k; saveRack(s, l); prefs().edit().remove(paramKey(s, slot)).apply() }; changed() }.show() } })
                continue
            }
            @Suppress("UNCHECKED_CAST") val st = runCatching { MiniJson.parse(slotJson(slot)) as? Map<String, Any?> }.getOrNull()
            val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            val byp = st?.get("bypass") == true
            head.addView(TextView(this).apply { text = (slot + 1).toString() + "  " + (st?.get("name")?.toString() ?: cat.firstOrNull { it.first == kind }?.second ?: kind); setTextColor(if (byp) dim else Color.WHITE); textSize = 14f; setTypeface(null, Typeface.BOLD)
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
            head.addView(Button(this).apply { text = getString(R.string.lm_bypass); textSize = 11f; setTextColor(if (byp) Color.parseColor("#FBBF24") else Color.WHITE)
                setOnClickListener { engine?.insertSet(s, slot, "bypass", if (byp) 0.0 else 1.0); setParam(s, slot, "bypass", if (byp) 0.0 else 1.0); changed() } })
            head.addView(Button(this).apply { text = "✕"; textSize = 11f; contentDescription = getString(R.string.lm_remove)
                setOnClickListener { engine?.insert(s, slot, ""); val l = rack(s); l[slot] = ""; saveRack(s, l); prefs().edit().remove(paramKey(s, slot)).apply(); changed() } })
            c.addView(head)
            @Suppress("UNCHECKED_CAST") val params = (st?.get("params") as? List<Map<String, Any?>>).orEmpty()
            val knobs = listOf(mapOf("id" to "mix", "title" to "MIX", "value" to ((st?.get("mix") as? Number)?.toDouble() ?: 1.0), "text" to String.format("%.0f %%", ((st?.get("mix") as? Number)?.toDouble() ?: 1.0) * 100))) + params
            for (k in knobs) {
                val id = k["id"].toString(); val v = (k["value"] as? Number)?.toDouble() ?: 0.0
                val t = TextView(this).apply { text = k["title"].toString() + "   " + k["text"].toString(); setTextColor(dim); textSize = 11f; typeface = Typeface.MONOSPACE }
                c.addView(t)
                c.addView(SeekBar(this).apply { max = 1000; progress = (v * 1000).toInt()
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(b: SeekBar, x: Int, u: Boolean) { if (!u) return; val nv = x / 1000.0; engine?.insertSet(s, slot, id, nv); setParam(s, slot, id, nv)
                            t.text = k["title"].toString() + "   " + (if (id == "mix") String.format("%.0f %%", nv * 100) else liveText(s, slot, id) ?: String.format("%.2f", nv)) }
                        override fun onStartTrackingTouch(b: SeekBar) {}; override fun onStopTrackingTouch(b: SeekBar) {} }) })
            }
        }
        return box
    }
    private fun liveText(s: Int, slot: Int, id: String): String? { @Suppress("UNCHECKED_CAST") val st = runCatching { MiniJson.parse(engine?.insertJson(s, slot) ?: "null") as? Map<String, Any?> }.getOrNull() ?: return null
        @Suppress("UNCHECKED_CAST") return (st["params"] as? List<Map<String, Any?>>)?.firstOrNull { it["id"] == id }?.get("text")?.toString() }
    /** a slot's knob values kept as "id=value;id=value" so the rack comes back as it was */
    private fun setParam(s: Int, slot: Int, id: String, v: Double) {
        val m = LinkedHashMap<String, String>(); prefs().getString(paramKey(s, slot), "").orEmpty().split(';').filter { '=' in it }.forEach { m[it.substringBefore('=')] = it.substringAfter('=') }
        m[id] = String.format(java.util.Locale.US, "%.4f", v); prefs().edit().putString(paramKey(s, slot), m.entries.joinToString(";") { it.key + "=" + it.value }).apply()
    }
    /** puts the saved levels, pans, mutes, limiter and racks onto a fresh engine */
    private fun applyAll(e: Engine) {
        val p = prefs(); val two = inChannels() == 2
        for (s in 0 until STRIPS) { e.setGain(s, p.getFloat("db$s", if (s == 1 && !two) -60f else 0f).toDouble()); e.setPan(s, p.getFloat("pan$s", if (two) (if (s == 0) -1f else 1f) else 0f).toDouble()); e.setMute(s, p.getBoolean("mute$s", false)) }
        e.setMaster(p.getFloat("masterDb", 0f).toDouble()); e.setLimiter(p.getBoolean("limiter", true), -0.5, 100.0)
        for (s in listOf(-1) + (0 until STRIPS)) rack(s).forEachIndexed { slot, k -> if (k.isNotEmpty() && e.insert(s, slot, k) == 0)
            p.getString(paramKey(s, slot), "").orEmpty().split(';').filter { '=' in it }.forEach { e.insertSet(s, slot, it.substringBefore('='), it.substringAfter('=').toDoubleOrNull() ?: 0.0) } }
    }

    // ---- DEVICES
    private fun deviceName(id: Int, out: Boolean): String {
        if (id == 0) return getString(if (out) R.string.lm_out_system else R.string.lm_in_system)
        val am = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val d = am.getDevices(if (out) AudioManager.GET_DEVICES_OUTPUTS else AudioManager.GET_DEVICES_INPUTS).firstOrNull { it.id == id } ?: return getString(R.string.lm_gone)
        return (d.productName?.toString()?.takeIf { it.isNotBlank() } ?: "") + "  ·  " + DevicesActivity.typeName(d.type)
    }
    private fun pickDevice(out: Boolean) {
        val am = getSystemService(Context.AUDIO_SERVICE) as AudioManager
        val list = am.getDevices(if (out) AudioManager.GET_DEVICES_OUTPUTS else AudioManager.GET_DEVICES_INPUTS).filter { !(out && it.type == AudioDeviceInfo.TYPE_TELEPHONY) }
        val labels = listOf(getString(if (out) R.string.lm_out_system else R.string.lm_in_system)) + list.map { d ->
            val ch = d.channelCounts.distinct().sorted(); (d.productName?.toString()?.takeIf { it.isNotBlank() } ?: "") + "  ·  " + DevicesActivity.typeName(d.type) + (if (ch.isNotEmpty()) "  ·  " + ch.joinToString("/") + " ch" else "") }
        AlertDialog.Builder(this).setTitle(if (out) R.string.lm_output else R.string.lm_input).setItems(labels.toTypedArray()) { _, w ->
            prefs().edit().putInt(if (out) "outDevice" else "inDevice", if (w == 0) 0 else list[w - 1].id).apply(); restartIfRunning(); render() }.show()
    }
    private fun setInChannels(n: Int) {
        val p = prefs(); if (inChannels() == n) return
        p.edit().putInt("inChannels", n).remove("pan0").remove("pan1").apply()
        engine?.let { e -> for (s in 0 until STRIPS) e.setPan(s, if (n == 2) (if (s == 0) -1.0 else 1.0) else 0.0); e.setGain(1, if (n == 2) p.getFloat("db1", 0f).toDouble() else -60.0) }
        restartIfRunning(); render()
    }
    private fun restartIfRunning() { if (loop != null) { stopAudio(); startAudio() } }

    // ---- AUDIO
    private fun startAudio() {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) { requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1); return }
        val e = engine ?: run { status.text = getString(R.string.lm_no_engine); return }
        loop = AudioLoop(e, rate, frames, Prefs.localOutDevice(this), Prefs.localInDevice(this), bursts, inChannels()).also { it.start() }
        if (::startBtn.isInitialized) startBtn.text = getString(R.string.stop)
        ui.postDelayed({ showStatus() }, 400)
    }
    private fun stopAudio() { loop?.stop(); loop = null; if (::startBtn.isInitialized) startBtn.text = getString(R.string.start); showStatus() }
    private fun showStatus() {
        if (!::status.isInitialized) return
        val l = loop; val ch = runCatching { Engine.aaudioInChannels() }.getOrDefault(0)
        status.text = if (l == null) getString(R.string.lm_stopped, rate, frames) else getString(R.string.lm_running, l.path.uppercase(), rate, frames, if (l.path == "aaudio") (if (ch == 2) "STEREO" else if (ch == 1) "MONO" else "—") else "MONO")
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 1 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) startAudio()
    }
    private fun showMeters() {
        val p = loop?.lastPeaks ?: return
        fun lvl(v: Float) = (((if (v > 1e-6f) 20 * Math.log10(v.toDouble()) else -120.0) + 60) / 60 * 1000).toInt().coerceIn(0, 1000)
        for (s in 0 until STRIPS) meters["s$s"]?.progress = lvl(p.getOrElse(s) { 0f })
        meters["L"]?.progress = lvl(p.getOrElse(STRIPS) { 0f }); meters["R"]?.progress = lvl(p.getOrElse(STRIPS + 1) { 0f })
    }
    override fun onDestroy() { stopAudio(); runCatching { engine?.close() }; super.onDestroy() }
}
