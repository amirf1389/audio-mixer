package com.amfb.amixos

import android.content.SharedPreferences
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow

/** LOUDNESS MANAGER on the phone's own mixer (the console's, in Kotlin at the meter rate, 50 ms): per channel
 *  - AUTO FADER: the channel's level (its input before the fader, from the engine's per-strip input peaks, as a 3 s power average:
 *    an estimate of the short-term loudness, peak-based, -3 dB crest) is ridden toward the TARGET within +-RANGE of the user's fader,
 *    at most UP dB/s up and DOWN dB/s down, never above +10 dB with the fader. Only while the channel has signal.
 *  - AUTO ON/OFF: a channel under the gate threshold for HOLD seconds fades to off over 1 s, and is back at the next tick with signal.
 *  - a hand on the fader ([touch]) pauses its ride for 5 s and the new position is the centre.
 *  The master target steers the master fader slowly (1 dB/s, +-6 dB) toward the preset (STREAMING -14, PODCAST -16, BROADCAST -23)
 *  with the limiter kept on as the safety. Gain order: user fader + DCA, then the ride and auto on/off, then the engine's ducker. */
object PhoneLoudness {
    const val FLOOR = -80.0
    const val MAX_DB = 10.0
    val presets = linkedMapOf("STREAMING" to -14.0, "PODCAST" to -16.0, "BROADCAST" to -23.0)
    data class Params(val target: Double = -18.0, val rangeDb: Double = 10.0, val upDbS: Double = 3.0, val downDbS: Double = 6.0, val gateDb: Double = -50.0, val holdS: Double = 4.0,
                      val masterOn: Boolean = false, val masterTarget: Double = -14.0) {
        fun clamped() = copy(target = target.coerceIn(-40.0, -6.0), rangeDb = rangeDb.coerceIn(0.0, 20.0), upDbS = upDbS.coerceIn(0.1, 40.0), downDbS = downDbS.coerceIn(0.1, 40.0),
            gateDb = gateDb.coerceIn(-80.0, -20.0), holdS = holdS.coerceIn(0.1, 30.0), masterTarget = masterTarget.coerceIn(-30.0, -8.0))
    }
    fun load(p: SharedPreferences) = Params(p.getFloat("lmTarget", -18f).toDouble(), p.getFloat("lmRange", 10f).toDouble(), p.getFloat("lmUp", 3f).toDouble(), p.getFloat("lmDown", 6f).toDouble(),
        p.getFloat("lmGate", -50f).toDouble(), p.getFloat("lmHold", 4f).toDouble(), p.getBoolean("lmMaster", false), p.getFloat("lmMasterTarget", -14f).toDouble()).clamped()
    fun save(p: SharedPreferences, v: Params) { val c = v.clamped()
        p.edit().putFloat("lmTarget", c.target.toFloat()).putFloat("lmRange", c.rangeDb.toFloat()).putFloat("lmUp", c.upDbS.toFloat()).putFloat("lmDown", c.downDbS.toFloat())
            .putFloat("lmGate", c.gateDb.toFloat()).putFloat("lmHold", c.holdS.toFloat()).putBoolean("lmMaster", c.masterOn).putFloat("lmMasterTarget", c.masterTarget.toFloat()).apply() }
    fun rideOn(p: SharedPreferences, ch: Int) = p.getBoolean("lmRide$ch", false)
    fun gateOn(p: SharedPreferences, ch: Int) = p.getBoolean("lmOnOff$ch", false)
    fun setRide(p: SharedPreferences, ch: Int, on: Boolean) = p.edit().putBoolean("lmRide$ch", on).apply()
    fun setGate(p: SharedPreferences, ch: Int, on: Boolean) = p.edit().putBoolean("lmOnOff$ch", on).apply()
    /** the sliders of TOOLS: (field, from, to) */
    val controls = listOf(Triple("target", -40.0, -6.0), Triple("rangeDb", 0.0, 20.0), Triple("gateDb", -80.0, -20.0), Triple("holdS", 0.5, 15.0))
    fun get(v: Params, k: String): Double = when (k) { "target" -> v.target; "rangeDb" -> v.rangeDb; "gateDb" -> v.gateDb; else -> v.holdS }
    fun with(v: Params, k: String, x: Double): Params = when (k) { "target" -> v.copy(target = x); "rangeDb" -> v.copy(rangeDb = x); "gateDb" -> v.copy(gateDb = x); else -> v.copy(holdS = x) }.clamped()

