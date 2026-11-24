package com.amfb.amixos

import android.app.Activity
import android.content.Context
import android.graphics.Color
import android.graphics.Typeface
import android.view.Gravity
import android.view.View
import android.widget.LinearLayout
import android.widget.TextView

/** CHROMEBOOK MODE: on ChromeOS (or any large window lying on its side) every main screen sits in a compact frame with a rail on the
 *  left instead of the bar at the bottom: MIXER, SOUND CARD, NETWORK CONNECTION, SETTINGS and LICENCE. Each entry opens the screen the
 *  app already has (nothing is done twice). SETUP of the phone mixer forces it ON or OFF; AUTO decides by the device. */
object ChromeFrame {
    const val KEY = "chromebookMode"
    val modes = listOf("auto", "on", "off")
    fun mode(c: Context): String = Prefs.local(c).getString(KEY, "auto").takeIf { it in modes } ?: "auto"
    fun setMode(c: Context, m: String) { Prefs.local(c).edit().putString(KEY, if (m in modes) m else "auto").apply() }
    fun isChromeOs(c: Context): Boolean = runCatching { c.packageManager.hasSystemFeature("org.chromium.arc") || c.packageManager.hasSystemFeature("org.chromium.arc.device_management") }.getOrDefault(false)
    /** a large window on its side: at least 600 dp on its short side and wider than tall */
    fun largeLandscape(c: Context): Boolean { val cf = c.resources.configuration; return cf.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE && cf.smallestScreenWidthDp >= 600 }
    fun decide(mode: String, chromeOs: Boolean, largeLand: Boolean): Boolean = when (mode) { "on" -> true; "off" -> false; else -> chromeOs || largeLand }
    fun active(c: Context): Boolean = decide(mode(c), isChromeOs(c), largeLandscape(c))

    enum class Entry(val label: Int, val glyph: String) { MIXER(R.string.cb_mixer, "☰"), SOUND_CARD(R.string.cb_sound_card, "♫"), NETWORK(R.string.cb_network, "⇄"), SETTINGS(R.string.cb_settings, "⚙"), LICENCE(R.string.cb_licence, "§") }
    /** the screen an entry opens (null: LICENCE, a panel over the open screen) */
    fun target(a: Activity, e: Entry): Class<out Activity>? = when (e) {
        Entry.MIXER -> Nav.mixerTarget(a)
        Entry.SOUND_CARD -> DevicesActivity::class.java
        Entry.NETWORK -> if (Nav.standalone(a)) DevicesActivity::class.java else RemoteActivity::class.java   // the console link, or (mixer app) the phone's networks
        Entry.SETTINGS -> SettingsActivity::class.java
        Entry.LICENCE -> null
    }
    /** the licence as the engine reports it */
    fun licenceText(c: Context): String = runCatching { Engine.licenseJson() }.getOrNull()?.takeIf { it.isNotBlank() && it != "null" } ?: c.getString(R.string.cb_licence_none)
    fun showLicence(a: Activity): androidx.appcompat.app.AlertDialog {
        val json = runCatching { Engine.licenseJson() }.getOrNull(); val link = LicenceLink.forLicence(a, json)
        val b = androidx.appcompat.app.AlertDialog.Builder(a).setTitle(R.string.cb_licence).setMessage(licenceText(a)).setPositiveButton(android.R.string.ok, null)
        if (link != null) b.setNeutralButton(R.string.cb_manage_licence) { _, _ -> LicenceLink.open(a, link) }   // MANAGE LICENCE ONLINE: the licence site, machine code filled in
        return b.show()
    }

    fun wrap(a: Activity, content: View): View {
        val d = a.resources.displayMetrics.density
        val root = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; setBackgroundColor(a.getColor(R.color.bg)); tag = "chromeFrame" }
        val rail = LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; tag = "chromeRail"; setBackgroundColor(a.getColor(R.color.md_surface_container)); setPadding(0, (12 * d).toInt(), 0, (12 * d).toInt())
            layoutParams = LinearLayout.LayoutParams((96 * d).toInt(), LinearLayout.LayoutParams.MATCH_PARENT) }
        for (e in Entry.values()) {
            val t = target(a, e); val on = t != null && a.javaClass == t
            rail.addView(LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; gravity = Gravity.CENTER; isClickable = true; contentDescription = "rail_" + e.name.lowercase(); setPadding(0, (10 * d).toInt(), 0, (10 * d).toInt())
                layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT)
                setOnClickListener { if (t == null) showLicence(a) else Nav.go(a, t) }
                addView(TextView(a).apply { text = e.glyph; textSize = 18f; gravity = Gravity.CENTER; includeFontPadding = false
                    setTextColor(a.getColor(if (on) R.color.md_on_secondary_container else R.color.md_on_surface_variant))
                    background = android.graphics.drawable.GradientDrawable().apply { cornerRadius = 16 * d; setColor(if (on) a.getColor(R.color.md_secondary_container) else Color.TRANSPARENT) }
                    layoutParams = LinearLayout.LayoutParams((56 * d).toInt(), (32 * d).toInt()) })
                addView(TextView(a).apply { text = a.getString(e.label); textSize = 10f; gravity = Gravity.CENTER; setPadding((4 * d).toInt(), (4 * d).toInt(), (4 * d).toInt(), 0)
                    setTextColor(a.getColor(if (on) R.color.md_on_surface else R.color.md_on_surface_variant)); typeface = Typeface.create(if (on) "sans-serif-medium" else "sans-serif", Typeface.NORMAL) }) })
        }
        content.layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f)
        root.addView(rail); root.addView(content); Nav.fitSystemBars(root); return root
    }
}
