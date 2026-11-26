package com.amfb.amixos

import android.app.AlertDialog
import android.content.Context
import android.content.SharedPreferences
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.CompoundButton
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

/** Every setting of the app in one place, kept in SharedPreferences ("remote" for the remote, "local" for the phone's own mixer).
 *  Read through [Prefs] so each screen sees the same defaults. Nothing here leaves the phone. */
object Prefs {
    fun remote(c: Context): SharedPreferences = c.getSharedPreferences("remote", Context.MODE_PRIVATE)
    fun local(c: Context): SharedPreferences = c.getSharedPreferences("local", Context.MODE_PRIVATE)
    fun autoConnect(c: Context) = remote(c).getBoolean("autoConnect", false)
    fun autoFind(c: Context) = remote(c).getBoolean("autoFind", true)
    fun bootAnim(c: Context) = remote(c).getBoolean("bootAnim", true)                  // the boot animation when the app starts               // AUTO-DETECT: search the Wi-Fi for the console by itself
    fun keepScreenOn(c: Context) = remote(c).getBoolean("keepScreenOn", true)
    fun showPan(c: Context) = remote(c).getBoolean("showPan", true)
    fun bigFaders(c: Context) = remote(c).getBoolean("bigFaders", true)             // the remote's FADERS view: five channels across, the fader over the whole height (KNOBS view: pan, gain and EQ on the strip)
    fun showTone(c: Context) = remote(c).getBoolean("showTone", true)               // GAIN and the analog 3-band EQ on every strip
    fun haptics(c: Context) = remote(c).getBoolean("haptics", true)
    fun startInPhoneMixer(c: Context) = remote(c).getBoolean("startPhoneMixer", false)   // STANDALONE: the app opens straight into the phone's own mixer
    fun confirmStop(c: Context) = remote(c).getBoolean("confirmStop", true)
    fun syncSeconds(c: Context) = remote(c).getInt("syncSeconds", 3)                 // 0 = do not follow the console
    fun stripCount(c: Context) = remote(c).getInt("stripCount", 32)                   // 8 / 16 / 32 strips on the mixer
    fun defaultPort(c: Context) = remote(c).getInt("defaultPort", 8765)
    fun meterFloor(c: Context) = remote(c).getInt("meterFloor", 60)                   // the meters show 0 to -N dBFS
    fun localOutDevice(c: Context) = local(c).getInt("outDevice", 0)                 // AudioDeviceInfo id the phone mixer plays to; 0 = the system route
    fun localInDevice(c: Context) = local(c).getInt("inDevice", 0)
    /** the phone mixer's INPUT ON / OFF: OFF opens no microphone at all */
    fun localInputOn(c: Context) = local(c).getBoolean("inputOn", true)
    fun setLocalInputOn(c: Context, on: Boolean) { local(c).edit().putBoolean("inputOn", on).apply() }
}

