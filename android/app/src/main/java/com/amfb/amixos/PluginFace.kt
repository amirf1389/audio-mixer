package com.amfb.amixos

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.GridLayout
import android.widget.LinearLayout
import android.widget.TextView
import com.amfb.amixos.remote.MiniJson

/** PLUG-IN FACES: an effect in a rack drawn like a plug-in window: a faceplate in the finish of its kind (reverbs brushed aluminium, delays
 *  deep green, modulation midnight blue, drive and amps black and gold, dynamics graphite, EQ and filters violet), its name, a power LED
 *  (bypass), PRESETS, and a rotary knob per parameter with its value. The same faces serve this phone's own rack and the computer's racks. */
object PluginFace {
    data class Style(val top: Int, val bottom: Int, val ink: Int, val accent: Int, val finish: String)
    /** the finish of an effect, from its kind and name */
    fun styleOf(kind: String, name: String): Style {
        val k = (kind + " " + name).lowercase()
        fun has(vararg w: String) = w.any { it in k }
        return when {
            has("reverb", "hall", "plate", "room", "space") -> Style(Color.parseColor("#D7DCE2"), Color.parseColor("#9AA3AE"), Color.parseColor("#1E293B"), Color.parseColor("#0EA5E9"), "aluminium")
            has("delay", "echo", "tape") -> Style(Color.parseColor("#1F4D3A"), Color.parseColor("#0F2E22"), Color.parseColor("#F5E9C8"), Color.parseColor("#F5E9C8"), "green")
            has("chorus", "flang", "phas", "trem", "vibr", "mod", "pitch") -> Style(Color.parseColor("#1E2A5A"), Color.parseColor("#0E1533"), Color.parseColor("#E0E7FF"), Color.parseColor("#818CF8"), "blue")
            has("drive", "satur", "dist", "amp", "fuzz", "analog", "tube", "barrier") -> Style(Color.parseColor("#1A1A1A"), Color.parseColor("#050505"), Color.parseColor("#E8C66A"), Color.parseColor("#E8C66A"), "gold")
            has("comp", "limit", "gate", "dyn", "expand", "de-ess", "deess", "squash") -> Style(Color.parseColor("#2B3138"), Color.parseColor("#171B20"), Color.parseColor("#E2E8F0"), Color.parseColor("#22D3EE"), "graphite")
            else -> Style(Color.parseColor("#2E1F4F"), Color.parseColor("#171029"), Color.parseColor("#EDE9FE"), Color.parseColor("#A78BFA"), "violet")
        }
    }

    // ---- PRESETS of an effect kind, kept on this phone: {"kind": {"name": {"id": value}}}
    @Suppress("UNCHECKED_CAST")
    fun presets(json: String?, kind: String): Map<String, Map<String, Double>> = runCatching {
        ((MiniJson.parse(json.orEmpty()) as Map<String, Any?>)[kind] as Map<String, Any?>).mapValues { (_, v) -> (v as Map<String, Any?>).mapValues { (it.value as Number).toDouble() } }
    }.getOrDefault(emptyMap())
    @Suppress("UNCHECKED_CAST")
    fun withPreset(json: String?, kind: String, name: String, values: Map<String, Double>?): String {
        val all = runCatching { LinkedHashMap(MiniJson.parse(json.orEmpty()) as Map<String, Any?>) }.getOrDefault(LinkedHashMap())
        val mine = LinkedHashMap(presets(json, kind)); if (values == null) mine.remove(name) else mine[name.trim().take(24).ifEmpty { "Preset" }] = values
        all[kind] = mine; return MiniJson.stringify(all)
    }
    fun store(c: Context) = c.getSharedPreferences("fxpresets", Context.MODE_PRIVATE)

    /** what a face needs from where the effect lives (this phone's engine, or the computer's rack over the network) */
    interface Host {
        fun set(id: String, value: Double)        // a knob, "mix" (0..1) or "bypass" (0 / 1)
        fun remove()
        fun changed()                              // redraw the rack
        fun text(id: String): String? = null       // a knob's value as the effect writes it (Hz, dB, ms), when it can say
    }

