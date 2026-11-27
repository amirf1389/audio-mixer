package com.amfb.amixos

import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.media.session.MediaController
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import java.util.concurrent.Executors

/** PLAY: the music on this phone (any app with a media session: TIDAL, Spotify, Deezer ...) and, in AmF X1, the console's STREAMING page
 *  on the computer. Title, artist, artwork, the app; play / pause, previous, next and seek. Control only: nothing is recorded. */
class MediaActivity : FitActivity() {
    private val ui = Handler(Looper.getMainLooper()); private val io = Executors.newSingleThreadExecutor()
    private lateinit var access: LinearLayout; private lateinit var card: LinearLayout; private lateinit var empty: TextView
    private lateinit var art: ImageView; private lateinit var title: TextView; private lateinit var artist: TextView; private lateinit var app: TextView
    private lateinit var seek: SeekBar; private lateinit var time: TextView; private lateinit var playBtn: Button; private lateinit var sessions: LinearLayout
    private lateinit var pc: LinearLayout; private lateinit var pcText: TextView
    internal var chosenPkg: String? = null; private var dragging = false; private var client: DaemonClient? = null; private var destroyed = false
    private val tick = object : Runnable { override fun run() { if (destroyed) return; refresh(); ui.postDelayed(this, 1000) } }
    private val pcTick = object : Runnable { override fun run() { if (destroyed) return; pollConsole(); ui.postDelayed(this, 3000) } }

