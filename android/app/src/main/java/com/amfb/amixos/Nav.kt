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
    enum class Tab(val label: Int, val glyph: String, val target: Class<out Activity>) {
        HOME(R.string.nav_home, "⌂", HomeActivity::class.java), MIXER(R.string.nav_mixer, "☰", RemoteActivity::class.java),
        TOOLS(R.string.nav_tools, "♪", ToolsActivity::class.java), SETTINGS(R.string.nav_settings, "⚙", SettingsActivity::class.java)
    }
    fun go(a: Activity, target: Class<out Activity>, extras: (Intent) -> Unit = {}) {
        if (a.javaClass == target) return
        Home.noteRecent(a, target.simpleName)   // RECENT on HOME
        a.startActivity(Intent(a, target).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT).also(extras)); a.overridePendingTransition(0, 0)
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
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f); setOnClickListener { go(a, t.target) }
                addView(TextView(a).apply { text = t.glyph; textSize = 20f; setTextColor(col); gravity = Gravity.CENTER })
                addView(TextView(a).apply { text = a.getString(t.label); textSize = 10f; setTextColor(col); gravity = Gravity.CENTER; typeface = if (on) Typeface.DEFAULT_BOLD else Typeface.DEFAULT; letterSpacing = 0.08f }) })
        }
        root.addView(bar); return root
    }
}