    /** the face of one effect from its JSON ({kind, name, bypass, mix, params:[{id, title, value 0..1, default, text}]}) */
    @Suppress("UNCHECKED_CAST")
    fun build(c: Context, json: Map<String, Any?>, slot: Int, host: Host): View {
        val kind = json["kind"]?.toString().orEmpty(); val name = json["name"]?.toString() ?: kind; val st = styleOf(kind, name); val byp = json["bypass"] == true
        val face = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; setPadding(26, 18, 26, 22)
            background = GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, intArrayOf(st.top, st.bottom)).apply { cornerRadius = 22f; setStroke(3, Color.argb(90, 255, 255, 255)) }
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 10, 0, 10) }
            alpha = if (byp) 0.55f else 1f; tag = "plugin-face:" + st.finish }
        // the top bar: slot, brand, name; power LED (bypass), PRESETS, remove
        val bar = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        bar.addView(TextView(c).apply { text = if (byp) "○" else "●"; textSize = 20f; setTextColor(if (byp) Color.argb(140, Color.red(st.ink), Color.green(st.ink), Color.blue(st.ink)) else Color.parseColor("#22C55E"))
            setPadding(0, 0, 18, 0); contentDescription = "power"; setOnClickListener { host.set("bypass", if (byp) 0.0 else 1.0); host.changed() } })
        bar.addView(LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            addView(TextView(c).apply { text = "AMF X1 · SLOT " + (slot + 1); setTextColor(Color.argb(150, Color.red(st.ink), Color.green(st.ink), Color.blue(st.ink))); textSize = 9f; letterSpacing = 0.2f; typeface = Typeface.MONOSPACE })
            addView(TextView(c).apply { text = name.uppercase(); setTextColor(st.ink); textSize = 16f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.06f }) })
        fun pill(t: String, f: () -> Unit) = bar.addView(TextView(c).apply { text = t; setTextColor(st.ink); textSize = 11f; typeface = Typeface.DEFAULT_BOLD; setPadding(20, 10, 20, 10); setOnClickListener { f() }
            background = GradientDrawable().apply { cornerRadius = 30f; setStroke(2, Color.argb(130, Color.red(st.ink), Color.green(st.ink), Color.blue(st.ink))) }
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(10, 0, 0, 0) } })
        val params = (json["params"] as? List<Map<String, Any?>>).orEmpty()
        val values = LinkedHashMap<String, Double>().apply { put("mix", (json["mix"] as? Number)?.toDouble() ?: 1.0); params.forEach { put(it["id"].toString(), (it["value"] as? Number)?.toDouble() ?: 0.0) } }
        pill(c.getString(R.string.fx_presets)) { presetMenu(c, kind, params, values, host) }
        pill("✕") { host.remove(); host.changed() }
        face.addView(bar)
        // the knobs: MIX first, then every parameter
        val grid = GridLayout(c).apply { columnCount = 4; setPadding(0, 16, 0, 0) }
        val knobs = listOf(mapOf("id" to "mix", "title" to "MIX", "value" to values["mix"], "default" to 1.0, "text" to String.format("%.0f %%", (values["mix"] ?: 1.0) * 100))) + params
        for (k in knobs) {
            val id = k["id"].toString(); val v = (k["value"] as? Number)?.toDouble() ?: 0.0; val def = (k["default"] as? Number)?.toDouble() ?: v
            val cell = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL; setPadding(4, 6, 4, 6)
                layoutParams = GridLayout.LayoutParams(GridLayout.spec(GridLayout.UNDEFINED), GridLayout.spec(GridLayout.UNDEFINED, 1f)).apply { width = 0 } }
            val readout = TextView(c).apply { text = k["text"]?.toString() ?: String.format("%.2f", v); setTextColor(st.accent); textSize = 10f; typeface = Typeface.MONOSPACE; gravity = Gravity.CENTER }
            cell.addView(RotaryKnob(c, st.accent, st.ink).apply { value = v.toFloat(); default = def.toFloat(); tag = "knob:$id"
                layoutParams = LinearLayout.LayoutParams(150, 150); onChange = { nv -> host.set(id, nv.toDouble()); readout.text = if (id == "mix") String.format("%.0f %%", nv * 100) else host.text(id) ?: String.format("%.0f %%", nv * 100) } })
            cell.addView(TextView(c).apply { text = k["title"].toString().uppercase(); setTextColor(st.ink); textSize = 9f; typeface = Typeface.DEFAULT_BOLD; gravity = Gravity.CENTER; letterSpacing = 0.1f; maxLines = 1 })
            cell.addView(readout); grid.addView(cell)
        }
        face.addView(grid)
        return face
    }

    private fun presetMenu(c: Context, kind: String, params: List<Map<String, Any?>>, values: Map<String, Double>, host: Host) {
        val mine = presets(store(c).getString("all", null), kind)
        val items = listOf(c.getString(R.string.fx_preset_init)) + mine.keys.map { "▸ $it" } + listOf(c.getString(R.string.fx_preset_save)) + (if (mine.isNotEmpty()) listOf(c.getString(R.string.fx_preset_delete)) else emptyList())
        android.app.AlertDialog.Builder(c).setTitle(R.string.fx_presets).setItems(items.toTypedArray()) { _, w ->
            when {
                w == 0 -> { params.forEach { p -> host.set(p["id"].toString(), (p["default"] as? Number)?.toDouble() ?: 0.5) }; host.set("mix", 1.0); host.changed() }   // INIT: every knob to its default
                w <= mine.size -> { mine.values.elementAt(w - 1).forEach { (id, v) -> host.set(id, v) }; host.changed() }
                items[w] == c.getString(R.string.fx_preset_save) -> { val e = android.widget.EditText(c).apply { setText(c.getString(R.string.fx_preset_n, mine.size + 1)); setSingleLine() }
                    android.app.AlertDialog.Builder(c).setTitle(R.string.fx_preset_save).setView(e).setPositiveButton(android.R.string.ok) { _, _ ->
                        store(c).edit().putString("all", withPreset(store(c).getString("all", null), kind, e.text.toString(), values)).apply() }.setNegativeButton(android.R.string.cancel, null).show() }
                else -> android.app.AlertDialog.Builder(c).setTitle(R.string.fx_preset_delete).setItems(mine.keys.toTypedArray()) { _, d ->
                    store(c).edit().putString("all", withPreset(store(c).getString("all", null), kind, mine.keys.elementAt(d), null)).apply() }.show()
            } }.show()
    }
}

