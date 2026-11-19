package com.amfb.amixos

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View
import com.amfb.amixos.remote.DaemonClient

/** A console fader: a vertical track with the dB scale (+10 .. -∞, the console's fader law), a cap you drag, and the channel's meter
 *  beside it (green / yellow / red, with a peak hold). progress 0..1000 is the fader position; level 0..1000 the meter.
 *  Drag moves relative to where the finger went down (no jump); a double tap puts the fader at 0 dB. */
class FaderView(ctx: Context, private val accent: Int = 0xFFFFB590.toInt()) : View(ctx) {
    var progress = 750; set(v) { field = v.coerceIn(0, 1000); invalidate() }
    var level = 0; set(v) { field = v.coerceIn(0, 1000); if (v >= hold) { hold = v; holdAt = System.currentTimeMillis() }; invalidate() }
    var clip = false; set(v) { field = v; invalidate() }
    var onMove: ((Int) -> Unit)? = null
    var onTouchState: ((Boolean) -> Unit)? = null
    private var hold = 0; private var holdAt = 0L
    private val track = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF05080C.toInt() }
    private val tick = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF3A4656.toInt(); strokeWidth = 2f }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFFC2C7CF.toInt(); textSize = 22f; typeface = Typeface.MONOSPACE; textAlign = Paint.Align.RIGHT }
    private val cap = Paint(Paint.ANTI_ALIAS_FLAG); private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { strokeWidth = 4f }
    private val meter = Paint(); private val holdP = Paint().apply { color = 0xFFFFFFFF.toInt() }; private val clipP = Paint().apply { color = 0xFFEF4444.toInt() }
    private val capH = 64f; private val pad = 40f

    private val capRect = RectF(); private val topRect = RectF(); private val trackRect = RectF(); private val meterRect = RectF()
    override fun onSizeChanged(w: Int, h: Int, ow: Int, oh: Int) {   // the meter's colours depend on the height only: made once per size, not on every frame
        meter.shader = LinearGradient(0f, h - pad, 0f, pad, intArrayOf(0xFF16A34A.toInt(), 0xFF16A34A.toInt(), 0xFFEAB308.toInt(), 0xFFEF4444.toInt()), floatArrayOf(0f, 0.7f, 0.86f, 1f), Shader.TileMode.CLAMP)
    }
    private fun yOf(p: Int) = pad + (1 - p / 1000f) * (height - 2 * pad)
    override fun onMeasure(w: Int, h: Int) { setMeasuredDimension(resolveSize(150, w), resolveSize(620, h)) }
    override fun onDraw(c: Canvas) {
        val w = width.toFloat(); val tx = w * 0.42f; val mx = w * 0.74f; val mw = w * 0.12f
        trackRect.set(tx - 6, pad, tx + 6, height - pad); c.drawRoundRect(trackRect, 6f, 6f, track)
        for (db in listOf(10.0, 5.0, 0.0, -5.0, -10.0, -20.0, -30.0, -40.0, -60.0, -120.0)) {
            val y = yOf((DaemonClient.dbToPos(db) * 1000).toInt()); c.drawLine(tx - 22, y, tx - 10, y, tick)
            if (db == 10.0 || db == 0.0 || db == -10.0 || db == -20.0 || db == -40.0 || db == -120.0) c.drawText(if (db <= -120) "∞" else (if (db > 0) "+" else "") + db.toInt(), tx - 26, y + 8, text)
        }
        // the meter: an LED ladder, 0..1000 of the meter range, green / amber / red by height
        meterRect.set(mx - 3, pad - 3, mx + mw + 3, height - pad + 3); c.drawRoundRect(meterRect, 5f, 5f, track)
        val top = pad + (1 - level / 1000f) * (height - 2 * pad); val seg = 9f; var yy = height - pad
        while (yy - seg + 2 >= pad) { if (yy <= top) break; c.drawRect(mx, maxOf(top, yy - seg + 2), mx + mw, yy, meter); yy -= seg }
        if (System.currentTimeMillis() - holdAt > 1500) hold = maxOf(level, hold - 8)
        val hy = pad + (1 - hold / 1000f) * (height - 2 * pad); if (hold > 0) c.drawRect(mx, hy - 2, mx + mw, hy + 2, holdP)
        clipP.color = if (clip) 0xFFEF4444.toInt() else 0xFF3A1D1D.toInt(); c.drawRoundRect(mx - 3, pad - 22, mx + mw + 3, pad - 8, 4f, 4f, clipP)
        // the cap
        val y = yOf(progress); val r = capRect.apply { set(tx - w * 0.3f, y - capH / 2, tx + w * 0.3f, y + capH / 2) }
        cap.color = 0xFF42474E.toInt(); c.drawRoundRect(r, 8f, 8f, cap); cap.color = 0xFF3B4658.toInt(); topRect.set(r.left, r.top, r.right, r.top + capH / 3); c.drawRoundRect(topRect, 8f, 8f, cap)
        cap.color = 0xFF272A2F.toInt(); for (k in listOf(-0.28f, 0.28f)) c.drawRect(r.left + 10, y + capH * k - 1.5f, r.right - 10, y + capH * k + 1.5f, cap)   // grip lines
        line.color = accent; c.drawLine(r.left + 6, y, r.right - 6, y, line)
    }
    private var downY = 0f; private var downP = 0; private var lastTap = 0L
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(e: MotionEvent): Boolean {
        when (e.actionMasked) {
            MotionEvent.ACTION_DOWN -> { parent?.requestDisallowInterceptTouchEvent(true); downY = e.y; downP = progress; onTouchState?.invoke(true)
                val now = System.currentTimeMillis(); if (now - lastTap < 300) { progress = 750; onMove?.invoke(progress) }; lastTap = now }
            MotionEvent.ACTION_MOVE -> { val p = downP + ((downY - e.y) / (height - 2 * pad) * 1000).toInt(); if (p.coerceIn(0, 1000) != progress) { progress = p; onMove?.invoke(progress) } }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> onTouchState?.invoke(false)
        }
        return true
    }
}
