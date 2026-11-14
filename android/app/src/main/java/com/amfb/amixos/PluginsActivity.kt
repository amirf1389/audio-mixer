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
class PluginsActivity : AppCompatActivity() {
    private val io = Executors.newSingleThreadExecutor(); private val ui = Handler(Looper.getMainLooper())
    private lateinit var body: LinearLayout; private lateinit var head: TextView
    private var info: Map<String, Any?> = emptyMap(); private var tab = 0; private var filter = ""
    private val tabs = ArrayList<Button>()
    private val accent = Color.parseColor("#EA580C"); private val cyan = Color.parseColor("#00F2FF"); private val dim = Color.parseColor("#8B98A8"); private val card = Color.parseColor("#141B26")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.plugins_title); setTextColor(accent); textSize = 20f; setTypeface(null, Typeface.BOLD) })
        head = TextView(this).apply { setTextColor(dim); textSize = 12f; text = getString(R.string.connecting) }; root.addView(head)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        listOf(R.string.plugins_racks, R.string.plugins_vst, R.string.plugins_ext).forEachIndexed { i, r -> row.addView(Button(this).apply { text = getString(r); setOnClickListener { tab = i; render() } }.also { tabs.add(it) }) }
        root.addView(row)
        root.addView(EditText(this).apply { hint = getString(R.string.plugins_search); setTextColor(Color.WHITE); setHintTextColor(dim)
            addTextChangedListener(object : TextWatcher { override fun afterTextChanged(e: Editable?) { filter = e.toString().trim().lowercase(); render() }
                override fun beforeTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {}; override fun onTextChanged(s: CharSequence?, a: Int, b: Int, c: Int) {} }) })
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; root.addView(body)
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#0F141C")); addView(root) }, null))
        load()
    }
    fun load() {
        val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty()
        if (host.isEmpty()) { head.text = getString(R.string.eq_offline); return }
        val c = DaemonClient(Endpoint(host, p.getString("port", null)?.toIntOrNull() ?: Prefs.defaultPort(this), p.getString("token", "").orEmpty()))
        io.execute { val r = runCatching { c.consoleInfo() }; ui.post { r.onSuccess { show(it) }.onFailure { head.text = getString(R.string.failed, it.message ?: "") } } }
    }
    fun show(i: Map<String, Any?>) {
        info = i
        if (i["available"] != true) { head.text = getString(R.string.plugins_none); render(); return }
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
        t.addView(TextView(this).apply { text = title; setTextColor(Color.WHITE); textSize = 15f })
        if (sub.isNotEmpty()) t.addView(TextView(this).apply { text = sub; setTextColor(dim); textSize = 12f })
        l.addView(t); if (badge.isNotEmpty()) l.addView(TextView(this).apply { text = badge; setTextColor(badgeColor); typeface = Typeface.MONOSPACE; textSize = 12f })
        body.addView(l)
    }
    private fun section(t: String) = body.addView(TextView(this).apply { text = t; setTextColor(accent); typeface = Typeface.MONOSPACE; setPadding(0, 24, 0, 4) })
    private fun empty(t: String) = body.addView(TextView(this).apply { text = t; setTextColor(dim); setPadding(0, 16, 0, 0) })
    fun tabTo(i: Int) { tab = i; render() }
    fun render() {
        tabs.forEachIndexed { k, b -> b.setTextColor(if (k == tab) accent else Color.WHITE) }
        body.removeAllViews(); if (info["available"] != true) { empty(getString(R.string.plugins_none)); return }
        when (tab) {
            0 -> { val racks = list("racks"); if (racks.isEmpty()) empty(getString(R.string.plugins_no_racks))
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
        body.addView(TextView(this).apply { text = getString(R.string.plugins_note); setTextColor(dim); textSize = 11f; setPadding(0, 24, 0, 0) })
    }
    override fun onDestroy() { io.shutdownNow(); super.onDestroy() }
}
