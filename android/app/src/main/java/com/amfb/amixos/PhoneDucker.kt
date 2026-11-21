package com.amfb.amixos

import android.content.SharedPreferences

/** DUCKER · VOICE OVER MUSIC on the phone's own mixer: while the CH inputs (the voice: the KEY) are above the threshold, the DECK
 *  channels (the music: the TARGET) go down by the depth and come back after the hold and release, and the voices can be lifted a
 *  little. The envelope runs in the engine sample by sample (the same ducker as the console: no steps, no clicks); this object keeps
 *  the settings in the phone mixer's prefs and tells the engine which strips are key and target. */
object PhoneDucker {
    data class Params(val on: Boolean = false, val thresholdDb: Double = -40.0, val depthDb: Double = -12.0, val attackMs: Double = 20.0,
                      val holdMs: Double = 300.0, val releaseMs: Double = 600.0, val liftDb: Double = 0.0) {
        /** every value inside the range the engine takes */
        fun clamped() = copy(thresholdDb = thresholdDb.coerceIn(-80.0, 0.0), depthDb = depthDb.coerceIn(-40.0, 0.0), attackMs = attackMs.coerceIn(1.0, 500.0),
            holdMs = holdMs.coerceIn(0.0, 5000.0), releaseMs = releaseMs.coerceIn(10.0, 5000.0), liftDb = liftDb.coerceIn(0.0, 6.0))
    }
    fun load(p: SharedPreferences) = Params(p.getBoolean("duckOn", false), p.getFloat("duckThr", -40f).toDouble(), p.getFloat("duckDepth", -12f).toDouble(),
        p.getFloat("duckAtt", 20f).toDouble(), p.getFloat("duckHold", 300f).toDouble(), p.getFloat("duckRel", 600f).toDouble(), p.getFloat("duckLift", 0f).toDouble()).clamped()
    fun save(p: SharedPreferences, v: Params) { val c = v.clamped()
        p.edit().putBoolean("duckOn", c.on).putFloat("duckThr", c.thresholdDb.toFloat()).putFloat("duckDepth", c.depthDb.toFloat()).putFloat("duckAtt", c.attackMs.toFloat())
            .putFloat("duckHold", c.holdMs.toFloat()).putFloat("duckRel", c.releaseMs.toFloat()).putFloat("duckLift", c.liftDb.toFloat()).apply() }
    /** the role of each engine strip: the CH inputs are the key (1), both sides of every deck the target (2) */
    fun roles(inputs: Int, deckStrips: List<Int>, strips: Int): IntArray = IntArray(strips) { s -> when { s < inputs -> 1; deckStrips.any { s == it || s == it + 1 } -> 2; else -> 0 } }
    fun apply(e: Engine?, v: Params, roles: IntArray) { e ?: return; val c = v.clamped()
        runCatching { e.setDucker(c.on, c.thresholdDb, c.depthDb, c.attackMs, c.holdMs, c.releaseMs, c.liftDb); for (s in roles.indices) e.setDuckRole(s, if (c.on) roles[s] else 0) } }
    /** what the music is down by now, for the readout (0 when off or no engine) */
    fun reductionDb(e: Engine?, v: Params): Double = if (e == null || !v.on) 0.0 else runCatching { e.duckGainDb() }.getOrDefault(0.0).coerceAtMost(0.0)
    /** the sliders of TOOLS: (field, from, to) */
    val controls = listOf(Triple("thresholdDb", -80.0, 0.0), Triple("depthDb", -40.0, 0.0), Triple("attackMs", 1.0, 200.0), Triple("holdMs", 0.0, 2000.0), Triple("releaseMs", 50.0, 3000.0), Triple("liftDb", 0.0, 6.0))
    fun get(v: Params, k: String): Double = when (k) { "thresholdDb" -> v.thresholdDb; "depthDb" -> v.depthDb; "attackMs" -> v.attackMs; "holdMs" -> v.holdMs; "releaseMs" -> v.releaseMs; else -> v.liftDb }
    fun with(v: Params, k: String, x: Double): Params = when (k) { "thresholdDb" -> v.copy(thresholdDb = x); "depthDb" -> v.copy(depthDb = x); "attackMs" -> v.copy(attackMs = x); "holdMs" -> v.copy(holdMs = x); "releaseMs" -> v.copy(releaseMs = x); else -> v.copy(liftDb = x) }.clamped()
}
