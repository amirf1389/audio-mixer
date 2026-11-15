package com.amfb.amixos

import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.GridLayout
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/** HOME: one place for the whole app. The console card (which computer, which plan, paired or not), then a tile for every part:
 *  the console mixer, this phone's own mixer, EQ, scenes, tools, calculator, plug-ins and settings. */
class HomeActivity : AppCompatActivity() {
    private lateinit var card: LinearLayout
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 40, 32, 32) }
        box.addView(TextView(this).apply { text = "AMF X1"; setTextColor(getColor(R.color.accent)); textSize = 28f; typeface = Typeface.DEFAULT_BOLD; letterSpacing = 0.18f })
        box.addView(TextView(this).apply { text = getString(R.string.home_by); setTextColor(getColor(R.color.dim)); textSize = 12f; letterSpacing = 0.2f; setPadding(0, 0, 0, 24) })
        card = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.card_bg); setPadding(32, 28, 32, 28) }
        box.addView(card)
        val grid = GridLayout(this).apply { columnCount = 2; setPadding(0, 24, 0, 0) }
        fun tile(glyph: String, title: Int, sub: Int, color: Int, f: () -> Unit) = grid.addView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; background = getDrawable(R.drawable.tile_bg); setPadding(28, 26, 28, 26); isClickable = true; setOnClickListener { f() }
            layoutParams = GridLayout.LayoutParams(GridLayout.spec(GridLayout.UNDEFINED, 1f), GridLayout.spec(GridLayout.UNDEFINED, 1f)).apply { width = 0; setMargins(8, 8, 8, 8) }
            addView(TextView(this@HomeActivity).apply { text = glyph; textSize = 26f; setTextColor(color) })
            addView(TextView(this@HomeActivity).apply { text = getString(title); textSize = 15f; setTextColor(Color.WHITE); typeface = Typeface.DEFAULT_BOLD; setPadding(0, 10, 0, 2) })
            addView(TextView(this@HomeActivity).apply { text = getString(sub); textSize = 11f; setTextColor(getColor(R.color.dim)); maxLines = 2 }) })
        val o = getColor(R.color.accent); val c = getColor(R.color.cyan); val v = Color.parseColor("#A78BFA"); val g = Color.parseColor("#34D399"); val y = Color.parseColor("#FBBF24")
        tile("☰", R.string.home_mixer, R.string.home_mixer_sub, o) { Nav.go(this, RemoteActivity::class.java) }
        tile("🎙", R.string.home_phone, R.string.home_phone_sub, c) { Nav.go(this, LocalMixerActivity::class.java) }
        tile("∿", R.string.home_eq, R.string.home_eq_sub, v) { pickEqChannel() }
        tile("▤", R.string.home_scenes, R.string.home_scenes_sub, g) { Nav.go(this, RemoteActivity::class.java) { it.putExtra("open", "scenes") } }
        tile("♪", R.string.home_tools, R.string.home_tools_sub, y) { Nav.go(this, ToolsActivity::class.java) }
        tile("÷", R.string.home_calc, R.string.home_calc_sub, c) { Nav.go(this, CalculatorActivity::class.java) }
        tile("⧉", R.string.home_plugins, R.string.home_plugins_sub, v) { Nav.go(this, PluginsActivity::class.java) }
        tile("⊕", R.string.home_mixdown, R.string.home_mixdown_sub, o) { Nav.go(this, MixdownActivity::class.java) }
        tile("⚙", R.string.home_settings, R.string.home_settings_sub, Color.parseColor("#94A3B8")) { Nav.go(this, SettingsActivity::class.java) }
        box.addView(grid)
        setContentView(Nav.wrap(this, ScrollView(this).apply { addView(box) }, Nav.Tab.HOME))
    }
    override fun onResume() { super.onResume(); showCard() }
    /** The console card: from what the remote saved at its last connection (nothing is fetched here). */
    fun showCard() {
        card.removeAllViews(); val p = Prefs.remote(this); val host = p.getString("host", "").orEmpty(); val paired = !p.getString("token", "").isNullOrEmpty()
        card.addView(TextView(this).apply { text = getString(R.string.home_console); setTextColor(getColor(R.color.accent)); textSize = 11f; letterSpacing = 0.2f; typeface = Typeface.MONOSPACE })
        if (host.isEmpty()) {
            card.addView(TextView(this).apply { text = getString(R.string.home_no_console); setTextColor(Color.WHITE); textSize = 16f; setPadding(0, 8, 0, 4) })
            card.addView(TextView(this).apply { text = getString(R.string.home_no_console_sub); setTextColor(getColor(R.color.dim)); textSize = 12f })
            return
        }
        val name = p.getString("lastName", "AmF X1").orEmpty(); val plan = p.getString("lastPlan", "").orEmpty(); val strips = p.getInt("lastStrips", 0)
        card.addView(TextView(this).apply { text = name; setTextColor(Color.WHITE); textSize = 18f; typeface = Typeface.DEFAULT_BOLD; setPadding(0, 8, 0, 0) })
        card.addView(TextView(this).apply { text = host + ":" + (p.getString("port", null) ?: Prefs.defaultPort(this@HomeActivity)); setTextColor(getColor(R.color.cyan)); typeface = Typeface.MONOSPACE; textSize = 13f })
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL; setPadding(0, 10, 0, 0) }
        fun chip(t: String, col: Int) = row.addView(TextView(this).apply { text = t; setTextColor(col); textSize = 11f; typeface = Typeface.MONOSPACE; setPadding(0, 0, 28, 0) })
        chip(if (paired) "● " + getString(R.string.set_paired).uppercase() else "○ " + getString(R.string.home_not_paired), if (paired) Color.parseColor("#34D399") else Color.parseColor("#FBBF24"))
        if (plan.isNotEmpty()) chip(plan, Color.parseColor("#FBBF24")); if (strips > 0) chip("$strips CH", getColor(R.color.dim))
        card.addView(row)
    }
    private fun pickEqChannel() {
        val n = Prefs.remote(this).getInt("lastStrips", 0).takeIf { it > 0 } ?: 10
        AlertDialog.Builder(this).setTitle(R.string.home_eq).setItems((1..n).map { "CH %02d".format(it) }.toTypedArray()) { _, w -> Nav.go(this, EqActivity::class.java) { it.putExtra("strip", w) } }.show()
    }
}