    /** a peak (linear) as an estimated loudness (LUFS-like): -3 dB crest, -0.691 */
    fun estimate(peak: Double): Double = if (peak <= 1e-6) -120.0 else 20 * log10(peak) - 3.0 - 0.691

    /** one channel's ride and auto on/off */
    class Rider(private val windowS: Double = 3.0) {
        private val ring = DoubleArray(60); private var w = 0; private var n = 0
        var shortTerm = -120.0; private set
        var fast = -120.0; private set
        var rideDb = 0.0; private set
        var gateDb = 0.0; private set
        var suspendS = 0.0; private set
        private var silentS = 0.0
        private var acc = 0.0; private var accS = 0.0
        /** one tick: the channel's input peak (linear, before the fader) over dt seconds */
        fun feed(peak: Double, dt: Double) {
            fast = estimate(peak); val e = if (peak > 1e-6) peak * peak / 2.0 else 0.0   // power of a sine with that peak
            acc += e * dt; accS += dt
            val piece = windowS / ring.size
            while (accS >= piece) { ring[w] = acc / accS; w = (w + 1) % ring.size; if (n < ring.size) n++; acc = 0.0; accS = 0.0 }
            if (n > 0) { var s = 0.0; for (k in 0 until n) s += ring[k]; val ms = s / n; shortTerm = if (ms > 1e-12) 10 * log10(ms) - 0.691 else -120.0 }
        }
        fun touch(seconds: Double = 5.0) { rideDb = 0.0; suspendS = seconds }
        /** moves the ride and the gate; returns ride + gate (dB) to add to the user's fader */
        fun step(dt: Double, userDb: Double, muted: Boolean, ride: Boolean, gate: Boolean, v: Params): Double {
            if (suspendS > 0) suspendS = max(0.0, suspendS - dt)
            if (ride && !muted && suspendS <= 0 && n >= 10 && shortTerm > v.gateDb && shortTerm > -70) {
                val hi = min(v.rangeDb, MAX_DB - userDb); val lo = -v.rangeDb
                val want = (v.target - (shortTerm + userDb)).coerceIn(lo, max(lo, hi))
                rideDb += if (want > rideDb) min(want - rideDb, v.upDbS * dt) else max(want - rideDb, -v.downDbS * dt)
            }
            if (ride) { val hi = min(v.rangeDb, MAX_DB - userDb); if (rideDb > hi) rideDb = max(-v.rangeDb, hi); if (rideDb < -v.rangeDb) rideDb = -v.rangeDb } else rideDb = 0.0
            if (gate && !muted) {
                if (fast >= v.gateDb) { silentS = 0.0; gateDb = 0.0 }
                else { silentS += dt; if (silentS >= v.holdS) gateDb = max(FLOOR, gateDb + FLOOR * dt) }
            } else if (!gate) { gateDb = 0.0; silentS = 0.0 }
            return rideDb + gateDb
        }
        val closed get() = gateDb < -0.01
    }

    /** the master target: the master fader's offset, 1 dB/s, +-6 dB, from the master's peak */
    class MasterRider {
        private val r = Rider()
        var gainDb = 0.0; private set
        fun step(masterPeak: Double, dt: Double, v: Params): Double {
            if (!v.masterOn) { gainDb = 0.0; return 0.0 }
            // the master is measured after this gain: take it out so the loop does not chase itself
            r.feed(masterPeak / 10.0.pow(gainDb / 20), dt)
            if (r.shortTerm > -70) { val want = (v.masterTarget - r.shortTerm).coerceIn(-6.0, 6.0); gainDb += (want - gainDb).coerceIn(-1.0 * dt, 1.0 * dt) }
            return gainDb
        }
        val shortTerm get() = r.shortTerm
    }
}
