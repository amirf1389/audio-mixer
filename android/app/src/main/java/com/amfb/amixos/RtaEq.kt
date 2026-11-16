package com.amfb.amixos

import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.log2
import kotlin.math.pow
import kotlin.math.roundToInt

/** RTA EQ on the phone: the console's master program EQ (10 bells at the ISO octave centres, Q 1.41) and AUTO EQ, which measures the
 *  spectrum for a few seconds and proposes gains that bring it towards a target curve. The same rules as the web remote: the overall
 *  level is the median difference (one bump does not move the rest), 70 % of the difference is corrected, at most ±9 dB, in 0.5 dB
 *  steps, and bands with no sound stay at 0. Pure functions, tested without a phone. */
object RtaEq {
    val ISO = doubleArrayOf(31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0)
    const val SILENT = -80.0

    enum class Target(val label: String, val curve: (Double) -> Double) {
        FLAT("FLAT", { 0.0 }),
        HOUSE("HOUSE", { f -> if (f < 1000) 3 * log2(1000 / f) / 5 else -1.5 * log2(f / 1000) }),
        WARM("WARM", { f -> -log2(f / 250) });
    }

    /** the measured spectrum ([f] band centres, [db] dBFS per band) summed into the 10 octave bands: the mean power of the bands
     *  within half an octave of each centre, in dB (-120 when there is none) */
    fun octaves(f: List<Double>, db: List<Double>): DoubleArray = DoubleArray(ISO.size) { k ->
        var p = 0.0; var n = 0
        f.forEachIndexed { i, x -> if (abs(log2(x / ISO[k])) <= 0.5) { p += 10.0.pow(db[i] / 10); n++ } }
        if (n > 0 && p > 0) 10 * log10(p / n) else -120.0
    }

    /** AUTO EQ: the gains (10, dB) that bring the octave levels [lvl] towards [target]; null when there is no sound to measure */
    fun autoEq(lvl: DoubleArray, target: Target = Target.FLAT): DoubleArray? {
        val t = ISO.map { target.curve(it) }
        val use = ISO.indices.filter { ISO[it] in 125.0..8000.0 && lvl[it] > SILENT }
        if (use.isEmpty()) return null
        val ds = use.map { lvl[it] - t[it] }.sorted()
        val off = if (ds.size % 2 == 1) ds[ds.size / 2] else (ds[ds.size / 2 - 1] + ds[ds.size / 2]) / 2
        return DoubleArray(ISO.size) { i -> if (lvl[i] <= SILENT) 0.0 else ((-0.7 * (lvl[i] - t[i] - off)).coerceIn(-9.0, 9.0) * 2).roundToInt() / 2.0 }
    }

    /** a running power average of spectra (AUTO EQ measures for a few seconds) */
    class Average(n: Int = ISO.size) { private val p = DoubleArray(n); var frames = 0; private set
        fun add(db: DoubleArray) { frames++; for (i in p.indices) p[i] += 10.0.pow(db[i] / 10) }
        fun result(): DoubleArray = DoubleArray(p.size) { if (frames > 0 && p[it] > 0) 10 * log10(p[it] / frames) else -120.0 } }

    fun hz(f: Double) = if (f >= 1000) (if (f % 1000 == 0.0) "${(f / 1000).toInt()}k" else "%.1fk".format(f / 1000)) else if (f % 1.0 == 0.0) "${f.toInt()}" else "$f"
}