    private fun dp(v: Int) = (v * resources.displayMetrics.density).toInt()
    private fun cardBg() = GradientDrawable().apply { cornerRadius = dp(16).toFloat(); setColor(getColor(R.color.md_surface_container)) }
    private fun label(t: String, size: Float, color: Int, bold: Boolean = false) = TextView(this).apply { text = t; textSize = size; setTextColor(getColor(color)); if (bold) typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL) }
    private fun key(t: String, desc: String, click: () -> Unit) = Button(this).apply { text = t; contentDescription = desc; minimumWidth = dp(56); minimumHeight = dp(56); textSize = 18f; setOnClickListener { click() } }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        chosenPkg = Prefs.local(this).getString("playPkg", null)
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(16), dp(16), dp(16), dp(16)) }
        box.addView(label(getString(R.string.play_title), 14f, R.color.accent, true))
        box.addView(label(getString(R.string.play_intro), 13f, R.color.dim).apply { setPadding(0, dp(4), 0, dp(12)) })

        access = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = cardBg(); setPadding(dp(16), dp(16), dp(16), dp(16)); contentDescription = "playAccess" }
        access.addView(label(getString(R.string.play_access_title), 15f, R.color.md_on_surface, true))
        access.addView(label(getString(R.string.play_access_why), 13f, R.color.dim).apply { setPadding(0, dp(6), 0, dp(10)) })
        access.addView(Button(this).apply { text = getString(R.string.play_access_allow); contentDescription = "playAllow"; setOnClickListener { runCatching { startActivity(MediaHub.accessIntent()) } } })
        box.addView(access)

        card = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = cardBg(); setPadding(dp(16), dp(16), dp(16), dp(16)); contentDescription = "playCard" }
        val top = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        art = ImageView(this).apply { scaleType = ImageView.ScaleType.CENTER_CROP; background = GradientDrawable().apply { cornerRadius = dp(12).toFloat(); setColor(getColor(R.color.md_surface_container_highest)) }; clipToOutline = true; contentDescription = getString(R.string.play_artwork) }
        top.addView(art, LinearLayout.LayoutParams(dp(96), dp(96)))
        val words = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(16), 0, 0, 0) }
        title = label("", 17f, R.color.md_on_surface, true).apply { maxLines = 2; tag = "playTitleText" }; artist = label("", 14f, R.color.dim).apply { maxLines = 1 }
        app = label("", 12f, R.color.cyan).apply { setPadding(0, dp(4), 0, 0) }
        words.addView(title); words.addView(artist); words.addView(app); top.addView(words, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)); card.addView(top)
        seek = SeekBar(this).apply { max = 1000; contentDescription = "playSeek"; setPadding(dp(8), dp(16), dp(8), dp(8))
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar, p: Int, user: Boolean) {}
                override fun onStartTrackingTouch(s: SeekBar) { dragging = true }
                override fun onStopTrackingTouch(s: SeekBar) { dragging = false; current()?.let { mc -> MediaHub.seek(mc, MediaHub.seekTarget(s.progress / 1000.0, MediaHub.now(mc).durationMs)) } }
            }) }
        card.addView(seek, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(48)))
        time = label("", 12f, R.color.dim).apply { gravity = Gravity.END }; card.addView(time)
        val keys = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER; setPadding(0, dp(8), 0, 0) }
        keys.addView(key("⏮", getString(R.string.play_prev)) { current()?.let { MediaHub.previous(it) } })
        playBtn = key("▶", getString(R.string.play_toggle)) { current()?.let { MediaHub.toggle(it); ui.postDelayed({ refresh() }, 200) } }
        keys.addView(playBtn, LinearLayout.LayoutParams(dp(88), dp(56)).apply { setMargins(dp(12), 0, dp(12), 0) })
        keys.addView(key("⏭", getString(R.string.play_next)) { current()?.let { MediaHub.next(it) } })
        card.addView(keys)
        box.addView(card, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(12) })
        empty = label(getString(R.string.play_nothing), 13f, R.color.dim).apply { setPadding(0, dp(12), 0, 0); contentDescription = "playEmpty" }; box.addView(empty)
        sessions = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; contentDescription = "playSessions" }
        box.addView(android.widget.HorizontalScrollView(this).apply { addView(sessions) })

        box.addView(label(getString(R.string.play_open_app), 13f, R.color.md_on_surface, true).apply { setPadding(0, dp(20), 0, dp(8)) })
        val apps = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; contentDescription = "playApps" }
        for (p in MediaHub.players) apps.addView(Button(this).apply { text = p.name; setOnClickListener { val i = MediaHub.launch(this@MediaActivity, p.pkg)
            if (i != null) runCatching { startActivity(i) } else Toast.makeText(this@MediaActivity, getString(R.string.play_not_installed, p.name), Toast.LENGTH_SHORT).show() } },
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(dp(4), 0, dp(4), 0) })
        box.addView(apps)

        pc = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = cardBg(); setPadding(dp(16), dp(16), dp(16), dp(16)); contentDescription = "playComputer"; visibility = View.GONE }
        pc.addView(label(getString(R.string.play_computer), 15f, R.color.md_on_surface, true)); pcText = label("", 13f, R.color.dim).apply { setPadding(0, dp(6), 0, dp(8)) }; pc.addView(pcText)
        val pcKeys = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER }
        for ((g, cmd) in listOf("⏮" to "previous", "⏯" to "toggle", "⏭" to "next")) pcKeys.addView(key(g, "pc_$cmd") { consoleCmd(cmd) }, LinearLayout.LayoutParams(0, dp(56), 1f).apply { setMargins(dp(4), 0, dp(4), 0) })
        pc.addView(pcKeys)
        box.addView(pc, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(20) })

        box.addView(Button(this).apply { text = getString(R.string.play_web_players); contentDescription = "playWebPlayers"; setOnClickListener { Nav.go(this@MediaActivity, StreamPlayerActivity::class.java) } },
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = dp(20) })
        box.addView(label(getString(R.string.play_note), 11f, R.color.dim).apply { setPadding(0, dp(12), 0, 0); contentDescription = "playNote" })
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(getColor(R.color.bg)); addView(box) }, Nav.Tab.PLAY))

        if (!Nav.standalone(this)) { val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty()
            if (host.isNotEmpty()) client = DaemonClient(Endpoint(host, p.getString("port", null)?.toIntOrNull() ?: Prefs.defaultPort(this), p.getString("token", "").orEmpty())) }
        refresh()
    }
    override fun onResume() { super.onResume(); ui.removeCallbacks(tick); ui.removeCallbacks(pcTick); ui.post(tick); if (client != null) ui.post(pcTick) }
    override fun onPause() { ui.removeCallbacks(tick); ui.removeCallbacks(pcTick); super.onPause() }
    override fun onDestroy() { destroyed = true; ui.removeCallbacksAndMessages(null); io.shutdownNow(); super.onDestroy() }

    private fun current(): MediaController? { val list = MediaHub.controllers(this); return list.firstOrNull { it.packageName == chosenPkg } ?: list.firstOrNull { MediaHub.now(it).playing } ?: list.firstOrNull() }

    internal fun refresh() {
        val ok = MediaHub.allowed(this); access.visibility = if (ok) View.GONE else View.VISIBLE
        val list = MediaHub.controllers(this); val mc = current()
        sessions.removeAllViews()
        if (list.size > 1) for (c in list) { val on = c.packageName == mc?.packageName
            sessions.addView(Button(this).apply { text = MediaHub.appName(c.packageName, appLabel(c.packageName)); isSelected = on; alpha = if (on) 1f else 0.7f
                setOnClickListener { chosenPkg = c.packageName; Prefs.local(this@MediaActivity).edit().putString("playPkg", c.packageName).apply(); refresh() } }) }
        if (mc == null) { card.visibility = View.GONE; empty.visibility = if (ok) View.VISIBLE else View.GONE; return }
        card.visibility = View.VISIBLE; empty.visibility = View.GONE
        val n = MediaHub.now(mc)
        title.text = n.title.ifEmpty { getString(R.string.play_unknown) }; artist.text = listOf(n.artist, n.album).filter { it.isNotEmpty() }.joinToString(" · ")
        app.text = MediaHub.appName(n.pkg, appLabel(n.pkg)); playBtn.text = if (n.playing) "⏸" else "▶"
        MediaHub.art(mc).let { if (it != null) art.setImageBitmap(it) else art.setImageDrawable(null) }
        val pos = MediaHub.positionNow(n, android.os.SystemClock.elapsedRealtime())
        seek.isEnabled = n.canSeek && n.durationMs > 0
        if (!dragging) seek.progress = if (n.durationMs > 0) (pos * 1000 / n.durationMs).toInt() else 0
        time.text = if (n.durationMs > 0) MediaHub.fmt(pos) + " / " + MediaHub.fmt(n.durationMs) else ""
    }
    private fun appLabel(pkg: String): String? = runCatching { packageManager.getApplicationLabel(packageManager.getApplicationInfo(pkg, 0)).toString() }.getOrNull()

    private fun pollConsole() { val c = client ?: return
        io.execute { val r = runCatching { MediaHub.parseConsole(c.streaming()) }.getOrNull(); ui.post { if (destroyed) return@post
            if (r == null || !r.shown) { pc.visibility = View.GONE; return@post }
            pc.visibility = View.VISIBLE; pcText.text = listOf(r.title.ifEmpty { getString(R.string.play_unknown) }, r.artist, r.service).filter { it.isNotEmpty() }.joinToString(" · ") + if (r.playing) "" else "  (" + getString(R.string.play_paused) + ")" } } }
    private fun consoleCmd(cmd: String) { val c = client ?: return
        io.execute { val r = runCatching { c.streamingCmd(cmd) }; ui.post { if (!destroyed && r.isFailure) Toast.makeText(this, getString(R.string.failed, r.exceptionOrNull()?.message ?: ""), Toast.LENGTH_SHORT).show() } } }
}

