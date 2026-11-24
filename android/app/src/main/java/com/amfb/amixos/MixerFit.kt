package com.amfb.amixos

import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView

/** FADERS FIT THE SCREEN: a strip's keys (FX, EQ, MUTE, SOLO ...) are sized from the height the desk really has, so the fader keeps at
 *  least [FADER_SHARE] of its strip on any phone, upright or on its side, and nothing is cut off at the bottom. */
object MixerFit {
    const val FADER_SHARE = 0.42
    const val MIN_KEY_DP = 22
    const val MAX_KEY_DP = 40
    const val LAND_KEY_DP = 30
    /** the height of one key (px) for a strip [stripPx] tall with [keys] keys and [fixedPx] of titles and readout */
    fun keyPx(stripPx: Int, keys: Int, fixedPx: Int, density: Float, maxDp: Int = MAX_KEY_DP): Int {
        if (keys <= 0) return 0
        val room = (stripPx * (1 - FADER_SHARE) - fixedPx).toInt() / keys
        return room.coerceIn((MIN_KEY_DP * density).toInt(), (maxDp * density).toInt())
    }
    /** channels across: upright as asked, on its side as many as fit at ~[stripDp] dp each (the rest by a swipe) */
    fun across(widthPx: Int, density: Float, land: Boolean, base: Int, stripDp: Int = 84): Int =
        if (!land) base else maxOf(base, ((widthPx / density - 140) / stripDp).toInt()).coerceAtMost(12)

    private fun isKey(v: View) = v is TextView && v !is android.widget.EditText && (v.layoutParams?.height ?: 0) > 0
    /** sizes the keys of every strip under [root] for the strips' real height; returns the number of strips fitted */
    fun fit(root: View, density: Float, land: Boolean = false): Int {   // on its side the keys stay smaller (LAND_KEY_DP): the height is short
        var n = 0
        fun walk(v: View) {
            if (v is LinearLayout && v.orientation == LinearLayout.VERTICAL && (0 until v.childCount).any { v.getChildAt(it) is FaderView } && v.height > 0) {
                val keys = ArrayList<View>(); var fixed = v.paddingTop + v.paddingBottom
                for (i in 0 until v.childCount) { val c = v.getChildAt(i); if (c.visibility == View.GONE || c is FaderView) continue
                    val lp = c.layoutParams as? ViewGroup.MarginLayoutParams; val m = (lp?.topMargin ?: 0) + (lp?.bottomMargin ?: 0)
                    if (isKey(c)) { keys += c; fixed += m } else if (c is LinearLayout && c.orientation == LinearLayout.HORIZONTAL && (0 until c.childCount).any { isKey(c.getChildAt(it)) }) { keys += c; fixed += m } else fixed += c.measuredHeight + m }
                val k = keyPx(v.height, keys.size, fixed, density, if (land) LAND_KEY_DP else MAX_KEY_DP)
                for (c in keys) { if (c is LinearLayout) { for (j in 0 until c.childCount) c.getChildAt(j).let { if (isKey(it)) it.layoutParams = it.layoutParams.apply { height = k } }; c.layoutParams = c.layoutParams.apply { height = k } }
                    else c.layoutParams = c.layoutParams.apply { height = k } }
                n++; return }
            if (v is ViewGroup) for (i in 0 until v.childCount) walk(v.getChildAt(i))
        }
        walk(root); if (n > 0) root.requestLayout(); return n
    }
}
