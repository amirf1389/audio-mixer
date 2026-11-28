package com.amfb.amixos

import android.content.SharedPreferences
import androidx.annotation.StringRes

/** FX+ : every channel's (and the master's) dynamics behind one key under its fader: COMPRESSOR, LIMITER, GATE, DE-ESSER.
 *  The values are one array of [N] numbers in the order the phone's engine takes them (amix.h, amix_mixer_set_dynamics), each held
 *  inside its range here (the engine and the console hold it again). The same array becomes the console's per-strip ops on a remote. */
object FxPlus {
    const val N = 20
    data class Spec(val index: Int, @StringRes val title: Int, val min: Double, val max: Double, val def: Double, val unit: String, val log: Boolean = false)
    data class Module(val id: String, @StringRes val title: Int, val onIndex: Int, val params: List<Spec>, val onMaster: Boolean = true)

    val COMP = Module("comp", R.string.fxp_comp, 0, listOf(
        Spec(1, R.string.fxp_threshold, -60.0, 0.0, -18.0, "dB"), Spec(2, R.string.fxp_ratio, 1.0, 20.0, 3.0, ":1", log = true),
        Spec(3, R.string.fxp_knee, 0.0, 24.0, 6.0, "dB"), Spec(4, R.string.fxp_attack, 0.1, 500.0, 10.0, "ms", log = true),
        Spec(5, R.string.fxp_release, 5.0, 5000.0, 100.0, "ms", log = true), Spec(6, R.string.fxp_makeup, 0.0, 24.0, 0.0, "dB")))
    val LIMIT = Module("limit", R.string.fxp_limiter, 7, listOf(Spec(8, R.string.fxp_ceiling, -20.0, 0.0, -1.0, "dB"), Spec(9, R.string.fxp_release, 5.0, 2000.0, 80.0, "ms", log = true)))
    val GATE = Module("gate", R.string.fxp_gate, 10, listOf(
        Spec(11, R.string.fxp_threshold, -80.0, 0.0, -50.0, "dB"), Spec(12, R.string.fxp_range, -90.0, 0.0, -80.0, "dB"),
        Spec(13, R.string.fxp_attack, 0.1, 500.0, 1.0, "ms", log = true), Spec(14, R.string.fxp_hold, 0.0, 2000.0, 50.0, "ms"),
        Spec(15, R.string.fxp_release, 5.0, 5000.0, 100.0, "ms", log = true)), onMaster = false)
    val DEESS = Module("deess", R.string.fxp_deesser, 16, listOf(Spec(17, R.string.fxp_threshold, -60.0, 0.0, -24.0, "dB"), Spec(18, R.string.fxp_amount, 0.0, 24.0, 8.0, "dB"),
        Spec(19, R.string.fxp_freq, 1000.0, 16000.0, 5000.0, "Hz", log = true)))
    val MODULES = listOf(COMP, LIMIT, GATE, DEESS)
    fun modules(master: Boolean) = if (master) MODULES.filter { it.onMaster } else MODULES
    private val specs: Array<Spec?> = arrayOfNulls<Spec>(N).also { a -> MODULES.forEach { m -> m.params.forEach { a[it.index] = it } } }
    fun spec(i: Int): Spec? = specs.getOrNull(i)
    fun isSwitch(i: Int) = MODULES.any { it.onIndex == i }

    /** everything at its default, off (the master's limiter on at -0.5 dB, as the master always had it) */
    fun defaults(master: Boolean): DoubleArray = DoubleArray(N) { i -> spec(i)?.def ?: 0.0 }.also { if (master) { it[7] = 1.0; it[8] = -0.5; it[9] = 100.0; it[1] = -12.0; it[2] = 2.0; it[4] = 30.0; it[5] = 200.0 } }
    /** a value held in its range: a switch is 0 or 1, a number that is not finite takes the default */
    fun clamp(i: Int, v: Double): Double {
        if (isSwitch(i)) return if (v.isFinite() && v != 0.0) 1.0 else 0.0
        val s = spec(i) ?: return 0.0
        return if (!v.isFinite()) s.def else v.coerceIn(s.min, s.max)
    }
    fun clampAll(v: DoubleArray): DoubleArray = DoubleArray(N) { i -> clamp(i, v.getOrElse(i) { spec(i)?.def ?: 0.0 }) }
    /** knob travel 0..1 <-> value (log for times, ratio and frequency) */
    fun toKnob(s: Spec, v: Double): Double { val x = v.coerceIn(s.min, s.max)
        return if (s.log) Math.log(x / s.min) / Math.log(s.max / s.min) else (x - s.min) / (s.max - s.min) }
    fun fromKnob(s: Spec, k: Double): Double { val t = if (k.isFinite()) k.coerceIn(0.0, 1.0) else 0.0
        val v = if (s.log) s.min * Math.pow(s.max / s.min, t) else s.min + t * (s.max - s.min)
        return Math.round(v * (if (s.unit == "Hz") 1.0 else 10.0)) / (if (s.unit == "Hz") 1.0 else 10.0) }
    fun fmt(s: Spec, v: Double): String = when {
        s.unit == "Hz" -> if (v >= 1000) String.format("%.1f kHz", v / 1000) else String.format("%.0f Hz", v)
        s.unit == ":1" -> String.format("%.1f:1", v)
        s.unit == "ms" && v >= 1000 -> String.format("%.2f s", v / 1000)
        else -> String.format("%.1f %s", v, s.unit) }
    fun encode(v: DoubleArray): String = clampAll(v).joinToString(",")
    fun decode(s: String?, master: Boolean): DoubleArray { val d = defaults(master); val parts = s?.split(',') ?: return d
        if (parts.size != N) return d
        return DoubleArray(N) { i -> clamp(i, parts[i].toDoubleOrNull() ?: d[i]) } }
    fun anyOn(v: DoubleArray, master: Boolean) = modules(master).any { v[it.onIndex] != 0.0 && !(master && it === LIMIT) }