/** NOW PLAYING: a thin bar above the bottom tabs while a music app plays on the phone (title · artist, play / pause); a tap opens PLAY. */
object NowPlaying {
    fun bar(a: android.app.Activity): View {
        val d = a.resources.displayMetrics.density; fun px(v: Int) = (v * d).toInt()
        val row = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; tag = "nowPlaying"; setBackgroundColor(a.getColor(R.color.md_surface_container_high)); setPadding(px(16), 0, px(4), 0); visibility = View.GONE; isClickable = true
            contentDescription = a.getString(R.string.play_title); setOnClickListener { Nav.go(a, MediaActivity::class.java) } }
        val text = TextView(a).apply { textSize = 13f; maxLines = 1; ellipsize = android.text.TextUtils.TruncateAt.END; setTextColor(a.getColor(R.color.md_on_surface)) }
        val key = TextView(a).apply { textSize = 18f; gravity = Gravity.CENTER; setTextColor(a.getColor(R.color.md_on_surface)); contentDescription = a.getString(R.string.play_toggle); isClickable = true }
        row.addView(text, LinearLayout.LayoutParams(0, px(48), 1f).apply { }); text.gravity = Gravity.CENTER_VERTICAL
        row.addView(key, LinearLayout.LayoutParams(px(48), px(48)))
        val h = Handler(Looper.getMainLooper())
        val upd = object : Runnable { override fun run() {
            if (a.isDestroyed || a.isFinishing) return
            val mc = MediaHub.controllers(a).let { l -> l.firstOrNull { MediaHub.now(it).playing } ?: l.firstOrNull() }
            val n = mc?.let { MediaHub.now(it) }
            if (n == null || n.title.isEmpty()) row.visibility = View.GONE else { row.visibility = View.VISIBLE
                text.text = listOf(n.title, n.artist, MediaHub.appName(n.pkg)).filter { it.isNotEmpty() }.joinToString(" · "); key.text = if (n.playing) "⏸" else "▶"
                key.setOnClickListener { MediaHub.toggle(mc); h.removeCallbacks(this); h.postDelayed(this, 250) } }
            if (row.isAttachedToWindow) h.postDelayed(this, 2000) } }
        row.addOnAttachStateChangeListener(object : View.OnAttachStateChangeListener {
            override fun onViewAttachedToWindow(v: View) { h.removeCallbacks(upd); h.post(upd) }
            override fun onViewDetachedFromWindow(v: View) { h.removeCallbacks(upd) } })
        return row
    }
}
