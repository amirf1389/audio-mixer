package com.amfb.amixos

import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.ln
import kotlin.math.log10
import kotlin.math.pow
import kotlin.math.roundToInt
import kotlin.math.sin
import kotlin.math.sqrt

/** One EQ band as the console's engine has it (RBJ cookbook biquads, the same formulas as engine/cpp). */
data class EqBand(var type: String, var freq: Double, var gainDb: Double, var q: Double) {
    val usesGain get() = type == "peaking" || type == "lowshelf" || type == "highshelf"
}

object AudioMath {
    val EQ_TYPES = listOf("off", "peaking", "lowshelf", "highshelf", "lowpass", "highpass", "notch")

    /** Magnitude in dB of one band at frequency f (sample rate fs). */
    fun bandDb(b: EqBand, f: Double, fs: Double = 48000.0): Double {
        if (b.type == "off") return 0.0
        val w0 = 2 * PI * b.freq.coerceIn(10.0, fs * 0.49) / fs; val cw = cos(w0); val sw = sin(w0)
        val q = b.q.coerceIn(0.1, 30.0); val alpha = sw / (2 * q); val a = 10.0.pow(b.gainDb / 40)
        val (num, den) = when (b.type) {
            "peaking" -> doubleArrayOf(1 + alpha * a, -2 * cw, 1 - alpha * a) to doubleArrayOf(1 + alpha / a, -2 * cw, 1 - alpha / a)
            "lowshelf" -> { val s = 2 * sqrt(a) * alpha
                doubleArrayOf(a * ((a + 1) - (a - 1) * cw + s), 2 * a * ((a - 1) - (a + 1) * cw), a * ((a + 1) - (a - 1) * cw - s)) to
                    doubleArrayOf((a + 1) + (a - 1) * cw + s, -2 * ((a - 1) + (a + 1) * cw), (a + 1) + (a - 1) * cw - s) }
            "highshelf" -> { val s = 2 * sqrt(a) * alpha
                doubleArrayOf(a * ((a + 1) + (a - 1) * cw + s), -2 * a * ((a - 1) + (a + 1) * cw), a * ((a + 1) + (a - 1) * cw - s)) to
                    doubleArrayOf((a + 1) - (a - 1) * cw + s, 2 * ((a - 1) - (a + 1) * cw), (a + 1) - (a - 1) * cw - s) }
            "lowpass" -> doubleArrayOf((1 - cw) / 2, 1 - cw, (1 - cw) / 2) to doubleArrayOf(1 + alpha, -2 * cw, 1 - alpha)
            "highpass" -> doubleArrayOf((1 + cw) / 2, -(1 + cw), (1 + cw) / 2) to doubleArrayOf(1 + alpha, -2 * cw, 1 - alpha)
            "notch" -> doubleArrayOf(1.0, -2 * cw, 1.0) to doubleArrayOf(1 + alpha, -2 * cw, 1 - alpha)
            else -> return 0.0
        }
        val w = 2 * PI * f / fs
        fun mag2(c: DoubleArray): Double { val re = c[0] + c[1] * cos(w) + c[2] * cos(2 * w); val im = -(c[1] * sin(w) + c[2] * sin(2 * w)); return re * re + im * im }
        return 10 * log10(mag2(num) / mag2(den).coerceAtLeast(1e-30))
    }
    fun totalDb(bands: List<EqBand>, f: Double) = bands.sumOf { bandDb(it, f) }

    // ── CALCULATOR ────────────────────────────────────────────────────────────────────────────────
    /** Speed of sound in air (m/s) at a temperature in °C. */
    fun speedOfSound(tempC: Double) = 331.3 * sqrt(1 + tempC / 273.15)
    /** Delay to align a speaker that is d metres further away (ms), and back. */
    fun distanceToMs(m: Double, tempC: Double = 20.0) = m / speedOfSound(tempC) * 1000
    fun msToDistance(ms: Double, tempC: Double = 20.0) = ms / 1000 * speedOfSound(tempC)
    /** Note lengths at a tempo: name to milliseconds (quarter note = 60000 / bpm). */
    fun noteTimes(bpm: Double): List<Pair<String, Double>> { val q = 60000 / bpm
        return listOf("1/1" to q * 4, "1/2" to q * 2, "1/2 dotted" to q * 3, "1/4" to q, "1/4 dotted" to q * 1.5, "1/4 triplet" to q * 2 / 3,
            "1/8" to q / 2, "1/8 dotted" to q * 0.75, "1/8 triplet" to q / 3, "1/16" to q / 4, "1/32" to q / 8) }
    fun dbToVoltageRatio(db: Double) = 10.0.pow(db / 20)
    fun dbToPowerRatio(db: Double) = 10.0.pow(db / 10)
    fun ratioToDb(r: Double) = 20 * log10(r)
    /** n equal, unrelated (incoherent) sources add 10·log10(n) dB; equal coherent ones (in phase) add 20·log10(n). */
    fun sumIncoherent(levelsDb: List<Double>) = 10 * log10(levelsDb.sumOf { 10.0.pow(it / 10) })
    fun wavelength(hz: Double, tempC: Double = 20.0) = speedOfSound(tempC) / hz
    /** The nearest note (A4 = 440 Hz) and how many cents off. */
    fun noteOf(hz: Double, a4: Double = 440.0): Pair<String, Int> {
        val n = 12 * ln(hz / a4) / ln(2.0) + 69; val k = n.roundToInt(); val cents = ((n - k) * 100).roundToInt()
        val names = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
        return (names[((k % 12) + 12) % 12] + (k / 12 - 1)) to cents
    }
    fun latencyMs(frames: Int, rate: Double) = frames / rate * 1000
    /** Uncompressed audio size in megabytes (10^6 bytes). */
    fun pcmMegabytes(rate: Double, bits: Int, channels: Int, minutes: Double) = rate * bits / 8 * channels * minutes * 60 / 1e6
    fun fmt(v: Double, d: Int = 2) = if (abs(v) >= 1e6 || (abs(v) < 1e-3 && v != 0.0)) String.format("%.3e", v) else String.format("%.${d}f", v)
}
