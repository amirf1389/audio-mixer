package com.amfb.amixos

import kotlin.math.PI
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.sin
import kotlin.math.sqrt

/** The maths of the phone TOOLS, kept apart from the audio I/O so it is tested on the JVM. */
object SignalMath {
    /** RMS level in dBFS of a block (-120 for silence). */
    fun rmsDb(x: FloatArray, n: Int = x.size): Double { var s = 0.0; for (i in 0 until n) s += x[i] * x[i]; val r = sqrt(s / max(1, n)); return if (r > 1e-6) 20 * log10(r) else -120.0 }

    /** Pitch by the YIN difference function (de Cheveigné & Kawahara): the fundamental in Hz, or 0 when the block has no clear pitch.
     *  Good from about 40 Hz to 2 kHz at 48 kHz with 4096 samples. */
    fun pitchHz(x: FloatArray, rate: Int, threshold: Double = 0.12): Double {
        val half = x.size / 2; val minLag = rate / 2000; val maxLag = minOf(half - 1, rate / 40)
        if (maxLag <= minLag + 2) return 0.0
        val d = DoubleArray(maxLag + 1)
        for (tau in 1..maxLag) { var s = 0.0; for (i in 0 until half) { val v = (x[i] - x[i + tau]).toDouble(); s += v * v }; d[tau] = s }
        var running = 0.0; val cmnd = DoubleArray(maxLag + 1) { 1.0 }
        for (tau in 1..maxLag) { running += d[tau]; cmnd[tau] = if (running > 0) d[tau] * tau / running else 1.0 }
        var tau = minLag
        while (tau < maxLag) { if (cmnd[tau] < threshold) { while (tau + 1 < maxLag && cmnd[tau + 1] < cmnd[tau]) tau++; break }; tau++ }
        if (tau >= maxLag) return 0.0
        // parabolic interpolation around the dip for a fraction of a sample
        val a = cmnd[tau - 1]; val b = cmnd[tau]; val c = cmnd[tau + 1]; val den = a - 2 * b + c
        val t = if (den != 0.0) tau + 0.5 * (a - c) / den else tau.toDouble()
        return rate / t
    }

    /** Test signals, block by block. phase / state carry over between blocks so there are no clicks. */
    class Generator(val rate: Int) {
        var kind = "sine"; var freq = 1000.0; var levelDb = -18.0
        private var phase = 0.0; private val rnd = java.util.Random(1); private val b = DoubleArray(7)
        private var sweepT = 0.0
        fun fill(out: FloatArray) {
            val g = Math.pow(10.0, levelDb / 20)
            for (i in out.indices) {
                val v = when (kind) {
                    "white" -> rnd.nextGaussian() * 0.3
                    "pink" -> { val w = rnd.nextGaussian() * 0.3   // Paul Kellet's filter: -3 dB per octave
                        b[0] = 0.99886 * b[0] + w * 0.0555179; b[1] = 0.99332 * b[1] + w * 0.0750759; b[2] = 0.96900 * b[2] + w * 0.1538520
                        b[3] = 0.86650 * b[3] + w * 0.3104856; b[4] = 0.55000 * b[4] + w * 0.5329522; b[5] = -0.7616 * b[5] - w * 0.0168980
                        val p = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362) * 0.11; b[6] = w * 0.115926; p }
                    "sweep" -> { val f = 20 * Math.pow(1000.0, (sweepT % 10.0) / 10.0); sweepT += 1.0 / rate; phase += 2 * PI * f / rate; sin(phase) }   // 20 Hz to 20 kHz in 10 s
                    else -> { phase += 2 * PI * freq / rate; sin(phase) }
                }
                if (phase > 2 * PI) phase -= 2 * PI
                out[i] = (v * g).toFloat().coerceIn(-1f, 1f)
            }
        }
    }
}
