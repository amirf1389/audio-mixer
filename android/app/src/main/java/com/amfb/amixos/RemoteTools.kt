package com.amfb.amixos

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.view.Gravity
import android.view.View
import android.widget.Button
import android.widget.LinearLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.ToggleButton
import com.amfb.amixos.remote.ConsoleState
import com.amfb.amixos.remote.DaemonClient
import com.amfb.amixos.remote.DcaState

/** The remote's TOOLS: one tidy list instead of a row of buttons, and the pages it opens.
 *  GROUP FADERS (DCA): the console's DCA groups (fader, mute, members), as many as the console's edition has (BASIC 1, PRO 4,
 *  STUDIO and up 8). A group with the console's WINDOWS VOLUME switch on drives the Windows slider: the phone moves it the same way
 *  and says so. 360 AUDIO: the console's spatial settings are not part of the remote link, so the page only says where to change them. */
object RemoteTools {
    /** the console edition's DCA groups by the plan name the console reports ("" or unknown: the console's own count holds) */
    fun dcaLimitForPlan(plan: String): Int = when (plan.trim().lowercase()) { "" -> 8; "basic" -> 1; "pro" -> 4; "studio", "master", "ultimate" -> 8; else -> 1 }
    fun dcaLimit(s: ConsoleState, plan: String): Int = minOf(s.dcaLimit, dcaLimitForPlan(plan)).coerceIn(1, 8)
    /** the groups the page shows: up to the limit; a group the console marks locked never moves from here */
    fun usable(s: ConsoleState, plan: String): List<DcaState> { val n = dcaLimit(s, plan); return s.dcas.filter { it.id in 1..n && !it.locked } }
    /** 360 AUDIO over the remote link: the console sends no spatial settings (no "spatial" key in its state) */
    fun spatialExposed(raw: Map<String, Any?>?): Boolean = raw?.containsKey("spatial") == true

    enum class Tool(val label: Int, val tag: String) { DCA(R.string.rt_dca, "tool_dca"), SPATIAL(R.string.rt_360, "tool_360"), EFFECTS(R.string.rt_effects, "tool_effects"),
        PLUGINS(R.string.plugins, "tool_plugins"), SCENES(R.string.scenes, "tool_scenes"), CALCULATOR(R.string.calculator, "tool_calculator"), MIC(R.string.rt_mic, "tool_mic") }

    private fun dp(a: Activity, v: Float) = ConsoleUi.dp(a, v)
    private fun head(a: Activity, t: String) = TextView(a).apply { text = t; setTextColor(Color.parseColor("#FFB590")); typeface = Typeface.DEFAULT_BOLD; textSize = 13f; setPadding(0, dp(a, 8f), 0, dp(a, 4f)) }
    private fun note(a: Activity, t: String) = TextView(a).apply { text = t; setTextColor(Color.parseColor("#C2C7CF")); textSize = 12f; setPadding(0, 0, 0, dp(a, 8f)) }

    fun toolsPanel(a: Activity, open: (Tool) -> Unit): View {
        val box = LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; tag = "toolsPanel"; setPadding(dp(a, 16f), dp(a, 8f), dp(a, 16f), dp(a, 8f)) }
        val p = Prefs.remote(a); for (t in Tool.values().filter { Features.toolOn(p, it) }) box.addView(Button(a).apply { text = a.getString(t.label); contentDescription = t.tag; isAllCaps = true
            layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT); setOnClickListener { open(t) } })
        return box
    }

    /** send(group, db, mute): one change of one group (the caller sends only the newest move) */
    fun dcaPanel(a: Activity, s: ConsoleState, plan: String, send: (Int, Double?, Boolean?) -> Unit): View {
        val box = LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; tag = "dcaPanel"; setPadding(dp(a, 16f), dp(a, 8f), dp(a, 16f), dp(a, 8f)); setBackgroundColor(Color.parseColor("#1D2024")) }
        val n = dcaLimit(s, plan)
        box.addView(note(a, a.resources.getQuantityString(R.plurals.rt_dca_limit, n, n)).apply { contentDescription = "dcaLimit" })
        val names = s.strips.associate { it.id to it.name.ifBlank { "CH ${it.id + 1}" } }
        for (g in s.dcas.filter { it.id in 1..8 }) {
            val open = g.id <= n && !g.locked
            box.addView(head(a, if (g.name.isBlank() || g.name == "DCA ${g.id}") "DCA ${g.id}" else a.getString(R.string.rt_dca_head, g.id, g.name)).apply { if (!open) alpha = 0.5f })
            if (!open) { box.addView(note(a, a.getString(R.string.rt_dca_locked)).apply { contentDescription = "dcaLocked${g.id}" }); continue }
            box.addView(note(a, if (g.members.isEmpty()) a.getString(R.string.rt_dca_no_members) else a.getString(R.string.rt_dca_members, g.members.joinToString(", ") { names[it] ?: "CH ${it + 1}" })).apply { contentDescription = "dcaMembers${g.id}" })
            if (g.winVol) box.addView(note(a, a.getString(R.string.rt_dca_winvol)).apply { contentDescription = "dcaWinVol${g.id}"; setTextColor(Color.parseColor("#FBBF24")) })
            val readout = TextView(a).apply { text = DaemonClient.fmtDb(if (g.db <= -60) -120.0 else g.db); typeface = Typeface.MONOSPACE; setTextColor(Color.WHITE); minWidth = dp(a, 56f); gravity = Gravity.END }
            val row = LinearLayout(a).apply { orientation = LinearLayout.HORIZONTAL; gravity = Gravity.CENTER_VERTICAL }
            row.addView(ToggleButton(a).apply { textOn = "MUTE"; textOff = "MUTE"; isChecked = g.mute; contentDescription = "dcaPanelMute${g.id}"; setOnCheckedChangeListener { _, on -> send(g.id, null, on) } })
            row.addView(SeekBar(a).apply { max = 700; progress = ((g.db.coerceIn(-60.0, 10.0) + 60) * 10).toInt(); contentDescription = "dcaPanelFader${g.id}"
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(b: SeekBar, p: Int, user: Boolean) { if (!user) return; val db = p / 10.0 - 60; readout.text = DaemonClient.fmtDb(if (db <= -60) -120.0 else db); send(g.id, db, null) }
                    override fun onStartTrackingTouch(b: SeekBar) {}; override fun onStopTrackingTouch(b: SeekBar) {} }) })
            row.addView(readout); box.addView(row)
        }
        return box
    }

    fun spatialPanel(a: Activity, exposed: Boolean): View = LinearLayout(a).apply { orientation = LinearLayout.VERTICAL; tag = "spatialPanel"; setPadding(dp(a, 16f), dp(a, 8f), dp(a, 16f), dp(a, 8f)); setBackgroundColor(Color.parseColor("#1D2024"))
        addView(head(a, a.getString(R.string.rt_360)))
        addView(note(a, a.getString(if (exposed) R.string.rt_360_console else R.string.rt_360_readonly)).apply { contentDescription = "spatialReadOnly" })
    }
}
