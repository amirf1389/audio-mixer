package com.amfb.amixos

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.LayerDrawable
import android.view.Gravity
import android.view.MotionEvent
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

/** A PAN knob: -1 (left) .. 0 (centre) .. +1 (right), turned by dragging up / down (or sideways), centred by a double tap.
 *  The arc from the centre to the pointer is lit in the strip's colour; the position is printed under it (L40, C, R100). */
class KnobView(ctx: Context, private val accent: Int = 0xFFFFB590.toInt()) : View(ctx) {
    var value = 0f; set(v) { field = v.coerceIn(-1f, 1f); invalidate() }
    var onChange: ((Float) -> Unit)? = null
    /** a strip knob other than pan: its name above the cap, its value in its own words, where a double tap (and the snap) sends it */
    var title: String? = null; var fmt: ((Float) -> String)? = null; var home = 0f; var dim = false; set(v) { field = v; invalidate() }
    var onTouchState: ((Boolean) -> Unit)? = null
    private val p = Paint(Paint.ANTI_ALIAS_FLAG); private val arc = RectF()
    private var downX = 0f; private var downY = 0f; private var downV = 0f; private var lastTap = 0L
    companion object { fun label(v: Float): String = if (Math.abs(v) < 0.02f) "C" else if (v < 0) "L" + Math.round(-v * 100) else "R" + Math.round(v * 100) }
    override fun onMeasure(w: Int, h: Int) { val s = resolveSize(120, w); setMeasuredDimension(s, (s * (if (title != null) 1.18f else 1.02f)).toInt()) }
    override fun onDraw(c: Canvas) {
        val w = width.toFloat(); val r = w * 0.30f; val cx = w / 2; val cy = r + w * 0.10f + (if (title != null) w * 0.16f else 0f)
        // the scale: 270 degrees, from 135 (left) through 270 (top = centre) to 45 (right)
        arc.set(cx - r - 9, cy - r - 9, cx + r + 9, cy + r + 9); p.style = Paint.Style.STROKE; p.strokeCap = Paint.Cap.ROUND; p.strokeWidth = 6f
        p.color = 0xFF42474E.toInt(); c.drawArc(arc, 135f, 270f, false, p)
        title?.let { p.style = Paint.Style.FILL; p.color = 0xFFC2C7CF.toInt(); p.textSize = w * 0.15f; p.typeface = Typeface.DEFAULT_BOLD; p.textAlign = Paint.Align.CENTER; c.drawText(it, cx, w * 0.12f, p); p.style = Paint.Style.STROKE }
        p.color = if (dim) 0xFF5B6573.toInt() else accent; if (value >= 0) c.drawArc(arc, 270f, 135f * value, false, p) else c.drawArc(arc, 270f + 135f * value, -135f * value, false, p)
        p.style = Paint.Style.FILL; p.color = 0xFF10151D.toInt(); c.drawCircle(cx, cy + 3, r, p)          // the shadow under the cap
        p.color = 0xFF2B3443.toInt(); c.drawCircle(cx, cy, r, p); p.color = 0xFF3A4556.toInt(); c.drawCircle(cx, cy - r * 0.12f, r * 0.82f, p)
        val a = Math.toRadians((270 + 135.0 * value)); p.color = Color.WHITE; p.strokeWidth = 6f; p.style = Paint.Style.STROKE
        c.drawLine(cx + (Math.cos(a) * r * 0.35).toFloat(), cy + (Math.sin(a) * r * 0.35).toFloat(), cx + (Math.cos(a) * r * 0.9).toFloat(), cy + (Math.sin(a) * r * 0.9).toFloat(), p)
        p.style = Paint.Style.FILL; p.color = 0xFFC2C7CF.toInt(); p.textSize = w * 0.19f; p.typeface = Typeface.MONOSPACE; p.textAlign = Paint.Align.CENTER
        c.drawText(fmt?.invoke(value) ?: label(value), cx, height - w * 0.03f, p)
    }
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { parent?.requestDisallowInterceptTouchEvent(true); downX = e.x; downY = e.y; downV = value; onTouchState?.invoke(true)
                val now = System.currentTimeMillis(); if (now - lastTap < 300) { value = home; onChange?.invoke(home) }; lastTap = now }
            MotionEvent.ACTION_MOVE -> { val v = (downV + ((downY - e.y) + (e.x - downX)) / 220f).coerceIn(-1f, 1f); val snapped = if (Math.abs(v - home) < 0.04f) home else v
                if (snapped != value) { value = snapped; onChange?.invoke(snapped) } }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> onTouchState?.invoke(false)
        }
        return true
    }
}

