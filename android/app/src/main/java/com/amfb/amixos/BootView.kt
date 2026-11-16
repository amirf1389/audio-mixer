package com.amfb.amixos

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View
import android.view.animation.LinearInterpolator
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/** THE BOOT ANIMATION (about 1.5 s, then HOME fades in): the app icon's three faders build themselves. The caps rise into place with a
 *  small overshoot, their lit tracks fill in violet, pink and orange, the "mix rising" line draws through the caps, AMF X1 fades in,
 *  and a row of level meters rises and settles. A tap skips it. Every frame is drawn from one number, [t] (0..1), so a test can draw
 *  any moment of it. */
class BootView(context: Context, private val done: () -> Unit = {}) : View(context) {
    /** the moment shown, 0 (start) .. 1 (end, fully faded out) */
    var t = 0f; set(v) { field = v.coerceIn(0f, 1f); invalidate() }
    private var anim: ValueAnimator? = null; private var finished = false
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val bg = Color.parseColor("#0F141C")
    // the icon's colours: caps white, white, amber; tracks violet, pink, orange (top -> bottom of each gradient)
    private val trackTop = intArrayOf(Color.parseColor("#C4B5FD"), Color.parseColor("#F9A8D4"), Color.parseColor("#FCD34D"))
    private val trackBot = intArrayOf(Color.parseColor("#7C3AED"), Color.parseColor("#DB2777"), Color.parseColor("#F97316"))
    private val capCol = intArrayOf(Color.parseColor("#F1F5F9"), Color.parseColor("#F1F5F9"), Color.parseColor("#FBBF24"))
    private val capY = floatArrayOf(62.2f, 50.2f, 40.2f)   // the caps' centres in the icon's 108-unit canvas
    private val colX = floatArrayOf(40f, 54f, 68f)

    fun start(durationMs: Long = 1500) {
        anim?.cancel()
        anim = ValueAnimator.ofFloat(0f, 1f).apply { duration = durationMs; interpolator = LinearInterpolator()
            addUpdateListener { t = it.animatedValue as Float; if (t >= 1f) finish() }; start() }
    }
    fun skip() { anim?.cancel(); t = 1f; finish() }
    private fun finish() { if (finished) return; finished = true; done() }
    override fun onTouchEvent(e: MotionEvent): Boolean { if (e.action == MotionEvent.ACTION_UP) { performClick(); skip() }; return true }
    override fun performClick(): Boolean { super.performClick(); return true }
    override fun onDetachedFromWindow() { anim?.cancel(); super.onDetachedFromWindow() }

    companion object {
        /** 0..1 of a stage that runs from [a] to [b] of the whole animation */
        fun stage(t: Float, a: Float, b: Float) = ((t - a) / (b - a)).coerceIn(0f, 1f)
        /** ease out with a small overshoot (a fader cap that lands and settles) */
        fun overshoot(x: Float): Float { val c = 1.6f; val y = x - 1f; return 1f + (c + 1f) * y * y * y + c * y * y }
        fun easeOut(x: Float) = 1f - (1f - x) * (1f - x) * (1f - x)
        /** how visible the whole animation is: it fades out over the last 18 % */
        fun alphaAt(t: Float) = 1f - stage(t, 0.82f, 1f)
    }