class SettingsActivity : FitActivity() {
    private lateinit var box: LinearLayout
    private val accent = Color.parseColor("#FFB590"); private val dim = Color.parseColor("#C2C7CF")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 32, 32, 48) }
        setContentView(Nav.wrap(this, ScrollView(this).apply { setBackgroundColor(Color.parseColor("#111418")); addView(box) }, Nav.Tab.SETTINGS))
        title(getString(R.string.settings))
        val r = Prefs.remote(this); val l = Prefs.local(this)

        section(getString(R.string.set_connection))
        toggle(r, "autoConnect", false, getString(R.string.set_auto_connect), getString(R.string.set_auto_connect_note))
        toggle(r, "autoFind", true, getString(R.string.set_auto_find), getString(R.string.set_auto_find_note))
        toggle(r, "bootAnim", true, getString(R.string.set_boot_anim), getString(R.string.set_boot_anim_note))
        number(r, "defaultPort", 8765, 1024, 65535, getString(R.string.set_port))
        val saved = r.getString("host", "").orEmpty()
        info(getString(R.string.set_saved_console), if (saved.isEmpty()) getString(R.string.set_none) else saved + (if (r.getString("token", "").isNullOrEmpty()) "" else "  · " + getString(R.string.set_paired)))
        button(getString(R.string.set_forget)) {
            confirm(getString(R.string.set_forget_q)) { r.edit().remove("host").remove("token").apply(); recreate() } }

        section(getString(R.string.set_mixer))
        toggle(r, "startPhoneMixer", false, getString(R.string.set_start_phone_mixer), getString(R.string.set_start_phone_mixer_sub))
        choice(r, "stripCount", 32, listOf(8, 16, 32), getString(R.string.set_strips)) { "$it" }
        toggle(r, "showPan", true, getString(R.string.set_pan), null)
        toggle(r, "showTone", true, getString(R.string.set_tone), null)
        choice(r, "meterFloor", 60, listOf(40, 60, 90), getString(R.string.set_meter_range)) { "0 … -$it dBFS" }
        choice(r, "syncSeconds", 3, listOf(0, 1, 3, 5, 10), getString(R.string.set_sync)) { if (it == 0) getString(R.string.set_off) else "$it s" }

        // FEATURES: the parts of the app to show (HOME tiles, the remote's TOOLS, the bottom bar). Hiding a part never unlocks another.
        section(getString(R.string.set_features))
        box.addView(note(getString(R.string.set_features_note)))
        for (f in Features.forApp(Nav.standalone(this))) {
            toggle(r, "feat_" + f.id, f.default, getString(f.label), getString(f.sub))
            if (f.locked) box.findViewWithTag<Switch>("feat_" + f.id)?.apply { isChecked = true; isEnabled = false }
        }
        button(getString(R.string.set_features_reset)) { Features.resetAll(this); recreate() }

        section(getString(R.string.set_behaviour))
        toggle(r, "keepScreenOn", true, getString(R.string.set_screen_on), null)
        choice(r, "textSize", TextFit.AUTO, listOf(TextFit.AUTO, TextFit.SMALL, TextFit.NORMAL, TextFit.LARGE), getString(R.string.set_text_size), { recreate() }) { getString(listOf(R.string.set_text_auto, R.string.set_text_small, R.string.set_text_normal, R.string.set_text_large)[it]) }
        toggle(r, "haptics", true, getString(R.string.set_haptics), null)
        toggle(r, "confirmStop", true, getString(R.string.set_confirm_stop), getString(R.string.set_confirm_stop_note))
        toggle(r, CaptureGuard.KEY, false, getString(R.string.set_hide_capture), getString(R.string.set_hide_capture_note)) { CaptureGuard.apply(this) }

        // PHONE AUDIO is not chosen here any more: the phone mixer runs at what the phone's own audio HAL reports (rate, buffer, and the
        // brand profile's headroom), and the interface is picked on DEVICES & NETWORK

        section(getString(R.string.set_updates))
        toggle(r, "updAuto", true, getString(R.string.set_upd_auto), getString(R.string.set_upd_auto_note))
        toggle(r, "updWifi", true, getString(R.string.set_upd_wifi), null)
        AppUpdater.skipped(this).takeIf { it > 0 }?.let { box.addView(note(getString(R.string.set_upd_skipped, (it % 100).toString()))) }
        button(getString(R.string.set_upd_check)) { AppUpdater.check(this, manual = true) }

        section(getString(R.string.set_about))
        info(getString(R.string.app_name), "AMFB Audio · " + runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrDefault("?"))
        info(getString(R.string.set_privacy), getString(R.string.set_privacy_note))
        button(getString(R.string.set_reset)) { confirm(getString(R.string.set_reset_q)) {
            val host = r.getString("host", null); val token = r.getString("token", null)   // the pairing stays: only the preferences go back
            r.edit().clear().apply { if (host != null) putString("host", host); if (token != null) putString("token", token) }.apply(); l.edit().clear().apply(); recreate() } }
    }

    private fun title(t: String) = box.addView(TextView(this).apply { text = t; setTextColor(accent); textSize = 14f; setTypeface(null, Typeface.BOLD); setPadding(0, 0, 0, 16) })
    private fun section(t: String) = box.addView(TextView(this).apply { text = t; setTextColor(accent); textSize = 13f; typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL); setPadding(0, 20, 0, 4); letterSpacing = 0.1f })
    private fun note(t: String) = ConsoleUi.moreLine(TextView(this).apply { text = t; setTextColor(dim); textSize = 12f })
    private fun info(label: String, value: String) {
        if (label.isNotEmpty()) box.addView(TextView(this).apply { text = label; setTextColor(Color.WHITE); textSize = 13f; setPadding(0, 12, 0, 0) })
        box.addView(note(value))
    }
    private fun toggle(p: SharedPreferences, key: String, def: Boolean, label: String, sub: String?, after: (() -> Unit)? = null) {
        val sw = Switch(this).apply { text = label; setTextColor(Color.WHITE); textSize = 15f; isChecked = p.getBoolean(key, def); setPadding(0, 12, 0, 0); tag = key
            setOnCheckedChangeListener { _: CompoundButton, on: Boolean -> p.edit().putBoolean(key, on).apply(); after?.invoke() } }
        box.addView(sw); if (sub != null) box.addView(note(sub))
    }
    private fun <T> choice(p: SharedPreferences, key: String, def: Int, values: List<Int>, label: String, after: (() -> Unit)? = null, show: (Int) -> T) {
        val b = Button(this).apply { isAllCaps = false; textAlignment = View.TEXT_ALIGNMENT_VIEW_START }
        fun refresh() { b.text = label + ":  " + show(p.getInt(key, def)) }
        refresh(); b.setOnClickListener { AlertDialog.Builder(this).setTitle(label).setItems(values.map { show(it).toString() }.toTypedArray()) { _, w -> p.edit().putInt(key, values[w]).apply(); refresh(); after?.invoke() }.show() }
        box.addView(b)
    }
    private fun number(p: SharedPreferences, key: String, def: Int, min: Int, max: Int, label: String) {
        val b = Button(this).apply { isAllCaps = false; textAlignment = View.TEXT_ALIGNMENT_VIEW_START }
        fun refresh() { b.text = label + ":  " + p.getInt(key, def) }
        refresh(); b.setOnClickListener {
            val e = EditText(this).apply { inputType = android.text.InputType.TYPE_CLASS_NUMBER; setText(p.getInt(key, def).toString()) }
            AlertDialog.Builder(this).setTitle(label).setView(e).setPositiveButton(android.R.string.ok) { _, _ ->
                val v = e.text.toString().toIntOrNull(); if (v != null && v in min..max) { p.edit().putInt(key, v).apply(); refresh() } }.setNegativeButton(android.R.string.cancel, null).show() }
        box.addView(b)
    }
    private fun button(label: String, f: () -> Unit) = box.addView(Button(this).apply { text = label; setOnClickListener { f() } })
    private fun confirm(q: String, f: () -> Unit) = AlertDialog.Builder(this).setMessage(q).setPositiveButton(android.R.string.ok) { _, _ -> f() }.setNegativeButton(android.R.string.cancel, null).show()
}
