package com.amfb.amixos

import android.app.Activity
import android.content.Intent
import android.graphics.Color
import android.graphics.Typeface
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

/** The bar along the bottom of every main screen: HOME · MIXER · TOOLS · SETTINGS. Going to a screen brings the open one to the front
 *  (the remote keeps its connection) instead of stacking a new copy. */
object Nav {
    /** pages switch without the system's slide (Android 14+ has its own call for it; the old one is kept for older phones) */
    @Suppress("DEPRECATION")
    private fun noAnimation(a: android.app.Activity) {
        if (android.os.Build.VERSION.SDK_INT >= 34) a.overrideActivityTransition(android.app.Activity.OVERRIDE_TRANSITION_OPEN, 0, 0) else a.overridePendingTransition(0, 0) }
    enum class Tab(val label: Int, val glyph: String, val target: Class<out Activity>) {
        HOME(R.string.nav_home, "⌂", HomeActivity::class.java), MIXER(R.string.nav_mixer, "☰", RemoteActivity::class.java),
        TOOLS(R.string.nav_tools, "♪", ToolsActivity::class.java), SETTINGS(R.string.nav_settings, "⚙", SettingsActivity::class.java)
    }
    // DUAL MODE MIXER: one MIXER with two modes, THIS PHONE (the standalone mixer: the phone's own sound) and CONSOLE (the remote of the
    // Windows console). The MIXER tab opens the mode used last; the bar at the top of each switches to the other.
    fun phoneMode(a: android.content.Context) = Prefs.remote(a).getString("mixerMode", "console") == "phone"
    fun mixerTarget(a: Activity): Class<out Activity> = if (phoneMode(a)) LocalMixerActivity::class.java else RemoteActivity::class.java
    /** the two-mode bar for the top of a mixer screen; [phone] = this screen is the phone's own mixer */
    fun modeBar(a: Activity, phone: Boolean): View {
        Prefs.remote(a).edit().putString("mixerMode", if (phone) "phone" else "console").apply()
        val bar = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 12); contentDescription = "mixerMode" }
        for ((isPhone, label) in listOf(true to a.getString(R.string.mode_phone), false to a.getString(R.string.mode_console))) {
            val on = isPhone == phone
            bar.addView(TextView(a).apply { text = label; gravity = Gravity.CENTER; textSize = 12f; letterSpacing = 0.1f; typeface = Typeface.DEFAULT_BOLD; setPadding(0, 18, 0, 18)
                setTextColor(if (on) Color.BLACK else a.getColor(R.color.dim)); tag = if (isPhone) "mode_phone" else "mode_console"
                background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = 22f; setColor(if (on) a.getColor(R.color.accent) else Color.parseColor("#141B26")); setStroke(2, Color.parseColor("#263041")) }
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(if (isPhone) 0 else 6, 0, if (isPhone) 6 else 0, 0) }
                isClickable = true; setOnClickListener { if (!on) { Prefs.remote(a).edit().putString("mixerMode", if (isPhone) "phone" else "console").apply(); go(a, if (isPhone) LocalMixerActivity::class.java else RemoteActivity::class.java) } } })
        }
        return bar
    }
    fun go(a: Activity, target: Class<out Activity>, extras: (Intent) -> Unit = {}) {
        if (a.javaClass == target) return
        Home.noteRecent(a, target.simpleName)   // RECENT on HOME
        a.startActivity(Intent(a, target).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT).also(extras)); noAnimation(a)
    }
    /** content above, the bar below; current = the tab of this screen (null: a sub-screen such as the calculator, nothing lit). */
    fun wrap(a: Activity, content: View, current: Tab?): View {
        val root = LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(a.getColor(R.color.bg)) }
        content.layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f); root.addView(content)
        root.addView(View(a).apply { setBackgroundColor(a.getColor(R.color.line)); layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 2) })
        val bar = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; setBackgroundColor(Color.parseColor("#0B0F15")); setPadding(0, 10, 0, 14) }
        for (t in Tab.values()) {
            val on = t == current; val col = if (on) a.getColor(R.color.accent) else a.getColor(R.color.dim)
            bar.addView(LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; isClickable = true; contentDescription = a.getString(t.label)
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); setOnClickListener { go(a, if (t == Tab.MIXER) mixerTarget(a) else t.target) }
                addView(TextView(a).apply { text = t.glyph; textSize = 20f; setTextColor(col); gravity = Gravity.CENTER })
                addView(TextView(a).apply { text = a.getString(t.label); textSize = 10f; setTextColor(col); gravity = Gravity.CENTER; typeface = if (on) Typeface.DEFAULT_BOLD else Typeface.DEFAULT; letterSpacing = 0.08f }) })
        }
        root.addView(bar); fitSystemBars(root); return root
    }
    /** EDGE TO EDGE (Android 15 draws every app under the status bar and the gesture bar): the page keeps clear of the clock, the notch and
     *  the gesture handle, so no title sits under the time and the bottom tabs stay tappable */
    fun fitSystemBars(v: View) {
        v.setOnApplyWindowInsetsListener { view, ins ->
            if (android.os.Build.VERSION.SDK_INT >= 30) { val b = ins.getInsets(android.view.WindowInsets.Type.systemBars() or android.view.WindowInsets.Type.displayCutout()); view.setPadding(b.left, b.top, b.right, b.bottom) }
            else @Suppress("DEPRECATION") view.setPadding(ins.systemWindowInsetLeft, ins.systemWindowInsetTop, ins.systemWindowInsetRight, ins.systemWindowInsetBottom)
            ins }
        v.requestApplyInsets()
    }
}
