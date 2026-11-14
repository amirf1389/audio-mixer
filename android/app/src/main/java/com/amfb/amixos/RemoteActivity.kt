package com.amfb.amixos

import android.app.AlertDialog
import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.ToggleButton
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.ConsoleState
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import com.amfb.amixos.remote.Meters
import java.util.concurrent.Executors

/** The remote: connect to amixos-daemon (host, port, token), then the mixer (32 strips: fader, pan, mute, solo, meter), the master,
 *  the devices (open / stop) and a channel page (EQ band, compressor, gate). Every call runs on a worker thread; the meter stream
 *  arrives over the WebSocket. The token stays in SharedPreferences on this phone; nothing leaves it except to the daemon. */
class RemoteActivity : AppCompatActivity() {
    private val io = Executors.newSingleThreadExecutor()
    private val ui = Handler(Looper.getMainLooper())
    private var client: DaemonClient? = null
    private var stream: DaemonClient.MeterStream? = null
    private var state: ConsoleState? = null
    private val faders = ArrayList<SeekBar>()
    private val meters = ArrayList<ProgressBar>()
    private val dbLabels = ArrayList<TextView>()
    private lateinit var status: TextView
    private lateinit var strips: LinearLayout
    private lateinit var masterMeter: ProgressBar
    private lateinit var masterLabel: TextView
    private var connected: List<Button> = emptyList()
    private val nameLabels = ArrayList<TextView>(); private val mutes = ArrayList<ToggleButton>(); private val solos = ArrayList<ToggleButton>()
    private var touching = 0   // a finger on a fader: the sync does not move it under the finger
    private var masterFaderRef: SeekBar? = null
    private var connectBtn: Button? = null; private var autoTried = false
    private fun buzz(v: View) { if (Prefs.haptics(this)) v.performHapticFeedback(android.view.HapticFeedbackConstants.KEYBOARD_TAP) }
    override fun onResume() {   // back from SETTINGS: the mixer is laid out again with the new choices
        super.onResume()
        if (!autoTried) { autoTried = true; val p = Prefs.remote(this); if (Prefs.autoConnect(this) && !p.getString("host", "").isNullOrEmpty() && !p.getString("token", "").isNullOrEmpty()) connectBtn?.performClick() }
        else state?.let { buildStrips(it) }
        if (client != null) { if (Prefs.keepScreenOn(this)) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) else window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("remote", Context.MODE_PRIVATE)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(Color.parseColor("#0F141C")); setPadding(24, 24, 24, 24) }
        val title = TextView(this).apply { text = getString(R.string.remote_title); setTextColor(Color.parseColor("#EA580C")); textSize = 18f; setTypeface(null, android.graphics.Typeface.BOLD) }
        root.addView(title)
        val host = EditText(this).apply { hint = "console address (192.168.1.20)"; setText(prefs.getString("host", "")); setTextColor(Color.WHITE); setHintTextColor(Color.GRAY) }
        val port = EditText(this).apply { hint = "port"; setText(prefs.getString("port", Prefs.defaultPort(this@RemoteActivity).toString())); setTextColor(Color.WHITE); setHintTextColor(Color.GRAY) }
        val token = EditText(this).apply { hint = "token (~/.amixos/daemon.token on the console)"; setText(prefs.getString("token", "")); setTextColor(Color.WHITE); setHintTextColor(Color.GRAY); inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD }
        root.addView(host); root.addView(port); root.addView(token)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val connect = Button(this).apply { text = getString(R.string.connect) }
        val devices = Button(this).apply { text = getString(R.string.devices); isEnabled = false }
        val pair = Button(this).apply { text = getString(R.string.pair_pin) }
        row.addView(pair); row.addView(connect); row.addView(devices); root.addView(row)
        val find = Button(this).apply { text = getString(R.string.find) }
        val busesBtn = Button(this).apply { text = getString(R.string.buses); isEnabled = false }
        val monBtn = Button(this).apply { text = getString(R.string.monitor); isEnabled = false }
        val clipBtn = Button(this).apply { text = getString(R.string.clear_clips); isEnabled = false }
        val row2 = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val settingsBtn = Button(this).apply { text = getString(R.string.settings); setOnClickListener { startActivity(android.content.Intent(this@RemoteActivity, SettingsActivity::class.java)) } }
        row2.addView(find); row2.addView(busesBtn); row2.addView(monBtn); row2.addView(clipBtn); root.addView(row2)
        root.addView(LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; addView(settingsBtn) })
        connectBtn = connect
        connected = listOf(busesBtn, monBtn, clipBtn)
        find.setOnClickListener {   // FIND: the consoles on this Wi-Fi that run the phone remote
            val ip = localIp(); if (ip == null) { status.text = getString(R.string.no_wifi); return@setOnClickListener }
            status.text = getString(R.string.searching)
            io.execute { val found = DaemonClient.discover(ip, port.text.toString().trim().toIntOrNull() ?: 8765)
                ui.post { if (found.isEmpty()) status.text = getString(R.string.none_found)
                    else AlertDialog.Builder(this).setTitle(R.string.find).setItems(found.map { it.first + "   " + it.second }.toTypedArray()) { _, w -> host.setText(found[w].first); status.text = getString(R.string.found_pair) }.show() } }
        }
        busesBtn.setOnClickListener { showBuses() }
        monBtn.setOnClickListener { showMonitor() }
        clipBtn.setOnClickListener { val c = client ?: return@setOnClickListener; io.execute { runCatching { c.clearClips() } } }
        status = TextView(this).apply { text = getString(R.string.not_connected); setTextColor(Color.parseColor("#FBBF24")); typeface = android.graphics.Typeface.MONOSPACE }
        root.addView(status)
        val masterRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        masterLabel = TextView(this).apply { setOnClickListener { showMaster() }; text = "MASTER"; setTextColor(Color.WHITE); typeface = android.graphics.Typeface.MONOSPACE }
        masterMeter = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000; layoutParams = LinearLayout.LayoutParams(0, 20, 1f) }
        masterRow.addView(masterLabel); masterRow.addView(masterMeter); root.addView(masterRow)
        val masterFader = SeekBar(this).apply { masterFaderRef = this; max = 1000; progress = 750; setOnSeekBarChangeListener(change { p -> client?.master(DaemonClient.posToDb(p / 1000.0)) }) }
        root.addView(masterFader)
        root.addView(TextView(this).apply { text = getString(R.string.tap_hint); setTextColor(Color.parseColor("#8B98A8")); textSize = 11f })
        strips = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val scroll = ScrollView(this).apply { addView(strips); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f) }
        root.addView(scroll)
        setContentView(root)

        connect.setOnClickListener {
            val ep = Endpoint(host.text.toString().trim(), port.text.toString().trim().toIntOrNull() ?: Prefs.defaultPort(this), token.text.toString().trim())
            prefs.edit().putString("host", ep.host).putString("port", ep.port.toString()).putString("token", ep.token).apply()
            status.text = getString(R.string.connecting)
            io.execute {
                try {
                    val c = DaemonClient(ep); val st = c.status(); val s = c.state()
                    ui.post { client = c; state = s; devices.isEnabled = true; connected.forEach { it.isEnabled = true }; if (Prefs.keepScreenOn(this)) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); startSync(c); status.text = st.name + " " + st.version + " · " + (if (st.running) st.backend else "engine stopped") + " · " + st.strips + " strips"; status.setTextColor(Color.parseColor("#10B981")); masterFader.progress = (DaemonClient.dbToPos(s.masterDb) * 1000).toInt(); buildStrips(s); startMeters(c) }
                } catch (e: Exception) { ui.post { status.text = getString(R.string.failed, e.message ?: e.javaClass.simpleName); status.setTextColor(Color.parseColor("#FF2A5F")) } }
            }
        }
        devices.setOnClickListener { showDevices() }
        pair.setOnClickListener {   // the PIN instead of typing the long token: the console shows it under NETWORK → PHONE REMOTE, new every minute
            val h = host.text.toString().trim(); val pt = port.text.toString().trim().toIntOrNull() ?: 8765
            if (h.isEmpty()) { status.text = getString(R.string.pair_need_host); return@setOnClickListener }
            val pinBox = EditText(this).apply { hint = "000000"; inputType = android.text.InputType.TYPE_CLASS_NUMBER; filters = arrayOf(android.text.InputFilter.LengthFilter(6)) }
            AlertDialog.Builder(this).setTitle(R.string.pair_pin).setMessage(R.string.pair_help).setView(pinBox)
                .setPositiveButton(R.string.pair_go) { _, _ ->
                    val pin = pinBox.text.toString().trim(); status.text = getString(R.string.connecting)
                    io.execute {
                        try {
                            val t = DaemonClient.pair(h, pt, pin, android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL)
                            ui.post { token.setText(t); connect.performClick() }   // connect saves host, port and the new token
                        } catch (e: Exception) { ui.post { status.text = getString(R.string.failed, e.message ?: e.javaClass.simpleName); status.setTextColor(Color.parseColor("#FF2A5F")) } }
                    }
                }.setNegativeButton(android.R.string.cancel, null).show()
        }
    }

    private fun change(f: (Int) -> Unit) = object : SeekBar.OnSeekBarChangeListener {
        override fun onProgressChanged(s: SeekBar, p: Int, fromUser: Boolean) { if (fromUser) io.execute { runCatching { f(p) }.onFailure { e -> ui.post { status.text = getString(R.string.failed, e.message ?: "") } } } }
        override fun onStartTrackingTouch(s: SeekBar) { touching++ }
        override fun onStopTrackingTouch(s: SeekBar) { touching = maxOf(0, touching - 1) }
    }

    private fun buildStrips(s: ConsoleState) {
        strips.removeAllViews(); faders.clear(); meters.clear(); dbLabels.clear(); nameLabels.clear(); mutes.clear(); solos.clear()
        for (st in s.strips.take(Prefs.stripCount(this))) {
            val card = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(12, 10, 12, 10); setBackgroundColor(Color.parseColor("#141B26")) }
            val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            val name = TextView(this).apply { text = String.format("CH %02d  %s", st.id + 1, st.name); setTextColor(Color.WHITE); typeface = android.graphics.Typeface.MONOSPACE; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); setOnClickListener { showChannel(st.id, st.name) }; setOnLongClickListener { rename(st.id, this); true } }
            val db = TextView(this).apply { text = DaemonClient.fmtDb(st.db); setTextColor(Color.parseColor("#00F2FF")); typeface = android.graphics.Typeface.MONOSPACE }
            val mute = ToggleButton(this).apply { textOn = "MUTED"; textOff = "MUTE"; isChecked = st.mute; setOnCheckedChangeListener { b, on -> if (b.tag == null) { buzz(b); io.execute { runCatching { client?.mute(st.id, on) } } } } }
            val solo = ToggleButton(this).apply { textOn = "SOLO"; textOff = "solo"; isChecked = st.solo; setOnCheckedChangeListener { b, on -> if (b.tag == null) { buzz(b); io.execute { runCatching { client?.solo(st.id, on) } } } } }
            head.addView(name); head.addView(db); head.addView(mute); head.addView(solo); card.addView(head)
            val meter = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal).apply { max = 1000; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 14) }
            card.addView(meter)
            val fader = SeekBar(this).apply { max = 1000; progress = (DaemonClient.dbToPos(st.db) * 1000).toInt(); setOnSeekBarChangeListener(change { p -> val v = DaemonClient.posToDb(p / 1000.0); client?.gain(st.id, v); ui.post { db.text = DaemonClient.fmtDb(v) } }) }
            card.addView(fader)
            val pan = SeekBar(this).apply { max = 200; progress = ((st.pan + 1) * 100).toInt(); setOnSeekBarChangeListener(change { p -> client?.pan(st.id, (p - 100) / 100.0) }) }
            if (Prefs.showPan(this)) card.addView(pan)
            val gap = View(this).apply { layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 8) }
            strips.addView(card); strips.addView(gap); faders.add(fader); meters.add(meter); dbLabels.add(db); nameLabels.add(name); mutes.add(mute); solos.add(solo)
        }
    }

    private fun startMeters(c: DaemonClient) {
        stream?.stop()
        stream = c.meterStream({ m -> ui.post { showMeters(m) } }, { ok, err -> ui.post { if (!ok && err != null) status.text = getString(R.string.failed, err) } })
    }
    private fun showMeters(m: Meters) {
        val floor = Prefs.meterFloor(this).toDouble(); fun bar(v: Double) = ((DaemonClient.lin2db(v) + floor) / floor * 1000).toInt().coerceIn(0, 1000)
        for (i in meters.indices) if (i < m.strips.size) meters[i].progress = bar(m.strips[i].pk)
        masterMeter.progress = bar(Math.max(m.masterL, m.masterR)); masterLabel.text = "MASTER " + DaemonClient.fmtDb(DaemonClient.lin2db(Math.max(m.masterL, m.masterR))) + (if (m.masterClip) " CLIP" else "")
    }

    private fun showDevices() {
        val c = client ?: return
        io.execute {
            val devs = runCatching { c.devices() }.getOrDefault(emptyList())
            ui.post {
                val names = devs.map { it.backend.toUpperCase() + "  " + it.name + "  (" + it.inputs + " in / " + it.outputs + " out)" }.toTypedArray()
                AlertDialog.Builder(this).setTitle(getString(R.string.devices)).setItems(names) { _, which -> val d = devs[which]
                    io.execute { runCatching { c.open(d.backend, if (d.inputs > 0) d.id else "", if (d.outputs > 0) d.id else "", d.defaultRate, 256, Math.min(32, Math.max(2, d.inputs)), Math.min(2, Math.max(2, d.outputs))) }.onFailure { e -> ui.post { status.text = getString(R.string.failed, e.message ?: "") } }.onSuccess { ui.post { status.text = "opened " + d.name } } } }
                    .setNegativeButton(getString(R.string.stop_engine)) { _, _ -> val doStop = { io.execute { runCatching { c.stop() }; ui.post { status.text = getString(R.string.stopped) } } }
                        if (Prefs.confirmStop(this)) AlertDialog.Builder(this).setMessage(R.string.stop_q).setPositiveButton(R.string.stop_engine) { _, _ -> doStop() }.setNegativeButton(android.R.string.cancel, null).show() else doStop() }
                    .setNeutralButton(android.R.string.cancel, null).show()
            }
        }
    }

    /** One channel: input trim, polarity, delay, an EQ band, compressor, gate and the sends to AUX 1-8. Every slider shows its value. */
    private fun showChannel(strip: Int, name: String) {
        val c = client ?: return
        AlertDialog.Builder(this).setTitle(String.format("CH %02d  %s", strip + 1, name)).setView(ScrollView(this).apply { addView(channelPanel(c, strip)) }).setPositiveButton(android.R.string.ok, null).show()
    }
    internal fun channelPanel(c: DaemonClient, strip: Int): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16); setBackgroundColor(Color.parseColor("#141B26")) }
        val accent = Color.parseColor("#EA580C"); val cyan = Color.parseColor("#00F2FF")
        fun head(t: String) = box.addView(TextView(this).apply { text = t; setTextColor(accent); typeface = android.graphics.Typeface.MONOSPACE; setPadding(0, 24, 0, 4) })
        fun row(label: String, max: Int, start: Int, show: (Int) -> String, f: (Int) -> Unit): SeekBar {
            val t = TextView(this).apply { text = label + "   " + show(start); setTextColor(Color.WHITE) }; box.addView(t)
            val sb = SeekBar(this).apply { this.max = max; progress = start; setOnSeekBarChangeListener(change { p -> ui.post { t.text = label + "   " + show(p) }; f(p) }) }; box.addView(sb); return sb }
        fun toggle(on: String, off: String, f: (Boolean) -> Unit) = ToggleButton(this).apply { textOn = on; textOff = off; text = off; setOnCheckedChangeListener { b, v -> buzz(b); io.execute { runCatching { f(v) } } } }.also { box.addView(it) }

        head("INPUT")
        row("TRIM", 480, 240, { String.format("%+.1f dB", (it - 240) / 10.0) }) { p -> c.trim(strip, (p - 240) / 10.0) }
        toggle("POLARITY Ø INVERTED", "POLARITY Ø NORMAL") { c.polarity(strip, it) }
        row("DELAY", 1000, 0, { String.format("%.1f ms", it / 10.0) }) { p -> c.delay(strip, p / 10.0) }

        head("EQ")
        var eqFreq = 1000.0; var eqGain = 0.0
        fun hz(p: Int) = 20 * Math.pow(1000.0, p / 1000.0)
        row("BAND 1 FREQUENCY", 1000, 566, { val f = hz(it); if (f >= 1000) String.format("%.2f kHz", f / 1000) else String.format("%.0f Hz", f) }) { p -> eqFreq = hz(p); c.eq(strip, 0, "peaking", eqFreq, 1.0, eqGain) }
        row("BAND 1 GAIN", 480, 240, { String.format("%+.1f dB", (it - 240) / 10.0) }) { p -> eqGain = (p - 240) / 10.0; c.eq(strip, 0, "peaking", eqFreq, 1.0, eqGain) }

        head("DYNAMICS")
        var thr = -18.0; var ratio = 3.0; var compOn = false
        toggle("COMPRESSOR ON", "COMPRESSOR OFF") { compOn = it; c.compressor(strip, it, thr, ratio, 6.0, 10.0, 100.0, 0.0) }
        row("THRESHOLD", 600, 420, { String.format("%.1f dB", it / 10.0 - 60) }) { p -> thr = p / 10.0 - 60; if (compOn) c.compressor(strip, true, thr, ratio, 6.0, 10.0, 100.0, 0.0) }
        row("RATIO", 190, 20, { String.format("%.1f : 1", 1 + it / 10.0) }) { p -> ratio = 1 + p / 10.0; if (compOn) c.compressor(strip, true, thr, ratio, 6.0, 10.0, 100.0, 0.0) }
        var gthr = -40.0; var gateOn = false
        toggle("GATE ON", "GATE OFF") { gateOn = it; c.gate(strip, it, gthr, -80.0, 1.0, 50.0, 100.0) }
        row("GATE THRESHOLD", 800, 400, { String.format("%.1f dB", it / 10.0 - 80) }) { p -> gthr = p / 10.0 - 80; if (gateOn) c.gate(strip, true, gthr, -80.0, 1.0, 50.0, 100.0) }

        head("SENDS")
        for (b in 0 until 8) { var pre = false; var lvl = -120.0
            val line = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            val t = TextView(this).apply { text = "AUX ${b + 1}  -∞"; setTextColor(cyan); typeface = android.graphics.Typeface.MONOSPACE; layoutParams = LinearLayout.LayoutParams(300, LinearLayout.LayoutParams.WRAP_CONTENT) }
            val sb = SeekBar(this).apply { max = 1000; progress = 0; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                setOnSeekBarChangeListener(change { p -> lvl = DaemonClient.posToDb(p / 1000.0); ui.post { t.text = "AUX ${b + 1}  " + DaemonClient.fmtDb(lvl) }; c.send(strip, b, lvl, pre) }) }
            val pp = ToggleButton(this).apply { textOn = "PRE"; textOff = "POST"; text = "POST"; setOnCheckedChangeListener { _, v -> pre = v; io.execute { runCatching { c.send(strip, b, lvl, pre) } } } }
            line.addView(t); line.addView(sb); line.addView(pp); box.addView(line) }
        return box
    }

    /** MASTER: the limiter (on / off, ceiling). */
    private fun showMaster() {
        val c = client ?: return
        AlertDialog.Builder(this).setTitle("MASTER").setView(masterPanel(c)).setPositiveButton(android.R.string.ok, null).show()
    }
    internal fun masterPanel(c: DaemonClient): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16); setBackgroundColor(Color.parseColor("#141B26")) }
        val st = state; var on = st?.limiter ?: true; var ceil = st?.ceilingDb ?: -0.5
        val t = TextView(this).apply { text = String.format("CEILING  %.1f dBFS", ceil); setTextColor(Color.WHITE) }
        box.addView(ToggleButton(this).apply { textOn = "LIMITER ON"; textOff = "LIMITER OFF"; isChecked = on; setOnCheckedChangeListener { b, v -> buzz(b); on = v; io.execute { runCatching { c.limiter(on, ceil) } } } })
        box.addView(t)
        box.addView(SeekBar(this).apply { max = 200; progress = ((ceil + 20) * 10).toInt().coerceIn(0, 200)
            setOnSeekBarChangeListener(change { p -> ceil = p / 10.0 - 20; ui.post { t.text = String.format("CEILING  %.1f dBFS", ceil) }; c.limiter(on, ceil) }) })
        box.addView(TextView(this).apply { text = "The limiter keeps the master under the ceiling (no clipping on the output)."; setTextColor(Color.parseColor("#8B98A8")) })
        return box
    }

    /** FOLLOW THE CONSOLE: every 3 s the state is read again; faders, mutes, solos and names that changed on the computer (or on another
     *  phone) move here too. A fader under a finger is left alone. Paused while the app is in the background. */
    private var syncing = false
    private fun startSync(c: DaemonClient) { if (syncing) return; syncing = true
        val tick = object : Runnable { override fun run() { if (!syncing) return
            val secs = Prefs.syncSeconds(this@RemoteActivity)
            if (secs > 0 && !isFinishing && hasWindowFocus()) io.execute { val s = runCatching { c.state() }.getOrNull(); if (s != null) ui.post { applyState(s) } }
            ui.postDelayed(this, 1000L * maxOf(1, secs)) } }
        ui.postDelayed(tick, 1000L * maxOf(1, Prefs.syncSeconds(this))) }
    private fun applyState(s: ConsoleState) {
        if (touching > 0) return
        for (st in s.strips) { val i = st.id; if (i >= faders.size || i >= mutes.size) continue
            val pos = (DaemonClient.dbToPos(st.db) * 1000).toInt(); if (Math.abs(faders[i].progress - pos) > 2) { faders[i].progress = pos; dbLabels[i].text = DaemonClient.fmtDb(st.db) }
            if (mutes[i].isChecked != st.mute) { mutes[i].tag = "sync"; mutes[i].isChecked = st.mute; mutes[i].tag = null }
            if (solos[i].isChecked != st.solo) { solos[i].tag = "sync"; solos[i].isChecked = st.solo; solos[i].tag = null }
            val label = String.format("CH %02d  %s", i + 1, st.name); if (nameLabels[i].text.toString() != label) nameLabels[i].text = label }
        masterFaderRef?.let { val p = (DaemonClient.dbToPos(s.masterDb) * 1000).toInt(); if (Math.abs(it.progress - p) > 2) it.progress = p }
        state = s
    }
    private fun rename(strip: Int, label: TextView) {   // long press on a strip's name
        val c = client ?: return; val box = EditText(this).apply { setText(state?.strips?.getOrNull(strip)?.name ?: ""); setSingleLine() }
        AlertDialog.Builder(this).setTitle(getString(R.string.rename, strip + 1)).setView(box).setPositiveButton(android.R.string.ok) { _, _ ->
            val n = box.text.toString().trim().take(24); io.execute { runCatching { c.name(strip, n) }.onSuccess { ui.post { label.text = String.format("CH %02d  %s", strip + 1, n) } } } }
            .setNegativeButton(android.R.string.cancel, null).show()
    }
    /** AUX / FX buses: level and mute, from the console's state. */
    private fun showBuses() {
        val c = client ?: return
        io.execute { val s = runCatching { c.state() }.getOrNull() ?: return@execute
            ui.post { val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16) }
                for (b in s.buses) { val label = TextView(this).apply { text = busName(b.id) + "   " + DaemonClient.fmtDb(b.db) }
                    val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
                    label.layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                    val m = ToggleButton(this).apply { textOn = "MUTED"; textOff = "MUTE"; isChecked = b.mute; setOnCheckedChangeListener { _, on -> io.execute { runCatching { c.busMute(b.id, on) } } } }
                    head.addView(label); head.addView(m); box.addView(head)
                    box.addView(SeekBar(this).apply { max = 1000; progress = (DaemonClient.dbToPos(b.db) * 1000).toInt()
                        setOnSeekBarChangeListener(change { p -> val v = DaemonClient.posToDb(p / 1000.0); c.busLevel(b.id, v); ui.post { label.text = busName(b.id) + "   " + DaemonClient.fmtDb(v) } }) }) }
                AlertDialog.Builder(this).setTitle(R.string.buses).setView(ScrollView(this).apply { addView(box) }).setPositiveButton(android.R.string.ok, null).show() } }
    }
    private fun busName(k: Int) = if (k < 8) "AUX ${k + 1}" else "FX ${k - 7}"
    /** CONTROL ROOM: what the speakers / headphones hear (master or a bus), level, DIM, MONO, MUTE. */
    private fun showMonitor() {
        val c = client ?: return
        var source = 0; var db = 0.0; var dim = false; var mono = false; var mute = false
        fun push() { io.execute { runCatching { c.monitor(source, db, dim, mono, mute) } } }
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16) }
        val srcNames = arrayOf("MASTER") + (1..8).map { "AUX $it" }
        val src = Button(this).apply { text = getString(R.string.source, srcNames[0]); setOnClickListener { AlertDialog.Builder(this@RemoteActivity).setItems(srcNames) { _, w -> source = w; text = getString(R.string.source, srcNames[w]); push() }.show() } }
        val lvl = TextView(this).apply { text = "LEVEL 0.0 dB" }
        box.addView(src); box.addView(lvl)
        box.addView(SeekBar(this).apply { max = 1000; progress = 750; setOnSeekBarChangeListener(change { p -> db = DaemonClient.posToDb(p / 1000.0); ui.post { lvl.text = "LEVEL " + DaemonClient.fmtDb(db) + " dB" }; c.monitor(source, db, dim, mono, mute) }) })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row.addView(ToggleButton(this).apply { textOn = "DIM"; textOff = "dim"; text = "dim"; setOnCheckedChangeListener { _, on -> dim = on; push() } })
        row.addView(ToggleButton(this).apply { textOn = "MONO"; textOff = "mono"; text = "mono"; setOnCheckedChangeListener { _, on -> mono = on; push() } })
        row.addView(ToggleButton(this).apply { textOn = "MUTED"; textOff = "mute"; text = "mute"; setOnCheckedChangeListener { _, on -> mute = on; push() } })
        box.addView(row)
        AlertDialog.Builder(this).setTitle(R.string.monitor).setView(box).setPositiveButton(android.R.string.ok, null).show()
    }
    private fun localIp(): String? = runCatching {
        java.net.NetworkInterface.getNetworkInterfaces().toList().filter { it.isUp && !it.isLoopback }.flatMap { it.inetAddresses.toList() }
            .filterIsInstance<java.net.Inet4Address>().map { it.hostAddress }.firstOrNull { it.startsWith("192.168.") || it.startsWith("10.") || Regex("^172\\.(1[6-9]|2\\d|3[01])\\.").containsMatchIn(it) }
    }.getOrNull()

    override fun onDestroy() { syncing = false; stream?.stop(); io.shutdownNow(); super.onDestroy() }
}
