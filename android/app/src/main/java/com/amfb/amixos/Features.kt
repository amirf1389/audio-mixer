package com.amfb.amixos

import android.content.Context
import android.content.SharedPreferences

/** FEATURES: the parts of the app a user wants to see. SETTINGS → FEATURES turns each on or off; HOME shows the tiles of the parts that
 *  are on, the remote's TOOLS list the pages that are on, and the bottom bar drops TOOLS / PLAYERS when they are off. Hiding never
 *  unlocks anything: a part the console's edition does not include stays locked on its own page whether it is shown or not.
 *  Kept in the "remote" preferences as feat_<id>; a part never switched keeps its default. */
object Features {
    /** one switchable part: [tiles] = the HOME tiles it owns, [locked] = always on (the app needs it), [app] = in which app it appears */
    enum class App { BOTH, CONSOLE, STANDALONE }
    data class Feature(val id: String, val label: Int, val sub: Int, val default: Boolean = true, val tiles: List<String> = emptyList(), val locked: Boolean = false, val app: App = App.BOTH)

    val all: List<Feature> = listOf(
        Feature("mixer", R.string.feat_mixer, R.string.feat_mixer_sub, tiles = listOf("RemoteActivity", "Scenes"), locked = true),
        Feature("phone", R.string.feat_phone, R.string.feat_phone_sub, tiles = listOf("LocalMixerActivity"), app = App.CONSOLE),
        Feature("dca", R.string.feat_dca, R.string.feat_dca_sub, app = App.CONSOLE),
        Feature("sends", R.string.feat_sends, R.string.feat_sends_sub, app = App.CONSOLE),
        Feature("eq", R.string.feat_eq, R.string.feat_eq_sub, tiles = listOf("EqActivity", "RtaEqActivity"), app = App.CONSOLE),
        Feature("meters", R.string.feat_meters, R.string.feat_meters_sub, tiles = listOf("MetersActivity", "SoundScoreActivity", "ConsoleStatusActivity"), app = App.CONSOLE),
        Feature("pads", R.string.feat_pads, R.string.feat_pads_sub, tiles = listOf("SamplePadsActivity"), app = App.CONSOLE),
        Feature("mastering", R.string.feat_mastering, R.string.feat_mastering_sub, tiles = listOf("MasteringActivity"), app = App.CONSOLE),
        Feature("routing", R.string.feat_routing, R.string.feat_routing_sub, tiles = listOf("RoutingActivity"), app = App.CONSOLE),
        Feature("spatial", R.string.feat_spatial, R.string.feat_spatial_sub, app = App.CONSOLE),
        Feature("mic", R.string.feat_mic, R.string.feat_mic_sub, tiles = listOf("PhoneMicActivity"), app = App.CONSOLE),
        Feature("plugins", R.string.feat_plugins, R.string.feat_plugins_sub, tiles = listOf("PluginsActivity")),
        Feature("tools", R.string.feat_tools, R.string.feat_tools_sub, tiles = listOf("ToolsActivity", "CalculatorActivity")),
        Feature("recorder", R.string.feat_recorder, R.string.feat_recorder_sub, tiles = listOf("RecorderActivity", "MixdownActivity")),
        Feature("streaming", R.string.feat_streaming, R.string.feat_streaming_sub),
        Feature("devices", R.string.feat_devices, R.string.feat_devices_sub, tiles = listOf("DevicesActivity")),
    )
    fun byId(id: String): Feature? = all.firstOrNull { it.id == id }
    /** the parts that belong to this app (AmF X1 or AmF X1 Mixer) */
    fun forApp(standalone: Boolean): List<Feature> = all.filter { it.app == App.BOTH || it.app == (if (standalone) App.STANDALONE else App.CONSOLE) }

    private fun prefs(c: Context): SharedPreferences = Prefs.remote(c)
    fun isOn(p: SharedPreferences, id: String): Boolean { val f = byId(id) ?: return true; return f.locked || p.getBoolean("feat_$id", f.default) }
    fun isOn(c: Context, id: String) = isOn(prefs(c), id)
    fun set(c: Context, id: String, on: Boolean) { val f = byId(id) ?: return; if (!f.locked) prefs(c).edit().putBoolean("feat_$id", on).apply() }
    fun resetAll(c: Context) { val e = prefs(c).edit(); all.forEach { e.remove("feat_" + it.id) }; e.apply() }
    /** a HOME tile is shown unless the part that owns it is off (a tile no part owns, e.g. SETTINGS, is always shown) */
    fun tileOn(p: SharedPreferences, tileId: String): Boolean { val f = all.firstOrNull { tileId in it.tiles } ?: return true; return isOn(p, f.id) }
    fun tileOn(c: Context, tileId: String) = tileOn(prefs(c), tileId)
    /** the bottom bar: TOOLS and PLAYERS follow their parts; HOME, MIXER and SETTINGS always stay (SETTINGS is how parts come back) */
    fun tabOn(p: SharedPreferences, t: Nav.Tab): Boolean = when (t) { Nav.Tab.TOOLS -> isOn(p, "tools"); Nav.Tab.PLAYERS, Nav.Tab.PLAY -> isOn(p, "streaming"); Nav.Tab.DEVICES -> isOn(p, "devices"); else -> true }
    /** the remote's TOOLS list */
    fun toolOn(p: SharedPreferences, t: RemoteTools.Tool): Boolean = when (t) {
        RemoteTools.Tool.DCA -> isOn(p, "dca"); RemoteTools.Tool.SPATIAL -> isOn(p, "spatial"); RemoteTools.Tool.EFFECTS, RemoteTools.Tool.PLUGINS -> isOn(p, "plugins")
        RemoteTools.Tool.CALCULATOR -> isOn(p, "tools"); RemoteTools.Tool.MIC -> isOn(p, "mic"); RemoteTools.Tool.SCENES -> true }
}