/** A console pad button (MUTE, SOLO, FX, LIMIT ...): a dark key that lights in its colour when ON. */
class PadButton(ctx: Context, label: String, private val onColor: Int, private val textSizeSp: Float = 13f) : TextView(ctx) {
    var on = false; set(v) { field = v; paint() }
    init { text = label; gravity = Gravity.CENTER; typeface = Typeface.DEFAULT_BOLD; textSize = textSizeSp; isClickable = true; isFocusable = true; minHeight = 84; setPadding(10, 0, 10, 0); paint() }
    private fun paint() {
        val g = GradientDrawable().apply { setColor(if (on) onColor else 0xFF32353A.toInt()) }   // Material: a flat tonal key, the whole key in its colour when ON
        g.cornerRadius = 36f; background = g
        setTextColor(if (on) 0xFF111418.toInt() else 0xFFE1E2E8.toInt())
    }
}

/** The shared look of the two Android mixers (THIS PHONE and the console remote): coloured channel strips, the master strip, header chips. */
object ConsoleUi {
    val palette = intArrayOf(0xFFFFB590.toInt(), 0xFF22D3EE.toInt(), 0xFFA78BFA.toInt(), 0xFF34D399.toInt(), 0xFFFBBF24.toInt(), 0xFFF472B6.toInt())
    const val RED = 0xFFEF4444.toInt(); const val AMBER = 0xFFFBBF24.toInt(); const val GREEN = 0xFF34D399.toInt(); const val CYAN = 0xFF7FD8E8.toInt(); const val VIOLET = 0xFFA78BFA.toInt()
    fun mix(a: Int, b: Int, t: Float): Int = Color.rgb((Color.red(a) + (Color.red(b) - Color.red(a)) * t).toInt(), (Color.green(a) + (Color.green(b) - Color.green(a)) * t).toInt(), (Color.blue(a) + (Color.blue(b) - Color.blue(a)) * t).toInt())
    /** a strip's panel: a dark body faintly tinted with its colour, rounded, with the colour as a bar across the top */
    fun stripBackground(color: Int, master: Boolean = false): LayerDrawable {
        val base = 0xFF1D2024.toInt()
        val body = GradientDrawable(GradientDrawable.Orientation.TOP_BOTTOM, intArrayOf(mix(base, color, if (master) 0.22f else 0.13f), mix(base, color, 0.04f))).apply { cornerRadius = 32f }
        val bar = GradientDrawable().apply { setColor(color); cornerRadii = floatArrayOf(32f, 32f, 32f, 32f, 0f, 0f, 0f, 0f) }
        return LayerDrawable(arrayOf(body, bar)).apply { setLayerGravity(1, Gravity.TOP); setLayerHeight(1, 10) }
    }
    fun strip(ctx: Context, color: Int, widthPx: Int, master: Boolean = false): LinearLayout = LinearLayout(ctx).apply {
        orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER_HORIZONTAL; background = stripBackground(color, master); setPadding(8, 22, 8, 10)
        layoutParams = LinearLayout.LayoutParams(widthPx, LinearLayout.LayoutParams.MATCH_PARENT).apply { setMargins(4, 0, 4, 0) } }
    /** the strip's number and name: "01" small over the name (the name alone is not repeated when it is just "CH 1") */
    fun title(id: Int, name: String): String { val n = name.trim(); return if (n.isEmpty() || n.equals("CH " + (id + 1), true) || n.equals(String.format("CH %02d", id + 1), true)) String.format("CH %02d", id + 1) else String.format("%02d\n%s", id + 1, n) }
    /** a small header key (FIND, BUSES, REC ...) */
    fun chip(ctx: Context, label: String, color: Int = 0xFFD5DCE6.toInt()): TextView = TextView(ctx).apply {
        text = label; setTextColor(color); textSize = 11f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.06f; gravity = Gravity.CENTER; isClickable = true; isFocusable = true; setPadding(26, 0, 26, 0); minHeight = 76
        background = GradientDrawable().apply { setColor(android.graphics.Color.TRANSPARENT); cornerRadius = 22f; setStroke(2, 0xFF8C9199.toInt()) }
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, 76).apply { setMargins(0, 0, 10, 0) } }
    /** a ToggleButton as a console key (M, S): dark when off, lit in its colour when checked */
    fun styleToggle(t: android.widget.ToggleButton, onColor: Int) {
        fun shape(fill: Int, stroke: Int) = GradientDrawable().apply { setColor(fill); cornerRadius = 14f; setStroke(2, stroke) }
        t.background = android.graphics.drawable.StateListDrawable().apply {
            addState(intArrayOf(android.R.attr.state_checked), shape(mix(0xFF272A2F.toInt(), onColor, 0.38f), onColor)); addState(intArrayOf(), shape(0xFF272A2F.toInt(), 0xFF42474E.toInt())) }
        t.typeface = Typeface.DEFAULT_BOLD
    }
    fun setChipEnabled(v: TextView, enabled: Boolean) { v.isEnabled = enabled; v.alpha = if (enabled) 1f else 0.4f }
    fun dbReadout(ctx: Context): TextView = TextView(ctx).apply { setTextColor(CYAN); typeface = Typeface.MONOSPACE; gravity = Gravity.CENTER; textSize = 12f
        background = GradientDrawable().apply { setColor(0xFF05080C.toInt()); cornerRadius = 8f }; setPadding(0, 6, 0, 6)
        layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT).apply { setMargins(6, 4, 6, 0) } }
}
