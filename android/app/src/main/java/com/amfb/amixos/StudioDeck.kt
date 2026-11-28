package com.amfb.amixos

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.view.inputmethod.EditorInfo
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView

/** 1.27 STUDIO: a section of the phone's own mixer. The top half is a WEB panel (address / search bar and quick tiles); a drag handle
 *  folds it away. The bottom half is five strips: MIC 1 (In 1), MIC 2 (In 2), BROWSER, PLAYER and MASTER (Out 1/2).
 *  - An input strip has four SENDS (ECHO, REVERB, HARMONIZER, CHORUS), a fader with a dB readout, and three INSERT slots.
 *  - The MASTER has the four RETURNS, its fader and three inserts (EQ, MULTIBAND, BASS).
 *  The sends and inserts are the engine's own rack effects. Inserts take slots 0-2 of the strip. A send is its effect in slots 5-8,
 *  wet by (send x return).
 *  The BROWSER strip is a link, not an audio path: its fader sets the page's own volume and -inf mutes it. Nothing the page plays is
 *  recorded or saved.
 *  The mapping is pure (StudioMap) so the unit tests check it. */
object StudioMap {
    data class Strip(val id: String, val label: String, val sub: String, val channel: Int, val inserts: List<String>, val sends: Boolean)
    val SENDS = listOf("delay" to "ECHO", "reverb" to "REVERB", "pitch" to "HARM", "chorus" to "CHORUS")
    const val SEND_SLOT0 = 5
    const val BROWSER = -2   // the BROWSER strip has no engine channel
    val strips = listOf(
        Strip("mic1", "MIC 1", "IN 1", 0, listOf("clean", "drive", "antifeedback"), true),
        Strip("mic2", "MIC 2", "IN 2", 1, listOf("clean", "drive", "antifeedback"), true),
        Strip("browser", "BROWSER", "WEB", BROWSER, listOf("isoeq", "imager", "duck"), false),
        Strip("player", "PLAYER", "PLAYER A", 100, listOf("isoeq", "imager", "duck"), true),
        Strip("master", "MASTER", "OUT 1/2", -1, listOf("isoeq", "multiband", "subsynth"), false))
    val INSERT_NAMES = mapOf("clean" to "VOCAL", "drive" to "MEGA", "antifeedback" to "FB SUPPRESS", "isoeq" to "EQ", "imager" to "WIDE", "duck" to "DUCK", "multiband" to "MULTIBAND", "subsynth" to "BASS")
    /** the engine slot of send [k] (0..3) */
    fun sendSlot(k: Int) = SEND_SLOT0 + k.coerceIn(0, 3)
    /** the wet amount of a send: the strip's send times the master's return, both 0..1 */
    fun wet(send: Double, ret: Double): Double = (send.coerceIn(0.0, 1.0) * ret.coerceIn(0.0, 1.0))
    /** "duck" is not a rack effect: it is the engine's ducker role (the strip is ducked under the mics) */
    fun isDuck(kind: String) = kind == "duck"
    /** the knob (-1..1 as KnobView keeps it) as a 0..1 amount, and back */
    fun amount(knob: Float) = ((knob + 1f) / 2f).toDouble().coerceIn(0.0, 1.0)
    fun knob(amount: Double) = (amount.coerceIn(0.0, 1.0) * 2 - 1).toFloat()
    /** the BROWSER fader as the page's volume 0..1 (-inf = 0) */
    fun pageVolume(db: Double) = if (db <= FaderSources.FLOOR_DB) 0.0 else Math.pow(10.0, minOf(0.0, db) / 20.0)
    fun pageScript(v: Double) = "(function(){var v=" + String.format(java.util.Locale.ROOT, "%.3f", v.coerceIn(0.0, 1.0)) + ";document.querySelectorAll('audio,video').forEach(function(m){m.volume=v;m.muted=v<=0})})()"
    /** what the address bar takes: an address as it is (https:// added), anything else as a search on the search page the user set */
    fun target(text: String, searchTemplate: String): String? {
        val t = text.trim(); if (t.isEmpty()) return null
        if (Regex("^https?://", RegexOption.IGNORE_CASE).containsMatchIn(t)) return t
        if (!t.contains(' ') && Regex("^[\\w-]+(\\.[\\w-]+)+(/.*)?$").matches(t)) return "https://$t"
        return if (searchTemplate.isBlank()) null else searchTemplate + java.net.URLEncoder.encode(t, "UTF-8")
    }
    /** what the web panel may open: https (and plain http) pages only */
    fun allowed(url: String) = Regex("^https?://", RegexOption.IGNORE_CASE).containsMatchIn(url)
    val TILES = listOf("VIDEO" to "video ", "SEARCH" to "", "ENCYCLOPEDIA" to "encyclopedia ", "PRIVACY SEARCH" to "")
    /** the STATUS line: rate · inputs · route · latency (the buffers the stream really has) · underruns · CPU */
    fun status(rate: Int, inputs: Int, usb: Boolean, bufferFrames: Int, burstFrames: Int, xruns: Int, cpuPct: Double): String {
        val ms = if (rate > 0) (bufferFrames + burstFrames) * 1000.0 / rate else 0.0
        return String.format(java.util.Locale.ROOT, "%.1f kHz · %d IN · %s · ≈%.1f ms · %d XRUN · CPU %.0f%%", rate / 1000.0, inputs, if (usb) "USB" else "INTERNAL", ms, xruns, cpuPct)
    }
    /** the drag handle: the web panel's share of the height after a drag, held between folded (0) and 60 % */
    fun panelShare(start: Double, dyPx: Float, heightPx: Int): Double = if (heightPx <= 0) start else (start + dyPx / heightPx).coerceIn(0.0, 0.6).let { if (it < 0.08) 0.0 else it }
}