/** a plug-in knob: value 0..1, drag up / right to raise (fine with two fingers' patience: 300 px for the whole travel), a double tap
 *  returns it to the effect's default */
class RotaryKnob(c: Context, private val accent: Int, private val ink: Int) : View(c) {
    var value = 0f; set(v) { field = v.coerceIn(0f, 1f); invalidate() }
    var default = 0.5f
    var onChange: ((Float) -> Unit)? = null
    private val p = Paint(Paint.ANTI_ALIAS_FLAG); private var downY = 0f; private var downX = 0f; private var downV = 0f; private var lastTap = 0L
    override fun onDraw(cv: Canvas) {
        val s = minOf(width, height).toFloat(); val cx = width / 2f; val cy = height / 2f; val r = s * 0.40f
        val arc = RectF(cx - r, cy - r, cx + r, cy + r)
        p.style = Paint.Style.STROKE; p.strokeCap = Paint.Cap.ROUND; p.strokeWidth = s * 0.07f
        p.color = Color.argb(70, Color.red(ink), Color.green(ink), Color.blue(ink)); cv.drawArc(arc, 135f, 270f, false, p)
        p.color = accent; cv.drawArc(arc, 135f, 270f * value, false, p)
        p.style = Paint.Style.FILL; p.shader = LinearGradient(0f, cy - r, 0f, cy + r, Color.parseColor("#4B5563"), Color.parseColor("#111827"), Shader.TileMode.CLAMP)
        cv.drawCircle(cx, cy, r * 0.74f, p); p.shader = null
        val a = Math.toRadians((135.0 + 270.0 * value)); p.color = Color.WHITE; p.strokeWidth = s * 0.05f; p.style = Paint.Style.STROKE
        cv.drawLine(cx + (r * 0.25f) * Math.cos(a).toFloat(), cy + (r * 0.25f) * Math.sin(a).toFloat(), cx + (r * 0.66f) * Math.cos(a).toFloat(), cy + (r * 0.66f) * Math.sin(a).toFloat(), p)
        p.style = Paint.Style.FILL
    }
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { parent?.requestDisallowInterceptTouchEvent(true); downY = e.y; downX = e.x; downV = value
                val now = System.currentTimeMillis(); if (now - lastTap < 300) { value = default; onChange?.invoke(value) }; lastTap = now }
            MotionEvent.ACTION_MOVE -> { val nv = (downV + ((downY - e.y) + (e.x - downX)) / 300f).coerceIn(0f, 1f); if (nv != value) { value = nv; onChange?.invoke(nv) } }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> { parent?.requestDisallowInterceptTouchEvent(false); performClick() }
        }
        return true
    }
    override fun performClick(): Boolean { super.performClick(); return true }
}
