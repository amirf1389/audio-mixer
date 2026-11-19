package com.amfb.amixos

import android.content.Context
import android.content.res.Configuration
import androidx.appcompat.app.AppCompatActivity

/**
 * RESPONSIVE TEXT: every text size in the app follows the screen. A narrow phone gets slightly smaller text so labels fit,
 * a tablet gets larger text so it reads from a distance. The phone's own font-size setting still counts, inside safe limits.
 */
object TextFit {
    const val AUTO = 0; const val SMALL = 1; const val NORMAL = 2; const val LARGE = 3
    private val curve = listOf(320 to 0.85f, 360 to 0.92f, 392 to 1.0f, 480 to 1.0f, 600 to 1.15f, 720 to 1.3f)

    /** The share of the normal text size for a screen whose shorter side is [swDp] wide. */
    fun screenFactor(swDp: Int): Float {
        if (swDp <= curve.first().first) return curve.first().second
        for (i in 1 until curve.size) { val (x0, y0) = curve[i - 1]; val (x1, y1) = curve[i]
            if (swDp <= x1) return y0 + (y1 - y0) * (swDp - x0) / (x1 - x0).toFloat() }
        return curve.last().second
    }

    /** The font scale the app uses: the chosen mode, times the phone's own font setting, kept between 0.8 and 1.5. */
    fun fontScale(swDp: Int, systemScale: Float, mode: Int): Float {
        val f = when (mode) { SMALL -> 0.88f; NORMAL -> 1.0f; LARGE -> 1.15f; else -> screenFactor(swDp) }
        return (f * systemScale).coerceIn(0.8f, 1.5f)
    }

    fun mode(c: Context): Int = c.getSharedPreferences("remote", Context.MODE_PRIVATE).getInt("textSize", AUTO)

    fun wrap(base: Context): Context {
        val cur = base.resources.configuration
        val scale = fontScale(cur.smallestScreenWidthDp, cur.fontScale, mode(base))
        if (kotlin.math.abs(scale - cur.fontScale) < 0.005f) return base
        val cfg = Configuration(cur); cfg.fontScale = scale
        return base.createConfigurationContext(cfg)
    }
}

/** Every screen of the app starts from this: it applies the responsive text size before anything is drawn. */
open class FitActivity : AppCompatActivity() {
    override fun attachBaseContext(newBase: Context) { super.attachBaseContext(TextFit.wrap(newBase)) }
}