    override fun onDraw(c: Canvas) {
        val w = width.toFloat(); val h = height.toFloat(); if (w <= 0 || h <= 0) return
        val a = alphaAt(t); c.drawColor(Color.argb((a * 255).toInt(), Color.red(bg), Color.green(bg), Color.blue(bg)))
        if (a <= 0f) return
        val s = min(w, h) * 0.62f / 54f; val ox = w / 2 - 54f * s; val oy = h * 0.40f - 54f * s   // the icon's 108 units, centred above the middle
        fun X(u: Float) = ox + u * s
        fun Y(u: Float) = oy + u * s
        val alpha = (a * 255).toInt()
        for (i in 0..2) {
            val k = stage(t, 0.04f + i * 0.08f, 0.40f + i * 0.08f); if (k <= 0f) continue
            val cy = 76f - (76f - capY[i]) * overshoot(k)   // the cap travels up from the bottom of its track
            // the faint track above, the lit track below the cap
            p.shader = null; p.color = Color.argb((0.2f * alpha).toInt(), 255, 255, 255)
            c.drawRoundRect(RectF(X(colX[i] - 1.5f), Y(33f), X(colX[i] + 1.5f), Y(cy)), 1.5f * s, 1.5f * s, p)
            p.shader = LinearGradient(0f, Y(cy), 0f, Y(75f), trackTop[i], trackBot[i], Shader.TileMode.CLAMP); p.alpha = alpha
            c.drawRoundRect(RectF(X(colX[i] - 1.5f), Y(cy), X(colX[i] + 1.5f), Y(75f)), 1.5f * s, 1.5f * s, p); p.shader = null
            p.color = capCol[i]; p.alpha = alpha
            c.drawRoundRect(RectF(X(colX[i] - 8.3f), Y(cy - 3f), X(colX[i] + 8.3f), Y(cy + 3f)), 1.8f * s, 1.8f * s, p)
            p.color = Color.parseColor("#1E293B"); p.alpha = alpha
            c.drawRect(X(colX[i] - 4f), Y(cy - 0.5f), X(colX[i] + 4f), Y(cy + 0.5f), p)
        }
        // the line through the caps, drawn left to right
        val l = stage(t, 0.50f, 0.66f)
        if (l > 0f) { p.color = Color.argb((0.35f * alpha).toInt(), 255, 255, 255); p.strokeWidth = 1.2f * s; p.strokeCap = Paint.Cap.ROUND; p.style = Paint.Style.STROKE
            val seg = l * 2f
            c.drawLine(X(colX[0]), Y(capY[0]), X(colX[0] + (colX[1] - colX[0]) * min(1f, seg)), Y(capY[0] + (capY[1] - capY[0]) * min(1f, seg)), p)
            if (seg > 1f) c.drawLine(X(colX[1]), Y(capY[1]), X(colX[1] + (colX[2] - colX[1]) * (seg - 1f)), Y(capY[1] + (capY[2] - capY[1]) * (seg - 1f)), p)
            p.style = Paint.Style.FILL }
        // AMF X1 and the maker
        val tx = easeOut(stage(t, 0.46f, 0.70f))
        if (tx > 0f) { p.color = Color.parseColor("#EA580C"); p.alpha = (tx * alpha).toInt(); p.textAlign = Paint.Align.CENTER; p.typeface = Typeface.DEFAULT_BOLD
            p.textSize = 0.11f * min(w, h); p.letterSpacing = 0.18f + (1f - tx) * 0.25f; c.drawText("AMF X1", w / 2, Y(92f) + p.textSize * 0.4f, p)
            p.color = Color.parseColor("#8B98A8"); p.alpha = (tx * alpha).toInt(); p.textSize = 0.035f * min(w, h); p.letterSpacing = 0.3f; p.typeface = Typeface.MONOSPACE
            c.drawText("BY AMFB AUDIO", w / 2, Y(92f) + 0.11f * min(w, h) * 0.4f + p.textSize * 2.2f, p); p.letterSpacing = 0f }
        // the meter row: each bar rises, peaks and settles at its own level
        val m = stage(t, 0.30f, 0.86f)
        if (m > 0f) { val n = 16; val bw = w * 0.6f / n; val base = h * 0.86f; val top = h * 0.08f
            for (i in 0 until n) { val ph = m * 6f - i * 0.18f; if (ph <= 0f) continue
                val settle = 0.35f + 0.45f * ((i * 37) % 11) / 11f; val lvl = settle + (1f - settle) * max(0f, sin(min(ph, 3.14f).toDouble()).toFloat()) * (1f - m)
                val bx = w * 0.2f + i * bw; val bh = top * lvl
                p.color = if (lvl > 0.85f) Color.parseColor("#EF4444") else if (lvl > 0.65f) Color.parseColor("#FBBF24") else Color.parseColor("#22D3EE"); p.alpha = (0.85f * alpha).toInt()
                c.drawRoundRect(RectF(bx + bw * 0.18f, base - bh, bx + bw * 0.82f, base), bw * 0.15f, bw * 0.15f, p) } }
    }
}
