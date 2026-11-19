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
        /** the engine's strips: 0-1 the inputs (mic / interface), 2-3 DECK A (left, right), 4-5 DECK B */
        const val STRIPS = 6
        const val INPUTS = 2
        fun deckStrip(d: Int) = INPUTS + 2 * d
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
    /** the mixer's real rate: the native stream may run at the device's own rate */
    private fun mixRate(): Double { val st = if (loop?.path == "aaudio") runCatching { engine?.nativeState() }.getOrNull() else null; return (st?.getOrNull(8)?.takeIf { it > 0 } ?: rate).toDouble() }
    /** THE TWO DECKS: a song file each, on strips 3-4 and 5-6 */
    private val decks: List<DeckPlayer> by lazy { engine?.let { e -> (0..1).map { d -> DeckPlayer(this, e, d, deckStrip(d), ::mixRate).also { it.listener = object : DeckPlayer.Listener { override fun changed(deck: DeckPlayer) { ui.post { showDecks() } } } } } } ?: emptyList() }
    private var pickFor = 0
    private val pickFile = registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.OpenDocument()) { u -> if (u != null) loadDeck(pickFor, u, true) }
    private val deckViews = HashMap<Int, DeckViews>()
    private class DeckViews(val title: TextView, val time: TextView, val seek: SeekBar, val play: PadButton, val loop: PadButton)
    // MASTER RECORDER: the stereo master to a file (FLAC 24-bit, the recorder's folder)
    @Volatile private var recWriter: HiRes.Writer? = null; private var recThread: Thread? = null; private var recFile: java.io.File? = null; private lateinit var recBtn: PadButton
    private val ui = Handler(Looper.getMainLooper())
    private val accent by lazy { getColor(R.color.accent) }; private val cyan by lazy { getColor(R.color.cyan) }; private val dim by lazy { getColor(R.color.dim) }
    private lateinit var body: LinearLayout; private lateinit var status: TextView; private lateinit var startBtn: PadButton
    private val meters = HashMap<String, ProgressBar>(); private val fxBtns = HashMap<Int, PadButton>()
    private class IoViews(val lamp: View, val name: TextView, val state: TextView, val meter: ProgressBar)
    private val io = HashMap<Boolean, IoViews>()   // false = INPUT, true = OUTPUT
    private fun prefs() = Prefs.local(this)
    private fun inChannels() = prefs().getInt("inChannels", 1).coerceIn(1, 2)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        runCatching { Engine.setDirs(filesDir.absolutePath, cacheDir.absolutePath, cacheDir.absolutePath) }
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(20, 20, 20, 16); setBackgroundColor(Color.parseColor("#0B0F15")) }
        setContentView(Nav.wrap(this, body, Nav.Tab.MIXER))   // the MIXER tab, THIS PHONE mode
        render()
        ui.post(object : Runnable { override fun run() { showMeters(); ui.postDelayed(this, 50) } })
    }

    /** builds the whole screen from the saved choices (again after a change of input channels or devices): a header with the INPUT and
     *  OUTPUT keys, then the desk: one strip per channel and the MASTER strip on the right, like the console's own mixer */
    fun render() {
        body.removeAllViews(); meters.clear(); fxBtns.clear(); faders.clear()
        body.addView(Nav.modeBar(this, true))   // DUAL MODE: THIS PHONE | CONSOLE
        val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        head.addView(TextView(this).apply { text = getString(R.string.home_phone); setTextColor(accent); textSize = 20f; setTypeface(null, Typeface.BOLD); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
        startBtn = PadButton(this, getString(if (loop == null) R.string.start else R.string.stop), ConsoleUi.GREEN, 14f).apply { on = loop != null; layoutParams = LinearLayout.LayoutParams(300, 96); setOnClickListener { if (loop == null) startAudio() else stopAudio() } }
        recBtn = PadButton(this, getString(if (recWriter != null) R.string.lm_rec_stop else R.string.lm_rec), ConsoleUi.RED, 14f).apply { on = recWriter != null; layoutParams = LinearLayout.LayoutParams(220, 96).apply { setMargins(0, 0, 12, 0) }; setOnClickListener { toggleRec() } }
        head.addView(recBtn); head.addView(startBtn); body.addView(head)
        status = TextView(this).apply { setTextColor(dim); textSize = 11f; typeface = Typeface.MONOSPACE; setPadding(0, 2, 0, 10) }; body.addView(status); showStatus()
        // ---- INPUT / OUTPUT: two keys that open the device lists, and MONO / STEREO
        // each row: a LAMP (stopped / not open / no signal / live / clipping), the device, what it is doing and a level bar under it
        fun ioRow(tag: String, name: String, color: Int, out: Boolean, click: () -> Unit): LinearLayout {
            val lamp = View(this).apply { background = android.graphics.drawable.GradientDrawable().apply { shape = android.graphics.drawable.GradientDrawable.OVAL; setColor(IoLamp.color(IoLamp.State.STOPPED)) }
                layoutParams = LinearLayout.LayoutParams(22, 22).apply { setMargins(0, 0, 16, 0) } }
            val nameV = TextView(this).apply { text = name; setTextColor(Color.WHITE); textSize = 13f; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }
            val stateV = TextView(this).apply { setTextColor(dim); textSize = 10f; typeface = Typeface.MONOSPACE; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }
            val meter = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000; progress = 0; progressTintList = android.content.res.ColorStateList.valueOf(color)
                progressBackgroundTintList = android.content.res.ColorStateList.valueOf(0xFF0B0F15.toInt()); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 10).apply { setMargins(0, 10, 0, 0) } }
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL
                addView(lamp)
                addView(TextView(this@LocalMixerActivity).apply { text = tag; setTextColor(color); textSize = 10f; letterSpacing = 0.15f; typeface = Typeface.DEFAULT_BOLD; layoutParams = LinearLayout.LayoutParams(170, LinearLayout.LayoutParams.WRAP_CONTENT) })
                addView(LinearLayout(this@LocalMixerActivity).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); addView(nameV); addView(stateV) })
                addView(TextView(this@LocalMixerActivity).apply { text = "▾"; setTextColor(dim); textSize = 14f }) }
            io[out] = IoViews(lamp, nameV, stateV, meter)
            return LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; isClickable = true; setOnClickListener { click() }
                background = android.graphics.drawable.GradientDrawable().apply { setColor(0xFF1D2024.toInt()); cornerRadius = 16f; setStroke(2, 0xFF42474E.toInt()) }; setPadding(22, 14, 22, 14)
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 0, 8) }
                addView(row); addView(meter); contentDescription = tag }
        }
        body.addView(ioRow(getString(R.string.lm_input), deviceName(Prefs.localInDevice(this), false), cyan, false) { pickDevice(false) })
        body.addView(ioRow(getString(R.string.lm_output), deviceName(Prefs.localOutDevice(this), true), accent, true) { pickDevice(true) })
        showIo()
        val chRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 10) }
        listOf(1 to getString(R.string.lm_mono), 2 to getString(R.string.lm_stereo)).forEach { (n, t) ->
            chRow.addView(PadButton(this, t, cyan, 11f).apply { on = inChannels() == n; layoutParams = LinearLayout.LayoutParams(0, 80, 1f).apply { setMargins(0, 0, if (n == 1) 8 else 0, 0) }; setOnClickListener { setInChannels(n) } }) }
        // INPUT ON / OFF: OFF opens no microphone at all (the phone records nothing); the lamp then says OFF, not "open"
        val inRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 10) }
        listOf(true to getString(R.string.lm_input_on), false to getString(R.string.lm_input_off)).forEach { (v, t) ->
            inRow.addView(PadButton(this, t, if (v) cyan else ConsoleUi.RED, 11f).apply { on = Prefs.localInputOn(this@LocalMixerActivity) == v; layoutParams = LinearLayout.LayoutParams(0, 80, 1f).apply { setMargins(0, 0, if (v) 8 else 0, 0) }
                setOnClickListener { if (Prefs.localInputOn(this@LocalMixerActivity) != v) { Prefs.setLocalInputOn(this@LocalMixerActivity, v); restartIfRunning(); render() } } }) }
        body.addView(chRow); body.addView(inRow)
        // ---- QUICK CHAINS: one tap fills the channel's rack (both channels in STEREO) with a ready chain
        body.addView(TextView(this).apply { text = getString(R.string.lm_chains); setTextColor(dim); textSize = 10f; letterSpacing = 0.15f; setPadding(0, 0, 0, 6) })
        val chainRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; tag = "chainRow" }; val nowChain = LocalChains.current(prefs(), 0)
        for (c in LocalChains.chains) chainRow.addView(PadButton(this, c.glyph + " " + c.title, cyan, 11f).apply { on = nowChain == c.id; contentDescription = c.sub; setPadding(22, 0, 22, 0)
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, 80).apply { setMargins(0, 0, 8, 0) }; setOnClickListener { applyChain(c) } })
        body.addView(android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; setPadding(0, 0, 0, 10); addView(chainRow) })
        // ---- THE DESK
        val desk = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f) }
        // ---- DECKS: two song players (a card each: the file, time, seek, LOAD / PLAY / STOP / LOOP); their channels are on the desk
        val deckRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 8) }
        for (d in 0..1) deckRow.addView(deckCard(d).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(if (d == 0) 0 else 6, 0, if (d == 0) 6 else 0, 0) } })
        body.addView(deckRow)
        for (s in 0 until inChannels()) desk.addView(stripView(s))
        for (d in 0..1) desk.addView(deckStripView(d))
        desk.addView(stripView(-1)); body.addView(desk); showDecks()
    }
    private val faders = HashMap<Int, FaderView>()

    /** one channel strip (0, 1) or the MASTER (-1), top to bottom: name, FX (the insert rack), pan knob or LIMIT, MUTE, the fader with
     *  its LED meter, the level in dB */
    private fun stripView(s: Int): View {
        val p = prefs(); val master = s < 0; val two = inChannels() == 2
        val color = if (master) accent else ConsoleUi.palette[(s + 1) % ConsoleUi.palette.size]
        val col = ConsoleUi.strip(this, color, 0, master).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, if (master) 1.15f else 1f).apply { setMargins(4, 0, 4, 0) } }
        col.addView(TextView(this).apply { text = if (master) getString(R.string.lm_master) else getString(R.string.lm_channel, s + 1, if (two) (if (s == 0) "L" else "R") else "").trim(); setTextColor(if (master) accent else Color.WHITE); textSize = 14f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER })
        col.addView(TextView(this).apply { text = if (master) getString(R.string.lm_sub_master) else getString(if (two) R.string.lm_sub_in_n else R.string.lm_sub_in, s + 1); setTextColor(dim); textSize = 9f; letterSpacing = 0.1f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 8) })
        fun full(v: View, h: Int = 84) = v.apply { layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, h).apply { setMargins(6, 4, 6, 4) } }
        col.addView(full(PadButton(this, fxLabel(s), ConsoleUi.VIOLET, 12f).apply { on = rack(s).any { it.isNotEmpty() }; setOnClickListener { showRack(s) } }.also { fxBtns[s] = it }))
        if (master) {
            col.addView(full(PadButton(this, getString(R.string.lm_limit), ConsoleUi.GREEN, 12f).apply { on = p.getBoolean("limiter", true)
                setOnClickListener { val v = !p.getBoolean("limiter", true); p.edit().putBoolean("limiter", v).apply(); engine?.setLimiter(v, -0.5, 100.0); on = v } }))
            col.addView(TextView(this).apply { text = getString(R.string.lm_limit_at); setTextColor(dim); textSize = 9f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 6) })
        } else {
            val defPan = if (two) (if (s == 0) -1f else 1f) else 0f
            col.addView(KnobView(this, color).apply { value = p.getFloat("pan$s", defPan); layoutParams = LinearLayout.LayoutParams(150, LinearLayout.LayoutParams.WRAP_CONTENT)
                onChange = { v -> p.edit().putFloat("pan$s", v).apply(); engine?.setPan(s, v.toDouble()) } })
            col.addView(full(PadButton(this, getString(R.string.lm_mute), ConsoleUi.RED, 12f).apply { on = p.getBoolean("mute$s", false)
                setOnClickListener { val v = !p.getBoolean("mute$s", false); p.edit().putBoolean("mute$s", v).apply(); engine?.setMute(s, v); on = v } }))
        }
        val key = if (master) "masterDb" else "db$s"; val readout = ConsoleUi.dbReadout(this)
        fun show(db: Double) { readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(db) }
        val db0 = p.getFloat(key, 0f).toDouble(); show(if (db0 <= -60) -120.0 else db0)
        col.addView(FaderView(this, color).apply { progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (db0 <= -60) -120.0 else db0) * 1000).toInt()
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onMove = { pos -> val db = com.amfb.amixos.remote.DaemonClient.posToDb(pos / 1000.0); val set = if (db <= -60) -60.0 else db; p.edit().putFloat(key, set.toFloat()).apply(); show(db); if (master) engine?.setMaster(set) else engine?.setGain(s, set) } }.also { faders[s] = it })
        col.addView(readout)
        return col
    }
    private fun card() = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(28, 22, 28, 22)
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 8, 0, 8) } }

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
        fun refresh() { holder.removeAllViews(); holder.addView(rackPanel(s, catalog, { slot -> engine?.insertJson(s, slot) ?: "null" }, { refresh() })); fxBtns[s]?.apply { text = fxLabel(s); on = rack(s).any { it.isNotEmpty() } } }
        refresh(); dlg.setView(holder); dlg.setOnDismissListener { fxBtns[s]?.apply { text = fxLabel(s); on = rack(s).any { it.isNotEmpty() } } }; dlg.show()
    }
    /** the rack of one strip: four slots, each empty (+ ADD) or an effect with BYPASS, REMOVE, MIX and its knobs. [slotJson] gives what the
     *  engine reports for a slot (a test passes made-up JSON); [changed] redraws. */
    fun rackPanel(s: Int, cat: List<Pair<String, String>>, slotJson: (Int) -> String, changed: () -> Unit): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 12, 24, 12); setBackgroundColor(Color.parseColor("#1D2024")) }
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
                ?: mapOf("kind" to kind, "name" to (cat.firstOrNull { it.first == kind }?.second ?: kind), "bypass" to false, "mix" to 1.0, "params" to emptyList<Any>())
            // the effect as a plug-in face: power (bypass), PRESETS, a knob per parameter
            c.addView(PluginFace.build(this, st, slot, object : PluginFace.Host {
                override fun set(id: String, value: Double) { engine?.insertSet(s, slot, id, value); setParam(s, slot, id, value) }
                override fun remove() { engine?.insert(s, slot, ""); val l = rack(s); l[slot] = ""; saveRack(s, l); prefs().edit().remove(paramKey(s, slot)).apply() }
                override fun changed() = changed()
                override fun text(id: String) = liveText(s, slot, id)
            }))
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
    /** a QUICK CHAIN onto the input channel(s): saved, and put into the running engine slot by slot (an emptied slot is taken out) */
    internal fun applyChain(c: LocalChains.Chain) {
        for (s in 0 until inChannels()) { LocalChains.save(prefs(), s, c, Engine.MAX_INSERTS)
            engine?.let { e -> for (slot in 0 until Engine.MAX_INSERTS) { val f = c.slots.getOrNull(slot); e.insert(s, slot, f?.kind ?: ""); f?.params?.forEach { (id, v) -> e.insertSet(s, slot, id, v) } } } }
        render()
    }
    private fun applyAll(e: Engine) {
        val p = prefs(); val two = inChannels() == 2
        for (s in 0 until INPUTS) { e.setGain(s, p.getFloat("db$s", if (s == 1 && !two) -60f else 0f).toDouble()); e.setPan(s, p.getFloat("pan$s", if (two) (if (s == 0) -1f else 1f) else 0f).toDouble()); e.setMute(s, p.getBoolean("mute$s", false)) }
        for (d in 0..1) { val l = deckStrip(d); val db = p.getFloat("deckDb$d", 0f).toDouble(); val mu = p.getBoolean("deckMute$d", false)   // a deck: its left and right on their own strips
            e.setGain(l, db); e.setGain(l + 1, db); e.setPan(l, -1.0); e.setPan(l + 1, 1.0); e.setMute(l, mu); e.setMute(l + 1, mu) }
        e.setMaster(p.getFloat("masterDb", 0f).toDouble()); e.setLimiter(p.getBoolean("limiter", true), -0.5, 100.0)
        for (s in listOf(-1) + (0 until INPUTS)) rack(s).forEachIndexed { slot, k -> if (k.isNotEmpty() && e.insert(s, slot, k) == 0)
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
        val inOn = Prefs.localInputOn(this)   // INPUT OFF: no microphone is opened and none is asked for
        if (inOn && checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) { requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 1); return }
        val e = engine ?: run { status.text = getString(R.string.lm_no_engine); return }
        loop = AudioLoop(e, rate, frames, Prefs.localOutDevice(this), Prefs.localInDevice(this), bursts, inChannels(), inOn).also { it.start() }
        if (::startBtn.isInitialized) { startBtn.text = getString(R.string.stop); startBtn.on = true }
        ui.postDelayed({ showStatus() }, 400)
    }
    private fun stopAudio() { decks.forEach { it.pause() }; loop?.stop(); loop = null; if (::startBtn.isInitialized) { startBtn.text = getString(R.string.start); startBtn.on = false }; showStatus() }
    private fun showStatus() {
        if (!::status.isInitialized) return
        val l = loop; val ch = runCatching { Engine.aaudioInChannels() }.getOrDefault(0)
        status.text = if (l == null) getString(R.string.lm_stopped, rate, frames) else getString(R.string.lm_running, l.path.uppercase(), rate, frames, if (l.path == "aaudio") (if (ch == 2) "STEREO" else if (ch == 1) "MONO" else "—") else "MONO")
    }
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 1 && grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) startAudio()
    }
    /** THE INPUT / OUTPUT INDICATOR: is each side open, which device it really is, and the level going in and out. A side that did not
     *  open (no microphone permission, a refused device) shows red; open but silent shows amber; sound shows green with its level. */
    private fun showIo() {
        val l = loop; val running = l != null && l.path.isNotEmpty()
        val st = if (l?.path == "aaudio") runCatching { engine?.nativeState() }.getOrNull() else null
        val inOpen = Prefs.localInputOn(this) && when (l?.path) { "aaudio" -> st?.get(5) == 1; "java" -> true; else -> false }   // INPUT OFF never reads as open
        val outOpen = when (l?.path) { "aaudio" -> st?.get(0) == 1 && st[1] == 0; "java" -> true; else -> false }
        val inPk = l?.lastInPeaks?.let { a -> (0 until minOf(inChannels(), a.size)).maxOfOrNull { a[it] } } ?: 0f
        val outPk = l?.lastPeaks?.let { p -> maxOf(p.getOrElse(STRIPS) { 0f }, p.getOrElse(STRIPS + 1) { 0f }) } ?: 0f
        paintIo(running, inOpen, outOpen, inPk, outPk, st?.get(7) ?: 0, st?.get(6) ?: 0)
    }
    /** draws both indicators; [inReal] / [outReal] are the device ids AAudio really opened (0 = not known) */
    internal fun paintIo(running: Boolean, inOpen: Boolean, outOpen: Boolean, inPk: Float, outPk: Float, inReal: Int, outReal: Int) {
        for ((out, open, pk) in listOf(Triple(false, inOpen, inPk), Triple(true, outOpen, outPk))) {
            val v = io[out] ?: continue; val s = IoLamp.of(running, open, pk)
            ((v.lamp.background) as? android.graphics.drawable.GradientDrawable)?.setColor(IoLamp.color(s))
            v.meter.progress = if (s == IoLamp.State.SIGNAL || s == IoLamp.State.CLIP || s == IoLamp.State.SILENT) IoLamp.pos(pk) else 0
            // "the phone's choice": name the device it really took (AAudio says which)
            val chosen = if (out) Prefs.localOutDevice(this) else Prefs.localInDevice(this); val real = if (out) outReal else inReal
            val using = if (chosen == 0 && real > 0) getString(R.string.io_using, deviceName(real, out)) else ""
            v.state.text = when (s) {
                IoLamp.State.STOPPED -> getString(R.string.io_stopped)
                IoLamp.State.NOT_OPEN -> getString(if (out) R.string.io_out_not_open else if (!Prefs.localInputOn(this)) R.string.io_in_off else R.string.io_in_not_open)
                IoLamp.State.SILENT -> getString(R.string.io_silent) + using
                IoLamp.State.SIGNAL -> getString(R.string.io_signal, String.format(java.util.Locale.ROOT, "%.0f", IoLamp.db(pk))) + using
                IoLamp.State.CLIP -> getString(R.string.io_clip) + using }
            v.state.setTextColor(if (s == IoLamp.State.STOPPED) dim else IoLamp.color(s))
        }
    }
    private fun showMeters() {
        showIo()
        showDeckTimes()
        val p = loop?.lastPeaks ?: return
        fun lvl(v: Float) = (((if (v > 1e-6f) 20 * Math.log10(v.toDouble()) else -120.0) + 60) / 60 * 1000).toInt().coerceIn(0, 1000)
        for (s in 0 until INPUTS) faders[s]?.apply { level = lvl(p.getOrElse(s) { 0f }); clip = p.getOrElse(s) { 0f } >= 1f }
        for (d in 0..1) faders[100 + d]?.apply { val l = deckStrip(d); val m = maxOf(p.getOrElse(l) { 0f }, p.getOrElse(l + 1) { 0f }); level = lvl(m); clip = m >= 1f }
        faders[-1]?.apply { val m = maxOf(p.getOrElse(STRIPS) { 0f }, p.getOrElse(STRIPS + 1) { 0f }); level = lvl(m); clip = m >= 1f }
    }
    override fun onDestroy() { stopRec(); decks.forEach { it.release() }; stopAudio(); runCatching { engine?.close() }; super.onDestroy() }

    // ---- DECKS
    private fun loadDeck(d: Int, u: android.net.Uri, fresh: Boolean) {
        if (fresh) runCatching { contentResolver.takePersistableUriPermission(u, android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION) }   // the deck comes back next time
        val name = runCatching { contentResolver.query(u, arrayOf(android.provider.OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c -> if (c.moveToFirst()) c.getString(0) else null } }.getOrNull() ?: u.lastPathSegment.orEmpty()
        prefs().edit().putString("deckUri$d", u.toString()).putString("deckTitle$d", name).apply()
        decks.getOrNull(d)?.load(u, name); showDecks()
    }
    private fun deckCard(d: Int): View {
        val c = card(); val letter = if (d == 0) "A" else "B"
        c.addView(TextView(this).apply { text = getString(R.string.lm_deck, letter); setTextColor(cyan); textSize = 11f; letterSpacing = 0.15f; typeface = Typeface.DEFAULT_BOLD })
        val title = TextView(this).apply { setTextColor(Color.WHITE); textSize = 12f; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END }; c.addView(title)
        val time = TextView(this).apply { setTextColor(dim); textSize = 10f; typeface = Typeface.MONOSPACE }; c.addView(time)
        val seek = SeekBar(this).apply { max = 1000; setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(sb: SeekBar, v: Int, user: Boolean) {}
            override fun onStartTrackingTouch(sb: SeekBar) {}
            override fun onStopTrackingTouch(sb: SeekBar) { decks.getOrNull(d)?.let { k -> if (k.durationUs > 0) k.seek(k.durationUs * sb.progress / 1000) } } }) }
        c.addView(seek)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun btn(t: String, col: Int, f: () -> Unit) = PadButton(this, t, col, 11f).apply { layoutParams = LinearLayout.LayoutParams(0, 76, 1f).apply { setMargins(2, 4, 2, 0) }; setOnClickListener { f() } }.also { row.addView(it) }
        btn(getString(R.string.lm_deck_load), cyan) { pickFor = d; runCatching { pickFile.launch(arrayOf("audio/*")) } }
        val play = btn("▶", ConsoleUi.GREEN) { val k = decks.getOrNull(d) ?: return@btn; if (k.playing) k.pause() else { if (loop == null) startAudio(); k.play() } }
        btn("■", ConsoleUi.RED) { decks.getOrNull(d)?.stop() }
        val lp = btn(getString(R.string.lm_deck_loop), ConsoleUi.AMBER) { decks.getOrNull(d)?.let { k -> k.loop = !k.loop; prefs().edit().putBoolean("deckLoop$d", k.loop).apply(); showDecks() } }
        c.addView(row)
        deckViews[d] = DeckViews(title, time, seek, play, lp)
        // the deck chosen last time comes back (if Android still lets the app read it)
        val saved = prefs().getString("deckUri$d", null)
        if (saved != null && decks.getOrNull(d)?.loaded == false) { decks[d].loop = prefs().getBoolean("deckLoop$d", false); runCatching { loadDeck(d, android.net.Uri.parse(saved), false) } }
        return c
    }
    private fun showDecks() { for (d in 0..1) { val v = deckViews[d] ?: continue; val k = decks.getOrNull(d)
        v.title.text = when { k == null -> getString(R.string.lm_no_engine); k.error.isNotEmpty() -> getString(R.string.lm_deck_error, k.error); k.loaded -> k.title; else -> getString(R.string.lm_deck_empty) }
        v.play.text = if (k?.playing == true) "❚❚" else "▶"; v.play.on = k?.playing == true; v.loop.on = k?.loop == true }
        showDeckTimes() }
    private fun showDeckTimes() { for (d in 0..1) { val v = deckViews[d] ?: continue; val k = decks.getOrNull(d) ?: continue
        val pos = k.positionUs; v.time.text = DeckPlayer.clock(pos) + " / " + DeckPlayer.clock(k.durationUs)
        if (!v.seek.isPressed && k.durationUs > 0) v.seek.progress = (pos * 1000 / k.durationUs).toInt() } }
    /** a deck's channel on the desk: one stereo strip (its left and right strips together): name, MUTE, fader with meter, level */
    private fun deckStripView(d: Int): View {
        val p = prefs(); val color = ConsoleUi.palette[(d + 3) % ConsoleUi.palette.size]; val l = deckStrip(d)
        val col = ConsoleUi.strip(this, color, 0, false).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply { setMargins(4, 0, 4, 0) } }
        col.addView(TextView(this).apply { text = getString(R.string.lm_deck, if (d == 0) "A" else "B"); setTextColor(Color.WHITE); textSize = 14f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER })
        col.addView(TextView(this).apply { text = getString(R.string.lm_sub_deck); setTextColor(dim); textSize = 9f; letterSpacing = 0.1f; gravity = Gravity.CENTER; setPadding(0, 0, 0, 8) })
        col.addView(PadButton(this, getString(R.string.lm_mute), ConsoleUi.RED, 12f).apply { on = p.getBoolean("deckMute$d", false); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 84).apply { setMargins(6, 4, 6, 4) }
            setOnClickListener { val v = !p.getBoolean("deckMute$d", false); p.edit().putBoolean("deckMute$d", v).apply(); engine?.setMute(l, v); engine?.setMute(l + 1, v); on = v } })
        val readout = ConsoleUi.dbReadout(this); fun show(db: Double) { readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(db) }
        val db0 = p.getFloat("deckDb$d", 0f).toDouble(); show(if (db0 <= -60) -120.0 else db0)
        col.addView(FaderView(this, color).apply { progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (db0 <= -60) -120.0 else db0) * 1000).toInt()
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onMove = { pos -> val db = com.amfb.amixos.remote.DaemonClient.posToDb(pos / 1000.0); val set = if (db <= -60) -60.0 else db; p.edit().putFloat("deckDb$d", set.toFloat()).apply(); show(db); engine?.setGain(l, set); engine?.setGain(l + 1, set) } }.also { faders[100 + d] = it })
        col.addView(readout); return col
    }

    // ---- MASTER RECORDER: the master as it is heard, to FLAC 24-bit in the recorder's folder (TOOLS → RECORDER lists it)
    private fun toggleRec() { if (recWriter != null) stopRec() else startRec() }
    private fun startRec() {
        val e = engine ?: return; if (loop == null) startAudio()
        val dir = java.io.File(getExternalFilesDir(null) ?: filesDir, "recordings").apply { mkdirs() }
        val f = java.io.File(dir, "mix_" + java.text.SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", java.util.Locale.US).format(java.util.Date()) + ".flac")
        val rateNow = mixRate().toInt(); val w = runCatching { HiRes.open(f.path, 2, rateNow, "flac24", "tpdf") }.getOrNull()
        if (w == null) { status.text = getString(R.string.rec_cannot_write, runCatching { HiRes.lastError() }.getOrDefault("")); return }
        recWriter = w; recFile = f; e.recArm(true)
        recThread = kotlin.concurrent.thread(name = "amix-master-rec", isDaemon = true) { val buf = FloatArray(2 * 4096)
            while (recWriter != null) { val n = e.recPop(buf, 4096); if (n > 0) recWriter?.write(buf, n) else Thread.sleep(10) } }
        recBtn.text = getString(R.string.lm_rec_stop); recBtn.on = true; window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }
    private fun stopRec() {
        val w = recWriter ?: return; engine?.recArm(false); recWriter = null; recThread?.join(1000); recThread = null
        val buf = FloatArray(2 * 4096); engine?.let { e -> while (true) { val n = e.recPop(buf, 4096); if (n <= 0) break; w.write(buf, n) } }   // what is left
        val dropped = engine?.recDropped() ?: 0L; w.finish()
        if (::recBtn.isInitialized) { recBtn.text = getString(R.string.lm_rec); recBtn.on = false }; window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        status.text = getString(R.string.lm_rec_saved, recFile?.name.orEmpty()) + if (dropped > 0) " · " + getString(R.string.lm_rec_dropped, dropped) else ""
    }
}
