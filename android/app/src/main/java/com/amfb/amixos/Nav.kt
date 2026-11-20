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
        TOOLS(R.string.nav_tools, "♪", ToolsActivity::class.java), SETTINGS(R.string.nav_settings, "⚙", SettingsActivity::class.java),
        PLAYERS(R.string.nav_players, "▶", StreamPlayerActivity::class.java)
    }
    // AmF X1 MIXER (the separate app, package ...mixer): the phone's own mixer without the console remote. Its bar is MIXER · PLAYERS ·
    // TOOLS · SETTINGS, the MIXER tab is always THIS PHONE, and there is no THIS PHONE / CONSOLE switch.
    @Volatile var standaloneForTests: Boolean? = null   // how a test becomes the AmF X1 Mixer app (tests run under the main app name)
    fun standalone(c: android.content.Context) = standaloneForTests ?: isStandalonePackage(c.packageName)
    fun isStandalonePackage(pkg: String) = pkg.endsWith(".mixer")
    fun tabs(standalone: Boolean): List<Tab> = if (standalone) listOf(Tab.MIXER, Tab.PLAYERS, Tab.TOOLS, Tab.SETTINGS) else listOf(Tab.HOME, Tab.MIXER, Tab.TOOLS, Tab.SETTINGS)
    // DUAL MODE MIXER: one MIXER with two modes, THIS PHONE (the standalone mixer: the phone's own sound) and CONSOLE (the remote of the
    // Windows console). The MIXER tab opens the mode used last; the bar at the top of each switches to the other.
    fun phoneMode(a: android.content.Context) = standalone(a) || Prefs.remote(a).getString("mixerMode", "console") == "phone"
    fun mixerTarget(a: Activity): Class<out Activity> = if (phoneMode(a)) LocalMixerActivity::class.java else RemoteActivity::class.java
    /** the two-mode bar for the top of a mixer screen; [phone] = this screen is the phone's own mixer */
    fun modeBar(a: Activity, phone: Boolean): View {
        if (standalone(a)) return View(a).apply { visibility = View.GONE; contentDescription = "mixerMode" }
        Prefs.remote(a).edit().putString("mixerMode", if (phone) "phone" else "console").apply()
        val bar = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; setPadding(0, 0, 0, 12); contentDescription = "mixerMode" }
        for ((isPhone, label) in listOf(true to a.getString(R.string.mode_phone), false to a.getString(R.string.mode_console))) {
            val on = isPhone == phone
            bar.addView(TextView(a).apply { text = label; gravity = Gravity.CENTER; textSize = 13f; letterSpacing = 0.02f; typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL); setPadding(0, 24, 0, 24)
                setTextColor(a.getColor(if (on) R.color.md_on_secondary_container else R.color.md_on_surface)); tag = if (isPhone) "mode_phone" else "mode_console"
                // a Material segmented button: one outlined pill cut in two, the chosen half filled
                val r = 200f; background = android.graphics.drawable.GradientDrawable().apply { cornerRadii = if (isPhone) floatArrayOf(r, r, 0f, 0f, 0f, 0f, r, r) else floatArrayOf(0f, 0f, r, r, r, r, 0f, 0f); setColor(if (on) a.getColor(R.color.md_secondary_container) else Color.TRANSPARENT); setStroke(2, a.getColor(R.color.md_outline)) }
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f).apply { setMargins(if (isPhone) 0 else -2, 0, 0, 0) }
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
        if (ChromeFrame.active(a)) return ChromeFrame.wrap(a, content)   // CHROMEBOOK MODE: a rail on the left instead of this bar
        val root = LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; setBackgroundColor(a.getColor(R.color.bg)) }
        content.layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f); root.addView(content)
        // a Material navigation bar: a tonal surface (no divider line), the open tab's icon on a pill, labels under the icons
        val d = a.resources.displayMetrics.density
        val bar = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; tag = "navBar"; setBackgroundColor(a.getColor(R.color.md_surface_container)); setPadding(0, (12 * d).toInt(), 0, (12 * d).toInt()) }
        for (t in tabs(standalone(a))) {
            val on = t == current
            bar.addView(LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; isClickable = true; contentDescription = a.getString(t.label)
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); setOnClickListener { go(a, if (t == Tab.MIXER) mixerTarget(a) else t.target) }
                addView(TextView(a).apply { text = t.glyph; textSize = 18f; setTextColor(a.getColor(if (on) R.color.md_on_secondary_container else R.color.md_on_surface_variant)); gravity = Gravity.CENTER; includeFontPadding = false
                    background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = 16 * d; setColor(if (on) a.getColor(R.color.md_secondary_container) else Color.TRANSPARENT) }
                    layoutParams = LinearLayout.LayoutParams((64 * d).toInt(), (32 * d).toInt()) })
                addView(TextView(a).apply { text = a.getString(t.label); textSize = 12f; setTextColor(a.getColor(if (on) R.color.md_on_surface else R.color.md_on_surface_variant)); gravity = Gravity.CENTER; setPadding(0, (4 * d).toInt(), 0, 0)
                    typeface = Typeface.create(if (on) "sans-serif-medium" else "sans-serif", Typeface.NORMAL); letterSpacing = 0.03f }) })
        }
        root.addView(bar); fitSystemBars(root); fitBar(a, bar); return root
    }
    /** ON ITS SIDE the screen is short: the bar becomes a thin row of icons (no labels); upright it is the usual bar. */
    private fun fitBar(a: Activity, bar: LinearLayout) {
        val land = a.resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE; val d = a.resources.displayMetrics.density
        val pad = ((if (land) 3 else 12) * d).toInt(); bar.setPadding(0, pad, 0, pad)
        for (i in 0 until bar.childCount) (bar.getChildAt(i) as? LinearLayout)?.let { tab -> if (tab.childCount > 1) tab.getChildAt(1).visibility = if (land) View.GONE else View.VISIBLE }
    }
    /** after the phone was turned (for the screens that stay open through a turn) */
    fun fit(a: Activity) { (a.window.decorView.findViewWithTag<View>("navBar") as? LinearLayout)?.let { fitBar(a, it) } }
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
