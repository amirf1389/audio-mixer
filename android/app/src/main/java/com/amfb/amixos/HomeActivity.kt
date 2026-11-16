package com.amfb.amixos

import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.GridLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/** HOME: one place for the whole app. The console card (which computer, which plan, paired or not), then a tile for every part:
 *  the console mixer, this phone's own mixer, EQ, scenes, tools, calculator, plug-ins and settings. */
class HomeActivity : AppCompatActivity() {
    private lateinit var card: LinearLayout; private lateinit var quick: LinearLayout; private lateinit var news: LinearLayout; private lateinit var recentRow: LinearLayout; private lateinit var grid: GridLayout
    private val io = java.util.concurrent.Executors.newSingleThreadExecutor(); private val ui = android.os.Handler(android.os.Looper.getMainLooper())
    private var live: com.amfb.amixos.remote.DaemonClient? = null; private var online = false; private var liveState: String = ""; private var meterL = 0.0; private var meterR = 0.0; private var lastOk = 0L
    private var meterView: MeterBar? = null; private var liveText: TextView? = null; private var dot: TextView? = null
    /** one tile of HOME: its id (for RECENT and pins), look and action */
    internal data class Tile(val id: String, val glyph: String, val title: Int, val sub: Int, val color: Int, val go: () -> Unit)
    internal fun tiles(): List<Tile> {
        val o = getColor(R.color.accent); val c = getColor(R.color.cyan); val v = Color.parseColor("#A78BFA"); val g = Color.parseColor("#34D399"); val y = Color.parseColor("#FBBF24")
        return listOf(
            Tile("RemoteActivity", "☰", R.string.home_mixer, R.string.home_mixer_sub, o) { Nav.go(this, RemoteActivity::class.java) },
            Tile("LocalMixerActivity", "🎙", R.string.home_phone, R.string.home_phone_sub, c) { Nav.go(this, LocalMixerActivity::class.java) },
            Tile("EqActivity", "∿", R.string.home_eq, R.string.home_eq_sub, v) { pickEqChannel() },
            Tile("RtaEqActivity", "▥", R.string.home_rta, R.string.home_rta_sub, c) { Nav.go(this, RtaEqActivity::class.java) },
            Tile("Scenes", "▤", R.string.home_scenes, R.string.home_scenes_sub, g) { Nav.go(this, RemoteActivity::class.java) { it.putExtra("open", "scenes") } },
            Tile("ToolsActivity", "♪", R.string.home_tools, R.string.home_tools_sub, y) { Nav.go(this, ToolsActivity::class.java) },
            Tile("RecorderActivity", "●", R.string.home_recorder, R.string.home_recorder_sub, Color.parseColor("#EF4444")) { Nav.go(this, RecorderActivity::class.java) },
            Tile("CalculatorActivity", "÷", R.string.home_calc, R.string.home_calc_sub, c) { Nav.go(this, CalculatorActivity::class.java) },
            Tile("PluginsActivity", "⧉", R.string.home_plugins, R.string.home_plugins_sub, v) { Nav.go(this, PluginsActivity::class.java) },
            Tile("MixdownActivity", "⊕", R.string.home_mixdown, R.string.home_mixdown_sub, o) { Nav.go(this, MixdownActivity::class.java) },
            Tile("DevicesActivity", "⇄", R.string.home_devices, R.string.home_devices_sub, c) { Nav.go(this, DevicesActivity::class.java) },
            Tile("SettingsActivity", "⚙", R.string.home_settings, R.string.home_settings_sub, Color.parseColor("#94A3B8")) { Nav.go(this, SettingsActivity::class.java) })
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 40, 32, 32) }
        box.addView(TextView(this).apply { text = "AMF X1"; setTextColor(getColor(R.color.accent)); textSize = 28f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.18f })
        box.addView(TextView(this).apply { text = getString(R.string.home_by) + "   ·   " + (runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull() ?: ""); setTextColor(getColor(R.color.dim)); textSize = 12f; letterSpacing = 0.2f; setPadding(0, 0, 0, 24) })
        card = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(32, 28, 32, 28) }
        box.addView(card)
        // QUICK ACTIONS: the things a show needs in one tap
        quick = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 18, 0, 0) }
        box.addView(android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(quick) })
        // WHAT'S NEW in this version, and RECENT pages
        news = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; box.addView(news)
        recentRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 14, 0, 0) }
        box.addView(android.widget.HorizontalScrollView(this).apply { isHorizontalScrollBarEnabled = false; addView(recentRow) })
        grid = GridLayout(this).apply { columnCount = 2; setPadding(0, 16, 0, 0) }
        box.addView(grid)
        setContentView(Nav.wrap(this, ScrollView(this).apply { addView(box) }, Nav.Tab.HOME))
        // THE BOOT ANIMATION: once per app start (not when coming back to HOME), unless switched off in SETTINGS
        if (savedInstanceState == null && !bootShown && Prefs.bootAnim(this)) { bootShown = true; showBoot() }
    }
    internal fun showBoot(durationMs: Long = 1500): BootView {
        val root = window.decorView as android.view.ViewGroup
        lateinit var v: BootView
        v = BootView(this) { v.animate().alpha(0f).setDuration(180).withEndAction { root.removeView(v) }.start() }
        root.addView(v, android.view.ViewGroup.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT))
        if (durationMs > 0) v.start(durationMs); return v
    }
    companion object { @JvmStatic internal var bootShown = false }
    override fun onResume() { super.onResume(); showCard(); drawQuick(); drawNews(); drawRecent(); drawTiles(); startLive() }
    override fun onPause() { ui.removeCallbacks(poll); super.onPause() }
    override fun onDestroy() { ui.removeCallbacks(poll); io.shutdownNow(); super.onDestroy() }
    // ---- THE TILES: pinned first (long press pins / unpins, ★ on the tile)
    internal fun drawTiles() {
        grid.removeAllViews(); val pins = Home.pinned(this)
        for (t in Home.pinnedFirst(tiles(), { it.id }, pins)) grid.addView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.tile_bg); setPadding(28, 26, 28, 26); isClickable = true; setOnClickListener { t.go() }
            setOnLongClickListener { val on = Home.togglePin(this@HomeActivity, t.id); android.widget.Toast.makeText(this@HomeActivity, getString(if (on) R.string.home_pinned else R.string.home_unpinned, getString(t.title)), android.widget.Toast.LENGTH_SHORT).show(); drawTiles(); true }
            layoutParams = GridLayout.LayoutParams(GridLayout.spec(GridLayout.UNDEFINED, 1f), GridLayout.spec(GridLayout.UNDEFINED, 1f)).apply { width = 0; setMargins(8, 8, 8, 8) }
            addView(LinearLayout(this@HomeActivity).apply { orientation = LinearLayout.HORIZONTAL
                addView(TextView(this@HomeActivity).apply { text = t.glyph; textSize = 26f; setTextColor(t.color); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
                if (t.id in pins) addView(TextView(this@HomeActivity).apply { text = "★"; textSize = 14f; setTextColor(Color.parseColor("#FBBF24")) }) })
            addView(TextView(this@HomeActivity).apply { text = getString(t.title); textSize = 15f; setTextColor(Color.WHITE); typeface = Typeface.DEFAULT_BOLD; setPadding(0, 10, 0, 2) })
            addView(TextView(this@HomeActivity).apply { text = getString(t.sub); textSize = 11f; setTextColor(getColor(R.color.dim)); maxLines = 2 }) })
    }
    private fun chip(parent: LinearLayout, text: String, color: Int, f: () -> Unit) = parent.addView(TextView(this).apply {
        this.text = text; setTextColor(color); textSize = 12f; typeface = Typeface.DEFAULT_BOLD; setPadding(26, 16, 26, 16); isClickable = true; setOnClickListener { f() }
        background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = 40f; setColor(Color.parseColor("#1A2230")); setStroke(2, Color.argb(120, Color.red(color), Color.green(color), Color.blue(color))) }
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 0, 14, 0) } })
    // ---- QUICK ACTIONS
    internal fun drawQuick() {
        quick.removeAllViews(); val paired = !Prefs.remote(this).getString("token", "").isNullOrEmpty()
        if (paired) chip(quick, "🔇 " + getString(R.string.home_q_mute), Color.parseColor("#EF4444")) { muteAll() }
        val sc = Home.lastScene(this); if (paired && sc.isNotEmpty()) chip(quick, "▤ " + getString(R.string.home_q_scene, sc), Color.parseColor("#34D399")) {
            Prefs.remote(this).edit().putString("recallOnConnect", sc).apply(); Nav.go(this, RemoteActivity::class.java) { it.putExtra("open", "recall") } }   // the console asks before it changes
        chip(quick, "♪ " + getString(R.string.home_q_tuner), Color.parseColor("#FBBF24")) { Nav.go(this, ToolsActivity::class.java) }
        chip(quick, "● " + getString(R.string.home_q_record), Color.parseColor("#F87171")) { Nav.go(this, RecorderActivity::class.java) }
        chip(quick, "⌕ " + getString(R.string.home_q_find), getColor(R.color.cyan)) { Nav.go(this, RemoteActivity::class.java) }
    }
    private fun muteAll() {
        val c = live; if (c == null || !online) { android.widget.Toast.makeText(this, R.string.home_offline, android.widget.Toast.LENGTH_SHORT).show(); return }
        AlertDialog.Builder(this).setMessage(R.string.home_q_mute_ask).setPositiveButton(R.string.home_q_mute) { _, _ ->
            io.execute { runCatching { val st = c.state(); st.strips.forEach { c.mute(it.id, true) }; st.strips.size }
                .onSuccess { n -> ui.post { android.widget.Toast.makeText(this, getString(R.string.home_q_muted, n), android.widget.Toast.LENGTH_SHORT).show() } } } }
            .setNegativeButton(android.R.string.cancel, null).show()
    }
    // ---- WHAT'S NEW (this version's entry of the release feed; ✕ hides it until the next version)
    internal fun drawNews() {
        news.removeAllViews(); val ver = runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull().orEmpty()
        if (Home.newsSeen(this, ver)) return; val n = Home.news(this, ver) ?: return
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(32, 24, 32, 24)
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 18, 0, 0) } }
        val head = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        head.addView(TextView(this).apply { text = getString(R.string.home_news, n.version); setTextColor(Color.parseColor("#A78BFA")); textSize = 11f; letterSpacing = 0.2f; typeface = Typeface.MONOSPACE; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
        head.addView(TextView(this).apply { text = "✕"; setTextColor(getColor(R.color.dim)); textSize = 16f; setPadding(20, 0, 0, 0); setOnClickListener { Home.setNewsSeen(this@HomeActivity, ver); drawNews() } })
        box.addView(head); var open = false
        val list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        fun fill() { list.removeAllViews(); for (it in (if (open) n.items else n.items.take(3))) list.addView(TextView(this).apply { text = "•  $it"; setTextColor(Color.WHITE); textSize = 12f; setPadding(0, 8, 0, 0); maxLines = if (open) 6 else 2; ellipsize = android.text.TextUtils.TruncateAt.END }) }
        fill(); box.addView(list)
        if (n.items.size > 3) box.addView(TextView(this).apply { text = getString(R.string.home_news_more, n.items.size); setTextColor(getColor(R.color.cyan)); textSize = 11f; setPadding(0, 12, 0, 0); setOnClickListener { open = !open; fill(); text = if (open) getString(R.string.home_news_less) else getString(R.string.home_news_more, n.items.size) } })
        news.addView(box)
    }
    // ---- RECENT pages
    internal fun drawRecent() {
        recentRow.removeAllViews(); val r = Home.recent(this); if (r.isEmpty()) return
        recentRow.addView(TextView(this).apply { text = getString(R.string.home_recent); setTextColor(getColor(R.color.dim)); textSize = 10f; letterSpacing = 0.2f; typeface = Typeface.MONOSPACE; setPadding(0, 16, 18, 0) })
        val all = tiles().associateBy { it.id }
        for (id in r) { val t = all[id] ?: continue; chip(recentRow, t.glyph + "  " + getString(t.title), t.color) { t.go() } }
    }
    // ---- THE LIVE CONSOLE CARD: while HOME is open and a console is paired, its state twice a second (status + master meter)
    private val poll = object : Runnable { override fun run() {
        val c = live ?: return
        io.execute { val st = runCatching { c.status() }.getOrNull(); val m = if (st != null) runCatching { c.meters() }.getOrNull() else null
            ui.post { online = st != null; if (st != null) { lastOk = System.currentTimeMillis(); liveState = (if (st.running) st.backend.uppercase() + " · " + getString(R.string.home_running) else getString(R.string.home_engine_stopped)) + (if (st.computer.isNotEmpty()) "  ·  " + st.computer else "") }
                if (m != null) { meterL = m.masterL; meterR = m.masterR }; showLive() } }
        ui.postDelayed(this, 500) } }
    private fun startLive() {
        ui.removeCallbacks(poll); val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty(); val tok = p.getString("token", "").orEmpty()
        if (host.isEmpty() || tok.isEmpty()) { live = null; return }
        live = com.amfb.amixos.remote.DaemonClient(com.amfb.amixos.remote.Endpoint(host, (p.getString("port", null) ?: Prefs.defaultPort(this).toString()).toIntOrNull() ?: 8765, tok)); ui.post(poll)
    }
    private fun showLive() {
        dot?.text = if (online) "● " + getString(R.string.home_online) else "○ " + getString(R.string.home_offline); dot?.setTextColor(Color.parseColor(if (online) "#34D399" else "#8B98A8"))
        liveText?.text = if (online) liveState else getString(R.string.home_offline_sub); meterView?.set(if (online) meterL else 0.0, if (online) meterR else 0.0)
    }
    /** The console card: from what the remote saved at its last connection (nothing is fetched here). */
    fun showCard() {
        card.removeAllViews(); val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty(); val paired = !p.getString("token", "").isNullOrEmpty()
        card.addView(TextView(this).apply { text = getString(R.string.home_console); setTextColor(getColor(R.color.accent)); textSize = 11f; letterSpacing = 0.2f; typeface = Typeface.MONOSPACE })
        if (host.isEmpty()) {
            card.addView(TextView(this).apply { text = getString(R.string.home_no_console); setTextColor(Color.WHITE); textSize = 16f; setPadding(0, 8, 0, 4) })
            card.addView(TextView(this).apply { text = getString(R.string.home_no_console_sub); setTextColor(getColor(R.color.dim)); textSize = 12f })
            return
        }
        val name = p.getString("lastName", "AmF X1").orEmpty(); val plan = p.getString("lastPlan", "").orEmpty(); val strips = p.getInt("lastStrips", 0)
        card.addView(TextView(this).apply { text = name; setTextColor(Color.WHITE); textSize = 18f; typeface = Typeface.DEFAULT_BOLD; setPadding(0, 8, 0, 0) })
        card.addView(TextView(this).apply { text = host + ":" + (p.getString("port", null) ?: Prefs.defaultPort(this@HomeActivity)); setTextColor(getColor(R.color.cyan)); typeface = Typeface.MONOSPACE; textSize = 13f })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 10, 0, 0) }
        fun chip(t: String, col: Int) = row.addView(TextView(this).apply { text = t; setTextColor(col); textSize = 11f; typeface = Typeface.MONOSPACE; setPadding(0, 0, 28, 0) })
        chip(if (paired) "● " + getString(R.string.set_paired).uppercase() else "○ " + getString(R.string.home_not_paired), if (paired) Color.parseColor("#34D399") else Color.parseColor("#FBBF24"))
        if (plan.isNotEmpty()) chip(plan, Color.parseColor("#FBBF24")); if (strips > 0) chip("$strips CH", getColor(R.color.dim))
        card.addView(row)
        // live: online or not, the engine, the master meter, CONNECT
        dot = TextView(this).apply { textSize = 11f; typeface = Typeface.MONOSPACE; setPadding(0, 14, 0, 0) }; card.addView(dot)
        liveText = TextView(this).apply { setTextColor(getColor(R.color.dim)); textSize = 11f; typeface = Typeface.MONOSPACE }; card.addView(liveText)
        meterView = MeterBar(this).apply { layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 44).apply { setMargins(0, 12, 0, 6) } }; card.addView(meterView)
        card.addView(android.widget.Button(this).apply { text = getString(R.string.connect); setOnClickListener { Nav.go(this@HomeActivity, RemoteActivity::class.java) } })
        showLive()
    }
    private fun pickEqChannel() {
        val n = Prefs.remote(this).getInt("lastStrips", 0).takeIf { it > 0 } ?: 10
        AlertDialog.Builder(this).setTitle(R.string.home_eq).setItems((1..n).map { "CH %02d".format(it) }.toTypedArray()) { _, w -> Nav.go(this, EqActivity::class.java) { it.putExtra("strip", w) } }.show()
    }
}

/** the master L / R on HOME's console card: two thin bars in dBFS (-60 .. 0), green / amber / red */
class MeterBar(c: android.content.Context) : View(c) {
    private var l = 0.0; private var r = 0.0; private val p = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG)
    fun set(left: Double, right: Double) { l = left; r = right; invalidate() }
    private fun pos(v: Double) = if (v <= 1e-6) 0f else ((20 * Math.log10(v) + 60) / 60).toFloat().coerceIn(0f, 1f)
    override fun onDraw(cv: android.graphics.Canvas) { val h = height / 2f - 3; for ((i, v) in listOf(l, r).withIndex()) { val y = i * (h + 6); p.color = Color.parseColor("#1E293B"); cv.drawRoundRect(0f, y, width.toFloat(), y + h, 6f, 6f, p)
        val x = pos(v); p.color = Color.parseColor(if (x > 0.95f) "#EF4444" else if (x > 0.8f) "#FBBF24" else "#34D399"); cv.drawRoundRect(0f, y, width * x, y + h, 6f, 6f, p) } }
}
