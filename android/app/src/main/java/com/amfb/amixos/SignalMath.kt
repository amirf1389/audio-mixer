package com.amfb.amixos

import kotlin.math.PI
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.sin
import kotlin.math.sqrt

/** The maths of the phone TOOLS, kept apart from the audio I/O so it is tested on the JVM. */
object SignalMath {
    /** The 31 third-octave centre frequencies of the audio band, 20 Hz .. 20 kHz (IEC 61260 nominal values). */
    val thirdOctaveCentres = doubleArrayOf(20.0, 25.0, 31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0, 200.0, 250.0, 315.0, 400.0, 500.0, 630.0, 800.0, 1000.0, 1250.0, 1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0, 10000.0, 12500.0, 16000.0, 20000.0)

    /** In-place radix-2 FFT (re, im of a power-of-two length). */
    fun fft(re: DoubleArray, im: DoubleArray) {
        val n = re.size; require(n > 0 && n and (n - 1) == 0 && im.size == n) { "the FFT length must be a power of two" }
        var j = 0
        for (i in 1 until n) { var bit = n shr 1; while (j and bit != 0) { j = j xor bit; bit = bit shr 1 }; j = j xor bit
            if (i < j) { val tr = re[i]; re[i] = re[j]; re[j] = tr; val ti = im[i]; im[i] = im[j]; im[j] = ti } }
        var len = 2
        while (len <= n) { val ang = -2 * PI / len; val wr = kotlin.math.cos(ang); val wi = sin(ang)
            var i = 0
            while (i < n) { var cr = 1.0; var ci = 0.0
                for (k in 0 until len / 2) { val a = i + k; val b = a + len / 2
                    val xr = re[b] * cr - im[b] * ci; val xi = re[b] * ci + im[b] * cr
                    re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi
                    val nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr }
                i += len }
            len = len shl 1 }
    }

    /** A block's level in each third-octave band, in dBFS (a full-scale sine reads about 0 dB in its band; -120 = nothing there).
     *  Hann window, power summed over the FFT bins between the band edges (centre / 2^(1/6) .. centre * 2^(1/6)).
     *  The block length must be a power of two; with 16384 samples at 48 kHz the 20 Hz band still holds bins. */
    fun thirdOctave(x: FloatArray, rate: Int): DoubleArray {
        val n = x.size; val re = DoubleArray(n); val im = DoubleArray(n)
        for (i in 0 until n) re[i] = x[i] * (0.5 - 0.5 * kotlin.math.cos(2 * PI * i / (n - 1)))
        fft(re, im)
        val out = DoubleArray(thirdOctaveCentres.size); val edge = Math.pow(2.0, 1.0 / 6); val binHz = rate.toDouble() / n
        for (b in out.indices) { val lo = thirdOctaveCentres[b] / edge; val hi = thirdOctaveCentres[b] * edge
            var k0 = Math.ceil(lo / binHz).toInt(); var k1 = Math.floor(hi / binHz).toInt()
            if (k1 < k0) { k0 = Math.round(thirdOctaveCentres[b] / binHz).toInt(); k1 = k0 }   // a band narrower than one bin: its nearest bin
            var p = 0.0; for (k in max(1, k0)..minOf(n / 2 - 1, k1)) p += re[k] * re[k] + im[k] * im[k]
            // a sine of amplitude A through a Hann window: |X| = A * n / 4 at its bin, its power spread over three bins (sum = 1.5 x the peak's)
            val amp2 = p / 1.5 / ((n / 4.0) * (n / 4.0)); out[b] = if (amp2 > 1e-12) 10 * log10(amp2) else -120.0 }
        return out
    }
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
