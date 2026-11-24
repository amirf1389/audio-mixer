package com.amfb.amixos

import android.app.Activity
import android.content.Context
import android.graphics.Typeface
import android.view.Gravity
import android.widget.LinearLayout
import android.widget.TextView

/** QUICK MASTER on every screen: a thin row over the bottom bar with the phone mixer's master level (− / +, 1 dB a tap) and MUTE.
 *  It writes the phone mixer's own settings; when the mixer is open its engine follows at once (the [listener] it sets). */
object QuickMaster {
    const val STEP_DB = 1.0
    @Volatile var listener: (() -> Unit)? = null
    private val views = java.util.WeakHashMap<TextView, Boolean>()
    fun db(c: Context) = Prefs.local(c).getFloat("masterDb", 0f).toDouble()
    fun muted(c: Context) = Prefs.local(c).getBoolean("masterMute", false)
    fun step(c: Context, dir: Int) { val n = (db(c) + dir * STEP_DB).coerceIn(-60.0, 10.0); Prefs.local(c).edit().putFloat("masterDb", n.toFloat()).commit(); listener?.invoke(); changed(c) }
    fun toggleMute(c: Context) { Prefs.local(c).edit().putBoolean("masterMute", !muted(c)).commit(); listener?.invoke(); changed(c) }
    fun label(c: Context) = c.getString(if (muted(c)) R.string.qm_muted else R.string.qm_level, db(c))
    fun changed(c: Context) { for (v in views.keys.toList()) v.text = label(c) }
    fun bar(a: Activity): LinearLayout {
        val d = a.resources.displayMetrics.density; fun px(v: Int) = (v * d).toInt()
        val row = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; tag = "quickMaster"; setBackgroundColor(a.getColor(R.color.md_surface_container)); setPadding(px(12), px(4), px(12), 0) }
        val readout = TextView(a).apply { tag = "qmLevel"; textSize = 13f; typeface = Typeface.MONOSPACE; setTextColor(a.getColor(R.color.md_on_surface)); text = label(a); layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f) }
        views[readout] = true
        fun key(t: String, tg: String, click: () -> Unit) = TextView(a).apply { text = t; tag = tg; textSize = 14f; gravity = Gravity.CENTER; setTextColor(a.getColor(R.color.md_on_surface)); isClickable = true; contentDescription = tg
            background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = px(16).toFloat(); setStroke(px(1), a.getColor(R.color.md_on_surface_variant)) }
            layoutParams = LinearLayout.LayoutParams(px(if (t.length > 2) 64 else 44), px(32)).apply { setMargins(px(6), 0, 0, 0) }; setOnClickListener { click() } }
        row.addView(readout)
        row.addView(key("−", "qmDown") { step(a, -1) }); row.addView(key("+", "qmUp") { step(a, 1) }); row.addView(key(a.getString(R.string.lm_mute), "qmMute") { toggleMute(a) })
        return row
    }
}