/** builds the STUDIO section; [host] is the phone mixer that owns the engine */
class StudioView(private val ctx: Context, private val host: Host) {
    interface Host {
        val engine: Engine?
        fun prefs(): android.content.SharedPreferences
        fun faderStart(ch: Int, a: FaderSources.Action)
        fun applyLevel(ch: Int)
        fun statusLine(): String
    }
    private val dp = ctx.resources.displayMetrics.density
    private fun px(v: Float) = (v * dp).toInt()
    var web: WebView? = null; private set
    private val meters = HashMap<Int, FaderView>()
    var statusView: TextView? = null; private set

    @SuppressLint("SetJavaScriptEnabled", "ClickableViewAccessibility")
    fun build(land: Boolean): View {
        val p = host.prefs()
        val root = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL; tag = "secStudio" }
        // WEB PANEL
        val panel = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL; tag = "studioWeb" }
        val bar = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        val addr = EditText(ctx).apply { hint = ctx.getString(R.string.st_address); setSingleLine(); imeOptions = EditorInfo.IME_ACTION_GO; setTextColor(Color.WHITE); setHintTextColor(Color.GRAY); contentDescription = "studioAddress"
            minHeight = px(44f); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
        val wv = WebView(ctx).apply { tag = "studioWebView"; settings.javaScriptEnabled = true; settings.mediaPlaybackRequiresUserGesture = true; settings.domStorageEnabled = true; settings.allowFileAccess = false; settings.allowContentAccess = false
            webViewClient = object : WebViewClient() { override fun shouldOverrideUrlLoading(view: WebView, req: android.webkit.WebResourceRequest): Boolean = !StudioMap.allowed(req.url.toString()) }   // web pages only: no file:, intent: or app links
            setBackgroundColor(Color.parseColor("#0E1216")); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f) }
        web = wv
        fun go(text: String) {
            val url = StudioMap.target(text, p.getString("studioSearch", "").orEmpty())
            if (url == null) askSearch { go(text) } else wv.loadUrl(url)
        }
        addr.setOnEditorActionListener { v, _, _ -> go(v.text.toString()); true }
        bar.addView(addr); bar.addView(PadButton(ctx, ctx.getString(R.string.st_go), ConsoleUi.GREEN, 12f).apply { layoutParams = LinearLayout.LayoutParams(px(64f), px(44f)); setOnClickListener { go(addr.text.toString()) } })
        panel.addView(bar)
        val tiles = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL }
        for ((label, prefix) in StudioMap.TILES) tiles.addView(PadButton(ctx, label, ConsoleUi.VIOLET, 11f).apply { contentDescription = "tile_$label"; layoutParams = LinearLayout.LayoutParams(0, px(44f), 1f).apply { setMargins(px(2f), px(4f), px(2f), px(4f)) }
            setOnClickListener { addr.setText(prefix); addr.setSelection(addr.text.length); addr.requestFocus() } })
        panel.addView(tiles); panel.addView(wv)
        val share = p.getFloat("studioShare", if (land) 0.42f else 0f).toDouble()   // upright the web panel starts folded: drag the handle to open it
        panel.layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, share.toFloat().coerceAtLeast(0.0001f)); if (share <= 0.0) panel.visibility = View.GONE
        root.addView(panel)
        // the DRAG HANDLE: drag to size the web panel, tap to fold / unfold it
        val handle = TextView(ctx).apply { text = "═══"; gravity = Gravity.CENTER; setTextColor(Color.parseColor("#9EAAB6")); contentDescription = "studioHandle"; minHeight = px(28f) }
        var startShare = share; var downY = 0f; var moved = false
        handle.setOnTouchListener { v, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> { downY = e.rawY; moved = false; startShare = p.getFloat("studioShare", if (land) 0.42f else 0f).toDouble() }
                MotionEvent.ACTION_MOVE -> { if (Math.abs(e.rawY - downY) > px(6f)) moved = true; if (moved) setShare(panel, StudioMap.panelShare(startShare, e.rawY - downY, root.height)) }
                MotionEvent.ACTION_UP -> { if (!moved) setShare(panel, if (panel.visibility == View.VISIBLE) 0.0 else 0.42); p.edit().putFloat("studioShare", (panel.layoutParams as LinearLayout.LayoutParams).weight.let { if (panel.visibility == View.GONE) 0f else it }).apply(); v.performClick() }
            }; true }
        root.addView(handle)
        // THE STRIPS
        val row = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL; tag = "studioStrips"; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, (1 - share).toFloat().coerceAtLeast(0.4f)) }
        for (s in StudioMap.strips) row.addView(strip(s).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, if (s.id == "master") 1.15f else 1f).apply { setMargins(px(2f), 0, px(2f), 0) } })
        root.addView(row)
        statusView = TextView(ctx).apply { tag = "studioStatus"; setTextColor(Color.parseColor("#C3CCD5")); typeface = Typeface.MONOSPACE; textSize = 11f; gravity = Gravity.CENTER; minHeight = px(28f); text = host.statusLine() }
        root.addView(statusView)
        return root
    }
    private fun setShare(panel: View, s: Double) {
        if (s <= 0.0) panel.visibility = View.GONE else { panel.visibility = View.VISIBLE; (panel.layoutParams as LinearLayout.LayoutParams).weight = s.toFloat(); panel.requestLayout() }
        val strips = (panel.parent as? LinearLayout)?.findViewWithTag<View>("studioStrips"); (strips?.layoutParams as? LinearLayout.LayoutParams)?.weight = (1 - s).toFloat().coerceAtLeast(0.4f); strips?.requestLayout()
    }
    private fun askSearch(then: () -> Unit) {
        val e = EditText(ctx).apply { hint = "https://…?q="; setSingleLine() }
        androidx.appcompat.app.AlertDialog.Builder(ctx).setTitle(R.string.st_search_page).setMessage(R.string.st_search_page_note).setView(e)
            .setPositiveButton(android.R.string.ok) { _, _ -> val t = e.text.toString().trim(); if (t.startsWith("https://")) { host.prefs().edit().putString("studioSearch", t).apply(); then() } }.setNegativeButton(android.R.string.cancel, null).show()
    }
    private fun key(ch: Int) = when (ch) { -1 -> "masterDb"; StudioMap.BROWSER -> "browserDb"; else -> if (ch >= 100) "deckDb${ch - 100}" else "db$ch" }
    private fun strip(s: StudioMap.Strip): View {
        val p = host.prefs(); val master = s.channel == -1
        val color = when (s.id) { "mic1" -> ConsoleUi.palette[1]; "mic2" -> ConsoleUi.palette[2]; "browser" -> ConsoleUi.palette[4 % ConsoleUi.palette.size]; "player" -> ConsoleUi.palette[3]; else -> ConsoleUi.AMBER }
        val col = ConsoleUi.strip(ctx, color, 0, master).apply { contentDescription = "studio_${s.id}" }
        col.addView(TextView(ctx).apply { text = s.label; setTextColor(Color.WHITE); textSize = 12f; setTypeface(null, Typeface.BOLD); gravity = Gravity.CENTER })
        col.addView(TextView(ctx).apply { text = s.sub; setTextColor(Color.parseColor("#9EAAB6")); textSize = 9f; gravity = Gravity.CENTER })
        // SENDS (or the master's RETURNS): four small knobs in a 2 x 2 grid
        if (s.sends || master) {
            val grid = LinearLayout(ctx).apply { orientation = LinearLayout.VERTICAL }
            for (r in 0..1) { val line = LinearLayout(ctx).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER }
                for (k in 2 * r..2 * r + 1) { val (kind, name) = StudioMap.SENDS[k]; val pk = if (master) "ret_$kind" else "send_${s.channel}_$kind"
                    line.addView(KnobView(ctx, color).apply { title = if (master) "$name RET" else name; value = StudioMap.knob(p.getFloat(pk, if (master) 1f else 0f).toDouble()); contentDescription = "knob_${s.id}_$kind"
                        fmt = { v -> String.format(java.util.Locale.ROOT, "%.0f", StudioMap.amount(v) * 100) }
                        layoutParams = LinearLayout.LayoutParams(0, px(56f), 1f)
                        onChange = { v -> p.edit().putFloat(pk, StudioMap.amount(v).toFloat()).apply(); if (master) applyReturns() else applySend(s.channel, k) } }) }
                grid.addView(line) }
            col.addView(grid)
        }
        // the FADER with its readout
        val readout = ConsoleUi.dbReadout(ctx)
        val db0 = p.getFloat(key(s.channel), 0f).toDouble(); readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(if (db0 <= -60) -120.0 else db0)
        col.addView(FaderView(ctx, color).apply { contentDescription = "fader_${s.id}"; progress = (com.amfb.amixos.remote.DaemonClient.dbToPos(if (db0 <= -60) -120.0 else db0) * 1000).toInt()
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
            onMove = { pos -> val db = com.amfb.amixos.remote.DaemonClient.posToDb(pos / 1000.0); val set = if (db <= -60) -60.0 else db; val was = p.getFloat(key(s.channel), 0f).toDouble()
                p.edit().putFloat(key(s.channel), set.toFloat()).apply(); readout.text = com.amfb.amixos.remote.DaemonClient.fmtDb(db)
                when (s.channel) { StudioMap.BROWSER -> web?.evaluateJavascript(StudioMap.pageScript(StudioMap.pageVolume(set)), null); -1 -> host.applyLevel(-1); else -> { host.faderStart(s.channel, FaderSources.action(was, set)); host.applyLevel(s.channel) } } } }.also { meters[s.channel] = it })
        col.addView(readout)
        // INSERTS: three slots
        for ((i, kind) in s.inserts.withIndex()) { val ik = "ins_${s.id}_$i"
            col.addView(PadButton(ctx, StudioMap.INSERT_NAMES[kind] ?: kind.uppercase(), ConsoleUi.VIOLET, 10f).apply { on = p.getBoolean(ik, false); contentDescription = "insert_${s.id}_$i"; isEnabled = s.channel != StudioMap.BROWSER || StudioMap.isDuck(kind)
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, px(44f)).apply { setMargins(px(2f), px(2f), px(2f), px(2f)) }
                setOnClickListener { val v = !p.getBoolean(ik, false); p.edit().putBoolean(ik, v).apply(); on = v; applyInsert(s, i, kind, v) } }) }
        return col
    }
    private fun strips(ch: Int): List<Int> = when { ch == -1 -> listOf(-1); ch >= 100 -> LocalMixerActivity.chStrips(ch); else -> listOf(ch) }
    internal fun applyInsert(s: StudioMap.Strip, slot: Int, kind: String, on: Boolean) {
        val e = host.engine ?: return
        if (StudioMap.isDuck(kind)) { for (t in strips(s.channel)) if (t >= 0) e.setDuckRole(t, if (on) 2 else 0); return }
        for (t in strips(s.channel)) e.insert(t, slot, if (on) kind else "")
    }
    internal fun applySend(ch: Int, k: Int) {
        val e = host.engine ?: return; val p = host.prefs(); val (kind, _) = StudioMap.SENDS[k]
        val w = StudioMap.wet(p.getFloat("send_${ch}_$kind", 0f).toDouble(), p.getFloat("ret_$kind", 1f).toDouble())
        for (t in strips(ch)) { if (w <= 0.001) e.insert(t, StudioMap.sendSlot(k), "") else { e.insert(t, StudioMap.sendSlot(k), kind); e.insertSet(t, StudioMap.sendSlot(k), "mix", w) } }
    }
    private fun applyReturns() { for (s in StudioMap.strips) if (s.sends) for (k in 0..3) applySend(s.channel, k) }
    fun tick(levels: FloatArray?) {
        statusView?.text = host.statusLine(); val l = levels ?: return
        fun lvl(v: Float) = (((if (v > 1e-6f) 20 * Math.log10(v.toDouble()) else -120.0) + 60) / 60 * 1000).toInt().coerceIn(0, 1000)
        for ((ch, f) in meters) { val st = strips(ch).map { if (it < 0) LocalMixerActivity.STRIPS else it }; val m = st.maxOf { maxOf(l.getOrElse(it) { 0f }, if (ch == -1) l.getOrElse(it + 1) { 0f } else 0f) }; if (ch != StudioMap.BROWSER) f.level = lvl(m) }
    }
}