    /** the console's ops for a strip (strip >= 0) or the master (-1): what a remote sends */
    fun daemonCmds(strip: Int, raw: DoubleArray): List<Map<String, Any?>> { val v = clampAll(raw); fun on(i: Int) = v[i] != 0.0
        if (strip < 0) return listOf(
            mapOf("op" to "limiter", "on" to on(7), "ceilingDb" to v[8], "releaseMs" to v[9]),
            buildMap { put("op", "mastering"); if (on(0) || on(16)) put("on", true); put("compOn", on(0)); put("thr", v[1].coerceIn(-40.0, 0.0)); put("ratio", v[2]); put("attack", v[4].coerceIn(0.1, 200.0))
                put("release", v[5].coerceIn(10.0, 2000.0)); put("makeup", v[6].coerceIn(0.0, 18.0)); put("deessOn", on(16)); put("deessThr", v[17]); put("deessMax", v[18]) })
        return listOf(
            mapOf("op" to "compressor", "strip" to strip, "on" to on(0), "thresholdDb" to v[1], "ratio" to v[2], "kneeDb" to v[3], "attackMs" to v[4], "releaseMs" to v[5], "makeupDb" to v[6]),
            mapOf("op" to "channelLimiter", "strip" to strip, "on" to on(7), "ceilingDb" to v[8], "releaseMs" to v[9]),
            mapOf("op" to "gate", "strip" to strip, "on" to on(10), "thresholdDb" to v[11], "rangeDb" to v[12], "attackMs" to v[13], "holdMs" to v[14], "releaseMs" to v[15]),
            mapOf("op" to "deesser", "strip" to strip, "on" to on(16), "thresholdDb" to v[17], "maxDb" to v[18], "freqHz" to v[19]))
    }
    /** [comp, limiter, gate, de-esser] reductions (<= 0 dB) from the console's /api/meters */
    @Suppress("UNCHECKED_CAST")
    fun grFromMeters(m: Map<String, Any?>, strip: Int): DoubleArray { fun n(o: Map<String, Any?>?, k: String) = ((o?.get(k) as? Number)?.toDouble() ?: 0.0).coerceIn(-60.0, 0.0)
        if (strip < 0) { val ms = m["master"] as? Map<String, Any?>; return doubleArrayOf(n(ms, "compGr"), n(ms, "limGr"), 0.0, n(ms, "deess")) }
        val st = (m["strips"] as? List<Any?>)?.getOrNull(strip) as? Map<String, Any?>; return doubleArrayOf(n(st, "gr"), 0.0, n(st, "gate"), 0.0) }
}

/** the FX+ view-model of one channel: its values (A and B), kept in the preferences so a rotation, a new panel or a restart finds them.
 *  [sink] gets the whole array after every change (the engine, or the console's ops). */
class FxPlusModel(private val prefs: SharedPreferences, val key: String, val master: Boolean, private val sink: (DoubleArray) -> Unit) {
    private var a = FxPlus.decode(prefs.getString("fxp:$key:A", null), master)
    private var b = FxPlus.decode(prefs.getString("fxp:$key:B", null), master)
    var showingB = prefs.getBoolean("fxp:$key:ab", false); private set
    val values: DoubleArray get() = (if (showingB) b else a).copyOf()
    private fun cur() = if (showingB) b else a
    private fun save() { prefs.edit().putString("fxp:$key:A", FxPlus.encode(a)).putString("fxp:$key:B", FxPlus.encode(b)).putBoolean("fxp:$key:ab", showingB).apply() }
    private fun push() { save(); sink(values) }
    fun set(i: Int, v: Double) { if (i !in 0 until FxPlus.N) return; cur()[i] = FxPlus.clamp(i, v); push() }
    fun get(i: Int): Double = cur().getOrElse(i) { 0.0 }
    fun toggle(m: FxPlus.Module) = set(m.onIndex, if (get(m.onIndex) != 0.0) 0.0 else 1.0)
    fun reset(m: FxPlus.Module) { val d = FxPlus.defaults(master); m.params.forEach { cur()[it.index] = d[it.index] }; push() }
    /** A / B: the other set of values sounds now */
    fun swapAB() { showingB = !showingB; push() }
    /** the set heard now copied onto the other one */
    fun copyToOther() { if (showingB) a = b.copyOf() else b = a.copyOf(); save() }
    fun anyOn() = FxPlus.anyOn(values, master)
    /** the values sent again (a new engine, a reconnect) */
    fun apply() = sink(values)
}
