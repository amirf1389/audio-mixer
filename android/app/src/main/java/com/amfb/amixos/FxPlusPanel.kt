package com.amfb.amixos

import android.app.AlertDialog
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.View
import android.widget.GridLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/** the FX+ panel: one plug-in face per module (graphite, like the rack's dynamics) with a power LED, a knob grid and a gain-reduction
 *  meter; A / B, copy and an EQ shortcut on top. [gr] is asked for the reductions now and answers on the main thread. */
object FxPlusPanel {
    fun view(c: Context, title: String, model: FxPlusModel, onEq: (() -> Unit)?, meters: HashMap<Int, GrMeter> = HashMap()): View {
        val st = PluginFace.styleOf("comp", "dynamics")
        val root = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; setPadding(20, 16, 20, 16); setBackgroundColor(Color.parseColor("#0B0E12")); tag = "fxplus-panel" }
        val top = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        top.addView(TextView(c).apply { text = title; setTextColor(Color.WHITE); textSize = 15f; typeface = Typeface.DEFAULT_BOLD; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
        val body = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL }
        fun pill(t: String, desc: String, f: (TextView) -> Unit) = TextView(c).apply { text = t; contentDescription = desc; setTextColor(st.ink); textSize = 11f; typeface = Typeface.DEFAULT_BOLD; setPadding(22, 12, 22, 12)
            background = GradientDrawable().apply { cornerRadius = 30f; setStroke(2, st.accent) }; setOnClickListener { f(this) }
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(10, 0, 0, 0) } }
        fun abText() = c.getString(if (model.showingB) R.string.fxp_b else R.string.fxp_a)
        lateinit var rebuild: () -> Unit
        top.addView(pill(abText(), "fxpAB") { model.swapAB(); it.text = abText(); rebuild() })
        top.addView(pill(c.getString(R.string.fxp_copy), "fxpCopy") { model.copyToOther() })
        if (onEq != null) top.addView(pill(c.getString(R.string.fxp_eq), "fxpEq") { onEq() })
        root.addView(top)
        rebuild = { body.removeAllViews(); meters.clear(); FxPlus.modules(model.master).forEachIndexed { k, m -> body.addView(face(c, m, model, st, rebuild).also { f -> meters[k] = f.findViewWithTag("gr:" + m.id) }) } }
        rebuild()
        root.addView(body)
        return ScrollView(c).apply { addView(root) }
    }

    private fun face(c: Context, m: FxPlus.Module, model: FxPlusModel, st: PluginFace.Style, rebuild: () -> Unit): View {
        val on = model.get(m.onIndex) != 0.0
        val face = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; setPadding(24, 16, 24, 18); tag = "fxp-face:" + m.id
            background = GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, intArrayOf(st.top, st.bottom)).apply { cornerRadius = 22f; setStroke(3, Color.argb(90, 255, 255, 255)) }
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(0, 12, 0, 6) } }
        val bar = LinearLayout(c).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
        bar.addView(TextView(c).apply { text = if (on) "●" else "○"; textSize = 20f; contentDescription = "fxpPower:" + m.id; setTextColor(if (on) Color.parseColor("#22C55E") else Color.GRAY); setPadding(0, 0, 18, 0)
            setOnClickListener { model.toggle(m); rebuild() } })
        bar.addView(TextView(c).apply { text = c.getString(m.title); setTextColor(st.ink); textSize = 13f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.08f; layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) })
        bar.addView(TextView(c).apply { text = c.getString(R.string.fxp_reset); setTextColor(st.ink); textSize = 10f; setPadding(18, 8, 18, 8); contentDescription = "fxpReset:" + m.id
            background = GradientDrawable().apply { cornerRadius = 26f; setStroke(2, Color.argb(130, 255, 255, 255)) }; setOnClickListener { model.reset(m); rebuild() } })
        face.addView(bar)
        face.addView(GrMeter(c, st.accent).apply { tag = "gr:" + m.id; layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 34).apply { setMargins(0, 12, 0, 4) } })
        val grid = GridLayout(c).apply { columnCount = 3; setPadding(0, 8, 0, 0); alpha = if (on) 1f else 0.5f }
        for (s in m.params) {
            val cell = LinearLayout(c).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL; setPadding(4, 6, 4, 6)
                layoutParams = GridLayout.LayoutParams(GridLayout.spec(GridLayout.UNDEFINED), GridLayout.spec(GridLayout.UNDEFINED, 1f)).apply { width = 0 } }
            val readout = TextView(c).apply { text = FxPlus.fmt(s, model.get(s.index)); setTextColor(st.accent); textSize = 10f; typeface = Typeface.MONOSPACE; gravity = Gravity.CENTER }
            cell.addView(RotaryKnob(c, st.accent, st.ink).apply { value = FxPlus.toKnob(s, model.get(s.index)).toFloat(); default = FxPlus.toKnob(s, s.def).toFloat(); tag = "fxp:" + m.id + ":" + s.index
                layoutParams = LinearLayout.LayoutParams(140, 140); onChange = { k -> val v = FxPlus.fromKnob(s, k.toDouble()); model.set(s.index, v); readout.text = FxPlus.fmt(s, model.get(s.index)) } })
            cell.addView(TextView(c).apply { text = c.getString(s.title); setTextColor(st.ink); textSize = 9f; typeface = Typeface.DEFAULT_BOLD; gravity = Gravity.CENTER; letterSpacing = 0.1f; maxLines = 1 })
            cell.addView(readout); grid.addView(cell)
        }
        face.addView(grid)
        return face
    }

    /** the panel in a dialog, its meters polled while it is open; the dialog (to dismiss with the activity) */
    fun show(c: Context, title: String, model: FxPlusModel, onEq: (() -> Unit)?, gr: (((DoubleArray) -> Unit) -> Unit)?): AlertDialog {
        val meters = HashMap<Int, GrMeter>(); val v = view(c, title, model, onEq, meters)
        val h = Handler(Looper.getMainLooper()); var open = true
        val idx = FxPlus.modules(model.master).map { FxPlus.MODULES.indexOf(it) }
        val poll = object : Runnable { override fun run() { if (!open) return
            gr?.invoke { r -> if (open) idx.forEachIndexed { k, mi -> meters[k]?.db = r.getOrElse(mi) { 0.0 } } }
            h.postDelayed(this, 120) } }
        val d = AlertDialog.Builder(c).setView(v).setPositiveButton(android.R.string.ok, null).create()
        d.setOnDismissListener { open = false; h.removeCallbacksAndMessages(null) }
        d.show(); h.post(poll); return d
    }
}

/** a gain-reduction meter: 0 dB on the right, down to -24 dB to the left, with the value written on it */
class GrMeter(c: Context, private val accent: Int = Color.parseColor("#22D3EE")) : View(c) {
    var db = 0.0; set(v) { val x = if (v.isFinite()) v.coerceIn(-60.0, 0.0) else 0.0; if (x != field) { field = x; invalidate() } }
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    override fun onDraw(cv: Canvas) {
        val w = width.toFloat(); val h = height.toFloat(); p.style = Paint.Style.FILL
        p.color = Color.argb(60, 255, 255, 255); cv.drawRoundRect(0f, 0f, w, h, 8f, 8f, p)
        val f = (minOf(24.0, -db) / 24.0).toFloat(); p.color = accent; cv.drawRoundRect(w * (1 - f), 0f, w, h, 8f, 8f, p)
        p.color = Color.WHITE; p.textSize = h * 0.62f; p.typeface = Typeface.MONOSPACE
        cv.drawText(String.format("GR %.1f dB", db), 10f, h * 0.74f, p)
    }
}
