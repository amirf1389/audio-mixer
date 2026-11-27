package com.amfb.amixos

import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.Editable
import android.text.TextWatcher
import android.view.Gravity
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import java.text.DateFormat
import java.util.Date
import java.util.concurrent.Executors

/** PLUG-INS & EXTENSIONS of the console, read-only: the channel racks (which plug-in in which slot, bypassed or not), the VST3 / VST2
 *  plug-ins its last scan found (search by name or vendor), and the extensions and built-in effects of its LOCAL STORE.
 *  The console saves this when the phone remote starts; while the remote runs the service's own engine plays, so the racks are not
 *  changed from here (that is done on the computer). */
class PluginsActivity : FitActivity() {
    private val io = Executors.newSingleThreadExecutor(); private val ui = Handler(Looper.getMainLooper())
    private lateinit var body: LinearLayout; private lateinit var head: TextView
    private var info: Map<String, Any?> = emptyMap(); private var tab = 0; private var filter = ""
    internal var liveRacks: Map<String, Any?>? = null; private var client: DaemonClient? = null   // LIVE: the console's plug-ins as the service plays them (tap one to control it)
    private val tabs = ArrayList<Button>()
    private val accent = Color.parseColor("#FFB590"); private val cyan = Color.parseColor("#7FD8E8"); private val dim = Color.parseColor("#C2C7CF"); private val card = Color.parseColor("#1D2024")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.plugins_title); setTextColor(accent); textSize = 14f; setTypeface(null, Typeface.BOLD) })
        head = TextView(this).apply { setTextColor(dim); textSize = 12f; text = getString(R.string.connecting) }; root.addView(head)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf(R.string.plugins_racks, R.string.plugins_vst, R.string.plugins_ext).forEachIndexed { i, r -> row.addView(Button(this).apply { text = getString(r); setOnClickListener { tab = i; render() } }.also { tabs.add(it) }) }
        root.addView(row)
        root.addView(EditText(this).apply { hint = getString(R.string.plugins_search); setTextColor(Color.WHITE); setHintTextColor(dim)
            addTextChangedListener(object : TextWatcher { override fun afterTextChanged(e: Editable?) { filter = e.toString().trim().lowercase(); render() }
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}; override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {} }) })
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; root.addView(body)
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(root) }, null))
        load()
    }
    fun load() {
        val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty()
        if (host.isEmpty()) { head.text = getString(R.string.eq_offline); return }
        val c = DaemonClient(Endpoint(host, p.getString("port", null)?.toIntOrNull() ?: Prefs.defaultPort(this), p.getString("token", "").orEmpty()))
        client = c
        io.execute { val r = runCatching { c.consoleInfo() }; val lr = runCatching { c.racks() }.getOrNull()
            ui.post { liveRacks = lr; r.onSuccess { show(it) }.onFailure { if (lr != null) { info = mapOf("available" to true); render() } else head.text = getString(R.string.failed, it.message ?: "") } } }
    }
    fun show(i: Map<String, Any?>) {
        info = i
        if (i["available"] != true) { head.text = if ((liveRacks?.get("racks") as? List<*>)?.isNotEmpty() == true) getString(R.string.plugins_live_head) else getString(R.string.plugins_none); render(); return }
        val at = (i["written"] as? Number)?.toLong() ?: 0L
        head.text = getString(R.string.plugins_head, i["version"]?.toString() ?: "", DateFormat.getDateTimeInstance(DateFormat.SHORT, DateFormat.SHORT).format(Date(at)))
        render()
    }
    @Suppress("UNCHECKED_CAST") private fun list(k: String) = (info[k] as? List<Any?>).orEmpty().mapNotNull { it as? Map<String, Any?> }
    private fun s(m: Map<String, Any?>, k: String) = m[k]?.toString().orEmpty()
    private fun match(vararg t: String) = filter.isEmpty() || t.any { it.lowercase().contains(filter) }
    private fun item(title: String, sub: String, badge: String, badgeColor: Int) {
        val l = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setBackgroundColor(card); setPadding(20, 14, 20, 14)
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { topMargin = 8 } }
        val t = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
        t.addView(TextView(this).apply { text = title; setTextColor(Color.WHITE); textSize = 13f })
        if (sub.isNotEmpty()) t.addView(TextView(this).apply { text = sub; setTextColor(dim); textSize = 12f })
        l.addView(t); if (badge.isNotEmpty()) l.addView(TextView(this).apply { text = badge; setTextColor(badgeColor); typeface = Typeface.MONOSPACE; textSize = 12f })
        body.addView(l)
    }
    private fun section(t: String) = body.addView(TextView(this).apply { text = t; setTextColor(accent); typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL); setPadding(0, 14, 0, 4) })
    private fun empty(t: String) = body.addView(TextView(this).apply { text = t; setTextColor(dim); setPadding(0, 16, 0, 0) })
    fun tabTo(i: Int) { tab = i; render() }
    fun render() {
        tabs.forEachIndexed { k, b -> b.setTextColor(if (k == tab) accent else Color.WHITE) }
        body.removeAllViews()
        val live = (liveRacks?.get("racks") as? List<*>)?.isNotEmpty() == true
        if (info["available"] != true && !(tab == 0 && live)) { empty(getString(R.string.plugins_none)); return }   // the live racks need no saved list
        when (tab) {
            0 -> if ((liveRacks?.get("racks") as? List<*>)?.isNotEmpty() == true) renderLive() else { val racks = list("racks"); if (racks.isEmpty()) empty(getString(R.string.plugins_no_racks))
                for (r in racks) { @Suppress("UNCHECKED_CAST") val ps = (r["plugins"] as? List<Any?>).orEmpty().mapNotNull { it as? Map<String, Any?> }.filter { match(s(it, "name"), s(it, "kind")) }
                    if (ps.isEmpty()) continue
                    section(String.format("CH %02d  %s", ((r["strip"] as? Number)?.toInt() ?: 0) + 1, s(r, "name")))
                    for (p in ps) { val vst = p["builtIn"] != true; val err = s(p, "error")
                        item("${((p["slot"] as? Number)?.toInt() ?: 0) + 1}.  " + s(p, "name").ifEmpty { s(p, "kind") }, if (err.isNotEmpty()) err else if (vst) "VST plug-in" else "built-in effect",
                            if (p["bypass"] == true) "BYPASS" else if (err.isNotEmpty()) "ERROR" else "ON", if (p["bypass"] == true) Color.parseColor("#FBBF24") else if (err.isNotEmpty()) Color.parseColor("#EF4444") else Color.parseColor("#34D399")) } } }
            1 -> { val v = list("vst").filter { match(s(it, "name"), s(it, "vendor")) }.sortedBy { s(it, "name").lowercase() }
                section(getString(R.string.plugins_count, v.size)); if (v.isEmpty()) empty(getString(R.string.plugins_no_vst))
                for (p in v) item(s(p, "name"), s(p, "vendor"), s(p, "format").ifEmpty { s(p, "kind").substringBefore(':') }.uppercase(), cyan) }
            else -> { val all = list("store").filter { match(s(it, "name"), s(it, "note")) }
                for ((title, kind) in listOf(getString(R.string.plugins_extensions) to "extension", getString(R.string.plugins_effects) to "effect")) {
                    val l = all.filter { s(it, "kind") == kind }; if (l.isEmpty()) continue; section(title)
                    for (e in l) item(s(e, "name"), s(e, "note"), if (e["installed"] != true) "NOT INSTALLED" else if (e["on"] == true) "ON" else "OFF",
                        if (e["installed"] != true) dim else if (e["on"] == true) Color.parseColor("#34D399") else Color.parseColor("#FBBF24")) } }
        }
        if (!(tab == 0 && live)) body.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.plugins_note); setTextColor(dim); textSize = 11f; setPadding(0, 24, 0, 0) }))   // the read-only note, not under the live racks
    }
    // ---- LIVE RACKS: tap a plug-in to open its face; every knob, the power LED and the presets act on the computer at once
    @Suppress("UNCHECKED_CAST")
    private fun renderLive() {
        body.addView(ConsoleUi.moreLine(TextView(this).apply { text = getString(R.string.plugins_live_note); setTextColor(Color.parseColor("#34D399")); textSize = 12f; setPadding(0, 12, 0, 4) }))
        for (r in ((liveRacks?.get("racks") as? List<Map<String, Any?>>).orEmpty())) { val strip = (r["strip"] as? Number)?.toInt() ?: 0
            val slots = (r["slots"] as? List<Map<String, Any?>>).orEmpty().filter { match(s(it, "name"), s(it, "kind"), s(it, "vendor")) }; if (slots.isEmpty()) continue
            section(String.format("CH %02d  %s", strip + 1, s(r, "name")))
            for (p in slots) { val slot = (p["slot"] as? Number)?.toInt() ?: 0; val err = s(p, "error"); val loaded = p["loaded"] == true
                item("${slot + 1}.  " + s(p, "name").ifEmpty { s(p, "kind") }, if (!loaded) err else listOf(s(p, "format"), s(p, "vendor")).filter { it.isNotEmpty() }.joinToString(" · ") + "   ·   " + getString(R.string.plugins_tap),
                    if (!loaded) "ERROR" else if (p["bypass"] == true) "BYPASS" else "ON", if (!loaded) Color.parseColor("#EF4444") else if (p["bypass"] == true) Color.parseColor("#FBBF24") else Color.parseColor("#34D399"))
                if (loaded) body.getChildAt(body.childCount - 1).apply { isClickable = true; setOnClickListener { openPlugin(strip, slot) } } } }
    }
    /** one of the computer's plug-ins as a face: its parameters fetched now, each change sent at once */
    @Suppress("UNCHECKED_CAST")
    internal fun openPlugin(strip: Int, slot: Int) {
        val c = client ?: return
        io.execute { val r = runCatching { c.racks(strip) }.getOrNull()
            val slotJson = ((r?.get("racks") as? List<Map<String, Any?>>)?.firstOrNull()?.get("slots") as? List<Map<String, Any?>>)?.firstOrNull { (it["slot"] as? Number)?.toInt() == slot }
            ui.post { if (slotJson == null) { head.text = getString(R.string.failed, "no plug-in"); return@post }; showFace(c, strip, slot, slotJson) } }
    }
    internal var lastFace: android.view.View? = null
    internal fun faceView(c: DaemonClient?, strip: Int, slot: Int, json: Map<String, Any?>, onChanged: () -> Unit): android.view.View {
        val texts = HashMap<String, String>(); @Suppress("UNCHECKED_CAST") (json["params"] as? List<Map<String, Any?>>).orEmpty().forEach { texts[it["id"].toString()] = it["text"].toString() }
        return PluginFace.build(this, json, slot, object : PluginFace.Host {
            override fun set(id: String, value: Double) { val cl = c ?: return
                io.execute { runCatching { if (id == "bypass") cl.pluginBypass(strip, slot, value >= 0.5) else cl.pluginParam(strip, slot, id.toDouble().toLong(), value) }
                    .onSuccess { m -> m["text"]?.let { texts[id] = it.toString() } }.onFailure { e -> ui.post { head.text = getString(R.string.failed, e.message ?: "") } } } }
            override fun remove() { head.text = getString(R.string.plugins_remove_here) }   // the rack itself is changed on the computer
            override fun changed() = onChanged()
            override fun text(id: String) = texts[id] })
    }
    private fun showFace(c: DaemonClient, strip: Int, slot: Int, json: Map<String, Any?>) {
        val holder = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(16, 8, 16, 8) }
        val dlg = android.app.AlertDialog.Builder(this).setPositiveButton(android.R.string.ok, null).create()
        fun refresh(j: Map<String, Any?>) { holder.removeAllViews(); lastFace = faceView(c, strip, slot, j) { io.execute { @Suppress("UNCHECKED_CAST") val r = runCatching { c.racks(strip) }.getOrNull()
            @Suppress("UNCHECKED_CAST") val nj = ((r?.get("racks") as? List<Map<String, Any?>>)?.firstOrNull()?.get("slots") as? List<Map<String, Any?>>)?.firstOrNull { (it["slot"] as? Number)?.toInt() == slot }
            if (nj != null) ui.post { refresh(nj) } } }; holder.addView(lastFace) }
        refresh(json); dlg.setView(ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(holder) }); dlg.setOnDismissListener { load() }; dlg.show()
    }
    override fun onDestroy() { io.shutdownNow(); super.onDestroy() }
}
