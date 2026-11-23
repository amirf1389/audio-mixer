package com.amfb.amixos

import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.Endpoint
import com.amfb.amixos.remote.Inserts
import com.amfb.amixos.remote.LatestSender
import java.util.concurrent.Executors

/** CHANNEL INSERTS from the phone: the slots of one channel (up to 10) as the console has them; add a plug-in from the console's own
 *  list (its scanned VST2 / VST3 plug-ins and the built-in effects), remove, move up / down, bypass, and every parameter (a slider each,
 *  with the plug-in's own value text), its factory presets and its defaults. A slider sends its latest value only (one at a time). */
class InsertsActivity : FitActivity() {
    private val io = Executors.newSingleThreadExecutor(); private val ui = Handler(Looper.getMainLooper()); private val live = LatestSender(io)
    private lateinit var body: LinearLayout; private lateinit var head: TextView; private lateinit var addSpin: Spinner
    internal var client: DaemonClient? = null; internal var strip = 0; private var catalog: List<Inserts.Entry> = emptyList(); internal var slots: List<Inserts.Slot> = emptyList(); private var open = -1
    private val accent = Color.parseColor("#FFB590"); private val cyan = Color.parseColor("#7FD8E8"); private val dim = Color.parseColor("#C2C7CF")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        strip = intent.getIntExtra("strip", 0).coerceAtLeast(0)
        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 24, 24, 32) }
        root.addView(TextView(this).apply { text = getString(R.string.ins_title); setTextColor(accent); textSize = 14f; setTypeface(null, Typeface.BOLD) })
        val nav = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        head = TextView(this).apply { setTextColor(Color.WHITE); textSize = 13f; setPadding(16, 0, 16, 0) }
        nav.addView(Button(this).apply { text = "◀"; contentDescription = getString(R.string.ins_prev); setOnClickListener { if (strip > 0) { strip--; open = -1; refresh() } } })
        nav.addView(head); nav.addView(Button(this).apply { text = "▶"; contentDescription = getString(R.string.ins_next); setOnClickListener { strip++; open = -1; refresh() } }); root.addView(nav)
        val addRow = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        addSpin = Spinner(this).apply { layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); contentDescription = getString(R.string.ins_choose) }
        addRow.addView(addSpin); addRow.addView(Button(this).apply { text = getString(R.string.ins_add); setOnClickListener { val e = catalog.getOrNull(addSpin.selectedItemPosition) ?: return@setOnClickListener; act { it.insertAdd(strip, e.id) } } })
        root.addView(addRow)
        body = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }; root.addView(body)
        root.addView(TextView(this).apply { text = getString(R.string.ins_note); setTextColor(dim); textSize = 11f; setPadding(0, 16, 0, 0) })
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(root) }, null))
        val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty()
        if (host.isEmpty()) { head.text = getString(R.string.eq_offline); return }
        client = DaemonClient(Endpoint(host, p.getString("port", null)?.toIntOrNull() ?: Prefs.defaultPort(this), p.getString("token", "").orEmpty()))
        io.execute { val cat = runCatching { client!!.insertCatalog() }.getOrDefault(emptyList()); ui.post { catalog = cat
            addSpin.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, cat.map { it.label }) } }
        refresh()
    }
    private fun act(f: (DaemonClient) -> Any?) { val c = client ?: return; io.execute { val r = runCatching { f(c) }; ui.post { r.onFailure { head.text = getString(R.string.failed, it.message ?: "") }; refresh() } } }
    internal fun refresh() { val c = client ?: return; val s = strip; io.execute { val l = runCatching { c.insertSlots(s) }.getOrDefault(emptyList()); ui.post { if (s == strip) { slots = l; render() } } } }
    internal fun render() {
        head.text = getString(R.string.ins_channel, strip + 1, slots.size, Inserts.MAX); body.removeAllViews()
        if (slots.isEmpty()) body.addView(TextView(this).apply { text = getString(R.string.ins_none); setTextColor(dim); setPadding(0, 12, 0, 12) })
        for (s in slots) {
            body.addView(TextView(this).apply { text = "${s.slot + 1} · ${s.name}" + if (s.note.isBlank()) "" else "  (${s.note})"; setTextColor(if (s.bypass) dim else Color.WHITE); setTypeface(null, Typeface.BOLD); setPadding(0, 16, 0, 4) })
            val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
            fun b(t: String, d: String, f: () -> Unit) = row.addView(Button(this).apply { text = t; contentDescription = d; setOnClickListener { f() } })
            b("▲", getString(R.string.ins_up)) { if (Inserts.inRange(s.slot - 1)) act { it.insertMove(strip, s.slot, s.slot - 1) } }
            b("▼", getString(R.string.ins_down)) { if (Inserts.inRange(s.slot + 1)) act { it.insertMove(strip, s.slot, s.slot + 1) } }
            b(if (s.bypass) getString(R.string.ins_bypassed) else getString(R.string.ins_bypass), getString(R.string.ins_bypass)) { act { it.pluginBypass(strip, s.slot, !s.bypass) } }
            if (s.loaded) b(getString(R.string.ins_edit), getString(R.string.ins_edit)) { open = if (open == s.slot) -1 else s.slot; render() }
            b("✕", getString(R.string.ins_remove)) { AlertDialog.Builder(this).setMessage(getString(R.string.ins_remove_q, s.name)).setPositiveButton(R.string.ins_remove) { _, _ -> act { it.insertRemove(strip, s.slot) } }.setNegativeButton(android.R.string.cancel, null).show() }
            body.addView(row)
            if (open == s.slot) editor(s)
        }
    }
    private fun editor(s: Inserts.Slot) {
        val bar = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        if (s.presets.isNotEmpty()) bar.addView(Button(this).apply { text = getString(R.string.ins_presets); setOnClickListener {
            AlertDialog.Builder(this@InsertsActivity).setItems(s.presets.map { it.name }.toTypedArray()) { _, i -> act { it.pluginPreset(strip, s.slot, s.presets[i].id) } }.show() } })
        bar.addView(Button(this).apply { text = getString(R.string.ins_defaults); setOnClickListener { act { it.pluginDefaults(strip, s.slot) } } }); body.addView(bar)
        for (p in s.params) {
            val t = TextView(this).apply { text = "${p.title}  ${p.text}"; setTextColor(cyan); textSize = 12f; typeface = Typeface.MONOSPACE }
            body.addView(t)
            body.addView(SeekBar(this).apply { max = 1000; progress = (p.value * 1000).toInt(); contentDescription = p.title
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(b: SeekBar, v: Int, user: Boolean) { if (!user) return; val c = client ?: return; val x = v / 1000.0
                        live.submit("p${s.slot}_${p.id}") { val r = runCatching { c.pluginParam(strip, s.slot, p.id, x) }.getOrNull(); val txt = r?.get("text")?.toString(); if (txt != null) ui.post { t.text = "${p.title}  $txt" } } }
                    override fun onStartTrackingTouch(b: SeekBar) {}; override fun onStopTrackingTouch(b: SeekBar) {} }) })
        }
    }
}
