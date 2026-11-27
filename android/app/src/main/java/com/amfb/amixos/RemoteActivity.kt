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
import android.widget.HorizontalScrollView
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
import com.amfb.amixos.remote.PairLink
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
import java.util.concurrent.Executors

/** The remote: connect to amixos-daemon (host, port, token), then the mixer (32 strips: fader, pan, mute, solo, meter), the master,
 *  the devices (open / stop) and a channel page (EQ band, compressor, gate). Every call runs on a worker thread; the meter stream
 *  arrives over the WebSocket. The token stays in SharedPreferences on this phone; nothing leaves it except to the daemon. */
class RemoteActivity : FitActivity() {
    private val io = Executors.newSingleThreadExecutor()
    private val scanIo = Executors.newSingleThreadExecutor()   // network searches: never on the fader thread (a search takes up to a second)
    private val live = com.amfb.amixos.remote.LatestSender(Executors.newSingleThreadExecutor())   // REAL-TIME FADERS: only the newest move of each fader is sent, never a queue; its own thread, so a slow status check never holds a fader back
    private val ui = Handler(Looper.getMainLooper())
    private var client: DaemonClient? = null
    private var stream: DaemonClient.MeterStream? = null
    private var state: ConsoleState? = null
    private val faders = ArrayList<FaderView>()
        private val dbLabels = ArrayList<TextView>()
    private lateinit var status: TextView
    private lateinit var strips: LinearLayout
    private lateinit var masterFader: FaderView; private lateinit var masterDb: TextView; private lateinit var planBadge: TextView
    private lateinit var masterLabel: TextView
    private var connected: List<Button> = emptyList()
    private val nameLabels = ArrayList<TextView>(); private val mutes = ArrayList<ToggleButton>(); private val solos = ArrayList<ToggleButton>()
    private var touching = 0   // a finger on a fader: the sync does not move it under the finger
    private var pendingOpen: String? = null   // "scenes": from the home screen's SCENES tile, opened as soon as the console answers
    override fun onNewIntent(i: android.content.Intent) { super.onNewIntent(i); setIntent(i); val o = i.getStringExtra("open")
        if (o == "recall") { client?.let { offerPendingScene(it) }; return }   // RECALL LAST SCENE from HOME: asked first; not linked yet: asked when it links
        if (o == "scenes") { if (client != null) showScenes() else { pendingOpen = o; connectBtn?.performClick() } } }
    private val clipFlags = ArrayList<Boolean>(); private var connBox: LinearLayout? = null
    private var connectBtn: Button? = null; private var autoTried = false
    private fun buzz(v: View) { if (Prefs.haptics(this)) v.performHapticFeedback(android.view.HapticFeedbackConstants.KEYBOARD_TAP) }
    override fun onResume() {   // back from SETTINGS: the mixer is laid out again with the new choices
        super.onResume()
        if (!autoTried) { autoTried = true; val p = Prefs.remote(this)
            if ((Prefs.autoConnect(this) || pendingOpen != null) && !p.getString("host", "").isNullOrEmpty() && !p.getString("token", "").isNullOrEmpty()) { autoMode = Prefs.autoFind(this); connectBtn?.performClick() }   // a failure falls back to the search
            else autoFind() }   // AUTO-DETECT: look for the console on the Wi-Fi
        else { state?.let { buildStrips(it) }; if (client == null) autoFind() }   // back on this screen, still not connected: search again
        if (client != null) { if (Prefs.keepScreenOn(this)) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) else window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ui.postDelayed(noticePoll, 5000)
        val prefs = getSharedPreferences("remote", Context.MODE_PRIVATE)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(Color.parseColor("#111418")); setPadding(24, 24, 24, 24) }
        root.addView(Nav.modeBar(this, false))   // DUAL MODE: THIS PHONE | CONSOLE
        val title = TextView(this).apply { text = getString(R.string.home_mixer).uppercase(); setTextColor(Color.parseColor("#FFB590")); textSize = 14f; setTypeface(null, android.graphics.Typeface.BOLD) }
        root.addView(title)
        // THE CONSOLE BAR: every paired console (and new ones on this Wi-Fi): tap one to switch to it
        consoleBar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        root.addView(HorizontalScrollView(this).apply { addView(consoleBar); isHorizontalScrollBarEnabled = false; setPadding(0, 6, 0, 6) })
        val host = EditText(this).apply { hint = "console address (192.168.1.20)"; setText(prefs.getString("host", "")); setTextColor(Color.WHITE); setHintTextColor(Color.GRAY) }
        val port = EditText(this).apply { hint = "port"; setText(prefs.getString("port", Prefs.defaultPort(this@RemoteActivity).toString())); setTextColor(Color.WHITE); setHintTextColor(Color.GRAY) }
        val token = EditText(this).apply { hint = "token (~/.amixos/daemon.token on the console)"; setText(prefs.getString("token", "")); setTextColor(Color.WHITE); setHintTextColor(Color.GRAY); inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD }
        hostBox = host; portBox = port; tokenBox = token
        val conn = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; connBox = conn; root.addView(conn)
        conn.addView(host); conn.addView(port); conn.addView(token)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        val connect = Button(this).apply { text = getString(R.string.connect) }
        val devices = Button(this).apply { text = getString(R.string.devices); isEnabled = false }
        val pair = Button(this).apply { text = getString(R.string.pair_pin) }; pairBtn = pair
        val scan = Button(this).apply { text = getString(R.string.scan_qr) }
        row.addView(scan); row.addView(pair); row.addView(connect); row.addView(devices); conn.addView(HorizontalScrollView(this).apply { addView(row) })
        val find = Button(this).apply { text = getString(R.string.find) }
        val consolesBtn = Button(this).apply { text = getString(R.string.consoles); setOnClickListener { showConsoles(host, port, token, connect) } }
        val busesBtn = Button(this).apply { text = getString(R.string.buses); isEnabled = false }
        val monBtn = Button(this).apply { text = getString(R.string.monitor); isEnabled = false }
        val clipBtn = Button(this).apply { text = getString(R.string.clear_clips); isEnabled = false }
        val row2 = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row2.addView(find); row2.addView(consolesBtn); row2.addView(busesBtn); row2.addView(monBtn); row2.addView(clipBtn); root.addView(android.widget.HorizontalScrollView(this).apply { addView(row2) })
        row2.addView(Button(this).apply { text = getString(R.string.rt_tools); contentDescription = "remoteTools"; setOnClickListener { showTools() } })   // TOOLS: group faders, 360 audio, effects, plug-ins, scenes, calculator
        connectBtn = connect
        // RECORD on the console, MUTE GROUPS (kept on this phone) and the console's INPUT source
        val recBtn = Button(this).apply { text = getString(R.string.remote_rec); isEnabled = false; setOnClickListener { toggleRecord(this) } }
        val mgBtn = Button(this).apply { text = getString(R.string.mute_groups); isEnabled = false; setOnClickListener { showMuteGroups() } }
        val inBtn = Button(this).apply { text = getString(R.string.remote_input); isEnabled = false; setOnClickListener { pickInput() } }
        row2.addView(recBtn, 1); row2.addView(mgBtn, 2); row2.addView(inBtn, 3)
        // FADERS / KNOBS: the strips as big faders (five channels across: ten channels are two swipes) or with pan, gain and EQ on them
        val viewName = { getString(if (Prefs.bigFaders(this)) R.string.remote_view_knobs else R.string.remote_view_faders) }
        val viewBtn = Button(this).apply { contentDescription = "remoteView"; text = viewName() }
        viewBtn.setOnClickListener { Prefs.remote(this).edit().putBoolean("bigFaders", !Prefs.bigFaders(this)).apply(); viewBtn.text = viewName(); state?.let { buildStrips(it) } }
        row2.addView(viewBtn, 0)
        for (i in 0 until row2.childCount) (row2.getChildAt(i) as? Button)?.let { ConsoleUi.compact(it, ConsoleUi.PRIMARY_DP, 12, fit = false).setPadding(ConsoleUi.dp(this, 12f), 0, ConsoleUi.dp(this, 12f), 0) }   // one line each, no cut text
        connected = listOf(busesBtn, monBtn, clipBtn, recBtn, mgBtn, inBtn)
        find.setOnClickListener {   // FIND: the consoles on this Wi-Fi that run the phone remote
            val ip = localIp(); if (ip == null) { status.text = getString(R.string.no_wifi); return@setOnClickListener }
            status.text = getString(R.string.searching)
            scanIo.execute { val paired = consoles(); val found = DaemonClient.discoverConsoles(ip, port.text.toString().trim().toIntOrNull() ?: 8765, first = PairedConsoles.hints(paired))
                ui.post { if (found.isEmpty()) status.text = getString(R.string.none_found)
                    else AlertDialog.Builder(this).setTitle(R.string.find).setItems(found.map { it.host + "   " + it.label }.toTypedArray()) { _, w -> host.setText(found[w].host)
                        val known = (PairedConsoles.match(paired, listOf(found[w])) as? PairedConsoles.Match.Known)?.console   // by its consoleId: also after a new address
                        if (known != null) { token.setText(known.token); port.setText(known.port.toString()); connect.performClick() }   // paired before: straight in
                        else { status.text = getString(R.string.found_pair); pairBtn?.performClick() } }.show() } }   // new: the PIN, at once
        }
        busesBtn.setOnClickListener { showBuses() }
        monBtn.setOnClickListener { showMonitor() }
        clipBtn.setOnClickListener { val c = client ?: return@setOnClickListener; io.execute { runCatching { c.clearClips() } } }
        status = TextView(this).apply { text = getString(R.string.not_connected); setTextColor(Color.parseColor("#FBBF24")); typeface = android.graphics.Typeface.MONOSPACE }
        root.addView(status)
        timelineView = TextView(this).apply { setTextColor(Color.parseColor("#C2C7CF")); textSize = 10f; typeface = android.graphics.Typeface.MONOSPACE; visibility = View.GONE; setPadding(0, 4, 0, 4) }
        root.addView(timelineView)
        status.setOnClickListener { connBox?.let { it.visibility = if (it.visibility == View.VISIBLE) View.GONE else View.VISIBLE } }   // tap the status line: show / hide the connection fields
        hintView = TextView(this).apply { text = getString(R.string.tap_hint); setTextColor(Color.parseColor("#C2C7CF")); textSize = 11f; contentDescription = "remoteHint" }
        root.addView(hintView); fitOrientation()
        // THE CONSOLE: channel strips side by side (scroll sideways), the master strip fixed on the right
        val desk = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f) }
        strips = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        desk.addView(android.widget.HorizontalScrollView(this).apply { addView(strips); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f) })
        val master = ConsoleUi.strip(this, Color.parseColor("#FFB590"), 252, master = true)
        masterLabel = TextView(this).apply { setOnClickListener { showMaster() }; text = "MASTER"; setTextColor(Color.parseColor("#FFB590")); typeface = android.graphics.Typeface.DEFAULT_BOLD; gravity = Gravity.CENTER }
        planBadge = TextView(this).apply { text = ""; setTextColor(Color.parseColor("#FBBF24")); textSize = 10f; typeface = android.graphics.Typeface.MONOSPACE; gravity = Gravity.CENTER }
        masterFader = FaderView(this).apply { layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onTouchState = { t -> touching = maxOf(0, touching + if (t) 1 else -1); if (touching == 0) catchUp() }
            onMove = { p -> val v = DaemonClient.posToDb(p / 1000.0); masterDb.text = DaemonClient.fmtDb(v); live.submit("master") { client?.master(v) } } }
        masterDb = ConsoleUi.dbReadout(this).apply { text = "0.0" }
        master.addView(masterLabel); master.addView(planBadge); master.addView(masterFader); master.addView(masterDb)
        desk.addView(master); root.addView(desk)
        setContentView(Nav.wrap(this, root, Nav.Tab.MIXER))
        pendingOpen = intent.getStringExtra("open")
        ui.postDelayed(presence, 30_000)   // who is on the Wi-Fi: the console bar's dots stay true while a console is linked
        renderBar()

        connect.setOnClickListener {
            val ep = Endpoint(host.text.toString().trim(), port.text.toString().trim().toIntOrNull() ?: Prefs.defaultPort(this), token.text.toString().trim())
            prefs.edit().putString("host", ep.host).putString("port", ep.port.toString()).putString("token", ep.token).apply()
            status.text = getString(R.string.connecting)
            io.execute {
                try {
                    val c = DaemonClient(ep); val st = c.status(); val s = c.state()
                    ui.post { client = c; state = s; devices.isEnabled = true; connected.forEach { it.isEnabled = true }; if (Prefs.keepScreenOn(this)) window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON); startSync(c); connBox?.visibility = View.GONE; rememberConsole(ep, st.computer.ifBlank { st.name }, st.consoleId); val shown = consoleName(ep, st); consoleId = st.consoleId; linkedKey = ep.host + ":" + ep.port; autoMode = false; searchRound = 0; renderBar(); note(shown + " " + ep.host + " · " + getString(R.string.auto_connected)); lastOk = System.currentTimeMillis(); retries = 0; Prefs.remote(this).edit().putString("lastName", shown + " " + st.version).putString("lastPlan", st.plan).putInt("lastStrips", st.strips).apply(); if (pendingOpen == "scenes") { pendingOpen = null; showScenes() }; planBadge.text = (if (st.plan.isEmpty()) "" else st.plan + "\n") + st.strips + " CH"; status.text = shown + " " + st.version + " · " + (if (st.running) st.backend else "engine stopped") + " · " + st.strips + " strips"; status.setTextColor(Color.parseColor("#10B981")); masterFader.progress = (DaemonClient.dbToPos(s.masterDb) * 1000).toInt(); buildStrips(s); startMeters(c); Prefs.remote(this).edit().putString("lastState", OfflineScenes.stateJson(s)).apply(); offerPendingScene(c) }
                } catch (e: Exception) { ui.post { status.text = getString(R.string.failed, e.message ?: e.javaClass.simpleName); status.setTextColor(Color.parseColor("#FF2A5F"))
                    if (autoMode) { autoMode = false; note(getString(R.string.auto_lost, e.message ?: e.javaClass.simpleName)); if ((e as? com.amfb.amixos.remote.DaemonException)?.status == 401) { pinAsked = true; pairBtn?.performClick() } else nextSearch() } } }   // not there: search the Wi-Fi; token refused: the PIN
            }
        }
        devices.setOnClickListener { showDevices() }
        // SCAN QR: Google's code scanner (Play services) reads the console's pairing QR code. It needs no camera permission: the scanner
        // runs in Play services and hands back only the text. Then the same pairing as the PIN dialog, without typing anything.
        fun pairWith(h: String, pt: Int, pin: String) {
            host.setText(h); port.setText(pt.toString()); status.text = getString(R.string.connecting); status.setTextColor(Color.parseColor("#FBBF24"))
            io.execute {
                try {
                    val t = DaemonClient.pair(h, pt, pin, android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL)
                    ui.post { token.setText(t); connect.performClick() }   // connect saves host, port and the new token
                } catch (e: Exception) { ui.post { status.text = getString(R.string.failed, e.message ?: e.javaClass.simpleName); status.setTextColor(Color.parseColor("#FF2A5F")) } }
            }
        }
        scan.setOnClickListener {
            val opts = GmsBarcodeScannerOptions.Builder().setBarcodeFormats(Barcode.FORMAT_QR_CODE).build()
            runCatching { GmsBarcodeScanning.getClient(this, opts).startScan()
                .addOnSuccessListener { b -> val l = PairLink.parse(b.rawValue); if (l == null) status.text = getString(R.string.scan_not_console) else pairWith(l.host, l.port, l.pin) }
                .addOnFailureListener { status.text = getString(R.string.scan_unavailable); status.setTextColor(Color.parseColor("#FBBF24")) } }
                .onFailure { status.text = getString(R.string.scan_unavailable) }
        }
        pair.setOnClickListener {   // the PIN instead of typing the long token: the console shows it under NETWORK → PHONE REMOTE, new every minute
            val h = host.text.toString().trim(); val pt = port.text.toString().trim().toIntOrNull() ?: 8765
            if (h.isEmpty()) { status.text = getString(R.string.pair_need_host); return@setOnClickListener }
            val pinBox = EditText(this).apply { hint = "000000"; inputType = android.text.InputType.TYPE_CLASS_NUMBER; filters = arrayOf(android.text.InputFilter.LengthFilter(6)) }
            AlertDialog.Builder(this).setTitle(R.string.pair_pin).setMessage(R.string.pair_help).setView(pinBox)
                .setPositiveButton(R.string.pair_go) { _, _ ->
                    pairWith(h, pt, pinBox.text.toString().trim())
                }.setNegativeButton(android.R.string.cancel, null).show()
        }
    }

    private fun change(f: (Int) -> Unit) = object : SeekBar.OnSeekBarChangeListener {
        override fun onProgressChanged(s: SeekBar, p: Int, fromUser: Boolean) { if (fromUser) io.execute { runCatching { f(p) }.onFailure { e -> ui.post { status.text = getString(R.string.failed, e.message ?: "") } } } }
        override fun onStartTrackingTouch(s: SeekBar) { touching++ }
        override fun onStopTrackingTouch(s: SeekBar) { touching = maxOf(0, touching - 1) }
    }

    /** One column per channel the console's plan allows (the service opens only those): name (tap: channel page, long press: rename),
     *  pan, MUTE / SOLO, the fader with its meter, and the level. */
    // LANDSCAPE: the height is short, so the hint under the status line goes and the faders take that room; six channels stand across
    private var hintView: TextView? = null
    private fun fitOrientation() { hintView?.visibility = if (ChromeFrame.active(this) || resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE) View.GONE else View.VISIBLE }
    /** the phone was turned: the strips are laid out again for the new shape; the link to the console stays up */
    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) { super.onConfigurationChanged(newConfig); Nav.fit(this); fitOrientation(); state?.let { buildStrips(it) } }
    companion object {
        /** the width of one channel strip so that [across] of them stand beside the master strip on a screen [screenPx] wide (never under 176 px) */
        const val ACROSS = 4   // channels beside the master in the FADERS view: four, so each fader is wide enough for a thumb
        /** channels across beside the master: four upright, six when the phone lies on its side (each still wider than a thumb) */
        fun acrossFor(landscape: Boolean) = if (landscape) 6 else ACROSS
        fun stripWidthFor(screenPx: Int, across: Int): Int = maxOf(176, (screenPx - 40 - (252 + 8)) / maxOf(1, across) - 8)
        /** COMPACT (the AmF X1 app; the AmF X1 Mixer app keeps [ACROSS]): five channels across upright, eight on its side */
        const val COMPACT_ACROSS = 5
        fun compactAcrossFor(landscape: Boolean) = if (landscape) 8 else COMPACT_ACROSS
        /** a compact strip: as [stripWidthFor] but never thinner than [MIN_STRIP_DP] (a fingertip with its key) */
        const val MIN_STRIP_DP = 52f
        fun compactStripWidthFor(screenPx: Int, across: Int, density: Float): Int = maxOf((MIN_STRIP_DP * density).toInt(), (screenPx - 40 - (252 + 8)) / maxOf(1, across) - 8)
        /** the KNOBS view's strip, the knobs on it and the keys under them, in dp */
        const val KNOB_STRIP_DP = 58f; const val KNOB_DP = 34f; const val PAN_DP = 38f
    }
    private fun buildStrips(s: ConsoleState) {
        strips.removeAllViews(); faders.clear(); dbLabels.clear(); nameLabels.clear(); mutes.clear(); solos.clear(); clipFlags.clear(); toneUi.clear(); panKnobs.clear()
        val colors = intArrayOf(0xFFFFB590.toInt(), 0xFF22D3EE.toInt(), 0xFFA78BFA.toInt(), 0xFF34D399.toInt())
        // FADERS view: exactly five channels beside the master, so the ten channels of a console are two pages and each fader is as wide
        // as a finger and as tall as the screen allows. KNOBS view: narrower strips with pan, gain and the 3-band EQ above a shorter fader.
        // COMPACT: five channels across upright (eight on its side), the keys 40 dp tall with text that shrinks instead of being cut
        val dens = resources.displayMetrics.density
        val big = Prefs.bigFaders(this); val stripW = if (big) compactStripWidthFor(resources.displayMetrics.widthPixels, compactAcrossFor(resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE), dens) else ConsoleUi.dp(this, KNOB_STRIP_DP)
        for (st in s.strips.take(Prefs.stripCount(this))) {
            val tint = colors[(st.id / 8) % colors.size]
            val col = ConsoleUi.strip(this, tint, stripW)
            val name = TextView(this).apply { text = ConsoleUi.title(st.id, st.name); setTextColor(Color.WHITE); textSize = 10f; gravity = Gravity.CENTER; minLines = 2; maxLines = 2; typeface = android.graphics.Typeface.DEFAULT_BOLD
                setOnClickListener { showChannel(st.id, st.name) }; setOnLongClickListener { rename(st.id, this); true } }
            col.addView(name)
            // KNOBS view: pan, gain and EQ sit in their own scrolling box that takes at most 45 % of the strip, so the fader under it always
            // keeps more than half the height (before, the knobs took it all and the fader shrank to a sliver)
            val knobBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL }
            // PAN on every strip (the big-fader view too): L100 .. C .. R100, a double tap centres it
            val panKnob = KnobView(this, tint).apply { value = st.pan.toFloat(); contentDescription = "stripPan"; layoutParams = LinearLayout.LayoutParams(ConsoleUi.dp(this@RemoteActivity, PAN_DP), LinearLayout.LayoutParams.WRAP_CONTENT).apply { gravity = Gravity.CENTER_HORIZONTAL }
                onTouchState = { t -> touching = maxOf(0, touching + if (t) 1 else -1); if (touching == 0) catchUp() }
                onChange = { v -> live.submit("pan${st.id}") { client?.pan(st.id, v.toDouble()) } } }
            if (big) col.addView(panKnob) else if (Prefs.showPan(this)) knobBox.addView(panKnob)
            if (big || Prefs.showPan(this)) panKnobs[st.id] = panKnob
            if (!big && Prefs.showTone(this)) knobBox.addView(toneBlock(st, tint))
            if (knobBox.childCount > 0) col.addView(android.widget.ScrollView(this).apply { tag = "knobScroll"; isVerticalScrollBarEnabled = false; addView(knobBox)
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 0.8f) })
            val ms = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            fun small(t: ToggleButton) = ConsoleUi.compact(t, ConsoleUi.KEY_DP, 12).apply { setPadding(0, 0, 0, 0); contentDescription = "stripKey"; layoutParams = LinearLayout.LayoutParams(0, ConsoleUi.dp(this@RemoteActivity, ConsoleUi.KEY_DP), 1f).apply { setMargins(2, 3, 2, 3) } }
            val mute = small(ToggleButton(this).apply { textOn = "M"; textOff = "M"; isChecked = st.mute; setOnCheckedChangeListener { b, on -> b.setTextColor(if (on) Color.parseColor("#EF4444") else Color.WHITE); if (b.tag == null) { buzz(b); io.execute { runCatching { client?.mute(st.id, on) } } } } })
            val solo = small(ToggleButton(this).apply { textOn = "S"; textOff = "S"; isChecked = st.solo; setOnCheckedChangeListener { b, on -> b.setTextColor(if (on) Color.parseColor("#FBBF24") else Color.WHITE); if (b.tag == null) { buzz(b); io.execute { runCatching { client?.solo(st.id, on) } } } } })
            mute.setTextColor(if (st.mute) Color.parseColor("#EF4444") else Color.WHITE); solo.setTextColor(if (st.solo) Color.parseColor("#FBBF24") else Color.WHITE)
            ConsoleUi.styleToggle(mute, ConsoleUi.RED); ConsoleUi.styleToggle(solo, ConsoleUi.AMBER)
            ms.addView(mute); ms.addView(solo); col.addView(ms)
            val db = ConsoleUi.dbReadout(this).apply { text = DaemonClient.fmtDb(st.db) }
            val fader = FaderView(this, colors[(st.id / 8) % colors.size]).apply { progress = (DaemonClient.dbToPos(st.db) * 1000).toInt(); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f); contentDescription = "stripFader"
                onTouchState = { t -> touching = maxOf(0, touching + if (t) 1 else -1); if (touching == 0) catchUp() }
                onMove = { p -> val v = DaemonClient.posToDb(p / 1000.0); db.text = DaemonClient.fmtDb(v); live.submit("gain${st.id}") { client?.gain(st.id, v) } } }
            col.addView(fader); col.addView(db)
            strips.addView(col); faders.add(fader); dbLabels.add(db); nameLabels.add(name); mutes.add(mute); solos.add(solo); clipFlags.add(false)
        }
        // DCA GROUP CHANNELS of the console: a group in use is a strip after the channels (its fader moves its members' level, not their faders)
        for (g in s.dcas.filter { it.members.isNotEmpty() && !it.locked }) {
            val tint = ConsoleUi.AMBER; val col = ConsoleUi.strip(this, tint, stripW).apply { tag = "dcaStrip${g.id}" }
            col.addView(TextView(this).apply { text = getString(R.string.rc_dca_title, g.id, g.name, g.members.size); setTextColor(tint); textSize = 10f; gravity = Gravity.CENTER; minLines = 2; maxLines = 2; typeface = android.graphics.Typeface.DEFAULT_BOLD })
            val mute = ConsoleUi.compact(ToggleButton(this).apply { textOn = "M"; textOff = "M"; isChecked = g.mute; contentDescription = "dcaMute${g.id}"; setOnCheckedChangeListener { b, on -> b.setTextColor(if (on) Color.parseColor("#EF4444") else Color.WHITE); buzz(b); io.execute { runCatching { client?.dca(g.id, mute = on) } } } }, ConsoleUi.KEY_DP, 12)
            mute.setTextColor(if (g.mute) Color.parseColor("#EF4444") else Color.WHITE); ConsoleUi.styleToggle(mute, ConsoleUi.RED); col.addView(mute)
            val db = ConsoleUi.dbReadout(this).apply { text = DaemonClient.fmtDb(if (g.db <= -60) -120.0 else g.db) }
            col.addView(FaderView(this, tint).apply { progress = (DaemonClient.dbToPos(if (g.db <= -60) -120.0 else g.db) * 1000).toInt(); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f); contentDescription = "dcaFader${g.id}"
                onTouchState = { t -> touching = maxOf(0, touching + if (t) 1 else -1); if (touching == 0) catchUp() }
                onMove = { p -> val v = DaemonClient.posToDb(p / 1000.0).coerceIn(-60.0, 10.0); db.text = DaemonClient.fmtDb(if (v <= -60) -120.0 else v); live.submit("dca${g.id}") { client?.dca(g.id, db = v) } } })
            col.addView(db); strips.addView(col)
        }
        masterFader.progress = (DaemonClient.dbToPos(s.masterDb) * 1000).toInt(); masterDb.text = DaemonClient.fmtDb(s.masterDb)
    }

    private fun startMeters(c: DaemonClient) {
        stream?.stop(); c.clientName = android.os.Build.MANUFACTURER.replaceFirstChar { it.uppercase() } + " " + android.os.Build.MODEL
        stream = c.meterStream({ m -> ui.post { lastOk = System.currentTimeMillis(); showMeters(m) } }, { ok, err -> ui.post { if (ok) { retries = 0; showLink() } else if (!closing) scheduleReconnect(err) } },
            { s -> ui.post { if (closing || client !== c || Prefs.syncSeconds(this) == 0) return@post; lastPush = System.currentTimeMillis(); applyState(s); if (retries == 0) showLink() } },   // LIVE SYNC: pushed by the console
            { hold -> ui.post { if (closing || client !== c) return@post; kicked(hold) } })   // DISCONNECT / FORGET on the console
    }
    // ---- the link: remembered consoles, reconnect, its state on the status line
    private var pairBtn: Button? = null; @Volatile private var closing = false; private var retries = 0; private var lastOk = 0L; private var pingMs = -1L
    private fun consoles() = PairedConsoles.parse(getSharedPreferences("consoles", MODE_PRIVATE).getString("list", "[]"))
    private fun saveConsoles(l: List<PairedConsoles.Console>) = getSharedPreferences("consoles", MODE_PRIVATE).edit().putString("list", PairedConsoles.format(l)).apply()
    /** after a connect: the console is saved with this address and pairing; the name the phone already has for it stays (a new one is named after its computer) */
    private fun rememberConsole(ep: Endpoint, name: String, id: String = "") { val now = System.currentTimeMillis(); val l = consoles()
        val known = l.firstOrNull { it.key == "${ep.host}:${ep.port}" && (id.isEmpty() || it.id.isEmpty() || it.id == id) } ?: l.firstOrNull { id.isNotEmpty() && it.id == id }
        saveConsoles(PairedConsoles.upsert(l, PairedConsoles.Console(ep.host, ep.port, ep.token, known?.name?.takeIf { it.isNotBlank() && it != "A mix OS" } ?: name, now, now, id))) }   // "A mix OS": the name 1.23 gave every console
    /** what this console is called here: the name the phone keeps for it (renamed, or its computer's name), else its computer, else the product */
    private fun consoleName(ep: Endpoint, st: com.amfb.amixos.remote.Status): String =
        consoles().firstOrNull { it.key == "${ep.host}:${ep.port}" }?.name?.takeIf { it.isNotBlank() && it != "A mix OS" } ?: st.computer.ifBlank { st.name }
    // ---- AUTO-DETECT: find the console on the Wi-Fi by itself (the console's /api/status carries a stable consoleId)
    private lateinit var hostBox: EditText; private lateinit var portBox: EditText; private lateinit var tokenBox: EditText; private lateinit var timelineView: TextView
    private val timeline = LinkTimeline(); private var autoMode = false; private var searchRound = 0; private var searching = false; private var pinAsked = false; private var consoleId = ""
    // NOTIFICATIONS: the console's events (clipping, device lost / connected, CPU overload, audio loop stopped, what's new) as a toast and in the timeline
    private val notices = Notices()
    private fun notice(e: Notices.Event?) { if (e == null || closing) return; android.widget.Toast.makeText(this, e.text, android.widget.Toast.LENGTH_SHORT).show(); note(e.text) }
    private val noticePoll = object : Runnable { override fun run() { if (closing) return
        val c = client; if (c != null && resumed()) io.execute { runCatching { c.status() }.getOrNull()?.let { st -> ui.post { if (closing) return@post
            val p = getSharedPreferences("notices", MODE_PRIVATE); notice(notices.versionSeen(System.currentTimeMillis(), st.version, p.getString("version", null))); p.edit().putString("version", st.version).apply()
            notices.status(System.currentTimeMillis(), st.running, st.outDevice, st.xruns, st.loadPct).forEach { notice(it) } } } }
        ui.postDelayed(this, 5000) } }
    private fun note(text: String) { timeline.add(System.currentTimeMillis(), text)
        timelineView.text = getString(R.string.link_timeline) + "\n" + timeline.render { t -> android.text.format.DateFormat.format("HH:mm:ss", t).toString() }; timelineView.visibility = View.VISIBLE }
    private fun resumed() = lifecycle.currentState.isAtLeast(androidx.lifecycle.Lifecycle.State.RESUMED)
    /** one search: a paired console (by its consoleId, so also at a new address) connects at once; a new one asks for its PIN once;
     *  nothing: search again in 3, 6, 12, then every 20 s while this screen is open and not connected (the computer may start its remote later) */
    internal fun autoFind(net: String? = null) {   // net: the address to search from (tests); else this phone's Wi-Fi address
        if (closing || client != null || searching || !Prefs.autoFind(this) || !resumed()) return
        val held = heldUntil - System.currentTimeMillis(); if (held > 0) { ui.postDelayed({ autoFind(net) }, held + 500); return }   // the console disconnected this phone: wait out its hold
        val ip = net ?: localIp(); searchRound++
        if (ip == null) { status.text = getString(R.string.no_wifi); note(getString(R.string.no_wifi)); nextSearch(); return }
        searching = true; val pt = portBox.text.toString().trim().toIntOrNull() ?: Prefs.defaultPort(this); val paired = consoles()
        note(getString(R.string.auto_searching, ip.substringBeforeLast('.'))); status.text = getString(R.string.searching); status.setTextColor(Color.parseColor("#FBBF24"))
        scanIo.execute {
            val found = runCatching { DaemonClient.discoverConsoles(ip, pt, first = PairedConsoles.hints(paired)) }.getOrDefault(emptyList())
            ui.post { searching = false; lastFound = found; renderBar(); if (closing || client != null) return@post
                when (val m = PairedConsoles.match(paired, found)) {
                    is PairedConsoles.Match.Known -> { val c = m.console; val label = c.name.ifBlank { found.firstOrNull { it.host == c.host }?.title ?: c.host }
                        note(if (m.moved) getString(R.string.auto_moved, label, c.host) else getString(R.string.auto_found, label, c.host))
                        if (m.moved) saveConsoles(PairedConsoles.upsert(consoles(), c))   // remember the new address
                        hostBox.setText(c.host); portBox.setText(c.port.toString()); tokenBox.setText(c.token); autoMode = true; connectBtn?.performClick() }
                    is PairedConsoles.Match.New -> { val f = m.found.first(); note(getString(R.string.auto_found, f.title, f.host)); note(getString(R.string.auto_new, f.title))
                        hostBox.setText(f.host); portBox.setText(f.port.toString()); status.text = getString(R.string.found_pair)
                        if (!pinAsked) { pinAsked = true; pairBtn?.performClick() }; nextSearch() }
                    PairedConsoles.Match.None -> { val wait = PairedConsoles.searchDelayMs(searchRound); note(getString(R.string.auto_none, (wait / 1000).toInt())); status.text = getString(R.string.none_found); nextSearch() }
                } } }
    }
    // ---- SEVERAL CONSOLES: the bar, switching, and who is on the Wi-Fi (a quiet search every 30 s while this screen is open)
    private lateinit var consoleBar: LinearLayout; private var lastFound: List<DaemonClient.Companion.Found> = emptyList(); private var linkedKey = ""; private var heldUntil = 0L
    private val presence = object : Runnable { override fun run() { if (closing) return; ui.postDelayed(this, 30_000); if (!resumed() || searching) return; presenceScan() } }
    internal fun presenceScan(net: String? = null) {   // net: the address to search from (tests)
        val ip = net ?: localIp() ?: return; val paired = consoles(); val pt = portBox.text.toString().trim().toIntOrNull() ?: Prefs.defaultPort(this)
        scanIo.execute { val f = runCatching { DaemonClient.discoverConsoles(ip, pt, first = PairedConsoles.hints(paired)) }.getOrDefault(emptyList()); ui.post { lastFound = f; renderBar() } }
    }
    internal fun barChips() = PairedConsoles.chips(consoles(), lastFound, if (client != null) consoleId else "", if (client != null) linkedKey else "")
    internal fun renderBar() {
        if (!::consoleBar.isInitialized) return; consoleBar.removeAllViews()
        val chips = barChips()
        for (ch in chips) {
            val (dot, ink) = when (ch.presence) { PairedConsoles.Presence.LINKED -> "#34D399" to "#FFFFFF"; PairedConsoles.Presence.ONLINE -> "#22D3EE" to "#E2E8F0"; PairedConsoles.Presence.AWAY -> "#475569" to "#C2C7CF"; PairedConsoles.Presence.NEW -> "#FBBF24" to "#FBBF24" }
            val tag = when (ch.presence) { PairedConsoles.Presence.LINKED -> getString(R.string.bar_linked); PairedConsoles.Presence.ONLINE -> getString(R.string.bar_online); PairedConsoles.Presence.AWAY -> getString(R.string.bar_away); PairedConsoles.Presence.NEW -> getString(R.string.bar_new) }
            consoleBar.addView(TextView(this).apply {
                text = android.text.SpannableStringBuilder().apply { append("● ", android.text.style.ForegroundColorSpan(Color.parseColor(dot)), 0); append(ch.label); append("  "); append(tag, android.text.style.RelativeSizeSpan(0.8f), 0) }
                setTextColor(Color.parseColor(ink)); textSize = 12f; typeface = android.graphics.Typeface.DEFAULT_BOLD; setPadding(26, 14, 26, 14)
                background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = 40f; setColor(Color.parseColor(if (ch.presence == PairedConsoles.Presence.LINKED) "#123B30" else "#272A2F")); setStroke(2, Color.parseColor(dot)) }
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 14, 0) }
                setOnClickListener { buzz(it); switchTo(ch) } })
        }
        consoleBar.visibility = if (chips.isEmpty()) View.GONE else View.VISIBLE
    }
    /** one tap on the bar: the linked console stays; a paired one is linked instead of the current one (at its address as last found); a new one asks for its PIN */
    internal fun switchTo(ch: PairedConsoles.Chip) {
        if (ch.presence == PairedConsoles.Presence.LINKED) return
        if (ch.presence == PairedConsoles.Presence.NEW) { hostBox.setText(ch.console.host); portBox.setText(ch.console.port.toString()); pinAsked = true; pairBtn?.performClick(); return }
        note(getString(R.string.bar_switch, ch.label)); dropLink()
        hostBox.setText(ch.console.host); portBox.setText(ch.console.port.toString()); tokenBox.setText(ch.console.token); autoMode = Prefs.autoFind(this); connectBtn?.performClick()
    }
    /** leaves the current console (its stream stops; nothing is changed on it) */
    private fun dropLink() { stream?.stop(); stream = null; client = null; state = null; linkedKey = ""; retries = 0; connected.forEach { it.isEnabled = false } }
    /** the console disconnected this phone: no reconnecting by itself while the console holds it out */
    private fun kicked(holdSeconds: Int) {
        dropLink(); heldUntil = System.currentTimeMillis() + holdSeconds * 1000L; connBox?.visibility = View.VISIBLE
        status.text = if (holdSeconds > 0) getString(R.string.kicked_hold, (holdSeconds + 59) / 60) else getString(R.string.kicked); status.setTextColor(Color.parseColor("#FF2A5F"))
        note(status.text.toString()); renderBar(); if (holdSeconds > 0) ui.postDelayed({ autoFind() }, holdSeconds * 1000L + 500)
    }
    private fun nextSearch() { ui.postDelayed({ autoFind() }, PairedConsoles.searchDelayMs(searchRound)) }
    /** the link is down for a while: is the same console up at another address (the router gave the PC a new IP)? Then follow it. */
    private fun followMove(c: DaemonClient) {
        val ip = localIp() ?: return; val me = consoles().firstOrNull { consoleId.isNotEmpty() && it.id == consoleId } ?: return
        scanIo.execute { val found = runCatching { DaemonClient.discoverConsoles(ip, me.port, first = listOf(me.host)) }.getOrDefault(emptyList())
            val m = PairedConsoles.match(listOf(me), found) as? PairedConsoles.Match.Known ?: return@execute
            if (!m.moved) return@execute
            ui.post { if (closing || client !== c) return@post; note(getString(R.string.auto_moved, m.console.name.ifBlank { m.console.host }, m.console.host)); saveConsoles(PairedConsoles.upsert(consoles(), m.console))
                stream?.stop(); stream = null; client = null; hostBox.setText(m.console.host); portBox.setText(m.console.port.toString()); tokenBox.setText(m.console.token); connectBtn?.performClick() } }
    }
    /** the connection dropped (Wi-Fi gone, the console restarted): try again, 1 s, 2 s, 4 s, 8 s, then every 15 s, as long as this screen is open */
    private fun scheduleReconnect(err: String?) {
        val c = client ?: return; retries++; val wait = PairedConsoles.retryDelayMs(retries)
        if (retries == 1) note(getString(R.string.auto_lost, err ?: "…")); if (retries >= 3 && retries % 3 == 0) followMove(c)   // down for a while: maybe a new address
        status.text = getString(R.string.reconnecting, retries, (wait / 1000).toInt()) + (if (err != null) "  ·  $err" else ""); status.setTextColor(Color.parseColor("#FBBF24"))
        ui.postDelayed({ if (closing || client !== c) return@postDelayed
            io.execute { val ok = runCatching { val t0 = System.currentTimeMillis(); c.status(); pingMs = System.currentTimeMillis() - t0 }.isSuccess
                ui.post { if (closing || client !== c) return@post; if (ok) { retries = 0; lastOk = System.currentTimeMillis(); startMeters(c); showLink(); io.execute { runCatching { c.state() }.getOrNull()?.let { s -> ui.post { applyState(s) } } } } else scheduleReconnect(null) } } }, wait)
    }
    private fun live() = stream != null && System.currentTimeMillis() - lastOk < 3000 && Prefs.syncSeconds(this) != 0   // the stream is up: changes arrive as they happen
    private fun showLink() { val ep = Prefs.remote(this); status.text = getString(R.string.link_up, ep.getString("lastName", "").orEmpty(), ep.getString("host", "").orEmpty(), if (pingMs >= 0) "$pingMs ms" else "—") + (if (live()) "  ·  " + getString(R.string.live_sync) else ""); status.setTextColor(Color.parseColor("#34D399")) }
    /** CONSOLES: the consoles this phone is paired with. Tap = connect; FORGET = remove it from this phone (the console keeps a token list of its own, NETWORK → TOKENS) */
    private fun showConsoles(host: EditText, port: EditText, token: EditText, connect: Button) {
        val l = consoles(); val now = System.currentTimeMillis()
        val dlg = AlertDialog.Builder(this).setTitle(R.string.consoles).setNegativeButton(android.R.string.cancel, null).setPositiveButton(R.string.pair_new) { _, _ -> pairBtn?.performClick() }.create()
        dlg.setView(ScrollView(this).apply { addView(consolesPanel(l, now, { c -> dlg.dismiss(); host.setText(c.host); port.setText(c.port.toString()); token.setText(c.token); connect.performClick() },
            { c -> AlertDialog.Builder(this@RemoteActivity).setMessage(getString(R.string.forget_ask, c.name.ifBlank { c.host })).setPositiveButton(R.string.forget) { _, _ -> saveConsoles(PairedConsoles.forget(consoles(), c.key)); dlg.dismiss(); showConsoles(host, port, token, connect) }.setNegativeButton(android.R.string.cancel, null).show() })) })
        dlg.show()
    }
    internal fun consolesPanel(l: List<PairedConsoles.Console>, now: Long, pick: (PairedConsoles.Console) -> Unit, forget: (PairedConsoles.Console) -> Unit): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16); setBackgroundColor(Color.parseColor("#1D2024")) }
        val current = Prefs.remote(this).getString("host", "")
        if (l.isEmpty()) box.addView(TextView(this).apply { text = getString(R.string.consoles_none); setTextColor(Color.parseColor("#C2C7CF")); setPadding(0, 8, 0, 8) })
        for (c in l) {
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 10, 0, 10); isClickable = true; setOnClickListener { pick(c) } }
            val col = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
            col.addView(TextView(this).apply { text = (if (c.host == current && client != null) "● " else "") + c.name.ifBlank { "AmF X1" }; setTextColor(if (c.host == current && client != null) Color.parseColor("#34D399") else Color.WHITE); textSize = 13f; setTypeface(null, android.graphics.Typeface.BOLD) })
            col.addView(TextView(this).apply { text = c.host + ":" + c.port; setTextColor(Color.parseColor("#7FD8E8")); typeface = android.graphics.Typeface.MONOSPACE; textSize = 12f })
            col.addView(TextView(this).apply { text = getString(R.string.consoles_seen, PairedConsoles.ago(now, c.lastSeen)); setTextColor(Color.parseColor("#C2C7CF")); textSize = 11f })
            row.addView(col)
            row.addView(Button(this).apply { text = getString(R.string.forget); textSize = 11f; setOnClickListener { forget(c) } })
            box.addView(row)
        }
        return box
    }
    private fun showMeters(m: Meters) {
        val floor = Prefs.meterFloor(this).toDouble(); fun bar(v: Double) = ((DaemonClient.lin2db(v) + floor) / floor * 1000).toInt().coerceIn(0, 1000)
        for (i in faders.indices) if (i < m.strips.size) { faders[i].level = bar(m.strips[i].pk); faders[i].clip = m.strips[i].clip }
        masterFader.level = bar(Math.max(m.masterL, m.masterR)); masterFader.clip = m.masterClip
        notice(notices.clip(System.currentTimeMillis(), m.masterClip || m.strips.any { it.clip }))
        masterMeterView?.let { v -> if (v.isAttachedToWindow) { val d = { x: Double -> if (x <= 1e-6) "-∞" else String.format("%.1f", 20 * Math.log10(x)) }; v.text = getString(R.string.master_meters, d(m.masterL), d(m.masterR)) } else masterMeterView = null }
    }

    private fun showDevices() {
        val c = client ?: return
        io.execute {
            val devs = runCatching { c.devices() }.getOrDefault(emptyList())
            ui.post {
                val names = devs.map { it.backend.uppercase() + "  " + it.name + "  (" + it.inputs + " in / " + it.outputs + " out)" }.toTypedArray()
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
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16); setBackgroundColor(Color.parseColor("#1D2024")) }
        val accent = Color.parseColor("#FFB590"); val cyan = Color.parseColor("#7FD8E8")
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
        box.addView(Button(this).apply { text = getString(R.string.eq_open_s); setOnClickListener { startActivity(android.content.Intent(this@RemoteActivity, EqActivity::class.java).putExtra("strip", strip).putExtra("name", state?.strips?.getOrNull(strip)?.name ?: "")) } })
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

        if (Features.isOn(this, "sends")) head("SENDS")
        for (b in 0 until (if (Features.isOn(this, "sends")) 8 else 0)) { var pre = false; var lvl = -120.0
            val line = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            val t = TextView(this).apply { text = "AUX ${b + 1}  -∞"; setTextColor(cyan); typeface = android.graphics.Typeface.MONOSPACE; layoutParams = LinearLayout.LayoutParams(300, LinearLayout.LayoutParams.WRAP_CONTENT) }
            val sb = SeekBar(this).apply { max = 1000; progress = 0; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                setOnSeekBarChangeListener(change { p -> lvl = DaemonClient.posToDb(p / 1000.0); ui.post { t.text = "AUX ${b + 1}  " + DaemonClient.fmtDb(lvl) }; c.send(strip, b, lvl, pre) }) }
            val pp = ToggleButton(this).apply { textOn = "PRE"; textOff = "POST"; text = "POST"; setOnCheckedChangeListener { _, v -> pre = v; io.execute { runCatching { c.send(strip, b, lvl, pre) } } } }
            line.addView(t); line.addView(sb); line.addView(pp); box.addView(line) }
        return box
    }

    /** RECORD: starts the console's own recording of its master (FLAC 24-bit, WAV 24-bit or 32-bit float, into the console's recordings
     *  folder), or stops the one that is running. */
    private fun toggleRecord(btn: Button) {
        val c = client; if (c == null) { status.text = getString(R.string.not_connected); return }
        io.execute { runCatching { c.recordState() }.onSuccess { (on, sec, _) -> ui.post {
            if (on) io.execute { runCatching { c.record(false) }.onSuccess { ui.post { btn.setTextColor(Color.WHITE); status.text = getString(R.string.remote_rec_stopped, RecorderActivity.mmss(sec)) } }.onFailure { ui.post { status.text = getString(R.string.failed, it.message ?: "") } } }
            else { val fm = listOf("flac" to "FLAC 24-BIT", "wav24" to "WAV 24-BIT", "wavf32" to "WAV 32-BIT FLOAT")
                AlertDialog.Builder(this).setTitle(R.string.remote_rec).setItems(fm.map { it.second }.toTypedArray()) { _, w ->
                    io.execute { runCatching { c.record(true, fm[w].first) }.onSuccess { f -> ui.post { btn.setTextColor(Color.parseColor("#EF4444")); status.text = getString(R.string.remote_rec_on, f.substringAfterLast('/').substringAfterLast('\\')) } }.onFailure { ui.post { status.text = getString(R.string.failed, it.message ?: "") } } } }.show() } } }
            .onFailure { ui.post { status.text = getString(R.string.failed, it.message ?: "") } } }
    }
    /** INPUT: what the console listens to. MICROPHONE = its default capture device; COMPUTER SOUND = every program playing on that computer
     *  (Windows 10 2004 and later: the console lists it as "loopback:system"). The console's stream is opened again with that input. */
    private fun pickInput() {
        // AUTO INPUT on the console: one switch, no list. ON: the console's microphone (its computer's sound where no microphone opens);
        // OFF: no input at all, nothing is captured. The console reopens its own sound card with it (op "input").
        val c = client; if (c == null) { status.text = getString(R.string.not_connected); return }
        io.execute {
            val on = runCatching { c.status().inChannels > 0 }.getOrDefault(false)
            val r = runCatching { if (on) c.input("-") else runCatching { c.input("") }.getOrElse { c.input("loopback:system") } }
            val now = runCatching { c.status().inChannels > 0 }.getOrDefault(!on)
            ui.post { status.text = if (r.isFailure) getString(R.string.failed, r.exceptionOrNull()?.message ?: "") else getString(if (now) R.string.remote_auto_input_on else R.string.remote_auto_input_off) }
        }
    }
    /** TOOLS: one list of the remote's pages */
    internal fun showTools(): AlertDialog { val dlg = AlertDialog.Builder(this).setTitle(R.string.rt_tools).setNegativeButton(android.R.string.cancel, null).create()
        dlg.setView(ScrollView(this).apply { addView(RemoteTools.toolsPanel(this@RemoteActivity) { t -> dlg.dismiss(); openTool(t) }) }); dlg.show(); return dlg }
    internal fun openTool(t: RemoteTools.Tool) { when (t) {
        RemoteTools.Tool.DCA -> showDcaPage()
        RemoteTools.Tool.SPATIAL -> AlertDialog.Builder(this).setTitle(R.string.rt_360).setView(RemoteTools.spatialPanel(this, false)).setPositiveButton(android.R.string.ok, null).show()
        RemoteTools.Tool.EFFECTS -> startActivity(android.content.Intent(this, InsertsActivity::class.java))   // the inserts flow: pick a channel, add an effect
        RemoteTools.Tool.PLUGINS -> startActivity(android.content.Intent(this, PluginsActivity::class.java))
        RemoteTools.Tool.SCENES -> showScenes()
        RemoteTools.Tool.CALCULATOR -> startActivity(android.content.Intent(this, CalculatorActivity::class.java))
        RemoteTools.Tool.MIC -> startActivity(android.content.Intent(this, PhoneMicActivity::class.java)) } }
    /** GROUP FADERS (DCA): the console's groups, as many as its edition has */
    internal fun showDcaPage() { val s = state; if (s == null) { status.text = getString(R.string.not_connected); return }
        AlertDialog.Builder(this).setTitle(R.string.rt_dca).setView(ScrollView(this).apply { addView(dcaPage(s)) }).setPositiveButton(android.R.string.ok, null).show() }
    internal fun dcaPage(s: ConsoleState): View = RemoteTools.dcaPanel(this, s, Prefs.remote(this).getString("lastPlan", "").orEmpty()) { g, db, mute ->
        if (db != null) live.submit("dca$g") { client?.dca(g, db = db) }; if (mute != null) io.execute { runCatching { client?.dca(g, mute = mute) } } }
    /** CHROMEBOOK MODE: the arrow keys move the master as on the console (1 dB, with SHIFT 0.1 dB) */
    override fun dispatchKeyEvent(e: android.view.KeyEvent): Boolean {
        val step = ChromeFrame.masterStep(e, currentFocus is android.widget.EditText); val s = state
        if (step != null && s != null && ChromeFrame.active(this)) { if (e.action == android.view.KeyEvent.ACTION_DOWN) { val v = ChromeFrame.stepMaster(s.masterDb, step); state = s.copy(masterDb = v)
            masterFader.progress = (DaemonClient.dbToPos(v) * 1000).toInt(); masterDb.text = DaemonClient.fmtDb(v); live.submit("master") { client?.master(v) } }; return true }
        return super.dispatchKeyEvent(e)
    }
    /** MUTE GROUPS: eight groups of strips, muted and un-muted together. Kept on this phone (SharedPreferences "muteGroups"). */
    private fun showMuteGroups() {
        val c = client; if (c == null) { status.text = getString(R.string.not_connected); return }
        val dlg = AlertDialog.Builder(this).setTitle(R.string.mute_groups).setPositiveButton(android.R.string.ok, null).create()
        dlg.setView(ScrollView(this).apply { addView(muteGroupsPanel(c)) }); dlg.show()
    }
    internal fun muteGroupsPanel(c: DaemonClient?): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16); setBackgroundColor(Color.parseColor("#1D2024")) }
        val store = getSharedPreferences("muteGroups", MODE_PRIVATE); val n = (state?.strips?.size ?: Prefs.stripCount(this)).coerceAtLeast(1)
        fun refresh() {
            box.removeAllViews()
            box.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.mute_groups_note); setTextColor(Color.parseColor("#C2C7CF")); textSize = 12f; setPadding(0, 0, 0, 12) }))
            for (g in 0 until MuteGroups.COUNT) {
                val m = MuteGroups.members(store, g); val on = MuteGroups.active(store, g)
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = android.view.Gravity.CENTER_VERTICAL }
                row.addView(Button(this).apply { text = getString(R.string.mute_group_n, g + 1); setTextColor(if (on) Color.parseColor("#EF4444") else Color.WHITE); isEnabled = m.isNotEmpty()
                    setOnClickListener { val groups = (0 until MuteGroups.COUNT).map { MuteGroups.members(store, it) }; val act = (0 until MuteGroups.COUNT).map { MuteGroups.active(store, it) }
                        val plan = MuteGroups.plan(groups, act, g, !on); MuteGroups.setActive(store, g, !on); refresh()
                        if (c != null) io.execute { runCatching { plan.forEach { (s, mu) -> c.mute(s, mu) } }.onFailure { ui.post { status.text = getString(R.string.failed, it.message ?: "") } } } } })
                row.addView(TextView(this).apply { text = MuteGroups.label(m); setTextColor(Color.parseColor("#7FD8E8")); typeface = android.graphics.Typeface.MONOSPACE; textSize = 12f; setPadding(16, 0, 16, 0); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
                row.addView(Button(this).apply { text = getString(R.string.mute_group_edit); textSize = 11f; setOnClickListener {
                    val names = (0 until n).map { i -> String.format("CH %02d  %s", i + 1, state?.strips?.getOrNull(i)?.name.orEmpty()) }.toTypedArray(); val sel = BooleanArray(n) { it in m }
                    AlertDialog.Builder(this@RemoteActivity).setTitle(getString(R.string.mute_group_n, g + 1)).setMultiChoiceItems(names, sel) { _, w, ch -> sel[w] = ch }
                        .setPositiveButton(android.R.string.ok) { _, _ -> MuteGroups.setMembers(store, g, (0 until n).filter { sel[it] }.toSet()); refresh() }.setNegativeButton(android.R.string.cancel, null).show() } })
                box.addView(row)
            }
        }
        refresh(); return box
    }

    /** SCENES on the phone: SAVE takes the console's mix as it is now (levels, pan, mute, solo, names, master, buses); RECALL sends it back.
     *  Kept on this phone (SharedPreferences "scenes", one JSON per scene). */
    private fun showScenes() {
        val c = client   // null: OFFLINE SCENES (make and edit them here; recall when a console is linked)
        val dlg = AlertDialog.Builder(this).setTitle(if (c == null) getString(R.string.scenes_offline) else getString(R.string.scenes)).setPositiveButton(android.R.string.ok, null).create()
        dlg.setView(ScrollView(this).apply { addView(scenesPanel(c) { dlg.dismiss() }) }); dlg.show()
    }
    internal fun scenesPanel(c: DaemonClient?, done: () -> Unit = {}): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16); setBackgroundColor(Color.parseColor("#1D2024")) }
        val store = getSharedPreferences("scenes", MODE_PRIVATE)
        fun refresh() {
            box.removeAllViews()
            val pending = Prefs.remote(this).getString("recallOnConnect", "").orEmpty()
            if (c == null) box.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.scenes_offline_note); setTextColor(Color.parseColor("#FBBF24")); textSize = 12f; setPadding(0, 0, 0, 10) }))
            box.addView(Button(this).apply { text = getString(if (c == null) R.string.scene_new_offline else R.string.scene_save); setOnClickListener {
                val e = EditText(this@RemoteActivity).apply { setText(String.format("Scene %d", store.all.size + 1)); setSingleLine() }
                if (c == null) { AlertDialog.Builder(this@RemoteActivity).setTitle(R.string.scene_new_offline).setView(e).setPositiveButton(android.R.string.ok) { _, _ ->
                        val name = e.text.toString().trim().ifEmpty { "Scene" }
                        store.edit().putString(name, OfflineScenes.stringify(OfflineScenes.blank(Prefs.remote(this@RemoteActivity).getString("lastState", null), Prefs.stripCount(this@RemoteActivity), System.currentTimeMillis()))).apply()
                        refresh(); editScene(name) { refresh() } }.setNegativeButton(android.R.string.cancel, null).show(); return@setOnClickListener }
                AlertDialog.Builder(this@RemoteActivity).setTitle(R.string.scene_save).setView(e).setPositiveButton(android.R.string.ok) { _, _ ->
                    val name = e.text.toString().trim().ifEmpty { "Scene" }
                    io.execute { runCatching { val st = c!!.state(); val o = mapOf("t" to System.currentTimeMillis(), "master" to st.masterDb,
                            "strips" to st.strips.map { mapOf("id" to it.id, "name" to it.name, "db" to it.db, "pan" to it.pan, "mute" to it.mute, "solo" to it.solo) },
                            "buses" to st.buses.map { mapOf("id" to it.id, "db" to it.db, "mute" to it.mute) })
                        store.edit().putString(name, com.amfb.amixos.remote.MiniJson.stringify(o)).apply() }
                        .onSuccess { ui.post { refresh() } }.onFailure { ui.post { status.text = getString(R.string.failed, it.message ?: "") } } } }
                    .setNegativeButton(android.R.string.cancel, null).show() } })
            val all = store.all; val names = all.keys.filter { OfflineScenes.read(all[it]) != null }.sortedBy { n -> ((OfflineScenes.read(all[n])?.get("t") as? Number)?.toLong() ?: 0L) }
            if (names.isEmpty()) box.addView(TextView(this).apply { text = getString(R.string.scene_none); setTextColor(Color.parseColor("#C2C7CF")); setPadding(0, 16, 0, 0) })
            for (n in names) {
                val o = OfflineScenes.read(all[n]) ?: continue
                val when_ = java.text.DateFormat.getDateTimeInstance(java.text.DateFormat.SHORT, java.text.DateFormat.SHORT).format(java.util.Date((o["t"] as? Number)?.toLong() ?: 0L))
                val strips = (o["strips"] as? List<*>).orEmpty(); val muted = strips.count { (it as? Map<*, *>)?.get("mute") == true }
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 12, 0, 0) }
                val t = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
                t.addView(TextView(this).apply { text = n + (if (o["offline"] == true) "  · " + getString(R.string.scene_offline_tag) else "") + (if (n == pending) "  · " + getString(R.string.scene_pending_tag) else ""); setTextColor(if (n == pending) Color.parseColor("#FBBF24") else Color.WHITE); textSize = 13f })
                t.addView(TextView(this).apply { text = getString(R.string.scene_info, when_, strips.size, muted); setTextColor(Color.parseColor("#C2C7CF")); textSize = 11f })
                row.addView(t)
                if (c != null) row.addView(Button(this).apply { text = getString(R.string.scene_recall); setOnClickListener {
                    AlertDialog.Builder(this@RemoteActivity).setMessage(getString(R.string.scene_recall_q, n)).setPositiveButton(R.string.scene_recall) { _, _ -> recallScene(c, n, o, done) }.setNegativeButton(android.R.string.cancel, null).show() } })
                else row.addView(Button(this).apply { text = getString(if (n == pending) R.string.scene_pending_off else R.string.scene_pending_on); setOnClickListener {   // RECALL WHEN CONNECTED (asks first)
                    Prefs.remote(this@RemoteActivity).edit().putString("recallOnConnect", if (n == pending) "" else n).apply(); refresh() } })
                row.addView(Button(this).apply { text = getString(R.string.scene_edit); setOnClickListener { editScene(n) { refresh() } } })
                row.addView(Button(this).apply { text = "✕"; setOnClickListener { store.edit().remove(n).apply(); if (n == pending) Prefs.remote(this@RemoteActivity).edit().putString("recallOnConnect", "").apply(); refresh() } })
                box.addView(row)
            }
        }
        refresh(); return box
    }

    /** a saved scene onto the console: every channel this console has, the buses, the master */
    private fun recallScene(c: DaemonClient, n: String, o: Map<String, Any?>, done: () -> Unit = {}) {
        io.execute { runCatching {
            for (x in OfflineScenes.forConsole(o, state?.strips?.size ?: 0)) { val id = (x["id"] as Number).toInt()   // a scene from a bigger plan: the extra channels are skipped
                c.gain(id, (x["db"] as Number).toDouble()); c.pan(id, (x["pan"] as? Number)?.toDouble() ?: 0.0); c.mute(id, x["mute"] == true); c.solo(id, x["solo"] == true); c.name(id, x["name"].toString()) }
            for (x in (o["buses"] as? List<*>).orEmpty().mapNotNull { it as? Map<*, *> }) { val id = (x["id"] as Number).toInt(); c.busLevel(id, (x["db"] as Number).toDouble()); c.busMute(id, x["mute"] == true) }
            c.master((o["master"] as? Number)?.toDouble() ?: 0.0); c.state() }
            .onSuccess { st -> ui.post { state = st; buildStrips(st); status.text = getString(R.string.scene_recalled, n); note(getString(R.string.scene_recalled, n)); Home.setLastScene(this, n); done() } }
            .onFailure { ui.post { status.text = getString(R.string.failed, it.message ?: "") } } }
    }
    /** SCENE EDITOR (works offline): each channel's name, level and mute, and the master; saved on the phone at once */
    private fun editScene(name: String, saved: () -> Unit) {
        val store = getSharedPreferences("scenes", MODE_PRIVATE); val scene = OfflineScenes.parse(store.getString(name, null)) ?: return
        val dlg = AlertDialog.Builder(this).setTitle(getString(R.string.scene_edit_title, name)).setPositiveButton(android.R.string.ok) { _, _ -> saved() }.create()
        dlg.setView(ScrollView(this).apply { addView(sceneEditor(name, scene) { store.edit().putString(name, OfflineScenes.stringify(it)).apply() }) }); dlg.show()
    }
    @Suppress("UNCHECKED_CAST")
    internal fun sceneEditor(name: String, scene: MutableMap<String, Any?>, save: (Map<String, Any?>) -> Unit): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 12, 24, 12); setBackgroundColor(Color.parseColor("#1D2024")) }
        fun pos(db: Double) = ((db + 60) * 10).toInt().coerceIn(0, 700)
        fun row(label: View, db: Double, mute: Boolean?, onDb: (Double) -> Unit, onMute: ((Boolean) -> Unit)?) {
            val r = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 4, 0, 4) }
            val v = TextView(this).apply { text = DaemonClient.fmtDb(db); setTextColor(Color.parseColor("#22D3EE")); typeface = android.graphics.Typeface.MONOSPACE; textSize = 12f; layoutParams = LinearLayout.LayoutParams(150, LinearLayout.LayoutParams.WRAP_CONTENT) }
            r.addView(label.apply { layoutParams = LinearLayout.LayoutParams(260, LinearLayout.LayoutParams.WRAP_CONTENT) })
            r.addView(SeekBar(this).apply { max = 700; progress = pos(db); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener { override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) { if (!u) return; val d = p / 10.0 - 60; v.text = DaemonClient.fmtDb(d); onDb(d) }
                    override fun onStartTrackingTouch(s: SeekBar) {}; override fun onStopTrackingTouch(s: SeekBar) {} }) })
            r.addView(v)
            if (mute != null && onMute != null) r.addView(ToggleButton(this).apply { textOn = "M"; textOff = "M"; isChecked = mute; minWidth = 0; minimumWidth = 0; layoutParams = LinearLayout.LayoutParams(110, 96)
                setTextColor(if (mute) Color.parseColor("#EF4444") else Color.WHITE); ConsoleUi.styleToggle(this, ConsoleUi.RED); setOnCheckedChangeListener { b, on -> b.setTextColor(if (on) Color.parseColor("#EF4444") else Color.WHITE); onMute(on) } })
            box.addView(r)
        }
        box.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.scene_edit_note); setTextColor(Color.parseColor("#C2C7CF")); textSize = 11f; setPadding(0, 0, 0, 8) }))
        row(TextView(this).apply { text = "MASTER"; setTextColor(Color.parseColor("#FFB590")); typeface = android.graphics.Typeface.DEFAULT_BOLD }, (scene["master"] as? Number)?.toDouble() ?: 0.0, null,
            { d -> save(OfflineScenes.setMaster(scene, d, System.currentTimeMillis())) }, null)
        for (st in (scene["strips"] as? List<Map<String, Any?>>).orEmpty().take(Prefs.stripCount(this))) { val id = (st["id"] as Number).toInt()
            val nameBox = EditText(this).apply { setText(st["name"]?.toString().orEmpty()); setSingleLine(); setTextColor(Color.WHITE); textSize = 13f
                setOnFocusChangeListener { _, f -> if (!f) save(OfflineScenes.setStrip(scene, id, System.currentTimeMillis(), name = text.toString())) } }
            row(nameBox, (st["db"] as? Number)?.toDouble() ?: 0.0, st["mute"] == true,
                { d -> save(OfflineScenes.setStrip(scene, id, System.currentTimeMillis(), db = d)) }, { m -> save(OfflineScenes.setStrip(scene, id, System.currentTimeMillis(), mute = m)) })
        }
        return box
    }
    /** after a connect: a scene chosen offline to recall now (asked first: a live console is never changed without a tap) */
    private fun offerPendingScene(c: DaemonClient) {
        val n = Prefs.remote(this).getString("recallOnConnect", "").orEmpty(); if (n.isEmpty()) return
        val o = OfflineScenes.parse(getSharedPreferences("scenes", MODE_PRIVATE).getString(n, null)) ?: run { Prefs.remote(this).edit().putString("recallOnConnect", "").apply(); return }
        AlertDialog.Builder(this).setTitle(R.string.scene_pending_title).setMessage(getString(R.string.scene_pending_q, n))
            .setPositiveButton(R.string.scene_recall) { _, _ -> Prefs.remote(this).edit().putString("recallOnConnect", "").apply(); recallScene(c, n, o) }
            .setNeutralButton(R.string.scene_pending_later, null)
            .setNegativeButton(R.string.scene_pending_off) { _, _ -> Prefs.remote(this).edit().putString("recallOnConnect", "").apply() }.show()
    }
    /** MASTER: the limiter (on / off, ceiling). */
    private var masterMeterView: TextView? = null
    private fun showMaster() {
        val c = client ?: return
        AlertDialog.Builder(this).setTitle("MASTER").setView(masterPanel(c)).setPositiveButton(android.R.string.ok, null).show()
    }
    internal fun masterPanel(c: DaemonClient): View {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 16); setBackgroundColor(Color.parseColor("#1D2024")) }
        val st = state; var on = st?.limiter ?: true; var ceil = st?.ceilingDb ?: -0.5
        // THE MASTER: fader with its dB, MUTE, DIM (-20 dB) and the master meters; a drag sends its latest value only, and the state's echo
        // of it does not move the fader back (it is ignored while the finger is down)
        val mdb = TextView(this).apply { text = getString(R.string.master_level, DaemonClient.fmtDb(st?.masterDb ?: 0.0)); setTextColor(Color.parseColor("#FFB590")); textSize = 18f; typeface = android.graphics.Typeface.MONOSPACE }
        box.addView(mdb)
        box.addView(SeekBar(this).apply { max = 1000; progress = (DaemonClient.dbToPos(st?.masterDb ?: 0.0) * 1000).toInt(); contentDescription = getString(R.string.master_fader)
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) { if (!u) return; val v = Math.round(DaemonClient.posToDb(p / 1000.0) * 10) / 10.0; mdb.text = getString(R.string.master_level, DaemonClient.fmtDb(v)); live.submit("master") { c.master(v) } }
                override fun onStartTrackingTouch(s: SeekBar) { touching++ }; override fun onStopTrackingTouch(s: SeekBar) { touching = maxOf(0, touching - 1); catchUp() } }) })
        val sw = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        sw.addView(ToggleButton(this).apply { textOn = getString(R.string.master_muted); textOff = getString(R.string.master_mute); isChecked = st?.masterMute == true; setOnCheckedChangeListener { b, v -> buzz(b); live.submit("masterMute") { c.masterMute(v) } } })
        sw.addView(ToggleButton(this).apply { textOn = getString(R.string.master_dimmed); textOff = getString(R.string.master_dim); isChecked = st?.masterDim == true; setOnCheckedChangeListener { b, v -> buzz(b); live.submit("masterDim") { c.masterDim(v) } } })
        box.addView(sw)
        masterMeterView = TextView(this).apply { setTextColor(Color.parseColor("#34D399")); typeface = android.graphics.Typeface.MONOSPACE; textSize = 12f; text = getString(R.string.master_meters, "—", "—") }
        box.addView(masterMeterView)
        val t = TextView(this).apply { text = String.format("CEILING  %.1f dBFS", ceil); setTextColor(Color.WHITE) }
        box.addView(ToggleButton(this).apply { textOn = "LIMITER ON"; textOff = "LIMITER OFF"; isChecked = on; setOnCheckedChangeListener { b, v -> buzz(b); on = v; io.execute { runCatching { c.limiter(on, ceil) } } } })
        box.addView(t)
        box.addView(SeekBar(this).apply { max = 200; progress = ((ceil + 20) * 10).toInt().coerceIn(0, 200)
            setOnSeekBarChangeListener(change { p -> ceil = p / 10.0 - 20; ui.post { t.text = String.format("CEILING  %.1f dBFS", ceil) }; c.limiter(on, ceil) }) })
        box.addView(TextView(this).apply { text = "The limiter keeps the master under the ceiling (no clipping on the output)."; setTextColor(Color.parseColor("#C2C7CF")) })
        // APPS: each program's volume and mute, as the Windows volume mixer of the console's computer has them
        if (st?.appsAvailable == true) {
            box.addView(TextView(this).apply { text = getString(R.string.apps_title); setTextColor(Color.parseColor("#22D3EE")); typeface = android.graphics.Typeface.DEFAULT_BOLD; setPadding(0, 28, 0, 6) })
            if (st.apps.isEmpty()) box.addView(TextView(this).apply { text = getString(R.string.apps_none); setTextColor(Color.parseColor("#C2C7CF")); textSize = 12f })
            for (a in st.apps) {
                val lab = TextView(this).apply { text = com.amfb.amixos.remote.AppVolumes.label(a); setTextColor(Color.WHITE); textSize = 13f }
                box.addView(lab)
                val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                var cur = a
                row.addView(SeekBar(this).apply { max = 100; progress = Math.round(a.volume).toInt(); contentDescription = getString(R.string.apps_volume, a.name); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                    setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                        override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) { if (!u) return; cur = cur.copy(volume = p.toDouble()); lab.text = com.amfb.amixos.remote.AppVolumes.label(cur); live.submit("app${a.id}") { c.appVolume(a.id, p.toDouble()) } }
                        override fun onStartTrackingTouch(s: SeekBar) { touching++ }; override fun onStopTrackingTouch(s: SeekBar) { touching = maxOf(0, touching - 1) } }) })
                row.addView(ToggleButton(this).apply { textOn = getString(R.string.master_muted); textOff = getString(R.string.master_mute); isChecked = a.mute; contentDescription = getString(R.string.apps_mute, a.name)
                    setOnCheckedChangeListener { b, v -> buzz(b); cur = cur.copy(mute = v); lab.text = com.amfb.amixos.remote.AppVolumes.label(cur); live.submit("appMute${a.id}") { c.appMute(a.id, v) } } })
                box.addView(row)
            }
        }
        // WINDOWS: the computer's own output slider, and GAIN MODE (that slider as a trim after the master)
        val w = st?.windows ?: com.amfb.amixos.remote.WindowsSound()
        box.addView(TextView(this).apply { text = getString(R.string.win_title); setTextColor(Color.parseColor("#22D3EE")); typeface = android.graphics.Typeface.DEFAULT_BOLD; setPadding(0, 28, 0, 6) })
        val trim = TextView(this).apply { setTextColor(Color.parseColor("#FBBF24")); typeface = android.graphics.Typeface.MONOSPACE; textSize = 12f }
        fun showTrim(on: Boolean, vol: Int) { trim.text = if (!on) getString(R.string.win_gain_off) else if (w.windowsSetsLevel) getString(R.string.win_sets_level) else { val t = if (vol <= 0) -120.0 else 30 * Math.log10(vol / 100.0); if (t <= -119) getString(R.string.win_trim_off) else getString(R.string.win_trim, String.format("%.1f", t)) } }
        var vol = w.volume; var gain = w.gainMode
        if (w.volume >= 0) {
            val vt = TextView(this).apply { text = getString(R.string.win_volume, vol); setTextColor(Color.WHITE) }
            box.addView(vt)
            box.addView(SeekBar(this).apply { max = 100; progress = vol; setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, p: Int, u: Boolean) { if (!u) return; vol = p; vt.text = getString(R.string.win_volume, p); showTrim(gain, p); live.submit("winVolume") { c.setting("winVolume", p) } }
                override fun onStartTrackingTouch(s: SeekBar) {}; override fun onStopTrackingTouch(s: SeekBar) {} }) })
        } else box.addView(TextView(this).apply { text = getString(R.string.win_none); setTextColor(Color.parseColor("#C2C7CF")); textSize = 12f })
        box.addView(ToggleButton(this).apply { textOn = getString(R.string.win_gain_on_btn); textOff = getString(R.string.win_gain_off_btn); isChecked = gain
            setOnCheckedChangeListener { b, v -> buzz(b); gain = v; showTrim(v, vol); io.execute { runCatching { c.setting("winGain", v) } } } })
        box.addView(trim); showTrim(gain, vol)
        box.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.win_gain_note); setTextColor(Color.parseColor("#C2C7CF")); textSize = 11f }))
        return box
    }

    /** FOLLOW THE CONSOLE (LIVE SYNC): the console pushes its state over the meter stream the moment anything changes, on the computer, on
     *  another phone or in a browser, and it lands here at once. This tick is the safety net: while the stream is up it only asks "still at
     *  change v?" (a few bytes, which also measures the ping); without the stream it reads the whole state. A fader under a finger is left
     *  alone and catches up when the finger lifts. Paused while the app is in the background. */
    private var syncing = false; @Volatile private var lastPush = 0L; private var pendingState: ConsoleState? = null
    private fun startSync(c: DaemonClient) { if (syncing) return; syncing = true
        val tick = object : Runnable { override fun run() { if (!syncing) return
            val secs = Prefs.syncSeconds(this@RemoteActivity)
            if (secs > 0 && !isFinishing && hasWindowFocus()) { val isLive = live(); val v = state?.v ?: 0L
                io.execute { val t0 = System.currentTimeMillis(); val r = runCatching { if (isLive) c.stateSince(v) else c.state() }
                    if (r.isSuccess) { pingMs = System.currentTimeMillis() - t0; val s = r.getOrNull(); ui.post { if (s != null) applyState(s); if (retries == 0) showLink() } } } }
            ui.postDelayed(this, 1000L * maxOf(1, secs)) } }
        ui.postDelayed(tick, 1000L * maxOf(1, Prefs.syncSeconds(this))) }
    private fun applyState(s: ConsoleState) {
        if (touching > 0) { pendingState = s; return }
        pendingState = null
        for (st in s.strips) { val i = st.id; if (i >= faders.size || i >= mutes.size) continue
            val pos = (DaemonClient.dbToPos(st.db) * 1000).toInt(); if (Math.abs(faders[i].progress - pos) > 2) { faders[i].progress = pos; dbLabels[i].text = DaemonClient.fmtDb(st.db) }
            if (mutes[i].isChecked != st.mute) { mutes[i].tag = "sync"; mutes[i].isChecked = st.mute; mutes[i].tag = null }
            if (solos[i].isChecked != st.solo) { solos[i].tag = "sync"; solos[i].isChecked = st.solo; solos[i].tag = null }
            val label = ConsoleUi.title(i, st.name); if (nameLabels[i].text.toString() != label) nameLabels[i].text = label
            panKnobs[i]?.let { k -> if (Math.abs(k.value - st.pan.toFloat()) > 0.005f) k.value = st.pan.toFloat() }
            toneUi[i]?.show(st) }
        run { val p = (DaemonClient.dbToPos(s.masterDb) * 1000).toInt(); if (Math.abs(masterFader.progress - p) > 2) { masterFader.progress = p; masterDb.text = DaemonClient.fmtDb(s.masterDb) } }
        state = s
    }
    // ---- GAIN and the analog 3-band channel EQ on each strip (the console's own: HI 10 kHz shelf, MID bell + FREQ, LO 100 Hz shelf)
    internal inner class ToneUi(val gain: KnobView, val eq: ToggleButton, val hi: KnobView, val mid: KnobView, val freq: KnobView, val lo: KnobView) {
        var tone = com.amfb.amixos.remote.Tone()
        fun show(st: com.amfb.amixos.remote.StripState) { gain.value = (st.trim / 24).toFloat(); hi.value = (st.tone.high / 15).toFloat(); mid.value = (st.tone.mid / 15).toFloat()
            freq.value = hzToKnob(st.tone.midHz); lo.value = (st.tone.low / 15).toFloat(); tone = st.tone
            if (eq.isChecked != st.tone.on) { eq.tag = "sync"; eq.isChecked = st.tone.on; eq.tag = null }; listOf(hi, mid, freq, lo).forEach { it.dim = !st.tone.on } }
    }
    internal val toneUi = HashMap<Int, ToneUi>()
    /** each strip's PAN knob, so a pan moved on the console (or mirrored by a linked stereo pair there) shows here */
    internal val panKnobs = HashMap<Int, KnobView>()
    private fun hzToKnob(hz: Double) = (2 * Math.log(hz / 100) / Math.log(80.0) - 1).toFloat()
    private fun knobToHz(v: Float) = Math.round(100 * Math.pow(80.0, (v + 1) / 2.0)).toDouble()
    private fun toneBlock(st: com.amfb.amixos.remote.StripState, tint: Int): View {
        // a little room above GAIN: the pan knob's value ("C", "L40") sits right under it
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL; setPadding(0, 14, 0, 0) }
        val dbF: (Float, Double) -> String = { v, r -> val d = Math.round(v * r * 2) / 2.0; (if (d > 0) "+" else "") + String.format("%.1f", d) }
        fun k(title: String, color: Int, f: (Float) -> String) = KnobView(this, color).apply { this.title = title; fmt = f; layoutParams = LinearLayout.LayoutParams(ConsoleUi.dp(this@RemoteActivity, KNOB_DP), LinearLayout.LayoutParams.WRAP_CONTENT)
            onTouchState = { t -> touching = maxOf(0, touching + if (t) 1 else -1); if (touching == 0) catchUp() } }
        val gain = k("GAIN", Color.parseColor("#FBBF24")) { dbF(it, 24.0) }
        val hi = k("HI", tint) { dbF(it, 15.0) }; val mid = k("MID", tint) { dbF(it, 15.0) }; val lo = k("LO", tint) { dbF(it, 15.0) }
        val freq = k("FREQ", Color.parseColor("#22D3EE")) { val hz = knobToHz(it); if (hz >= 1000) String.format("%.1fk", hz / 1000) else "${hz.toInt()}" }.apply { home = hzToKnob(1000.0) }
        val eq = ConsoleUi.compact(ToggleButton(this).apply { textOn = "EQ"; textOff = "EQ" }, ConsoleUi.KEY_DP, 11).apply { setPadding(0, 0, 0, 0); contentDescription = "eqKey"; layoutParams = LinearLayout.LayoutParams(ConsoleUi.dp(this@RemoteActivity, KNOB_DP + 8), ConsoleUi.dp(this@RemoteActivity, ConsoleUi.KEY_DP)).apply { setMargins(0, 2, 0, 2) } }
        ConsoleUi.styleToggle(eq, ConsoleUi.AMBER)
        val ui = ToneUi(gain, eq, hi, mid, freq, lo); toneUi[st.id] = ui; ui.show(st)
        val id = st.id; fun sendTone() { val t = ui.tone; live.submit("tone$id") { client?.tone(id, t) } }
        gain.onChange = { v -> live.submit("trim$id") { client?.trim(id, Math.round(v * 48) / 2.0) } }
        hi.onChange = { v -> ui.tone = ui.tone.copy(high = Math.round(v * 30) / 2.0, on = true); sendTone() }
        mid.onChange = { v -> ui.tone = ui.tone.copy(mid = Math.round(v * 30) / 2.0, on = true); sendTone() }
        lo.onChange = { v -> ui.tone = ui.tone.copy(low = Math.round(v * 30) / 2.0, on = true); sendTone() }
        freq.onChange = { v -> ui.tone = ui.tone.copy(midHz = knobToHz(v)); sendTone() }
        eq.setOnCheckedChangeListener { b, on -> if (b.tag == null) { buzz(b); ui.tone = ui.tone.copy(on = on); listOf(hi, mid, freq, lo).forEach { it.dim = !on }; sendTone() } }
        listOf<View>(gain, eq, hi, mid, freq, lo).forEach { box.addView(it) }
        return box
    }
    private fun catchUp() { val p = pendingState ?: return; ui.postDelayed({ if (touching == 0 && pendingState === p) applyState(p) }, 400) }   // the finger lifted: what changed elsewhere meanwhile
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
                        setOnSeekBarChangeListener(change { p -> val v = DaemonClient.posToDb(p / 1000.0); c.busLevel(b.id, v); ui.post { label.text = busName(b.id) + "   " + DaemonClient.fmtDb(v) } }) })
                    // RETURN: how much of the bus comes back into the master (an FX return, or a monitor mix heard in the room)
                    val ret = TextView(this).apply { text = getString(R.string.bus_return, DaemonClient.fmtDb(b.returnDb)); setTextColor(Color.parseColor("#C2C7CF")); textSize = 12f }
                    box.addView(ret)
                    box.addView(SeekBar(this).apply { max = 1000; progress = (DaemonClient.dbToPos(b.returnDb) * 1000).toInt()
                        setOnSeekBarChangeListener(change { p -> val v = DaemonClient.posToDb(p / 1000.0); c.busReturn(b.id, v); ui.post { ret.text = getString(R.string.bus_return, DaemonClient.fmtDb(v)) } }) }) }
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
            .filterIsInstance<java.net.Inet4Address>().map { it.hostAddress }.filterNotNull().firstOrNull { it.startsWith("192.168.") || it.startsWith("10.") || Regex("^172\\.(1[6-9]|2\\d|3[01])\\.").containsMatchIn(it) }
    }.getOrNull()

    override fun onDestroy() { closing = true; syncing = false; ui.removeCallbacks(presence); ui.removeCallbacks(noticePoll); stream?.stop(); io.shutdownNow(); scanIo.shutdownNow(); super.onDestroy() }
}
